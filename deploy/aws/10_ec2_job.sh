#!/usr/bin/env bash
# Run a heavy batch job on an EPHEMERAL EC2 instance in FIRE-AND-FORGET mode, then let
# the instance terminate ITSELF (the AWS mirror of GCP's 30_vm_job.sh).
#
# Design: this launcher does NOT babysit a long-lived ssh tunnel. It copies the repo,
# kicks off the job DETACHED (`setsid`, immune to SIGHUP), and exits. The job runs the
# eval, syncs results + its log to S3, and then powers the box off — which TERMINATES it
# (instance-initiated-shutdown-behavior=terminate). So your laptop can sleep, drop wifi,
# or close the lid mid-run and nothing is lost.
#
# THREE independent cost guards make a forgotten instance nearly impossible:
#   1. The job self-terminates (poweroff) the moment it finishes — success OR failure.
#   2. A boot-time watchdog powers the box off after MAX_RUN_MIN even if the job hangs.
#   3. If SETUP fails before the job is detached, an EXIT trap terminates the box. Once
#      the job is detached the trap is cleared, so a launcher exit/disconnect can't kill
#      the run (this is the bug that nuked the earlier 33%-complete run: a laptop sleep
#      dropped the ssh tunnel and the old EXIT trap terminated the box).
#
#   deploy/aws/10_ec2_job.sh deploy/aws/marco_job.sh
#   INSTANCE_TYPE=r6i.xlarge deploy/aws/10_ec2_job.sh deploy/aws/marco_job.sh
set -euo pipefail
cd "$(dirname "$0")/../.."          # repo root (this script lives in deploy/aws/)
source deploy/aws/env.sh

JOB_SCRIPT="${1:?usage: 10_ec2_job.sh <job_script.sh>}"
S3_PREFIX="${S3_PREFIX:-msmarco_passage_dev_small}"   # where the job + this launcher look in the bucket
[ -n "$ACCOUNT_ID" ] || { echo "!! no credentials for profile '$AWS_PROFILE'."; exit 1; }
SG_ID="${SG_ID:-$(cat deploy/aws/.sg_id 2>/dev/null || true)}"
[ -n "$SG_ID" ] || { echo "!! no security group — run deploy/aws/00_setup_aws.sh first."; exit 1; }
[ -f "$KEY_FILE" ] || { echo "!! missing key $KEY_FILE — run deploy/aws/00_setup_aws.sh first."; exit 1; }

echo "==> resolving latest Ubuntu 22.04 (amd64) AMI"
AMI="$(aws ssm get-parameters \
  --names /aws/service/canonical/ubuntu/server/22.04/stable/current/amd64/hvm/ebs-gp2/ami-id \
  --query 'Parameters[0].Value' --output text)"

# Boot-time self-terminate watchdog (cost guard #2). Tries to salvage the partial log to
# S3 before powering off, so even a hung run is debuggable. poweroff => terminate.
UD="$(mktemp)"
cat > "$UD" <<EOF
#!/bin/bash
export AWS_DEFAULT_REGION='$REGION'
(
  sleep $((MAX_RUN_MIN*60))
  aws s3 cp /home/ubuntu/joho_job.log "s3://$BUCKET/$S3_PREFIX/job.log.timeout" 2>/dev/null || true
  poweroff
) &
EOF

MARKET=()
PRICING="on-demand"
if [ "$SPOT" = "1" ]; then
  MARKET=(--instance-market-options '{"MarketType":"spot","SpotOptions":{"SpotInstanceType":"one-time","InstanceInterruptionBehavior":"terminate"}}')
  PRICING="Spot"
fi

