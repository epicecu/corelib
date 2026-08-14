#!/usr/bin/env python3
"""Reject function signatures split across physical source lines."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".ino"}
EXCLUDED_PARTS = {"vendor", "generated"}
CONTROL_WORDS = {"if", "for", "while", "switch", "return", "sizeof", "static_assert"}


def owned_files() -> list[Path]:
    files: list[Path] = []
    for base in (ROOT / "src", ROOT / "examples"):
        for path in base.rglob("*"):
            if path.suffix in SUFFIXES and not EXCLUDED_PARTS.intersection(path.parts):
                files.append(path)
    return sorted(files)


def split_signatures(path: Path) -> list[int]:
    lines = path.read_text(encoding="utf-8").splitlines()
    failures: list[int] = []
    for index, line in enumerate(lines[:-1]):
        stripped = line.strip()
        if not stripped or stripped.startswith(("//", "/*", "*", "#")):
            continue
        if "(" not in stripped or ")" in stripped:
            continue
        word = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", stripped)
        if word is None or word.group(1) in CONTROL_WORDS:
            continue
        if any(token in stripped for token in ("=", "?", "return ")):
            continue
        failures.append(index + 1)
    return failures


def format_signatures(path: Path) -> None:
    """Join signatures recognized by the same conservative check."""
    lines = path.read_text(encoding="utf-8").splitlines()
    starts = {line - 1 for line in split_signatures(path)}
    output: list[str] = []
    index = 0
    while index < len(lines):
        if index not in starts:
            output.append(lines[index])
            index += 1
            continue
        indent = lines[index][: len(lines[index]) - len(lines[index].lstrip())]
        parts = [lines[index].strip()]
        index += 1
        while index < len(lines):
            parts.append(lines[index].strip())
            complete = ")" in lines[index] and ("{" in lines[index] or ";" in lines[index] or lines[index].rstrip().endswith((")", "noexcept")))
            index += 1
            if complete:
                break
        output.append(indent + " ".join(part for part in parts if part))
    path.write_text("\n".join(output) + "\n", encoding="utf-8")


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--fix":
        for path in owned_files():
            format_signatures(path)
        print("Formatted owned Corelib function signatures onto one physical line.")
        return 0
    failed = False
    for path in owned_files():
        for line in split_signatures(path):
            failed = True
            print(f"{path.relative_to(ROOT)}:{line}: function signature must remain on one line")
    if failed:
        return 1
    print("Owned Corelib function signatures are on one physical line.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
