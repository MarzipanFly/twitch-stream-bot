#!/usr/bin/env python3
"""Audit reviewed ApelsinBot training/evaluation JSONL without exporting content.

Hard failures: exact duplicate conversations, train/eval prompt leakage,
credentials and obvious private contact details in training examples.
Near-duplicate prompts are informational and need human review.
"""
from __future__ import annotations

from collections import Counter
from difflib import SequenceMatcher
import json
from pathlib import Path
import re
import sys
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
TRAIN = ROOT / "training" / "dialogues.jsonl"
EVAL = ROOT / "training" / "evaluation.jsonl"

# Target real secrets rather than words like "OAuth token" used in tutorials.
SECRET_PATTERNS = (
    re.compile(r"(?i)\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9_]{30,}\b"),
    re.compile(r"(?i)\bgithub_pat_[A-Za-z0-9_]{30,}\b"),
    re.compile(r"(?i)\b(?:sk-[A-Za-z0-9_-]{20,}|oauth:[A-Za-z0-9]{25,})\b"),
    re.compile(r"(?i)\b(?:access_token|refresh_token|client_secret)\s*[:=]\s*[\"']?[A-Za-z0-9_\-]{20,}"),
)
EMAIL = re.compile(r"(?<![\w.+-])[\w.+-]+@[\w.-]+\.[A-Za-z]{2,}(?![\w.-])")
TELEPHONE = re.compile(r"(?<!\w)\+?[1-9]\d[\d\s()-]{8,}\d(?!\w)")


def normalized(text: str) -> str:
    return " ".join(unicodedata.normalize("NFKC", text).casefold().split())


def records(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()]


def audit(train: list[dict], evals: list[dict]) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    notes: list[str] = []
    conversations: dict[tuple, str] = {}
    train_prompts: dict[str, str] = {}
    prompt_texts: list[tuple[str, str]] = []
    by_category: Counter[str] = Counter()

    for item in train:
        identifier = item["id"]
        by_category[str(item.get("category", "unknown"))] += 1
        turns = item["messages"]
        fingerprint = tuple((m["role"], normalized(m["content"])) for m in turns)
        if fingerprint in conversations:
            errors.append(f"duplicate training dialogue: {identifier} / {conversations[fingerprint]}")
        else:
            conversations[fingerprint] = identifier
        for turn in turns:
            value = turn["content"]
            if turn["role"] == "user":
                key = normalized(value)
                train_prompts.setdefault(key, identifier)
                if len(key) >= 20:
                    prompt_texts.append((key, identifier))
            if any(pattern.search(value) for pattern in SECRET_PATTERNS):
                errors.append(f"possible credential in training entry: {identifier}")
            if EMAIL.search(value) or TELEPHONE.search(value):
                errors.append(f"possible personal contact data in training entry: {identifier}")

    for item in evals:
        identifier = item["id"]
        prompt = item["prompt"]
        key = normalized(prompt)
        if key in train_prompts:
            errors.append(f"eval prompt duplicates training dialogue: {identifier} / {train_prompts[key]}")
        if any(pattern.search(prompt) for pattern in SECRET_PATTERNS):
            errors.append(f"possible credential in eval case: {identifier}")
        if EMAIL.search(prompt) or TELEPHONE.search(prompt):
            errors.append(f"possible personal contact data in eval case: {identifier}")

    # Give reviewers a bounded list of near-overlap candidates, not an
    # overconfident automatic failure (common phrasings are legitimate).
    close_pairs = []
    for item in evals:
        candidate = normalized(item["prompt"])
        if len(candidate) < 20:
            continue
        for query, identifier in prompt_texts:
            if abs(len(query) - len(candidate)) > max(len(query), len(candidate)) // 3:
                continue
            score = SequenceMatcher(None, candidate, query, autojunk=False).ratio()
            if score >= 0.92:
                close_pairs.append((score, item["id"], identifier))
    close_pairs.sort(reverse=True)
    for score, eval_id, train_id in close_pairs[:12]:
        notes.append(f"similar train/eval prompts ({score:.2f}): {eval_id} / {train_id}")

    categories = ", ".join(f"{key}={val}" for key, val in sorted(by_category.items()))
    notes.append(f"training categories: {categories}")
    notes.append(f"training dialogues: {len(train)}, evaluation cases: {len(evals)}")
    return errors, notes


def main() -> int:
    errors, notes = audit(records(TRAIN), records(EVAL))
    for issue in errors:
        print("FAIL:", issue)
    for issue in notes:
        print("INFO:", issue)
    if errors:
        print(f"AI data quality audit FAILED: {len(errors)} findings")
        return 1
    print("AI data quality audit passed (heuristic checks; human review still required)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print("AI data quality audit error:", exc, file=sys.stderr)
        sys.exit(1)
