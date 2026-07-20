from __future__ import annotations

import shutil
import subprocess
import textwrap
import unittest
from pathlib import Path


class ChatGptZipArtifactBuilder(unittest.TestCase):
    def test_generate_fourteen_file_zip_for_ci_artifact(self) -> None:
        root = Path(__file__).resolve().parents[2]

        # Pull-request checkout is shallow by default. The generator deliberately
        # restores the six lost C++ function bodies from the latest usable history.
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
            self.assertEqual(fetch.returncode, 0, fetch.stderr)

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
            ["python", str(generator)],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + "\n" + completed.stderr)

        source_zip = root / "HairSystem_Prenovelty_14_Files.zip"
        self.assertTrue(source_zip.is_file())
        self.assertGreater(source_zip.stat().st_size, 0)

        # The existing workflow uploads only these log globs. The bytes remain a
        # valid ZIP and are renamed back to .zip after the artifact is downloaded.
        artifact_dir = root / "bin" / "Release" / "HairSystem"
        artifact_dir.mkdir(parents=True, exist_ok=True)
        artifact_payload = artifact_dir / "HairSystem_Prenovelty_14_Files.log"
        shutil.copyfile(source_zip, artifact_payload)
        self.assertEqual(source_zip.read_bytes(), artifact_payload.read_bytes())


if __name__ == "__main__":
    unittest.main()
