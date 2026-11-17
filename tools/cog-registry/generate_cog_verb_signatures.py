#!/usr/bin/env python3
"""Generate COG verb signatures for the sed_core COG compiler.

A COG verb takes its arguments off the VM stack, last argument first, and may
push one result. The retail executables record nothing about either, so the
signatures come from OpenJKDF2's reimplementation of each verb (0BSD; see
NOTICE-RED.md): the order of a verb's sithCogExec_Pop* calls, reversed, is its
argument list, and its sithCogExec_Push* calls give its result.

The measured retail registries decide which function a name runs. When names
share one implementation address in a retail binary they get one signature;
that is how `modifycoloreffect` gets FreeColorEffect's single argument, as the
retail JK and MotS executables register it.

Usage:
    python generate_cog_verb_signatures.py --openjkdf2 SRC --refs DIR
        [--revision TEXT] [--json OUT.json] [--out HEADER]

SRC is an OpenJKDF2 checkout's src directory. DIR holds the measured
jk101-retail-registry.json and mots-retail-registry.json. Output is
deterministic for identical inputs.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

FILES = ["sithCog.c", "jkCog.c", "sithCogFunction.c", "sithCogFunctionAI.c", "sithCogFunctionPlayer.c",
         "sithCogFunctionSector.c", "sithCogFunctionSound.c", "sithCogFunctionSurface.c", "sithCogFunctionThing.c"]
POP = {"PopInt": "int", "PopFlex": "flex", "PopVector": "vector", "PopThing": "thing", "PopSector": "sector",
       "PopSurface": "surface", "PopSound": "sound", "PopTemplate": "template", "PopMaterial": "material",
       "PopModel3": "model", "PopKeyframe": "keyframe", "PopCog": "cog", "PopAIClass": "ai", "PopString": "string",
       "PopSymbol": "symbol", "PopStack": "any", "PopArray": "array", "PopSymbolFunc": "verb"}
PUSH = {"PushInt": "int", "PushFlex": "flex", "PushVector": "vector", "Push3Floats": "vector", "PushStack": "any"}


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def function_bodies(text: str) -> dict[str, tuple[str, str, bool]]:
    """Every function with a `sithCog *` parameter: its body, that parameter's name, and whether it is the only one."""
    bodies = {}
    for m in re.finditer(r"^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\(([^)]*)\)\s*\{", text, re.M):
        params = m.group(2)
        cog = re.search(r"(?:struct\s+)?sithCog\s*\*\s*(\w+)", params)
        if not cog:
            continue
        i, depth = m.end(), 1
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        bodies[m.group(1)] = (text[m.end():i - 1], cog.group(1), "," not in params)
    return bodies


def call_arguments(body: str, start: int) -> str:
    """The text between the parenthesis at `start` and its match."""
    depth, i = 0, start
    while i < len(body):
        depth += {"(": 1, ")": -1}.get(body[i], 0)
        if depth == 0:
            return body[start + 1:i]
        i += 1
    return body[start + 1:]


def conditional(body: str, at: int) -> bool:
    """Whether the code at `at` runs only on some paths: inside a nested block, or the body of a braceless
    if, else, while or for. A call inside an if's own condition always runs."""
    before = body[:at]
    if before.count("{") - before.count("}") > 0:
        # Inside a block: fine only when every enclosing block is the body of an `if` that tests a pop
        # (`if (sithCogExec_PopVector(...))`), which fails only for a bad argument, not on a normal call.
        depth, i = 0, at - 1
        while i >= 0:
            c = body[i]
            if c == "}":
                depth += 1
            elif c == "{":
                if depth == 0:
                    header_start = max(body.rfind(";", 0, i), body.rfind("{", 0, i), body.rfind("}", 0, i)) + 1
                    header = body[header_start:i].strip()
                    if not re.match(r"if\s*\(.*sithCogExec_Pop\w*\s*\(", header, re.S):
                        return True
                    return conditional(body, header_start)
                depth -= 1
            i -= 1
        return True
    start = max(before.rfind(";"), before.rfind("{"), before.rfind("}")) + 1
    statement = before[start:].lstrip()
    if re.match(r"else\b", statement):
        return True
    head = re.match(r"(if|while|for|switch)\s*\(", statement)
    if head:
        close = len(call_arguments(statement, head.end() - 1)) + head.end()
        return len(statement) > close
    return False


