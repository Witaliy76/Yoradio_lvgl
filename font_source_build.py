# SPDX-License-Identifier: GPL-3.0-or-later
"""Embed the factory text TTF and generate+embed the Tabler icon subset.

``custom_text_ttf_source`` selects the raw factory text TTF.
``custom_tabler_ttf_source`` is the canonical full Tabler tooling TTF.
``custom_tabler_codepoint_manifest`` is the one production used-codepoint list.

Generated intermediates stay under ``.pio``:
  yoradio_tabler.ttf          codepoint-only scalable subset
  yoradio_text_ttf.S          .incbin of the factory TTF
  yoradio_tabler_ttf.S        .incbin of the subset TTF
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import sys
from pathlib import Path


FONTTOOLS_VERSION = "4.62.1"

ACCEPTED_TABLER_BYTES = 2520248
ACCEPTED_TABLER_CMAP = 5827
ACCEPTED_TABLER_SHA256 = (
    "53C70D2D7033FE3D304F3B4DD945F97FC9A2DA43A853B77F6AAE864D5912CB4F"
)

_MANIFEST_RE = re.compile(r"^U\+([0-9A-Fa-f]{4,6})(?:\s+(\S+))?\s*$")


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


def _write_bytes_if_changed(path: Path, data: bytes) -> None:
    if path.is_file() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _project_file(env, option: str) -> Path:
    configured = env.GetProjectOption(option, "").strip()
    if not configured:
        raise RuntimeError("missing PlatformIO option: %s" % option)
    project = Path(env.subst("$PROJECT_DIR")).resolve()
    source = (project / configured).resolve()
    try:
        source.relative_to(project)
    except ValueError as exc:
        raise RuntimeError("%s must stay inside PROJECT_DIR: %s" % (option, source)) from exc
    if not source.is_file():
        raise RuntimeError("%s does not exist: %s" % (option, source))
    return source


def _assembly(symbol: str, section: str, incbin: Path, comment_lines: list[str]) -> str:
    incbin_path = incbin.as_posix().replace('"', '\\"')
    header = "\n".join(" * " + line for line in comment_lines)
    return f"""/* Generated build intermediate. Do not edit or track.
{header}
 */
    .section .rodata.{section},"a",@progbits
    .balign 4
    .global {symbol}_data
    .type {symbol}_data,@object
{symbol}_data:
    .incbin "{incbin_path}"
    .global {symbol}_data_end
{symbol}_data_end:
    .size {symbol}_data, {symbol}_data_end-{symbol}_data
    .balign 4
    .section .note.GNU-stack,"",@progbits
