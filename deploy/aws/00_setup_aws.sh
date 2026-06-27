#!/usr/bin/env bash
# One-time AWS setup for the Joho MS MARCO job:
#   * S3 bucket (private) for corpora / indexes / run files / job outputs
#   * EC2 key pair (private key saved locally, 0600)
#   * a locked-down security group (SSH from YOUR public IP only)
#   * an IAM instance profile so the instance can write results to the bucket
#     WITHOUT embedded keys (it assumes the role via the instance metadata service)
#   * (optional) a monthly budget with email alerts
# Idempotent — safe to re-run.
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh

[ -n "$ACCOUNT_ID" ] || { echo "!! no credentials for profile '$AWS_PROFILE'.
   Run:  aws configure --profile $AWS_PROFILE   (then re-run this script)"; exit 1; }

echo "==> [1/5] S3 bucket s3://$BUCKET ($REGION)"
if ! aws s3api head-bucket --bucket "$BUCKET" 2>/dev/null; then
  if [ "$REGION" = "us-east-1" ]; then
    aws s3api create-bucket --bucket "$BUCKET" --region "$REGION" >/dev/null
  else
    aws s3api create-bucket --bucket "$BUCKET" --region "$REGION" \
      --create-bucket-configuration LocationConstraint="$REGION" >/dev/null
  fi
  aws s3api put-public-access-block --bucket "$BUCKET" \
    --public-access-block-configuration \
    BlockPublicAcls=true,IgnorePublicAcls=true,BlockPublicPolicy=true,RestrictPublicBuckets=true
  echo "   created (private)"
else
  echo "   exists"
fi

echo "==> [2/5] EC2 key pair $KEY_NAME"
if [ -s "$KEY_FILE" ]; then     # -s: exists AND non-empty (a truncated empty file is not "present")
  echo "   local key present: $KEY_FILE"
elif aws ec2 describe-key-pairs --key-names "$KEY_NAME" >/dev/null 2>&1; then
  echo "   !! key '$KEY_NAME' exists in AWS but $KEY_FILE is missing locally."
  echo "      Delete it (aws ec2 delete-key-pair --key-name $KEY_NAME) and re-run, or set KEY_NAME."
  exit 1
else
  rm -f "$KEY_FILE"             # clear any leftover empty file from a previous failed run
  TMP_KEY="$(mktemp)"
  # write to a temp file first; only move into place if the call actually succeeded
  if aws ec2 create-key-pair --key-name "$KEY_NAME" --query KeyMaterial --output text > "$TMP_KEY"; then
    install -m 600 "$TMP_KEY" "$KEY_FILE" && rm -f "$TMP_KEY"
    echo "   created -> $KEY_FILE"
  else
    rm -f "$TMP_KEY"; echo "   !! create-key-pair failed"; exit 1
  fi
fi

echo "==> [3/5] security group $SG_NAME (SSH from your IP only)"
VPC_ID="$(aws ec2 describe-vpcs --filters Name=isDefault,Values=true \
  --query 'Vpcs[0].VpcId' --output text)"
SG_ID="$(aws ec2 describe-security-groups \
  --filters Name=group-name,Values="$SG_NAME" Name=vpc-id,Values="$VPC_ID" \
  --query 'SecurityGroups[0].GroupId' --output text 2>/dev/null || true)"
if [ "$SG_ID" = "None" ] || [ -z "$SG_ID" ]; then
  SG_ID="$(aws ec2 create-security-group --group-name "$SG_NAME" \
    --description "Joho ephemeral job SSH" --vpc-id "$VPC_ID" --query GroupId --output text)"
  echo "   created $SG_ID in $VPC_ID"
fi
MYIP="$(curl -fsS https://checkip.amazonaws.com | tr -d '[:space:]')"
if aws ec2 authorize-security-group-ingress --group-id "$SG_ID" \
     --protocol tcp --port 22 --cidr "${MYIP}/32" 2>/dev/null; then
  echo "   allowed SSH from ${MYIP}/32"
else
  echo "   (SSH rule already present for ${MYIP}/32)"
fi
echo "$SG_ID" > .sg_id    # cached for 10_ec2_job.sh

echo "==> [4/5] IAM instance profile $PROFILE_NAME (write to s3://$BUCKET only)"
if ! aws iam get-role --role-name "$ROLE_NAME" >/dev/null 2>&1; then
  aws iam create-role --role-name "$ROLE_NAME" --assume-role-policy-document \
    '{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":{"Service":"ec2.amazonaws.com"},"Action":"sts:AssumeRole"}]}' >/dev/null
  echo "   created role"
fi
aws iam put-role-policy --role-name "$ROLE_NAME" --policy-name joho-s3 --policy-document \
  "{\"Version\":\"2012-10-17\",\"Statement\":[{\"Effect\":\"Allow\",\"Action\":[\"s3:PutObject\",\"s3:GetObject\",\"s3:ListBucket\"],\"Resource\":[\"arn:aws:s3:::$BUCKET\",\"arn:aws:s3:::$BUCKET/*\"]}]}"
if ! aws iam get-instance-profile --instance-profile-name "$PROFILE_NAME" >/dev/null 2>&1; then
  aws iam create-instance-profile --instance-profile-name "$PROFILE_NAME" >/dev/null
  aws iam add-role-to-instance-profile --instance-profile-name "$PROFILE_NAME" --role-name "$ROLE_NAME"
  echo "   created instance profile; waiting ~10s for IAM to propagate"
  sleep 10
fi

echo "==> [5/5] budget"
if [ -n "$ALERT_EMAIL" ]; then
  aws budgets create-budget --account-id "$ACCOUNT_ID" \
    --budget "{\"BudgetName\":\"joho-budget\",\"BudgetLimit\":{\"Amount\":\"$BUDGET_AMOUNT\",\"Unit\":\"USD\"},\"TimeUnit\":\"MONTHLY\",\"BudgetType\":\"COST\"}" \
    --notifications-with-subscribers "[{\"Notification\":{\"NotificationType\":\"ACTUAL\",\"ComparisonOperator\":\"GREATER_THAN\",\"Threshold\":50},\"Subscribers\":[{\"SubscriptionType\":\"EMAIL\",\"Address\":\"$ALERT_EMAIL\"}]},{\"Notification\":{\"NotificationType\":\"ACTUAL\",\"ComparisonOperator\":\"GREATER_THAN\",\"Threshold\":80},\"Subscribers\":[{\"SubscriptionType\":\"EMAIL\",\"Address\":\"$ALERT_EMAIL\"}]},{\"Notification\":{\"NotificationType\":\"FORECASTED\",\"ComparisonOperator\":\"GREATER_THAN\",\"Threshold\":100},\"Subscribers\":[{\"SubscriptionType\":\"EMAIL\",\"Address\":\"$ALERT_EMAIL\"}]}]" \
    2>/dev/null && echo "   created \$${BUDGET_AMOUNT} budget, alerts to $ALERT_EMAIL" \
    || echo "   (budget may already exist — skipping)"
else
  echo "   (skipped — set ALERT_EMAIL to create a \$${BUDGET_AMOUNT} budget with alerts)"
fi

echo
echo "Done. Next:"
echo "  deploy/aws/20_s3_sync.sh push data/beir_scifact_test   # (optional) seed the bucket"
echo "  deploy/aws/10_ec2_job.sh deploy/aws/marco_job.sh       # run MS MARCO on a throwaway Spot EC2"