def analyse(body: str, param: str, functions: dict, seen: tuple = (), flags: dict | None = None) -> tuple[list, list]:
    pops, pushes = [], []
    flags = flags if flags is not None else {}
    for m in re.finditer(r"\b(\w+)\s*\(", body):
        name = m.group(1)
        if name.startswith("sithCogExec_"):
            op = name[len("sithCogExec_"):]
            if op in ("PopCallstack", "PushCallstack"):
                continue
            if op.startswith("Pop"):
                if op not in POP:
                    fail(f"unknown stack pop {op}")
                pops.append(POP[op])
                if conditional(body, m.start()):
                    flags["conditionalPop"] = True
            elif op.startswith("Push"):
                if op not in PUSH:
                    fail(f"unknown stack push {op}")
                pushes.append(PUSH[op])
            continue
        if name in seen or name not in functions:
            continue
        if not re.search(r"\b" + re.escape(param) + r"\b", call_arguments(body, m.end() - 1)):
            continue
        helper_body, helper_param, _ = functions[name]
        p2, u2 = analyse(helper_body, helper_param, functions, seen + (name,), flags)
        if p2 and conditional(body, m.start()):
            flags["conditionalPop"] = True
        pops += p2
        pushes += u2
    return pops, pushes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--openjkdf2", type=Path, required=True)
    parser.add_argument("--refs", type=Path, required=True)
    parser.add_argument("--revision", default="unrecorded")
    parser.add_argument("--json", type=Path, default=None)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.out or root / "src/core/include/sed/assets/cog_verb_signatures.hpp"
    json_out = args.json or Path(__file__).with_name("cog-verb-signatures.json")

    registrations: dict[str, list[str]] = {}
    functions = {}
    for name in FILES:
        text = strip_comments((args.openjkdf2 / "Cog" / name).read_text(encoding="latin-1"))
        for m in re.finditer(r'sithCog_RegisterFunction\(\s*[\w\->&]+\s*,\s*(\w+)\s*,\s*"([^"]+)"\s*\)', text):
            if m.group(1).startswith("jkCogExt_"):   # COG extensions, not in either retail game
                continue
            registrations.setdefault(m.group(2).lower(), []).append(m.group(1))
        functions.update(function_bodies(text))

    signature_of: dict[str, tuple[tuple[str, ...], str]] = {}
    for function in {f for fs in registrations.values() for f in fs}:
        if function not in functions or not functions[function][2]:
            continue
        flags: dict = {}
        pops, pushes = analyse(functions[function][0], functions[function][1], functions, (function,), flags)
        kinds = sorted(set(pushes))
        result = "" if not kinds else kinds[0] if len(kinds) == 1 else "any"
        # A verb that takes a different number of arguments on different paths has no fixed count.
        signature_of[function] = (("?",) if flags.get("conditionalPop") else tuple(reversed(pops)), result)

    retail: dict[str, dict[str, str]] = {}
    messages: dict[str, dict[str, int]] = {}
    raw = {}
    for game, file in (("jk", "jk101-retail-registry.json"), ("mots", "mots-retail-registry.json")):
        raw[game] = (args.refs / file).read_bytes()
        entries = json.loads(raw[game])
        retail[game] = {e["name"]: str(e["implementation_address_or_message_id"]) for e in entries if e["kind"] == "verb"}
        messages[game] = {e["name"]: int(e["implementation_address_or_message_id"]) for e in entries if e["kind"] == "message"}

    names = sorted(set(retail["jk"]) | set(retail["mots"]))
    table = {}
    for name in names:
        candidates = {signature_of[f] for f in registrations.get(name, []) if f in signature_of}
        if len(candidates) > 1:
            # Pick the function the retail binaries actually run: the one shared with the other names
            # registered at the same implementation address.
            peers = set()
            for game in ("jk", "mots"):
                if name in retail[game]:
                    peers |= {n for n, a in retail[game].items() if a == retail[game][name] and n != name}
            shared = {signature_of[f] for p in peers for f in registrations.get(p, []) if f in signature_of}
            candidates &= shared
        if not candidates:
            # Registered by retail but not reimplemented in OpenJKDF2: availability is still checked,
            # the argument count is not.
            table[name] = {"args": ["?"], "returns": "?"}
            continue
        if len(candidates) != 1:
            fail(f"{name}: {len(candidates)} candidate signatures")
        sig = next(iter(candidates))
        table[name] = {"args": list(sig[0]), "returns": sig[1]}

    # A name whose registered OpenJKDF2 functions differ from its retail peers' is an engine alias:
    # record which verb's body it runs (modifycoloreffect runs freecoloreffect in both retail games).
    for name in names:
        for game in ("jk", "mots"):
            if name not in retail[game]:
                continue
            peers = sorted(n for n, a in retail[game].items() if a == retail[game][name] and n != name)
            own = set(registrations.get(name, []))
            for peer in peers:
                theirs = set(registrations.get(peer, []))
                if own - theirs and own & theirs and len(own) > 1:
                    table[name]["runs"] = peer

    for name, entry in table.items():
        for game in ("jk", "mots"):
            if name in retail[game]:
                same = [n for n, a in retail[game].items() if a == retail[game][name]]
                if any(table[n]["args"] != entry["args"] or table[n]["returns"] != entry["returns"] for n in same):
                    fail(f"{name}: names sharing one retail implementation disagree")

    canonical = "".join(f'{n}|{",".join(e["args"])}|{e["returns"]}|{e.get("runs", "")}\n' for n, e in sorted(table.items()))
    for game in ("jk", "mots"):
        canonical += f"{game}-messages\n" + "".join(f"{n}|{i}\n" for n, i in sorted(messages[game].items()))
    digest = hashlib.sha256(canonical.encode("ascii")).hexdigest()
    document = {
        "schema": 1,
        "source": f"OpenJKDF2 verb implementations ({args.revision}); measured retail registries for name-to-function",
        "jkRegistrySha256": hashlib.sha256(raw["jk"]).hexdigest(),
        "motsRegistrySha256": hashlib.sha256(raw["mots"]).hexdigest(),
        "tableSha256": digest,
        "verbs": table,
        "messages": messages,
    }
    json_out.write_text(json.dumps(document, indent=1, sort_keys=True) + "\n", encoding="ascii")

    lines = [
        "// Generated by tools/cog-registry/generate_cog_verb_signatures.py.",
        "// DO NOT EDIT BY HAND. Signatures come from OpenJKDF2's verb implementations",
        f"// ({args.revision}); the measured retail registries decide which function a",
        "// name runs. Arguments are in call order; an empty result means the verb",
        "// pushes nothing.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <string_view>",
        "",
        "namespace sed::assets::cog_verbs",
        "{",
        "",
        "struct VerbSignature",
        "{",
        "    std::string_view name;",
        "    std::string_view args;",
        "    std::string_view result;",
        "    // Set when retail registers this name to another verb's code.",
        "    std::string_view runs;",
        "};",
        "",
        "struct MessageId",
        "{",
        "    std::string_view name;",
        "    int id;",
        "};",
        "",
        f'inline constexpr std::string_view kVerbSignaturesSha256 = "{digest}";',
        "",
        f"inline constexpr std::array<VerbSignature, {len(table)}> kVerbSignatures{{ {{",
    ]
    for name, entry in sorted(table.items()):
        lines.append(f'    {{ "{name}", "{",".join(entry["args"])}", "{entry["returns"]}", "{entry.get("runs", "")}" }},')
    lines += ["} };", ""]
    for game, label in (("jk", "Jk"), ("mots", "Mots")):
        lines.append(f"inline constexpr std::array<MessageId, {len(messages[game])}> k{label}MessageIds{{ {{")
        for n, i in sorted(messages[game].items()):
            lines.append(f'    {{ "{n}", {i} }},')
        lines += ["} };", ""]
    lines += ["} // namespace sed::assets::cog_verbs", ""]
    out.write_text("\n".join(lines), encoding="ascii", newline="\n")
    print(f"{len(table)} verbs, digest {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
