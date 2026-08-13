# SPDX-License-Identifier: GPL-3.0-or-later
"""
YoRadio PlatformIO build helper.
Сборочный помощник YoRadio для PlatformIO.

Selects the project-local ESP-IDF library overlay and its linker policy.
Выбирает локальный набор библиотек ESP-IDF проекта и его политику компоновки.
Author: Witaliy76 - https://github.com/Witaliy76

THIS IS NOT A USER SCRIPT.  Never run `python yoradio_build.py`.
PlatformIO invokes it automatically from platformio.ini:

    [env]
    extra_scripts =
        pre:yoradio_build.py

The only supported user workflow stays `Build` / `pio run`.

--------------------------------------------------------------------------
What it owns
--------------------------------------------------------------------------
  * target selection                  ESP32-S3 / ESP32-P4
  * resolved platform/version check   pioarduino / Arduino core / ESP-IDF
  * project-local library overlay     library!/esp-idf-<ver>/<family>/
  * KnownGood SHA256 validation       atomic, all-or-nothing
  * mbedTLS wrap policy               12 GNU --wrap flags, coupled to the overlay
  * stock fallback                    never a hard error
  * build-console diagnostics

--------------------------------------------------------------------------
Why the overlay exists
--------------------------------------------------------------------------
YoRadio ships seven ESP-IDF archives rebuilt from stock Espressif sources
with a YoRadio sdkconfig (mbedTLS dynamic buffers, LwIP tuning, esp_lcd
RESTART_IN_VSYNC off).  They used to be copied by hand over the shared
PlatformIO framework package.  That mutated a globally shared package,
broke every other project on the machine, and could not be reproduced from
a clean checkout.

This helper resolves them through the *linker search path* instead:

    -L <project>/library!/esp-idf-5.5.5/s3     <-- prepended, wins
    -L <platformio>/packages/.../esp32s3/lib   <-- stock, read-only

GNU ld resolves each `-l<name>` from the first `-L` directory holding a
match, so an overlay directory containing exactly the seven YoRadio
archives shadows those seven names and nothing else.  The shared framework
package is only ever read.  (The stock platform uses the same shadowing
trick itself in builder/frameworks/arduino_relinker.py for sections.ld.)

--------------------------------------------------------------------------
Selection rules
--------------------------------------------------------------------------
  * the target MCU must have a YoRadio overlay profile (S3 only today);
  * the resolved ESP-IDF / Arduino-core / platform tuple must match the
    profile exactly - the archives are ABI-bound to it, no fuzzy matching;
  * every archive of the profile must be present AND match its accepted
    SHA256.  7/7 selects all of them; anything else selects NONE.

Anything short of that falls back to the stock framework libraries with
the wrap flags disabled, prints a warning, and lets the build continue.

Provenance of the SHA256 table and of the 12 mbedTLS wrap flags: Display
Track Stage 2.2, Slice 3 (differential rebuild, set C1), Slice 3B (device
A/B, profile P1 selected), Slice 3C (auto local overlay preflight).
"""

import hashlib
import json
import os
import re

# ---------------------------------------------------------------------------
# Accepted profiles.  Keyed by SoC family, not by board.
# `None` means "this family is known but has no YoRadio override set yet".
# ---------------------------------------------------------------------------

FAMILY_BY_MCU = {
    "esp32s3": "s3",
    "esp32p4": "p4",
}

# 12 mbedTLS GNU --wrap flags required by the dynamic-buffer build of
# libmbedtls_2.a.  log_printf / longjmp are NOT listed here: the stock
# framework already supplies those two.
S3_MBEDTLS_WRAPS = (
    "mbedtls_ssl_close_notify",
    "mbedtls_ssl_free",
    "mbedtls_ssl_handshake_client_step",
    "mbedtls_ssl_handshake_server_step",
    "mbedtls_ssl_read",
    "mbedtls_ssl_send_alert_message",
    "mbedtls_ssl_session_reset",
    "mbedtls_ssl_setup",
    "mbedtls_ssl_tls13_handshake_client_step",
    "mbedtls_ssl_tls13_handshake_server_step",
    "mbedtls_ssl_write",
    "mbedtls_ssl_write_client_hello",
)

