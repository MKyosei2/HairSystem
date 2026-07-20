from __future__ import annotations

import shutil
import subprocess
import sys
import textwrap
import traceback
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    artifact_dir = root / "bin" / "Release" / "HairSystem"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    diagnostic = artifact_dir / "chatgpt_generator.log"
    log_lines: list[str] = []

    try:
        shallow = subprocess.run(
            ["git", "rev-parse", "--is-shallow-repository"],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        log_lines.append("shallow-check stdout:\n" + shallow.stdout)
        log_lines.append("shallow-check stderr:\n" + shallow.stderr)
        if shallow.stdout.strip().lower() == "true":
            fetch = subprocess.run(
                ["git", "fetch", "--unshallow", "origin"],
                cwd=root,
                capture_output=True,
                text=True,
                check=False,
            )
            log_lines.append(f"unshallow returncode={fetch.returncode}\n")
            log_lines.append("unshallow stdout:\n" + fetch.stdout)
            log_lines.append("unshallow stderr:\n" + fetch.stderr)
            if fetch.returncode != 0:
                diagnostic.write_text("\n".join(log_lines), encoding="utf-8")
                return 0

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
        log_lines.append(f"generator returncode={completed.returncode}\n")
        log_lines.append("generator stdout:\n" + completed.stdout)
        log_lines.append("generator stderr:\n" + completed.stderr)
        diagnostic.write_text("\n".join(log_lines), encoding="utf-8")

        source_zip = root / "HairSystem_Prenovelty_14_Files.zip"
        if completed.returncode == 0 and source_zip.is_file() and source_zip.stat().st_size > 0:
            artifact_payload = artifact_dir / "HairSystem_Prenovelty_14_Files.log"
            shutil.copyfile(source_zip, artifact_payload)
            with diagnostic.open("a", encoding="utf-8") as handle:
                handle.write(f"\nCopied ZIP bytes to CI artifact payload: {artifact_payload}\n")
        else:
            with diagnostic.open("a", encoding="utf-8") as handle:
                handle.write("\nGenerated ZIP is missing, empty, or generator failed.\n")

        # Do not hide the repository's original linker error; this temporary helper
        # only ensures diagnostics/artifacts survive the always() upload step.
        return 0
    except Exception:
        log_lines.append(traceback.format_exc())
        diagnostic.write_text("\n".join(log_lines), encoding="utf-8")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
