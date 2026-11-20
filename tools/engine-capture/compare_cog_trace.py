"""Compare a RED assistant-check result to a target engine's accepted handler trace."""
import argparse
import json
from pathlib import Path, PureWindowsPath


# JK sithCog message numbers used by the stock door and elevator scenarios.
MESSAGES = {1: "activate", 3: "startup", 4: "timer", 11: "arrived"}


def event(message, sender_type, sender_ref):
    # The engine and simulator use different placeholders for an absent sender.
    return [message, sender_type, sender_ref if sender_type else None]


def compare(check, log, script):
    expected = [event(e["message"], e["senderType"], e["senderRef"])
                for e in check["breakpoints"]]
    observed = []
    for line in log.splitlines():
        if not line.startswith("RED_R11_MESSAGE\t"):
            continue
        _, source, message, kind, reference = line.split("\t")
        if PureWindowsPath(source).name.casefold() == script.casefold():
            observed.append(event(MESSAGES.get(int(message), "#" + message), int(kind), int(reference)))
    return {"script": script, "expected": expected, "observed": observed,
            "passed": bool(check["passed"] and expected and expected == observed)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assistant_check", type=Path)
    parser.add_argument("engine_stderr", type=Path)
    parser.add_argument("script", help="stock script basename, e.g. 00_door.cog")
    args = parser.parse_args()
    result = compare(json.loads(args.assistant_check.read_text(encoding="utf-8")),
                     args.engine_stderr.read_text(encoding="utf-8", errors="replace"), args.script)
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
