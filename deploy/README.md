# Deploying Joho to GCP (P8)

This directory holds everything needed to run Joho on Google Cloud, **cost-engineered to
keep spend low** (scale-to-zero serving, ephemeral batch jobs, budget alerts). Nothing here
runs until you have a GCP project with **billing enabled** — the scripts are otherwise
idempotent and safe.

> Region defaults to **`asia-south1` (Mumbai)** — set `REGION` to your nearest region.

## The mental model

Three classes of compute, each chosen for cost:

| What | Where | Why | Idle cost |
|------|-------|-----|-----------|
| Live demo (engine + gateway + UI API) | **Cloud Run** (combined image) | scale-to-zero; pay only per request | **~$0** |
| Durable data (corpora, indexes, embeddings, run files) | **GCS bucket** | cheap object storage, single source of truth | ~cents/GB/mo |
| Heavy batch jobs (MS MARCO index build) | **ephemeral Spot VM** | 8x cheaper than on-demand; self-deletes | $0 when not running |

Cloud Run and the VMs are **stateless and disposable**; the GCS bucket is the only thing
that persists. That's the whole cost story: nothing is left running by accident.

## Files

| Script | Does |
|--------|------|
| `env.sh` | Shared config (project, region, image names, bucket, budget). Sourced by all. Override any value via env var. |
| `00_setup_gcp.sh` | One-time: enable APIs, create Artifact Registry repo + GCS bucket, wire a **budget with alerts at ~$50/$150/$250**, configure Docker auth. |
| `10_deploy_cloudrun.sh` | Cloud Build the combined image → deploy to Cloud Run (min-instances 0, max 3, concurrency 8). Prints the live URL. |
| `20_gcs_sync.sh` | `push` / `pull` / `list` data between local repo and the bucket. |
| `30_vm_job.sh` | Launch an **ephemeral Spot VM**, copy the repo, run a job script, tear the VM down. Two self-delete guards. |
| `marco_job.sh` | The MS MARCO ingestion + BM25 eval job (runs *on* the VM via `30_vm_job.sh`). |
| `entrypoint_combined.sh` | Container entrypoint: start `joho_server`, wait for it, then `exec uvicorn`. |

## Quickstart

```bash
# 0. Point at your project (or edit env.sh). Billing must already be enabled.
export PROJECT_ID=your-project-id

# 1. One-time setup: APIs, registry, bucket, budget alerts.
deploy/00_setup_gcp.sh

# 2. Seed the bucket with the SciFact data the demo serves.
deploy/20_gcs_sync.sh push data/beir_scifact_test

# 3. Build + deploy the live demo. Prints a public URL.
deploy/10_deploy_cloudrun.sh

# 4. (optional) Run the big MS MARCO job on a throwaway Spot VM.
deploy/30_vm_job.sh deploy/marco_job.sh
```

Then point the Vercel UI at the printed URL:

```
NEXT_PUBLIC_GATEWAY_URL=https://joho-xxxxx-el.a.run.app
```

## Cost guards (why this won't drain the credit)

- **Cloud Run min-instances=0** — an idle demo costs nothing; cold start spins it up.
- **max-instances=3, concurrency=8** — a traffic spike (or a crawler) can't fan out into a big bill.
- **Spot VMs** for batch — preemptible pricing, a fraction of on-demand.
- **`--max-run-duration` + `--instance-termination-action=DELETE`** — a job VM deletes
  *itself* after the cap even if your laptop disconnects or the job hangs. `30_vm_job.sh`
  also deletes it explicitly via an `EXIT` trap. A forgotten VM is nearly impossible.
- **Budget alerts** at 17% / 50% / 83% / 100% of the configured budget — email before, not after.
- **`asia-south1`** keeps egress local and latency low.

## Notes

- The combined Cloud Run image runs `joho_server` (C++ gRPC) and the FastAPI gateway in
  one container on `localhost` — simplest topology, no cross-service networking, and it
  scales to zero as a unit. Split images (`Dockerfile.engine` + `Dockerfile.gateway`,
  wired by `docker-compose.yml`) exist for local full-stack runs.
- Rerank is **off by default** on Cloud Run (`JOHO_ENABLE_RERANK=0`) to keep the instance
  small (2 GiB) and cold starts fast; dense + RRF still run. Flip it on if you bump memory.
- MS MARCO artifacts (multi-GB) live only in GCS, never in git or the image. The demo
  serves SciFact, which is small enough to bake into the image.
