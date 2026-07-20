from __future__ import annotations

import shutil
import subprocess
import sys
import textwrap
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[2]

    shallow = subprocess.run(
        ["git", "rev-parse", "--is-shallow-repository"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    if shallow.stdout.strip().lower() == "true":
        fetch = subprocess.run(
            ["git", "fetch", "--unshallow", "origin"],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        if fetch.returncode != 0:
            sys.stderr.write(fetch.stdout + fetch.stderr)
            return fetch.returncode

    workflow = root / ".github" / "workflows" / "chatgpt-build-prenovelty-14files.yml"
    source = workflow.read_text(encoding="utf-8")
    begin_token = "          python - <<'PY'\n"
    end_token = "\n          PY\n"
    begin = source.index(begin_token) + len(begin_token)
    end = source.index(end_token, begin)
    script = textwrap.dedent(source[begin:end]) + "\n"

    generator = root / "chatgpt_generate_prenovelty_zip.py"
    generator.write_text(script, encoding="utf-8")
    completed = subprocess.run(
        [sys.executable, str(generator)],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    sys.stdout.write(completed.stdout)
    sys.stderr.write(completed.stderr)
    if completed.returncode != 0:
        return completed.returncode

    source_zip = root / "HairSystem_Prenovelty_14_Files.zip"
    if not source_zip.is_file() or source_zip.stat().st_size == 0:
        print("Generated ZIP is missing or empty.", file=sys.stderr)
        return 80

    artifact_dir = root / "bin" / "Release" / "HairSystem"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    artifact_payload = artifact_dir / "HairSystem_Prenovelty_14_Files.log"
    shutil.copyfile(source_zip, artifact_payload)
    print(f"Copied ZIP bytes to CI artifact payload: {artifact_payload}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
