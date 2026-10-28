#!/usr/bin/env python3
"""Prepare ApelsinBot's reviewed synthetic dialogues for supervised fine-tuning.

Keeps whole dialogue records together when splitting, so turns from one dialogue
cannot leak into both training and validation. Outputs are local-only artifacts.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "training" / "dialogues.jsonl"
DEFAULT_OUT = ROOT / "training" / "generated"


def read_records(path):
    records, ids, seen_user_text = [], set(), {}
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
        rid = row.get("id") if isinstance(row, dict) else None
        messages = row.get("messages") if isinstance(row, dict) else None
        if not isinstance(rid, str) or not rid or rid in ids:
            raise ValueError(f"{path}:{line_no}: empty or duplicate id")
        if not isinstance(messages, list) or len(messages) < 2 or len(messages) % 2:
            raise ValueError(f"{path}:{line_no}: expected complete user/assistant turns")
        for i, msg in enumerate(messages):
            expected = "user" if i % 2 == 0 else "assistant"
            if (not isinstance(msg, dict) or msg.get("role") != expected or
                    not isinstance(msg.get("content"), str) or not msg["content"].strip()):
                raise ValueError(f"{path}:{line_no}: invalid message {i}; expected {expected}")
        for msg in messages:
            if msg["role"] == "user":
                normalized = " ".join(msg["content"].casefold().split())
                previous = seen_user_text.get(normalized)
                if previous:
                    raise ValueError(
                        f"{path}:{line_no}: duplicate user message in {rid}; first seen in {previous}"
                    )
                seen_user_text[normalized] = rid
        ids.add(rid)
        records.append({"id": rid, "category": row.get("category", "general"),
                        "game": row.get("game", "general"), "messages": messages})
    if len(records) < 30:
        raise ValueError(f"Only {len(records)} dialogues; collect more reviewed examples first")
    return records


def split_records(records, seed, validation_percent):
    def score(record):
        raw = f"{seed}:{record['id']}".encode("utf-8")
        return int(hashlib.sha256(raw).hexdigest()[:8], 16) % 100
    train, validation = [], []
    for record in records:
        (validation if score(record) < validation_percent else train).append(record)
    if not train or not validation:
        raise ValueError("Split produced an empty partition; change seed or validation percentage")
    return train, validation


def write_jsonl(path, records):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        for record in records:
            # Chat-template-compatible conversational format for TRL SFTTrainer.
            fh.write(json.dumps({"messages": record["messages"]},
                                ensure_ascii=False, separators=(",", ":")) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--seed", type=int, default=20261028)
    parser.add_argument("--validation-percent", type=int, default=10)
    args = parser.parse_args()
    if not 5 <= args.validation_percent <= 25:
        parser.error("--validation-percent must be between 5 and 25")
    records = read_records(args.input)
    train, validation = split_records(records, args.seed, args.validation_percent)
    write_jsonl(args.out_dir / "sft_train.jsonl", train)
    write_jsonl(args.out_dir / "sft_validation.jsonl", validation)
    manifest = {
        "source": str(args.input),
        "seed": args.seed,
        "validation_percent": args.validation_percent,
        "total_dialogues": len(records),
        "train_dialogues": len(train),
        "validation_dialogues": len(validation),
        "note": "Synthetic curated examples only; review facts and evaluate before training."
    }
    (args.out_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print(f"Dataset preparation failed: {exc}", file=sys.stderr)
        sys.exit(1)
