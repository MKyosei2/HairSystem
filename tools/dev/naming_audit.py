#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

CPP_FILE_RE = re.compile(r"^[A-Z][A-Za-z0-9]*\.(cpp|h)$")
PY_FILE_RE = re.compile(r"^[a-z][a-z0-9_]*\.py$")

EXCLUDED_DIRS = {".git", ".vs", "bin", "bin-int", "build_check", "generated_scaffold", "generated_scaffold_v5"}


def iter_files() -> list[pathlib.Path]:
    results = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        if any(part in EXCLUDED_DIRS for part in path.parts):
            continue
        results.append(path)
    return results


def check_file_names(files: list[pathlib.Path]) -> list[str]:
    problems: list[str] = []
    for path in files:
        rel = path.relative_to(ROOT)
        name = path.name
        if rel.parts[0] in {"src", "include", "apps"} and path.suffix in {".cpp", ".h"}:
            if not CPP_FILE_RE.match(name):
                problems.append(f"C++ file name should be PascalCase: {rel}")
        elif rel.parts[0] == "tools" and path.suffix == ".py":
            if not PY_FILE_RE.match(name):
                problems.append(f"Python file name should be snake_case: {rel}")
    return problems


def check_include_path_spelling(files: list[pathlib.Path]) -> list[str]:
    include_roots = [ROOT / "include", ROOT / "apps", ROOT / "src"]
    known_headers: dict[str, pathlib.Path] = {}
    for header in (ROOT / "include").rglob("*.h"):
        rel = header.relative_to(ROOT / "include").as_posix()
        known_headers[rel.lower()] = header.relative_to(ROOT).as_posix()

    problems: list[str] = []
    include_re = re.compile(r'#include\s+"([^"]+)"')
    for path in files:
        if path.suffix not in {".cpp", ".h"}:
            continue
        for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), start=1):
            m = include_re.search(line)
            if not m:
                continue
            include_path = m.group(1)
            lowered = include_path.lower()
            if lowered in known_headers:
                actual = known_headers[lowered]
                expected = actual.split('/', 1)[1]
                if include_path != expected:
                    problems.append(
                        f'Include path casing mismatch: {path.relative_to(ROOT)}:{line_number} -> "{include_path}" should be "{expected}"'
                    )
    return problems


def main() -> int:
    files = iter_files()
    problems = []
    problems.extend(check_file_names(files))
    problems.extend(check_include_path_spelling(files))
    if problems:
        print("Naming audit found issues:")
        for problem in problems:
            print(f"- {problem}")
        return 1
    print("Naming audit passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
