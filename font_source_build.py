# SPDX-License-Identifier: GPL-3.0-or-later
"""Embed the build-selected raw factory text TTF in firmware Flash.

``custom_text_ttf_source`` is the single developer-facing selection point.
The generated assembly stays under ``.pio`` and exposes the raw bytes through
stable linker symbols; no generated font source is committed to the project.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def _write_if_changed(path: Path, content: str) -> None:
    if path.is_file() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


Import("env")  # noqa: F821 - injected by PlatformIO/SCons

configured = env.GetProjectOption("custom_text_ttf_source", "").strip()  # noqa: F821
if not configured:
    raise RuntimeError("missing PlatformIO option: custom_text_ttf_source")

project = Path(env.subst("$PROJECT_DIR")).resolve()  # noqa: F821
source = (project / configured).resolve()
try:
    source.relative_to(project)
except ValueError as exc:
    raise RuntimeError(f"custom_text_ttf_source must stay inside PROJECT_DIR: {source}") from exc
if not source.is_file():
    raise RuntimeError(f"custom_text_ttf_source does not exist: {source}")

source_hash = _sha256(source)
generated_dir = Path(env.subst("$BUILD_DIR")) / "yoradio_font_embed_src"  # noqa: F821
generated = generated_dir / "yoradio_text_ttf.S"
incbin_path = source.as_posix().replace('"', '\\"')
assembly = f"""/* Generated build intermediate. Do not edit or track.
 * source: {configured.replace(chr(92), '/')}
 * bytes: {source.stat().st_size}
 * sha256: {source_hash}
 */
    .section .rodata.yoradio_text_ttf,"a",@progbits
    .balign 4
    .global yoradio_text_ttf_data
    .type yoradio_text_ttf_data,@object
yoradio_text_ttf_data:
    .incbin "{incbin_path}"
    .global yoradio_text_ttf_data_end
yoradio_text_ttf_data_end:
    .size yoradio_text_ttf_data, yoradio_text_ttf_data_end-yoradio_text_ttf_data
    .balign 4
    .section .note.GNU-stack,"",@progbits
"""
_write_if_changed(generated, assembly)
env.BuildSources(  # noqa: F821
    os.path.join("$BUILD_DIR", "yoradio_font_embed_obj"),
    str(generated_dir),
)
print(
    "[YoRadio Font Source] text=%s bytes=%d sha256=%s embed=.pio/.incbin"
    % (configured.replace("\\", "/"), source.stat().st_size, source_hash)
)
