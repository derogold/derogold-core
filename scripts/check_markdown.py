#!/usr/bin/env python3

"""Small dependency-free checks for Markdown kept in this repository."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
HTML_LINK = re.compile(r"(?:href|src)=[\"']([^\"']+)[\"']")
EXCLUDED_PARTS = {".git", "build", "external"}


def markdown_files() -> list[Path]:
    return sorted(
        path
        for path in ROOT.rglob("*.md")
        if not EXCLUDED_PARTS.intersection(path.relative_to(ROOT).parts)
    )


def local_target(raw_target: str) -> str | None:
    target = raw_target.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]

    # Markdown permits an optional quoted title after the destination.
    target = target.split(maxsplit=1)[0]
    parsed = urlsplit(target)
    if parsed.scheme or parsed.netloc or target.startswith(("#", "mailto:")):
        return None

    path = unquote(parsed.path)
    return path or None


def main() -> int:
    errors: list[str] = []

    for document in markdown_files():
        text = document.read_text(encoding="utf-8")
        relative = document.relative_to(ROOT)

        for line_number, line in enumerate(text.splitlines(), start=1):
            if line.endswith((" ", "\t")):
                errors.append(f"{relative}:{line_number}: trailing whitespace")

        targets = MARKDOWN_LINK.findall(text) + HTML_LINK.findall(text)
        for raw_target in targets:
            target = local_target(raw_target)
            if target is None:
                continue

            destination = (document.parent / target).resolve()
            try:
                destination.relative_to(ROOT)
            except ValueError:
                errors.append(f"{relative}: local link leaves repository: {raw_target}")
                continue

            if not destination.exists():
                errors.append(f"{relative}: missing local link target: {raw_target}")

    if errors:
        print("Documentation checks failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(f"Checked {len(markdown_files())} Markdown files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
