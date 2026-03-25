"""
Pre-build script: copies the BME688 H2S/VSC selectivity config (bsec_selectivity.txt)
from the installed Bosch BSEC2 library into src/config/model/
so it can be #included by bsec_model_config.h.

Model used: bme688_sel_33v_3s_4d
  - BME688 sensor, 3.3 V supply, 3-second heater scan, 4-day calibration period
  - GAS_ESTIMATE_1 = H2S (Volatile Sulfur Compound) probability
  - GAS_ESTIMATE_2 = Non-H2S probability

Runs automatically before every build via extra_scripts in platformio.ini.
"""
import glob
import os
import shutil

Import("env")  # noqa: F821 (PlatformIO build script context)

DEST = os.path.join("src", "config", "model", "bsec_selectivity.txt")

# FieldAir_HandSanitizer — the only pre-trained classifier Bosch ships
# (selectivity configs may be heater profiles without classification weights)
MODEL_PATH = os.path.join("FieldAir_HandSanitizer", "bsec_selectivity.txt")

SEARCH_PATTERNS = [
    os.path.join(".pio", "libdeps", "*", "*BSEC2*",
                 "src", "config", MODEL_PATH),
    os.path.join(".pio", "libdeps", "*", "*BSEC*",
                 "src", "config", MODEL_PATH),
    os.path.join(".pio", "libdeps", "*", "*bsec*",
                 "src", "config", MODEL_PATH),
]


def copy_config(source, target, env):  # noqa: ARG001
    for pattern in SEARCH_PATTERNS:
        matches = glob.glob(pattern)
        if matches:
            os.makedirs(os.path.dirname(DEST), exist_ok=True)
            shutil.copy(matches[0], DEST)
            print(f"[BSEC2] Copied FieldAir_HandSanitizer config -> {DEST}")
            return

    # Check if dest already has real content (not the placeholder '0')
    if os.path.exists(DEST):
        with open(DEST) as f:
            content = f.read().strip()
        if content and content != "0":
            print("[BSEC2] Config already present, skipping copy.")
            return

    print(
        "[BSEC2] WARNING: Could not find FieldAir_HandSanitizer config in "
        ".pio/libdeps. Run 'pio run' once to install libraries, then build again."
    )


# Run immediately at script load time (script is loaded as pre: so this
# executes before any source files are compiled).
copy_config(None, None, env)
