from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PARTS = ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts"
CANONICAL = PARTS / "02a_required_runtime_foundation.inl"
OWNERSHIP = {
    "CreateUiControls",
    "ApplyWorkspaceMode",
    "ApplyGroomToolPreset",
    "GetWorkspaceDisplayName",
    "GetRenderViewportRect",
    "CompileShaderFromFile",
    "CreateStructuredBuffer",
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write(path: Path, text: str) -> None:
    path.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")


def find_function(text: str, name: str) -> tuple[int, int] | None:
    pattern = re.compile(
        rf"^[ \t]*(?:(?:const\s+)?[A-Za-z_][\w:<>,*&\s]*?\s+)GpuHairViewer::{re.escape(name)}\s*\(",
        re.MULTILINE,
    )
    match = pattern.search(text)
    if not match:
        return None
    open_brace = text.find("{", match.end())
    if open_brace < 0:
        return None
    depth = 0
    state = "code"
    index = open_brace
    while index < len(text):
        ch = text[index]
        nxt = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if ch == "/" and nxt == "/": state = "line"; index += 2; continue
            if ch == "/" and nxt == "*": state = "block"; index += 2; continue
            if ch == '"': state = "string"; index += 1; continue
            if ch == "'": state = "char"; index += 1; continue
            if ch == "{": depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return match.start(), index + 1
            index += 1
            continue
        if state == "line":
            if ch == "\n": state = "code"
            index += 1
            continue
        if state == "block":
            if ch == "*" and nxt == "/": state = "code"; index += 2; continue
            index += 1
            continue
        if state in {"string", "char"}:
            if ch == "\\": index += 2; continue
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = "code"
            index += 1
    return None


def main() -> int:
    canonical_text = read(CANONICAL)
    for name in OWNERSHIP:
        if find_function(canonical_text, name) is None:
            raise RuntimeError(f"Canonical foundation is missing {name}")

    modified: set[Path] = set()
    for path in sorted(PARTS.glob("*.inl")):
        if path == CANONICAL:
            continue
        text = read(path)
        original = text
        for name in OWNERSHIP:
            while True:
                bounds = find_function(text, name)
                if bounds is None:
                    break
                begin, end = bounds
                text = text[:begin] + text[end:]
                text = re.sub(r"\n{3,}", "\n\n", text)
        if text != original:
            write(path, text)
            modified.add(path)

    call_paths = [
        PARTS / "07_draw_path_and_gpu_timing.inl",
        PARTS / "09_settings_load_save_and_export.inl",
        PARTS / "11_experiment_env_and_performance_sample.inl",
        PARTS / "12_strand_pick_gizmo_and_brush_edit.inl",
        PARTS / "14_scalp_overlay_and_window_messages.inl",
    ]
    for path in call_paths:
        text = read(path)
        original = text
        text = re.sub(r"(?<!::)(?<!->)(?<!\.)\bGetRenderViewportRect\s*\(\s*\)", "this->GetRenderViewportRect()", text)
        text = re.sub(r"(?<!::)(?<!->)(?<!\.)\bGetWorkspaceDisplayName\s*\(\s*\)", "this->GetWorkspaceDisplayName()", text)
        if text != original:
            write(path, text)
            modified.add(path)

    for name in OWNERSHIP:
        locations = [p.name for p in sorted(PARTS.glob("*.inl")) if find_function(read(p), name) is not None]
        if locations != [CANONICAL.name]:
            raise RuntimeError(f"{name} ownership mismatch: {locations}")

    modified.add(CANONICAL)
    manifest = ROOT / "viewer_dedup_modified_files.txt"
    manifest.write_text(
        "\n".join(sorted(str(p.relative_to(ROOT)).replace("\\", "/") for p in modified)) + "\n",
        encoding="utf-8",
    )
    print(manifest.read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
