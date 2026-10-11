# SPDX-License-Identifier: GPL-3.0-or-later
"""
YoRadio PlatformIO build helper — mixed-build compatibility glue.
Сборочный помощник YoRadio: совместимость mixed Arduino + ESP-IDF.

The legacy precompiled ESP-IDF overlay under library!/esp-idf-5.5.5/ is retired.
Native sdkconfig.defaults owns LwIP, mbedTLS, Wi-Fi, and esp_lcd for env:4848S040.

This script only, for mixed Arduino + ESP-IDF:
  * applies the 12 mbedTLS --wrap flags used with CONFIG_MBEDTLS_DYNAMIC_BUFFER
  * defines YORADIO_IDF_C1_CONFIG for the Audio.cpp AAC / static-RX path

Arduino-only environments no longer receive an overlay LIBPATH or those wraps.
PlatformIO invokes this file from platformio.ini extra_scripts. Do not run by hand.
"""

import json
import os

# 12 mbedTLS GNU --wrap flags paired with CONFIG_MBEDTLS_DYNAMIC_BUFFER.
# log_printf / longjmp stay with the stock framework.
# 12 флагов --wrap для CONFIG_MBEDTLS_DYNAMIC_BUFFER.
# log_printf / longjmp остаются у stock framework.
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

BANNER = "=" * 56


def _framework_names(env):
    """Return the PlatformIO framework list as a lowercase set."""
    raw = env.get("PIOFRAMEWORK", [])
    if isinstance(raw, (list, tuple)):
        parts = raw
    else:
        parts = str(raw).replace(",", " ").split()
    return {str(part).strip().lower() for part in parts if str(part).strip()}


def _is_mixed_arduino_idf(env):
    """True when this env is mixed Arduino + ESP-IDF."""
    names = _framework_names(env)
    return "arduino" in names and "espidf" in names


def render(decision):
    """Human-readable build banner."""
    lines = [BANNER]
    if decision.get("mixed"):
        lines += [
            "[YoRadio Build Profile]",
            "",
            "Framework:               arduino + espidf",
            "Native sdkconfig owns LwIP, mbedTLS, Wi-Fi, and esp_lcd.",
            "Legacy overlay:          retired",
            "TLS Dynamic Buffer wraps: ENABLED (%d/%d)"
            % (len(decision["wraps"]), len(S3_MBEDTLS_WRAPS)),
            "YORADIO_IDF_C1_CONFIG:   enabled",
            "",
            "Profile: MIXED NATIVE",
        ]
    else:
        lines += [
            "[YoRadio Build Profile]",
            "",
            "Framework:               arduino-only",
            "Legacy precompiled ESP-IDF overlay: retired",
            "TLS wraps / YORADIO_IDF_C1_CONFIG: not applied",
            "",
            "Profile: STOCK",
        ]
    lines.append(BANNER)
    return lines


def apply(env):
    mixed = _is_mixed_arduino_idf(env)
    wraps = list(S3_MBEDTLS_WRAPS) if mixed else []
    decision = {
        "mixed": mixed,
        "wraps": wraps,
        "yoradio_idf_c1_config": mixed,
        "link_overlay": False,
        "profile": "MIXED NATIVE" if mixed else "STOCK",
        "overlay_retired": True,
    }

    if mixed:
        env.Append(LINKFLAGS=["-Wl,--wrap=%s" % sym for sym in wraps])
        # Compile-time gate for Audio.cpp AAC / static-RX TLS path.
        # Шлюз компиляции для AAC / static-RX TLS пути в Audio.cpp.
        env.Append(CPPDEFINES=[("YORADIO_IDF_C1_CONFIG", 1)])

    for line in render(decision):
        print(line)

    try:
        build_dir = env.subst("$BUILD_DIR")
        os.makedirs(build_dir, exist_ok=True)
        with open(os.path.join(build_dir, "yoradio_build_decision.json"),
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
