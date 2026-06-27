# Running the MS MARCO job on AWS (ephemeral EC2)

The AWS mirror of the GCP Spot-VM path: spin up a cheap **Spot EC2**, copy the repo, run
the MS MARCO BM25 index + eval job, push the results to **S3**, and **terminate the
instance**. Nothing is left running.

> Uses a dedicated `joho` AWS profile (personal account) so this never touches work
> billing. Region defaults to `ap-south-1` (Mumbai) — set `REGION` to your nearest one.

## The mental model

| What | Where | Idle cost |
|------|-------|-----------|
| Durable data (corpus, index, run file, scorecard) | **S3 bucket** | ~cents/GB/mo |
| Heavy batch job (MS MARCO index build + grade) | **ephemeral Spot EC2** | **$0 when not running** |

The instance is **stateless and disposable**; the S3 bucket is the only thing that
persists. That's the whole cost story.

## Files

| Script | Does |
|--------|------|
| `env.sh` | Shared config (profile, region, instance type, bucket, budget). Sourced by all. Override any value via env var. |
| `00_setup_aws.sh` | One-time: private S3 bucket, EC2 key pair, a security group (**SSH from your IP only**), an IAM instance profile that lets the instance write to the bucket without keys, and an optional budget with alerts. |
| `10_ec2_job.sh` | Launch an **ephemeral Spot EC2**, copy the repo, run a job script, terminate the instance. **Three** self-terminate guards. |
| `marco_job.sh` | The MS MARCO ingestion + BM25 eval job (runs *on* the instance via `10_ec2_job.sh`). |
| `20_s3_sync.sh` | `push` / `pull` / `list` data between the local repo and the bucket. |
| `99_teardown_aws.sh` | Remove the one-time scaffolding (SG, key, role); `DELETE_BUCKET=1` also drops the bucket. |

## Quickstart

```bash
# 0. One-time: add the personal profile (secret stays in your terminal, never in git).
aws configure --profile joho

# 1. One-time: bucket, key pair, security group, instance profile, (optional) budget.
deploy/aws/00_setup_aws.sh
#   ...to also get email budget alerts:
#   ALERT_EMAIL=you@example.com deploy/aws/00_setup_aws.sh

# 2. Run MS MARCO on a throwaway Spot EC2. Pushes results to S3, then terminates.
deploy/aws/10_ec2_job.sh deploy/aws/marco_job.sh

# 3. Pull the scorecard back down.
deploy/aws/20_s3_sync.sh pull msmarco_passage_dev_small ./data
cat data/msmarco_passage_dev_small/scorecard.txt

# 4. When fully done, tear the scaffolding down.
deploy/aws/99_teardown_aws.sh                 # keep the bucket
DELETE_BUCKET=1 deploy/aws/99_teardown_aws.sh # nuke everything
```

## Cost guards (why a forgotten instance is nearly impossible)

- **Spot pricing** — `m6i.2xlarge` Spot is a fraction of on-demand. Set `SPOT=0` to fall
  back to on-demand if Spot capacity is unavailable.
- **Boot-time watchdog** — user-data runs `shutdown -h now` after `MAX_RUN_MIN` (default
  120), and the instance is launched with `--instance-initiated-shutdown-behavior
  terminate`, so it **terminates itself** even if the job hangs or your laptop disconnects.
- **EXIT trap** — `10_ec2_job.sh` terminates the instance explicitly when it finishes (or
  is interrupted).
- **Root volume `DeleteOnTermination=true`** — no orphaned EBS volumes.
- **Locked-down SSH** — the security group only allows port 22 from your current public IP.
- **Optional budget** — set `ALERT_EMAIL` for email alerts at 50% / 80% / forecast-100%
  of `$BUDGET_AMOUNT` (default $50).

## Notes

- The instance writes to S3 via an **IAM instance profile** (assumed through the instance
  metadata service) — no access keys are ever copied onto the box.
- MS MARCO artifacts (multi-GB) live only in S3, never in git. The instance is sized for
  `dev/small`; bump `INSTANCE_TYPE` for the full dev/eval sets.
- This is the batch-job half of the AWS story. The live demo still runs on the GCP path
  (Cloud Run) in [`../README.md`](../README.md); an equivalent App Runner deployment
  would be the natural next step.
