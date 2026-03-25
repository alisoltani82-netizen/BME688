"""
Pre-build script: patches the Bosch BSEC2 library so that BSEC warnings
(positive return codes like BSEC_W_DOSTEPS_GASINDEXMISS = 5) do NOT abort
processData() and break the sensor readout loop in run().

Without this patch, BSEC treats warnings the same as errors in processData(),
causing run() to exit early and skip remaining sensor data fields.  In scan/
parallel mode this means some heater-profile steps never reach the callback,
BSEC never accumulates a complete gas profile, and the algorithm is stuck at
accuracy 0 with 0.0 gas estimates forever.

The fix: change  `if (status != BSEC_OK)` → `if (status < BSEC_OK)` so only
real errors (negative codes) abort processing.

Runs automatically before every build via extra_scripts in platformio.ini.
"""
import glob
import os

Import("env")  # noqa: F821

OLD = "if (status != BSEC_OK)"
NEW = "if (status < BSEC_OK)"

patterns = [
    os.path.join(".pio", "libdeps", "*", "*bsec*", "src", "bsec2.cpp"),
    os.path.join(".pio", "libdeps", "*", "*BSEC*", "src", "bsec2.cpp"),
]

for pat in patterns:
    for path in glob.glob(pat):
        with open(path, "r") as f:
            content = f.read()
        if OLD in content:
            content = content.replace(OLD, NEW)
            with open(path, "w") as f:
                f.write(content)
            print(f"[BSEC2-PATCH] Patched warning handling in {path}")
        elif NEW in content:
            print(f"[BSEC2-PATCH] Already patched: {path}")
        else:
            print(f"[BSEC2-PATCH] WARNING: Pattern not found in {path}")
