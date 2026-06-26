#!/usr/bin/env bash
# Shared configuration for all deploy scripts (P8). Override any value by exporting
# it before sourcing, e.g.  PROJECT_ID=my-proj bash deploy/10_deploy_cloudrun.sh
#
# Region defaults to asia-south1 (Mumbai) — change REGION to your nearest region.

export PROJECT_ID="${PROJECT_ID:-joho-search}"
export REGION="${REGION:-asia-south1}"
export ZONE="${ZONE:-asia-south1-a}"

# Artifact Registry (Docker images)
export AR_REPO="${AR_REPO:-joho}"
export AR_HOST="${REGION}-docker.pkg.dev"
export IMAGE_COMBINED="${AR_HOST}/${PROJECT_ID}/${AR_REPO}/joho:latest"
export IMAGE_ENGINE="${AR_HOST}/${PROJECT_ID}/${AR_REPO}/joho-engine:latest"
export IMAGE_GATEWAY="${AR_HOST}/${PROJECT_ID}/${AR_REPO}/joho-gateway:latest"

# GCS bucket for corpora / indexes / embeddings / job outputs
export BUCKET="${BUCKET:-gs://${PROJECT_ID}-joho-data}"

# Cloud Run service
export SERVICE="${SERVICE:-joho}"

# Budget alerts (USD) — keep cloud spend bounded; override BUDGET_AMOUNT as needed.
export BUDGET_AMOUNT="${BUDGET_AMOUNT:-300}"

# Resolve gcloud even if PATH wasn't reloaded in this shell.
GCLOUD_BIN="$(command -v gcloud || echo gcloud)"
export GCLOUD_BIN

echo "[env] project=$PROJECT_ID region=$REGION repo=$AR_REPO bucket=$BUCKET service=$SERVICE"