echo "==> launching $PRICING $INSTANCE_TYPE (AMI $AMI, ${VOLUME_GB}GB, watchdog ${MAX_RUN_MIN}m)"
IID="$(aws ec2 run-instances \
  --image-id "$AMI" --instance-type "$INSTANCE_TYPE" \
  --key-name "$KEY_NAME" --security-group-ids "$SG_ID" \
  --iam-instance-profile "Name=$PROFILE_NAME" \
  --instance-initiated-shutdown-behavior terminate \
  ${MARKET[@]+"${MARKET[@]}"} \
  --block-device-mappings "[{\"DeviceName\":\"/dev/sda1\",\"Ebs\":{\"VolumeSize\":$VOLUME_GB,\"VolumeType\":\"gp3\",\"DeleteOnTermination\":true}}]" \
  --user-data "file://$UD" \
  --tag-specifications 'ResourceType=instance,Tags=[{Key=Name,Value=joho-marco-job},{Key=project,Value=joho}]' \
  --query 'Instances[0].InstanceId' --output text)"
rm -f "$UD"
echo "   instance $IID"
echo "$IID" > deploy/aws/.last_instance

# SETUP-phase guard only (cost guard #3). Terminates the box if we fail BEFORE the job is
# detached. Cleared the instant the job is running, so a later launcher exit is harmless.
setup_failed() {
  echo "!! setup failed before job launch — terminating $IID so it can't linger"
  aws ec2 terminate-instances --instance-ids "$IID" \
    --query 'TerminatingInstances[0].CurrentState.Name' --output text || true
}
trap setup_failed EXIT

echo "==> waiting for instance to run"
aws ec2 wait instance-running --instance-ids "$IID"
IP="$(aws ec2 describe-instances --instance-ids "$IID" \
  --query 'Reservations[0].Instances[0].PublicIpAddress' --output text)"
echo "   public IP $IP"

SSH=(ssh -i "$KEY_FILE" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 "ubuntu@$IP")
echo "==> waiting for SSH"
for _ in $(seq 1 40); do "${SSH[@]}" true 2>/dev/null && break; sleep 5; done

echo "==> copying repo to the instance (excluding venvs/build/node_modules/docs)"
tar --exclude='.git' --exclude='*/.venv' --exclude='engine/build' \
    --exclude='*/node_modules' --exclude='*/.next' --exclude='docs' -czf /tmp/joho_src.tgz .
scp -i "$KEY_FILE" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    /tmp/joho_src.tgz "ubuntu@$IP":~/joho_src.tgz

# Build the detached runner. It runs the job, ALWAYS ships the full log to S3 (so we can
# read what happened even though the box self-destructs), then powers off => terminate.
WRAP="$(mktemp)"
cat > "$WRAP" <<EOF
#!/usr/bin/env bash
cd "\$HOME/joho"
export BUCKET='$BUCKET' AWS_DEFAULT_REGION='$REGION' SWAP_GB='${SWAP_GB:-0}'
bash '$JOB_SCRIPT' > "\$HOME/joho_job.log" 2>&1
aws s3 cp "\$HOME/joho_job.log" "s3://$BUCKET/$S3_PREFIX/job.log" 2>/dev/null || true
sudo poweroff
EOF

echo "==> staging + DETACHING the job (setsid: survives ssh drops, laptop sleep, lid close)"
scp -i "$KEY_FILE" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    "$WRAP" "ubuntu@$IP":/tmp/run_job.sh
rm -f "$WRAP"
"${SSH[@]}" "
  set -e
  mkdir -p joho && tar -xzf joho_src.tgz -C joho
  chmod +x /tmp/run_job.sh
  setsid /tmp/run_job.sh </dev/null >/dev/null 2>&1 &
  sleep 2
  echo '   detached job started (pid in its own session)'
"

# Job is detached and running independently. Disarm the setup guard — from here a launcher
# exit, a dropped connection, or a sleeping laptop must NOT terminate the box.
trap - EXIT

cat <<EOF

==> Job is now running DETACHED on $IID. You can close the laptop.
    It will sync results to  s3://$BUCKET/$S3_PREFIX/  and then self-terminate.

    Watch it (laptop can sleep between checks):
      aws ec2 describe-instances --profile $AWS_PROFILE --region $REGION \\
        --instance-ids $IID --query 'Reservations[0].Instances[0].State.Name' --output text
      aws s3 ls s3://$BUCKET/$S3_PREFIX/ --profile $AWS_PROFILE
      aws s3 cp s3://$BUCKET/$S3_PREFIX/scorecard.txt - --profile $AWS_PROFILE   # the MRR, when done

    'terminated' state + scorecard.txt in S3 = success. job.log is uploaded either way.
EOF
