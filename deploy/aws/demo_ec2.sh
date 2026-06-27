#!/usr/bin/env bash
# Stand up the PERSISTENT live-demo backend on a single EC2 box (engine + gateway).
#
# Unlike 10_ec2_job.sh (ephemeral, self-terminating batch), this instance is meant to
# stay up so a public demo URL is always live. Cost control is therefore manual and
# explicit: stop it to pause the burn, start it to bring the demo back.
#
#   deploy/aws/demo_ec2.sh up        # open ports, allocate EIP, launch t3.medium
#   deploy/aws/demo_ec2.sh ip        # print the Elastic IP
#   deploy/aws/demo_ec2.sh stop      # pause billing (~$2/mo storage only); link goes dark
#   deploy/aws/demo_ec2.sh start     # bring the demo back on the SAME IP
#   deploy/aws/demo_ec2.sh destroy   # terminate instance + release EIP (full teardown)
set -euo pipefail
cd "$(dirname "$0")/../.."
source deploy/aws/env.sh

# Force a demo-appropriate type; env.sh exports a big BATCH default (m6i.2xlarge) that
# this account blocks, so we deliberately ignore it. Override with DEMO_INSTANCE_TYPE.
INSTANCE_TYPE="${DEMO_INSTANCE_TYPE:-t3.medium}"   # 2 vCPU / 4 GB — fits torch + BGE-small
DISK_GB="${DISK_GB:-20}"
NAME="${NAME:-joho-demo}"
STATE_FILE="deploy/aws/.demo_instance"
EIP_FILE="deploy/aws/.demo_eip"               # holds the allocation-id of the Elastic IP

[ -n "${ACCOUNT_ID:-}" ] || { echo "!! no credentials for profile '$AWS_PROFILE'."; exit 1; }
SG_ID="${SG_ID:-$(cat deploy/aws/.sg_id 2>/dev/null || true)}"
[ -n "$SG_ID" ] || { echo "!! no security group — run deploy/aws/00_setup_aws.sh first."; exit 1; }

iid()  { cat "$STATE_FILE" 2>/dev/null || true; }
eipid(){ cat "$EIP_FILE"  2>/dev/null || true; }

open_ports() {
  echo "==> ensuring SG $SG_ID opens 80 + 443 to the world (22 stays locked)"
  for p in 80 443; do
    aws ec2 authorize-security-group-ingress --group-id "$SG_ID" \
      --protocol tcp --port "$p" --cidr 0.0.0.0/0 >/dev/null 2>&1 \
      && echo "   opened $p" || echo "   $p already open"
  done
}

up() {
  [ -z "$(iid)" ] || { echo "!! instance $(iid) already tracked. Use start/destroy."; exit 1; }
  open_ports

  echo "==> resolving latest Ubuntu 22.04 (amd64) AMI"
  AMI="$(aws ssm get-parameters \
    --names /aws/service/canonical/ubuntu/server/22.04/stable/current/amd64/hvm/ebs-gp2/ami-id \
    --query 'Parameters[0].Value' --output text)"

  # Pre-install Docker + git so the box is ready to provision the second it's up.
  UD="$(mktemp)"
  cat > "$UD" <<'CLOUDINIT'
#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y docker.io git
systemctl enable --now docker
usermod -aG docker ubuntu
CLOUDINIT

  echo "==> launching $INSTANCE_TYPE ($DISK_GB GB gp3), shutdown=stop (persistent)"
  IID="$(aws ec2 run-instances \
    --image-id "$AMI" \
    --instance-type "$INSTANCE_TYPE" \
    --key-name "$KEY_NAME" \
    --security-group-ids "$SG_ID" \
    --instance-initiated-shutdown-behavior stop \
    --block-device-mappings "DeviceName=/dev/sda1,Ebs={VolumeSize=${DISK_GB},VolumeType=gp3,DeleteOnTermination=true}" \
    --user-data "file://$UD" \
    --tag-specifications "ResourceType=instance,Tags=[{Key=Name,Value=$NAME}]" \
    --query 'Instances[0].InstanceId' --output text)"
  rm -f "$UD"
  echo "$IID" > "$STATE_FILE"
  echo "   instance $IID — waiting for running..."
  aws ec2 wait instance-running --instance-ids "$IID"

  # Stable address: reuse a tracked EIP or allocate a fresh one, then associate.
  ALLOC="$(eipid)"
  if [ -z "$ALLOC" ]; then
    echo "==> allocating Elastic IP"
    ALLOC="$(aws ec2 allocate-address --domain vpc --query AllocationId --output text)"
    echo "$ALLOC" > "$EIP_FILE"
  fi
  aws ec2 associate-address --instance-id "$IID" --allocation-id "$ALLOC" >/dev/null
  ip
}

ip() {
  ALLOC="$(eipid)"; [ -n "$ALLOC" ] || { echo "!! no EIP tracked"; exit 1; }
  PIP="$(aws ec2 describe-addresses --allocation-ids "$ALLOC" \
        --query 'Addresses[0].PublicIp' --output text)"
  echo "$PIP"
}

case "${1:-up}" in
  up)      up ;;
  ip)      ip ;;
  stop)    aws ec2 stop-instances  --instance-ids "$(iid)" --query 'StoppingInstances[0].CurrentState.Name' --output text ;;
  start)   aws ec2 start-instances --instance-ids "$(iid)" --query 'StartingInstances[0].CurrentState.Name' --output text
           aws ec2 wait instance-running --instance-ids "$(iid)"
           aws ec2 associate-address --instance-id "$(iid)" --allocation-id "$(eipid)" >/dev/null
           echo "demo back at $(ip)" ;;
  destroy) aws ec2 terminate-instances --instance-ids "$(iid)" >/dev/null 2>&1 || true
           aws ec2 wait instance-terminated --instance-ids "$(iid)" 2>/dev/null || true
           aws ec2 release-address --allocation-id "$(eipid)" 2>/dev/null || true
           rm -f "$STATE_FILE" "$EIP_FILE"; echo "destroyed." ;;
  *) echo "usage: $0 {up|ip|stop|start|destroy}"; exit 1 ;;
esac
