#!/usr/bin/env bash
# Run a heavy batch job on an EPHEMERAL Spot VM, then delete it (P8).
#
# This is the cost-engineered way to do jobs too big for a local dev machine (e.g. the
# full MS MARCO index build / embedding): spin a cheap Spot VM, copy the repo, run a
# job script, let the job push results to GCS, and tear the VM down. Two independent
# cost guards make a forgotten VM nearly impossible:
#   * Spot (preemptible) pricing — a fraction of on-demand
#   * --max-run-duration + --instance-termination-action=DELETE — the VM deletes
#     itself after the cap even if the job hangs or your laptop disconnects
# and we also `delete` it explicitly on exit.
#
#   deploy/30_vm_job.sh deploy/marco_job.sh            # run the MS MARCO job
#   MACHINE=e2-standard-16 deploy/30_vm_job.sh my_job.sh
set -euo pipefail
cd "$(dirname "$0")/.."
source deploy/env.sh
GC="$GCLOUD_BIN"

JOB_SCRIPT="${1:?usage: 30_vm_job.sh <job_script.sh>}"
VM="${VM:-joho-job-$(whoami)}"
MACHINE="${MACHINE:-e2-standard-8}"      # 8 vCPU / 32 GB — fits MS MARCO in RAM
MAX_RUN="${MAX_RUN:-7200s}"              # hard cap: VM self-deletes after this
DISK="${DISK:-60GB}"

cleanup() {
  echo "==> deleting VM $VM"
  "$GC" compute instances delete "$VM" --zone "$ZONE" --project "$PROJECT_ID" --quiet || true
}
trap cleanup EXIT

echo "==> creating Spot VM $VM ($MACHINE, max-run $MAX_RUN, self-delete on timeout)"
"$GC" compute instances create "$VM" \
  --project "$PROJECT_ID" --zone "$ZONE" \
  --machine-type "$MACHINE" \
  --provisioning-model=SPOT \
  --instance-termination-action=DELETE \
  --max-run-duration="$MAX_RUN" \
  --image-family=debian-12 --image-project=debian-cloud \
  --boot-disk-size="$DISK" \
  --scopes=storage-rw                       # so the job can push results to GCS

echo "==> waiting for SSH"
for _ in $(seq 1 30); do
  "$GC" compute ssh "$VM" --zone "$ZONE" --project "$PROJECT_ID" --command "true" 2>/dev/null && break
  sleep 5
done

echo "==> copying repo (excluding venvs/build/node_modules) to the VM"
tar --exclude='.git' --exclude='*/.venv' --exclude='engine/build' \
    --exclude='*/node_modules' --exclude='*/.next' -czf /tmp/joho_src.tgz .
"$GC" compute scp /tmp/joho_src.tgz "$VM":~/joho_src.tgz --zone "$ZONE" --project "$PROJECT_ID"

echo "==> running $JOB_SCRIPT on the VM"
"$GC" compute ssh "$VM" --zone "$ZONE" --project "$PROJECT_ID" --command "
  set -e
  mkdir -p joho && tar -xzf joho_src.tgz -C joho
  cd joho
  export BUCKET='$BUCKET'
  bash '$JOB_SCRIPT'
"

echo "==> job finished; results were pushed to $BUCKET by the job. Tearing down."
# trap cleanup runs here