# Accepted C1 archive set.  This table is the authority; the manifest.txt
# shipped next to the archives is the human-readable provenance record.
# Keeping the authority here means a tampered overlay cannot self-certify.
S3_KNOWN_GOOD = {
    "liblwip.a":            "F8A0CC32D911FC0758580C5D317660686087F1838E372068011218ACB44723A1",
    "libmbedtls_2.a":       "4EEFB5AA4BF0E2C63286EE17CAE78031348F535BB67B736ACC20775FA411353C",
    "libesp-tls.a":         "EDDFAFDB5296BE874289DD409A2F675144195BE13EF3CE111E42AC2291C3B69A",
    "libtcp_transport.a":   "7FDAA2BFE9DF085BBAA86F39842100E01C09D3CF81B2C79E9E5A5A871CF25723",
    "libesp_http_client.a": "11238D44B2A5BE1F6E4E35B02F1F311F857FB94FEB6F8ACA4FB8445ACF49A4B3",
    "libesp_lcd.a":         "B349B9F2969D4CF21ABEEA7971EC42E07C32EBB6B761679F6E429D5974F6DF62",
    "libwpa_supplicant.a":  "8A13CD4C72D171F886ED43947B56AB3BFDFA0A1F325BF31DAF7C804D15750A2F",
}

PROFILES = {
    "s3": {
        "label": "ESP32-S3",
        "idf": "5.5.5",
        "arduino": "3.3.11",
        "platform": "55.3.311",
        "overlay": ("library!", "esp-idf-5.5.5", "s3"),
        "archives": S3_KNOWN_GOOD,
        "wraps": S3_MBEDTLS_WRAPS,
    },
    # ESP32-P4 is a recognised target but ships no YoRadio archives yet.
    # Keeping the key (with a None value) makes "P4 never inherits the S3
    # overlay" explicit rather than accidental.
    "p4": None,
}

BANNER = "=" * 56

# ---------------------------------------------------------------------------
# Version probing.  Read the version *headers* that ship inside the
# packages, not the top-level package.json: after a platform pin bump the
# manifest can still declare the previous version while the extracted tree
# is already the new one.
# ---------------------------------------------------------------------------


