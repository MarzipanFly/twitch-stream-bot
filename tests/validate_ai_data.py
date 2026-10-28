#!/usr/bin/env python3
"""Validate reviewed AI knowledge and dialogue/evaluation JSONL (stdlib only)."""
import json
import re
import sys
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
KNOWLEDGE = ROOT / "knowledge" / "facts.tsv"
DIALOGUES = ROOT / "training" / "dialogues.jsonl"
EVAL = ROOT / "training" / "evaluation.jsonl"
SEMANTIC = ROOT / "src" / "ai_semantic.c"
TRUSTED_HOSTS = {
    "github.com", "www.curseforge.com", "blog.scssoft.com", "www.ea.com",
    "kineticgames.co.uk", "www.kineticgames.co.uk", "worldoftanks.eu",
    "store.steampowered.com", "wiki.warthunder.com", "playvalorant.com",
    "www.counter-strike.net", "www.leagueoflegends.com",
    "www.rocketleague.com", "wiki.factorio.com",
    "help.steampowered.com", "www.epicgames.com"
}


def require(condition, detail):
    if not condition:
        raise ValueError(detail)


def read_jsonl(path):
    items = []
    ids = set()
    for i, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        obj = json.loads(line)
        require(isinstance(obj, dict), "%s:%d must be an object" % (path, i))
        key = obj.get("id")
        require(isinstance(key, str) and key and key not in ids,
                "%s:%d has an empty or duplicate id" % (path, i))
        ids.add(key)
        items.append(obj)
    return items


def main():
    knowledge = KNOWLEDGE.read_text(encoding="utf-8")
    titles = set()
    count = 0
    for line_no, line in enumerate(knowledge.splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split("\t")
        require(len(fields) == 4, "facts.tsv:%d needs 4 TSV columns" % line_no)
        title, aliases, facts, url = fields
        require(title and aliases and len(facts) >= 45 and url,
                "facts.tsv:%d has a missing field" % line_no)
        require(title not in titles, "Duplicate topic: " + title)
        titles.add(title)
        require(len((line + "\n").encode("utf-8")) < 1600,
                "facts.tsv:%d exceeds SEM_MAX_LINE" % line_no)
        parsed = urlsplit(url)
        require(parsed.scheme == "https" and parsed.hostname in TRUSTED_HOSTS,
                "Unreviewed host in facts.tsv:%d: %s" % (line_no, url))
        count += 1

    match = re.search(r"#define\s+SEM_MAX_DOCS\s+(\d+)", SEMANTIC.read_text(encoding="utf-8"))
    require(match is not None, "SEM_MAX_DOCS constant missing")
    maximum = int(match.group(1))
    require(count <= maximum,
            "Semantic index truncates %d topics (SEM_MAX_DOCS=%d)" % (count, maximum))
    require(count >= 70, "Knowledge base unexpectedly small: %d topics" % count)

    dialogues = read_jsonl(DIALOGUES)
    for ex in dialogues:
        messages = ex.get("messages", [])
        require(len(messages) >= 2 and len(messages) % 2 == 0,
                "Bad dialogue turn count: " + ex["id"])
        for i, message in enumerate(messages):
            require(isinstance(message, dict) and
                    message.get("role") == ("user" if i % 2 == 0 else "assistant") and
                    isinstance(message.get("content"), str) and message["content"].strip(),
                    "Bad role or content in " + ex["id"])
    require(len(dialogues) >= 60, "Not enough dialogue examples")

    cases = read_jsonl(EVAL)
    for case in cases:
        prompt = case.get("prompt")
        require(isinstance(prompt, str) and prompt.strip(),
                "Bad eval prompt: " + case["id"])
        require(len(prompt.encode("utf-8")) <= 480,
                "Eval prompt exceeds AI_QUESTION_BYTES: " + case["id"])

    print("AI data validation passed")
    print("Knowledge entries: %d / %d max" % (count, maximum))
    print("Curated dialogue examples: %d" % len(dialogues))
    print("Evaluation scenarios: %d" % len(cases))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, json.JSONDecodeError) as exc:
        print("AI data validation failed: %s" % exc, file=sys.stderr)
        sys.exit(1)
