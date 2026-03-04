#!/usr/bin/env python3
"""Validate local Markdown links and image references.

This check is intentionally dependency-free so it can run in CI on all default
Python installations.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
IMG_RE = re.compile(r"!\[[^\]]*\]\(([^)]+)\)")


def markdown_files() -> list[Path]:
    return sorted(p for p in ROOT.rglob("*.md") if ".git" not in p.parts)


def is_external_link(target: str) -> bool:
    lower = target.lower()
    return (
        lower.startswith("http://")
        or lower.startswith("https://")
        or lower.startswith("mailto:")
        or lower.startswith("tel:")
        or lower.startswith("#")
    )


def normalize_target(raw: str) -> str:
    target = raw.strip()
    if " " in target and target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    return target


def resolve_target(base_file: Path, target: str) -> Path:
    path_only = target.split("#", 1)[0].strip()
    return (base_file.parent / path_only).resolve()


def main() -> int:
    failures: list[str] = []
    files = markdown_files()

    for md in files:
        text = md.read_text(encoding="utf-8")

        for raw in LINK_RE.findall(text):
            target = normalize_target(raw)
            if not target or is_external_link(target):
                continue

            resolved = resolve_target(md, target)
            if not resolved.exists():
                failures.append(f"{md.relative_to(ROOT)}: broken link -> {target}")

        for raw in IMG_RE.findall(text):
            target = normalize_target(raw)
            if not target or is_external_link(target):
                continue

            resolved = resolve_target(md, target)
            if not resolved.exists():
                failures.append(f"{md.relative_to(ROOT)}: missing image -> {target}")

    if failures:
        print("Documentation validation failed:")
        for item in failures:
            print(f"- {item}")
        return 1

    print(f"Documentation validation passed for {len(files)} Markdown files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
