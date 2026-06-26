# Eval harness (P1) — proving the ranking is correct

This is the **grading half** of the keystone phase. The C++ engine retrieves; this
Python harness measures *how good* the retrieval is, using standard IR metrics so
our numbers are comparable to published baselines.

```
dataset (ir_datasets)  ──export──▶  corpus.tsv + queries.tsv
                                          │
                                   joho_batch (C++ BM25)
                                          │
                                       run.txt  ──grade (ir_measures)──▶  nDCG@10, MRR@10, ...
```

## One-time setup

```bash
bash eval/setup_env.sh        # creates eval/.venv from public PyPI
```

> We install explicitly from public PyPI because this machine's global pip config
> pointed at a private work index. The venv keeps Joho's deps isolated.

Also build the engine if you haven't:

```bash
cmake -S engine -B engine/build && cmake --build engine/build
```

## Run a baseline

```bash
# Activate (or prefix commands with eval/.venv/bin/python)
source eval/.venv/bin/activate

# Small + fast — proves the whole pipeline end to end:
python eval/run_eval.py --dataset beir/scifact/test

# The headline baseline — BM25 MRR@10 ≈ 0.187:
python eval/run_eval.py --dataset msmarco-passage/dev/small --docs-dataset msmarco-passage
```

First run downloads + caches the dataset under `~/.ir_datasets` (slow once, then
instant). Outputs land in `data/<dataset>/` (gitignored).

## The pieces

| File | Role |
|---|---|
| `export_dataset.py` | dataset → `corpus.tsv` + `queries.tsv` (TSV the C++ engine reads) |
| `evaluate.py` | TREC run file + qrels → scorecard (nDCG@10, RR@10, Recall, MAP) |
| `run_eval.py` | one command: export → run engine → grade |
| `setup_env.sh` | create the isolated venv from public PyPI |

## Tuning BM25

`run_eval.py` forwards `--k1` and `--b` to the engine, e.g. the Anserini-tuned
MS MARCO setting:

```bash
python eval/run_eval.py --dataset msmarco-passage/dev/small \
  --docs-dataset msmarco-passage --k1 0.82 --b 0.68
```
