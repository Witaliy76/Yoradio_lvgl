ESP32-P4 — no YoRadio custom archive profile is currently adopted.

This directory is a reserved placeholder.  It deliberately contains no
archives, and it must never receive the ESP32-S3 ones.

P4 builds resolve every ESP-IDF library from the stock PlatformIO framework
package.  yoradio_build.py maps mcu "esp32p4" to family "p4", finds that
PROFILES["p4"] is None, and returns the STOCK decision before it ever
constructs an overlay path.  Consequently a P4 build receives:

    no project-local library search path
    no S3 archives
    no S3 mbedTLS --wrap flags

That stays true until a future P4-specific preflight proves a P4 pack, at
which point it would be adopted here with its own manifest and its own
KnownGood SHA256 table in yoradio_build.py.