def _read_macro_version(path, prefix, parts=("MAJOR", "MINOR", "PATCH")):
    """Extract ``<prefix>_MAJOR/MINOR/PATCH`` from a C header."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError:
        return None
    out = []
    for part in parts:
        match = re.search(
            r"^\s*#define\s+%s_%s\s+(\d+)" % (re.escape(prefix), part),
            text,
            re.MULTILINE,
        )
        if not match:
            return None
        out.append(match.group(1))
    return ".".join(out)


def probe_idf_version(sdk_dir, mcu):
    """Resolved ESP-IDF version of the precompiled libs package."""
    return _read_macro_version(
        os.path.join(sdk_dir, mcu, "include", "esp_common", "include",
                     "esp_idf_version.h"),
        "ESP_IDF_VERSION",
    )


def probe_arduino_version(framework_dir):
    """Resolved Arduino-ESP32 core version."""
    return _read_macro_version(
        os.path.join(framework_dir, "cores", "esp32", "esp_arduino_version.h"),
        "ESP_ARDUINO_VERSION",
    )


def normalize_version(value):
    """``55.03.311`` and ``55.3.311`` are the same version, differently typed."""
    if not value:
        return None
    head = str(value).split("+")[0].strip()
    if not re.match(r"^\d+(\.\d+)*$", head):
        return head
    return ".".join(str(int(part)) for part in head.split("."))


def sha256_file(path, chunk=1 << 20):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(chunk), b""):
            digest.update(block)
    return digest.hexdigest().upper()


# ---------------------------------------------------------------------------
# Pure decision logic.  No SCons, no I/O beyond hashing the overlay.
# ---------------------------------------------------------------------------


def validate_overlay(overlay_dir, known_good):
    """Hash every expected archive.  Returns (ok_count, total, per-file rows).

    Selection is atomic: the caller must treat anything below total as
    "no overlay", never as a partial overlay.
    """
    rows = []
    for name in sorted(known_good):
        path = os.path.join(overlay_dir, name)
        expected = known_good[name].upper()
        if not os.path.isfile(path):
            rows.append({"archive": name, "state": "MISSING",
                         "expected": expected, "actual": None, "ok": False})
            continue
        actual = sha256_file(path)
        ok = actual == expected
        rows.append({"archive": name,
                     "state": "OK" if ok else "SHA-MISMATCH",
                     "expected": expected, "actual": actual, "ok": ok})
    return sum(1 for row in rows if row["ok"]), len(rows), rows


def decide(mcu, project_dir, idf_version, arduino_version, platform_version):
    """Resolve the build profile.  Never raises; always returns a decision."""
    mcu = (mcu or "").lower()
    family = FAMILY_BY_MCU.get(mcu)
    decision = {
        "mcu": mcu,
        "family": family,
        "profile": "STOCK",
        "reason": None,
        "overlay_dir": None,
        "valid": 0,
        "total": 0,
        "rows": [],
        "wraps": [],
        "detected": {
            "idf": idf_version,
            "arduino": arduino_version,
            "platform": normalize_version(platform_version),
        },
        "expected": None,
        "severity": "info",
    }

    profile = PROFILES.get(family) if family else None
    if profile is None:
        decision["reason"] = "no-overlay-profile-for-target"
        return decision

    decision["expected"] = {
        "idf": profile["idf"],
        "arduino": profile["arduino"],
        "platform": normalize_version(profile["platform"]),
    }
    # From here on a profile *could* have applied, so every miss is a warning.
    decision["severity"] = "warning"

    mismatched = [
        key for key in ("idf", "arduino", "platform")
        if normalize_version(decision["detected"][key])
        != normalize_version(decision["expected"][key])
    ]
    if mismatched:
        decision["reason"] = "version-mismatch:" + ",".join(mismatched)
        return decision

    overlay_dir = os.path.join(project_dir, *profile["overlay"])
    decision["overlay_dir"] = overlay_dir
    decision["total"] = len(profile["archives"])

    if not os.path.isdir(overlay_dir):
        decision["reason"] = "overlay-directory-not-found"
        return decision

    valid, total, rows = validate_overlay(overlay_dir, profile["archives"])
    decision["valid"], decision["total"], decision["rows"] = valid, total, rows

    if valid != total:
        # Atomic: a partial overlay is treated exactly like a missing one.
        decision["reason"] = "overlay-invalid-%d-of-%d" % (valid, total)
        return decision

    decision["profile"] = "OPTIMIZED"
    decision["reason"] = "overlay-valid-%d-of-%d" % (valid, total)
    decision["severity"] = "info"
    decision["wraps"] = list(profile["wraps"])
    return decision


# ---------------------------------------------------------------------------
# Console output
# ---------------------------------------------------------------------------


def render(decision):
    """Human-readable build banner.  Returns a list of lines."""
    det = decision["detected"]
    exp = decision["expected"]
    label = None
    if decision["family"] and PROFILES.get(decision["family"]):
        label = PROFILES[decision["family"]]["label"]
    target = label or decision["mcu"] or "unknown"

    lines = [BANNER]
    if decision["profile"] == "OPTIMIZED":
        lines += [
            "[YoRadio Build Profile]",
            "",
            "Target:  %s" % target,
            "ESP-IDF: %s   Arduino core: %s" % (det["idf"], det["arduino"]),
            "",
            "Local KnownGood overlay: VALID (%d/%d)"
            % (decision["valid"], decision["total"]),
            "Library source:          project-local",
            "  %s" % decision["overlay_dir"],
            "TLS Dynamic Buffer wraps: ENABLED (%d/%d)"
            % (len(decision["wraps"]), len(decision["wraps"])),
            "",
            "Profile: YORADIO OPTIMIZED",
        ]
    elif decision["severity"] == "info":
        lines += [
            "[YoRadio Build Profile]",
            "",
            "Target: %s" % (decision["mcu"] or "unknown"),
            "",
            "No YoRadio library overlay is defined for this target.",
            "Building with the stock PlatformIO framework libraries.",
            "",
            "Profile: STOCK",
        ]
    else:
        why = {
            "overlay-directory-not-found":
                "the overlay directory was not found",
            "version-mismatch":
                "the installed framework is not the version this overlay "
                "was built for",
        }.get(decision["reason"].split(":")[0], None)
        if why is None and decision["reason"].startswith("overlay-invalid"):
            why = ("only %d of %d archives validated - the overlay is "
                   "incomplete or altered" % (decision["valid"],
                                              decision["total"]))
        lines += [
            "[YoRadio WARNING]",
            "",
            "Target:  %s" % target,
            "ESP-IDF: %s   Arduino core: %s" % (det["idf"], det["arduino"]),
            "",
            "Local KnownGood overlay: NOT AVAILABLE / INVALID",
            "Reason: %s." % (why or decision["reason"]),
        ]
        if decision["reason"].startswith("version-mismatch"):
            lines += [
                "  detected: ESP-IDF %s / Arduino %s / platform %s"
                % (det["idf"], det["arduino"], det["platform"]),
                "  expected: ESP-IDF %s / Arduino %s / platform %s"
                % (exp["idf"], exp["arduino"], exp["platform"]),
            ]
        if decision["reason"].startswith("overlay-invalid"):
            for row in decision["rows"]:
                if not row["ok"]:
                    lines.append("  %-22s %s" % (row["archive"], row["state"]))
        lines += [
            "",
            "Library source:           STOCK PlatformIO framework",
            "TLS Dynamic Buffer wraps: DISABLED",
            "",
            "Profile: STOCK FALLBACK",
            "Build will continue using the stock framework.",
        ]
    lines.append(BANNER)
    return lines


# ---------------------------------------------------------------------------
# PlatformIO / SCons integration
# ---------------------------------------------------------------------------

def apply(env):
    platform = env.PioPlatform()
    board = env.BoardConfig()
    mcu = board.get("build.mcu", "").lower()
    project_dir = env.subst("$PROJECT_DIR")

    sdk_dir = platform.get_package_dir("framework-arduinoespressif32-libs") or ""
    framework_dir = platform.get_package_dir("framework-arduinoespressif32") or ""

    decision = decide(
        mcu=mcu,
        project_dir=project_dir,
        idf_version=probe_idf_version(sdk_dir, mcu) if sdk_dir else None,
        arduino_version=probe_arduino_version(framework_dir)
        if framework_dir else None,
        platform_version=getattr(platform, "version", None),
    )

    if decision["profile"] == "OPTIMIZED":
        # Prepend, never replace: the stock -L entries stay exactly where
        # pioarduino-build.py puts them, they just lose the seven names.
        env.Prepend(LIBPATH=[decision["overlay_dir"]])
        env.Append(LINKFLAGS=["-Wl,--wrap=%s" % sym
                              for sym in decision["wraps"]])

    for line in render(decision):
        print(line)

    # Machine-readable trace for CI / release evidence.  Written into the
    # project build directory only - nothing outside the project is touched.
    try:
        build_dir = env.subst("$BUILD_DIR")
        os.makedirs(build_dir, exist_ok=True)
        with open(os.path.join(build_dir, "yoradio_overlay_decision.json"),
                  "w", encoding="utf-8") as handle:
            json.dump(decision, handle, indent=2, sort_keys=True)
    except OSError:
        pass

    return decision


try:
    Import("env")  # noqa: F821  - injected by SCons
except NameError:
    pass           # imported as a plain module (offline checks)
else:
    apply(env)     # noqa: F821
