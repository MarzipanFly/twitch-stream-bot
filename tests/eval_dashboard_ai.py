#!/usr/bin/env python3
"""End-to-end, local-only ApelsinBot AI regression runner (stdlib only).

Requires running twitchbot.exe with Ollama enabled. No stream/OBS required.
Every test stays within 127.0.0.1:8765, never sends a Twitch chat message.
This is a heuristic review tool, not an objective proof of LLM correctness.
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


DEFAULT_CASES = Path(__file__).resolve().parents[1] / "training" / "evaluation.jsonl"


def read_cases(path):
    cases = []
    ids = set()
    with path.open("r", encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            if not line.strip():
                continue
            case = json.loads(line)
            if not isinstance(case, dict) or not isinstance(case.get("id"), str):
                raise ValueError("Bad case on line %d" % lineno)
            if case["id"] in ids:
                raise ValueError("Duplicate case: %s" % case["id"])
            if not isinstance(case.get("prompt"), str) or not case["prompt"]:
                raise ValueError("Missing prompt: %s" % case["id"])
            if len(case["prompt"].encode("utf-8")) > 480:
                raise ValueError("Prompt too long for native AI worker: %s" % case["id"])
            ids.add(case["id"])
            cases.append(case)
    return cases


def get_status(base):
    req = urllib.request.Request(base + "/api/status",
                                 headers={"Cache-Control": "no-store"})
    with urllib.request.urlopen(req, timeout=8) as response:
        return json.load(response)


def control(base, action, message=None):
    fields = {"action": action}
    if message is not None:
        fields["message"] = message
    encoded = urllib.parse.urlencode(fields).encode("ascii")
    req = urllib.request.Request(
        base + "/api/control", data=encoded, method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded",
                 "X-Apelsin-Local": "1", "Origin": base})
    with urllib.request.urlopen(req, timeout=8) as response:
        if response.status != 202:
            raise RuntimeError("Dashboard rejected control request: %d" %
                               response.status)


def wait_sequence(base, previous, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = get_status(base)
        if (int(state.get("ai_test_seq", 0)) != previous and
                not state.get("ai_test_pending", False)):
            return state
        time.sleep(0.75)
    raise TimeoutError("No finished AI result within %.0f seconds" % timeout)


def check_answer(case, answer, previous):
    lowered = answer.casefold()
    warnings = []
    if not answer.strip():
        warnings.append("empty_response")
    for term in case.get("avoid", []):
        if term.casefold() in lowered:
            warnings.append("unrelated_or_forbidden: " + term)
    terms = case.get("look_for", [])
    if terms and not any(term.casefold() in lowered for term in terms):
        warnings.append("missing_expected_topic_terms")
    if answer.strip() and answer.strip() == previous:
        warnings.append("exact_repeat")
    generic = ("я — apelsinbot", "я искусственный интеллект",
               "проверь рецепт в jei/emi")
    if any(word in lowered for word in generic):
        warnings.append("generic_or_offtopic_template")
    return warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, default=DEFAULT_CASES)
    parser.add_argument("--url", default="http://127.0.0.1:8765")
    parser.add_argument("--limit", type=int, default=10,
                        help="number of scenarios (default 10; 0 means all)")
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--list", action="store_true", help="show cases only")
    parser.add_argument("--output", type=Path, help="optional JSON report path")
    args = parser.parse_args()

    parsed = urllib.parse.urlsplit(args.url)
    if parsed.scheme != "http" or parsed.hostname != "127.0.0.1" or parsed.path:
        parser.error("Only local http://127.0.0.1:<port> is allowed")
    base = args.url.rstrip("/")
    cases = read_cases(args.cases)
    if args.limit > 0:
        cases = cases[:args.limit]
    if args.list:
        for case in cases:
            print("%s [%s]: %s" % (case["id"], case.get("game", "?"),
                                   case["prompt"]))
        print("Total: %d" % len(cases))
        return 0

    state = get_status(base)
    if not state.get("ai"):
        raise RuntimeError("AI is disabled in the dashboard; enable Ollama AI first.")

    report = []
    prior_thread = None
    previous_reply = ""
    for index, case in enumerate(cases, 1):
        thread = case.get("thread", case["id"])
        if thread != prior_thread:
            before = int(get_status(base).get("ai_test_seq", 0))
            control(base, "ai_test_reset")
            wait_sequence(base, before, 12)
            previous_reply = ""
            prior_thread = thread
        before = int(get_status(base).get("ai_test_seq", 0))
        control(base, "ai_test", case["prompt"])
        result = wait_sequence(base, before, args.timeout)
        answer = result.get("ai_test_reply", "")
        warnings = check_answer(case, answer, previous_reply)
        previous_reply = answer
        item = {"id": case["id"], "thread": thread, "prompt": case["prompt"],
                "reply": answer, "review_flags": warnings,
                "history_turns": result.get("ai_test_turns", 0),
                "review_goal": case.get("goal", "")}
        report.append(item)
        print("\n[%d/%d] %s" % (index, len(cases), case["id"]))
        print("Viewer: " + case["prompt"])
        print("ApelsinBot: " + answer)
        print("Review flags: " + (", ".join(warnings) or "none"))
        print("Test-only history turns: " + str(item["history_turns"]))
    if args.output:
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2),
                               encoding="utf-8")
        print("\nReport saved: " + str(args.output))
    flagged = sum(bool(item["review_flags"]) for item in report)
    print("\nFinished: %d scenarios; %d flagged for manual review." %
          (len(report), flagged))
    print("These are heuristic checks. Read the replies and assess quality manually.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, TimeoutError,
            urllib.error.URLError) as exc:
        print("Evaluation failed: %s" % exc, file=sys.stderr)
        sys.exit(1)
