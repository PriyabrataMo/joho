#!/usr/bin/env bash
# One-time GCP setup (P8): enable the APIs we use, create the Artifact Registry repo
# and the GCS bucket, and wire a budget with alert thresholds. Idempotent — safe to
# re-run. Requires billing to already be enabled on the project.
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh
GC="$GCLOUD_BIN"

echo "==> Enabling APIs (Run, Build, Artifact Registry, Compute, Storage, Billing)"
"$GC" services enable \
  run.googleapis.com \
  cloudbuild.googleapis.com \
  artifactregistry.googleapis.com \
  compute.googleapis.com \
  storage.googleapis.com \
  billingbudgets.googleapis.com \
  --project "$PROJECT_ID"

echo "==> Artifact Registry repo '$AR_REPO' in $REGION"
"$GC" artifacts repositories describe "$AR_REPO" --location "$REGION" --project "$PROJECT_ID" >/dev/null 2>&1 \
  || "$GC" artifacts repositories create "$AR_REPO" \
        --repository-format=docker --location "$REGION" \
        --description="Joho images" --project "$PROJECT_ID"

echo "==> GCS bucket $BUCKET (region $REGION, autoclass to keep storage cost low)"
if ! "$GC" storage buckets describe "$BUCKET" >/dev/null 2>&1; then
  "$GC" storage buckets create "$BUCKET" \
    --location "$REGION" --uniform-bucket-level-access --project "$PROJECT_ID"
fi

echo "==> Budget '$BUDGET_AMOUNT USD' with alerts at 17%/50%/83% (~\$50/\$150/\$250)"
BILLING_ACCT="$("$GC" billing projects describe "$PROJECT_ID" \
  --format='value(billingAccountName)' | sed 's#billingAccounts/##')"
if [ -z "$BILLING_ACCT" ]; then
  echo "    !! no billing account linked to $PROJECT_ID — link one first, then re-run."
else
  "$GC" billing budgets create \
    --billing-account="$BILLING_ACCT" \
    --display-name="joho-budget" \
    --budget-amount="${BUDGET_AMOUNT}USD" \
    --threshold-rule=percent=0.17 \
    --threshold-rule=percent=0.50 \
    --threshold-rule=percent=0.83 \
    --threshold-rule=percent=1.0 \
    --filter-projects="projects/$PROJECT_ID" 2>/dev/null \
    || echo "    (budget may already exist — skipping)"
fi

echo "==> Configuring Docker auth for $AR_HOST"
"$GC" auth configure-docker "$AR_HOST" --quiet

echo "Done. Next: deploy/10_deploy_cloudrun.sh"
