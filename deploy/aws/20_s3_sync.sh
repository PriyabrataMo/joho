#!/usr/bin/env bash
# Move data (corpora, on-disk indexes, run files, job outputs) between the local repo
# and the S3 bucket — the AWS mirror of 20_gcs_sync.sh. The bucket is the durable store;
# EC2 instances are ephemeral and pull what they need from it.
#
#   deploy/aws/20_s3_sync.sh push data/beir_scifact_test            # local dir -> s3://.../beir_scifact_test
#   deploy/aws/20_s3_sync.sh pull msmarco_passage_dev_small ./data  # bucket subpath -> local
#   deploy/aws/20_s3_sync.sh list                                   # what's in the bucket
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh

cmd="${1:-}"
case "$cmd" in
  push)
    SRC="${2:?usage: push <local_dir> [dest_subpath]}"
    DEST="${3:-$(basename "$SRC")}"
    echo "==> pushing $SRC -> s3://$BUCKET/$DEST"
    aws s3 sync "$SRC" "s3://$BUCKET/$DEST"
    ;;
  pull)
    SUB="${2:?usage: pull <bucket_subpath> <local_dir>}"
    DEST="${3:?usage: pull <bucket_subpath> <local_dir>}"
    echo "==> pulling s3://$BUCKET/$SUB -> $DEST/$SUB"
    mkdir -p "$DEST/$SUB"
    aws s3 sync "s3://$BUCKET/$SUB" "$DEST/$SUB"
    ;;
  list)
    aws s3 ls --recursive "s3://$BUCKET" ;;
  *)
    echo "usage: $0 {push <dir> [dest] | pull <subpath> <dir> | list}"; exit 1 ;;
esac
