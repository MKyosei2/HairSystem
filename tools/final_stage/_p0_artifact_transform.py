from __future__ import annotations

import base64
import lzma
from pathlib import Path

HERE = Path(__file__).resolve().parent
payload = "".join(
    (HERE / f".p0_transform_{index}").read_text(encoding="utf-8").strip()
    for index in range(5)
)
source = lzma.decompress(base64.b64decode(payload)).decode("utf-8")
exec(compile(source, str(Path(__file__)), "exec"), globals(), globals())
