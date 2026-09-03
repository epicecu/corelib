#!/usr/bin/env python3
"""Reject the obsolete expansion of the PFP acronym in owned text."""

from pathlib import Path
import sys


OBSOLETE = "Programmor" + " Frame Protocol"
EXCLUDED_PARTS = {".git", "build", "node_modules", "vendor", "generated"}
TEXT_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".h",
    ".hpp",
    ".ino",
    ".json",
    ".md",
    ".mts",
    ".py",
    ".txt",
    ".yml",
    ".yaml",
}


def obsolete_occurrences(root: Path) -> list[Path]:
    """Return owned text files containing the obsolete protocol name."""
    failures = []
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix not in TEXT_SUFFIXES:
            continue
        if any(part in EXCLUDED_PARTS for part in path.relative_to(root).parts):
            continue
        try:
            content = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if OBSOLETE in content:
            failures.append(path)
    return failures


def main() -> int:
    """Check the current repository."""
    root = Path(__file__).resolve().parents[1]
    failures = obsolete_occurrences(root)
    if failures:
        for path in failures:
            print(f"obsolete PFP expansion: {path.relative_to(root)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
