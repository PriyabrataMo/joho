#!/usr/bin/env bash
# Shared config for the AWS deploy path (ephemeral EC2 MS MARCO job). Override any
# value by exporting it first, e.g.  REGION=us-east-1 deploy/aws/10_ec2_job.sh ...
#
# This path mirrors the GCP Spot-VM job: spin a cheap Spot EC2, run the job, push the
# results to S3, then terminate the instance. Region defaults to ap-south-1 (Mumbai) —
# change REGION to your nearest region.

# Personal profile — NOT a work account. Keep MS MARCO / EC2 spend off work billing.
export AWS_PROFILE="${AWS_PROFILE:-joho}"
export REGION="${REGION:-ap-south-1}"
export AWS_DEFAULT_REGION="$REGION"

# Resolve the account id (used for a globally-unique bucket name + budget). Empty until
# credentials exist; the scripts that need it check and fail with a clear message.
ACCOUNT_ID="$(aws sts get-caller-identity --query Account --output text 2>/dev/null || true)"
export ACCOUNT_ID

# Durable store for corpora / indexes / run files / job outputs (bare S3 bucket name).
export BUCKET="${BUCKET:-joho-marco-${ACCOUNT_ID:-data}}"

# Ephemeral compute for the heavy batch job.
export INSTANCE_TYPE="${INSTANCE_TYPE:-m6i.2xlarge}"   # 8 vCPU / 32 GB — fits MS MARCO in RAM
export VOLUME_GB="${VOLUME_GB:-60}"
export MAX_RUN_MIN="${MAX_RUN_MIN:-300}"               # watchdog backstop; MS MARCO BM25 takes ~3.2h, so 5h leaves margin
export SPOT="${SPOT:-1}"                               # 1 = Spot pricing; 0 = on-demand fallback

# One-time resource names created by 00_setup_aws.sh.
export KEY_NAME="${KEY_NAME:-joho-key}"
export KEY_FILE="${KEY_FILE:-$HOME/.ssh/${KEY_NAME}.pem}"
export SG_NAME="${SG_NAME:-joho-sg}"
export ROLE_NAME="${ROLE_NAME:-joho-ec2-role}"
export PROFILE_NAME="${PROFILE_NAME:-joho-ec2-profile}"

# Budget alerts (USD). Set ALERT_EMAIL to enable budget creation in 00_setup_aws.sh.
export BUDGET_AMOUNT="${BUDGET_AMOUNT:-50}"
export ALERT_EMAIL="${ALERT_EMAIL:-}"

echo "[aws-env] profile=$AWS_PROFILE region=$REGION bucket=$BUCKET type=$INSTANCE_TYPE spot=$SPOT acct=${ACCOUNT_ID:-?}"