"""


def _embed_text(env, generated_dir: Path) -> None:
    configured = env.GetProjectOption("custom_text_ttf_source", "").strip()
    source = _project_file(env, "custom_text_ttf_source")
    source_hash = _sha256(source)
    generated = generated_dir / "yoradio_text_ttf.S"
    _write_if_changed(
        generated,
        _assembly(
            "yoradio_text_ttf",
            "yoradio_text_ttf",
            source,
            [
                "source: " + configured.replace("\\", "/"),
                "bytes: %d" % source.stat().st_size,
                "sha256: %s" % source_hash,
            ],
        ),
    )
    print(
        "[YoRadio Font Source] text=%s bytes=%d sha256=%s embed=.pio/.incbin"
        % (configured.replace("\\", "/"), source.stat().st_size, source_hash)
    )


def _require_fonttools() -> str:
    try:
        from importlib.metadata import version
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("Python importlib.metadata is required for fontTools pinning") from exc
    try:
        got = version("fonttools")
    except Exception as exc:
        raise RuntimeError(
            "fontTools %s is required to subset Tabler. Install once into the "
            "PlatformIO Python (no per-build download), then rebuild:\n"
            "  \"%s\" -m pip install fonttools==%s"
            % (FONTTOOLS_VERSION, sys.executable, FONTTOOLS_VERSION)
        ) from exc
    if got != FONTTOOLS_VERSION:
        raise RuntimeError(
            "fontTools version mismatch: have %s, need %s. Install once with:\n"
            "  \"%s\" -m pip install fonttools==%s"
            % (got, FONTTOOLS_VERSION, sys.executable, FONTTOOLS_VERSION)
        )
    return got


def _parse_manifest(path: Path) -> list[int]:
    unicodes: list[int] = []
    seen: dict[int, int] = {}
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        match = _MANIFEST_RE.match(line)
        if not match:
            raise RuntimeError(
                "%s:%d: expected 'U+XXXX  name', got %r" % (path, lineno, raw)
            )
        codepoint = int(match.group(1), 16)
        if codepoint in seen:
            raise RuntimeError(
                "%s:%d: duplicate codepoint U+%04X (first at line %d)"
                % (path, lineno, codepoint, seen[codepoint])
            )
        seen[codepoint] = lineno
        unicodes.append(codepoint)
    if not unicodes:
        raise RuntimeError("%s contains no codepoints" % path)
    unicodes.sort()
    return unicodes


def _subset_tabler(source: Path, unicodes: list[int], output: Path) -> dict:
    from fontTools.subset import Options, Subsetter
    from fontTools.ttLib import TTFont

    font = TTFont(str(source), recalcTimestamp=False)
    cmap = font.getBestCmap() or {}
    cmap_count = len(cmap)
    if source.stat().st_size != ACCEPTED_TABLER_BYTES or cmap_count != ACCEPTED_TABLER_CMAP:
        font.close()
        raise RuntimeError(
            "canonical Tabler identity mismatch: bytes=%d cmap=%d (expected bytes=%d cmap=%d)"
            % (source.stat().st_size, cmap_count, ACCEPTED_TABLER_BYTES, ACCEPTED_TABLER_CMAP)
        )
    missing = [cp for cp in unicodes if cp not in cmap]
    if missing:
        font.close()
        formatted = ", ".join("U+%04X" % cp for cp in missing)
        raise RuntimeError(
            "Tabler manifest codepoint(s) absent from canonical cmap: %s" % formatted
        )

    options = Options()
    options.recalc_timestamp = False
    options.canonical_order = True
    options.layout_features = []
    options.layout_scripts = []
    options.layout_closure = False
    options.hinting = True
    options.notdef_glyph = True
    options.notdef_outline = True
    options.ignore_missing_unicodes = False
    options.drop_tables += ["GSUB", "GPOS", "GDEF", "MVAR", "STAT", "meta"]

    subsetter = Subsetter(options=options)
    subsetter.populate(unicodes=unicodes)
    subsetter.subset(font)

    output.parent.mkdir(parents=True, exist_ok=True)
    tmp = output.with_suffix(".tmp")
    font.save(str(tmp), reorderTables=True)
    font.close()

    result = TTFont(str(tmp), recalcTimestamp=False)
    result_cmap = result.getBestCmap() or {}
    result.close()
    missing_after = [cp for cp in unicodes if cp not in result_cmap]
    if missing_after:
        tmp.unlink(missing_ok=True)
        formatted = ", ".join("U+%04X" % cp for cp in missing_after)
        raise RuntimeError("subset cmap missing requested codepoint(s): %s" % formatted)
    extras = sorted(cp for cp in result_cmap if cp not in set(unicodes))
    if extras:
        tmp.unlink(missing_ok=True)
        formatted = ", ".join("U+%04X" % cp for cp in extras)
        raise RuntimeError(
            "subset cmap contains unexpected extra codepoint(s): %s" % formatted
        )

    _write_bytes_if_changed(output, tmp.read_bytes())
    tmp.unlink(missing_ok=True)
    return {
        "cmap_count": len(result_cmap),
        "bytes": output.stat().st_size,
        "sha256": _sha256(output),
    }


def _embed_tabler(env, generated_dir: Path) -> None:
    tool_version = _require_fonttools()
    configured_source = env.GetProjectOption("custom_tabler_ttf_source", "").strip()
    configured_manifest = env.GetProjectOption("custom_tabler_codepoint_manifest", "").strip()
    source = _project_file(env, "custom_tabler_ttf_source")
    manifest = _project_file(env, "custom_tabler_codepoint_manifest")
    source_hash = _sha256(source)
    if source_hash != ACCEPTED_TABLER_SHA256:
        raise RuntimeError(
            "canonical Tabler SHA256 mismatch:\n  have %s\n  need %s"
            % (source_hash, ACCEPTED_TABLER_SHA256)
        )

    unicodes = _parse_manifest(manifest)
    subset_path = Path(env.subst("$BUILD_DIR")) / "yoradio_tabler.ttf"
    subset_info = _subset_tabler(source, unicodes, subset_path)

    generated = generated_dir / "yoradio_tabler_ttf.S"
    _write_if_changed(
        generated,
        _assembly(
            "yoradio_tabler_ttf",
            "yoradio_tabler_ttf",
            subset_path,
            [
                "canonical: " + configured_source.replace("\\", "/"),
                "canonical-sha256: " + source_hash,
                "manifest: " + configured_manifest.replace("\\", "/"),
                "codepoints: %d" % len(unicodes),
                "tool: fontTools %s" % tool_version,
                "subset-bytes: %d" % subset_info["bytes"],
                "subset-sha256: " + subset_info["sha256"],
                "subset-cmap: %d" % subset_info["cmap_count"],
            ],
        ),
    )

    report = {
        "canonical": configured_source.replace("\\", "/"),
        "canonical_bytes": ACCEPTED_TABLER_BYTES,
        "canonical_sha256": source_hash,
        "canonical_cmap": ACCEPTED_TABLER_CMAP,
        "manifest": configured_manifest.replace("\\", "/"),
        "manifest_codepoints": len(unicodes),
        "codepoints": ["U+%04X" % cp for cp in unicodes],
        "tool": "fontTools",
        "tool_version": tool_version,
        "subset_path": str(subset_path).replace("\\", "/"),
        "subset_bytes": subset_info["bytes"],
        "subset_sha256": subset_info["sha256"],
        "subset_cmap": subset_info["cmap_count"],
    }
    report_path = Path(env.subst("$BUILD_DIR")) / "yoradio_tabler_embed.json"
    _write_if_changed(report_path, json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(
        "[YoRadio Font Source] tabler subset bytes=%d cmap=%d sha256=%s "
        "codepoints=%d tool=fontTools/%s embed=.pio/.incbin"
        % (
            subset_info["bytes"],
            subset_info["cmap_count"],
            subset_info["sha256"],
            len(unicodes),
            tool_version,
        )
    )


Import("env")  # noqa: F821 - injected by PlatformIO/SCons

generated_dir = Path(env.subst("$BUILD_DIR")) / "yoradio_font_embed_src"  # noqa: F821
_embed_text(env, generated_dir)  # noqa: F821
_embed_tabler(env, generated_dir)  # noqa: F821
env.BuildSources(  # noqa: F821
    os.path.join("$BUILD_DIR", "yoradio_font_embed_obj"),
    str(generated_dir),
)
