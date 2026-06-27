#!/usr/bin/env bash
# Remove the one-time AWS resources created by 00_setup_aws.sh. Ephemeral instances
# terminate themselves; this cleans up the durable scaffolding (security group, key
# pair, IAM role + instance profile) and, optionally, the bucket. Run when you're
# fully done with the AWS path. Idempotent.
#
#   deploy/aws/99_teardown_aws.sh                 # keep the bucket (and its data)
#   DELETE_BUCKET=1 deploy/aws/99_teardown_aws.sh # also empty + delete the bucket
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh

echo "==> terminating any running joho job instances"
IIDS="$(aws ec2 describe-instances \
  --filters Name=tag:project,Values=joho \
            Name=instance-state-name,Values=pending,running,stopping,stopped \
  --query 'Reservations[].Instances[].InstanceId' --output text)"
if [ -n "$IIDS" ]; then
  aws ec2 terminate-instances --instance-ids $IIDS >/dev/null
  echo "   $IIDS — waiting for termination (so the SG can be freed)"
  aws ec2 wait instance-terminated --instance-ids $IIDS
else
  echo "   (none)"
fi

echo "==> deleting security group $SG_NAME"
SG_ID="$(cat .sg_id 2>/dev/null || true)"
if [ -n "$SG_ID" ] && aws ec2 delete-security-group --group-id "$SG_ID" 2>/dev/null; then
  rm -f .sg_id; echo "   deleted $SG_ID"
else
  echo "   (not found or already gone)"
fi

echo "==> deleting key pair $KEY_NAME"
aws ec2 delete-key-pair --key-name "$KEY_NAME" 2>/dev/null || true
rm -f "$KEY_FILE" 2>/dev/null || true

echo "==> deleting instance profile + role"
aws iam remove-role-from-instance-profile --instance-profile-name "$PROFILE_NAME" --role-name "$ROLE_NAME" 2>/dev/null || true
aws iam delete-instance-profile --instance-profile-name "$PROFILE_NAME" 2>/dev/null || true
aws iam delete-role-policy --role-name "$ROLE_NAME" --policy-name joho-s3 2>/dev/null || true
aws iam delete-role --role-name "$ROLE_NAME" 2>/dev/null || true

if [ "${DELETE_BUCKET:-0}" = "1" ]; then
  echo "==> emptying + deleting bucket s3://$BUCKET"
  aws s3 rb "s3://$BUCKET" --force
else
  echo "==> keeping bucket s3://$BUCKET (set DELETE_BUCKET=1 to remove it)"
fi
echo "Done."
