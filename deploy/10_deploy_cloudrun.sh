#!/usr/bin/env bash
# Build the combined image with Cloud Build and deploy it to Cloud Run (P8).
#
# Cloud Run is scale-to-zero: you pay only while a request is in flight, so an idle
# demo costs ~$0. We give the instance enough memory for torch + BGE-small and cap
# concurrency/instances so a traffic spike can't blow the budget.
set -euo pipefail
cd "$(dirname "$0")/.."         # repo root (Docker build context)
source deploy/env.sh
GC="$GCLOUD_BIN"

echo "==> Cloud Build: $IMAGE_COMBINED"
"$GC" builds submit --tag "$IMAGE_COMBINED" --project "$PROJECT_ID" .

echo "==> Deploy to Cloud Run service '$SERVICE' in $REGION"
"$GC" run deploy "$SERVICE" \
  --image "$IMAGE_COMBINED" \
  --project "$PROJECT_ID" \
  --region "$REGION" \
  --platform managed \
  --allow-unauthenticated \
  --port 8080 \
  --memory 2Gi \
  --cpu 2 \
  --concurrency 8 \
  --min-instances 0 \
  --max-instances 3 \
  --timeout 120 \
  --set-env-vars "JOHO_ENABLE_DENSE=1,JOHO_ENABLE_RERANK=0"

URL="$("$GC" run services describe "$SERVICE" --region "$REGION" --project "$PROJECT_ID" \
  --format='value(status.url)')"
echo
echo "Live demo URL: $URL"
echo "  health:  curl $URL/healthz"
echo "  search:  curl \"$URL/search?q=cardiac+myocyte+regeneration\""
echo
echo "Point the Vercel UI at it:  NEXT_PUBLIC_GATEWAY_URL=$URL"
