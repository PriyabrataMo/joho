#!/usr/bin/env bash
# Move data (corpora, on-disk indexes, embeddings, run files) between the local repo
# and the GCS bucket (P8). The bucket is the durable store; VMs and Cloud Run are
# ephemeral and pull what they need from it.
#
#   deploy/20_gcs_sync.sh push data/beir_scifact_test       # local dir -> gs://.../beir_scifact_test
#   deploy/20_gcs_sync.sh pull beir_scifact_test ./data      # bucket subpath -> local
#   deploy/20_gcs_sync.sh list                               # what's in the bucket
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh
GC="$GCLOUD_BIN"

cmd="${1:-}"
case "$cmd" in
  push)
    SRC="${2:?usage: push <local_dir> [dest_subpath]}"
    DEST="${3:-$(basename "$SRC")}"
    echo "==> pushing $SRC -> $BUCKET/$DEST"
    "$GC" storage rsync -r "$SRC" "$BUCKET/$DEST"
    ;;
  pull)
    SUB="${2:?usage: pull <bucket_subpath> <local_dir>}"
    DEST="${3:?usage: pull <bucket_subpath> <local_dir>}"
    echo "==> pulling $BUCKET/$SUB -> $DEST"
    mkdir -p "$DEST"
    "$GC" storage rsync -r "$BUCKET/$SUB" "$DEST/$SUB"
    ;;
  list)
    "$GC" storage ls -r "$BUCKET" ;;
  *)
    echo "usage: $0 {push <dir> [dest] | pull <subpath> <dir> | list}"; exit 1 ;;
esac
