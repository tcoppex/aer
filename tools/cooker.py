#!/usr/bin/env python3

# -----------------------------------------------------------------------------
# Cooker - v0.1.0
#
# Handles the transformation of raw source formats (PNG, JPG, EXR, ..) into
# GPU-optimized KTX2 containers.
#
# Usage:
#   $ ./cooker.py
#
# -----------------------------------------------------------------------------

import os
import subprocess
from pathlib import Path

# -----------------------------------------------------------------------------

# Configuration
SOURCE_DIR = "./assets/textures"
OUTPUT_DIR = "./assets/ktx"
TOKTX_PATH = "toktx"

LDR_FORMATS = {".png", ".jpg", ".jpeg", ".tga"}
HDR_FORMATS = {".exr"}

DATA_MAP_SUFFIXES = {"_norm", "_metal", "_rough", "_ao", "_height"}

# -----------------------------------------------------------------------------

def bake_textures():
  RAW_IMAGE_FORMATS = (*LDR_FORMATS, *HDR_FORMATS)
  for root, _, files in os.walk(SOURCE_DIR):
    for file in files:
      if file.endswith(RAW_IMAGE_FORMATS):
        src_path = Path(root) / file

        rel_path = src_path.relative_to(SOURCE_DIR)
        out_path = (Path(OUTPUT_DIR) / rel_path).with_suffix(".ktx2")
        out_path.parent.mkdir(parents=True, exist_ok=True)

        if out_path.exists() and out_path.stat().st_mtime > src_path.stat().st_mtime:
          continue

        print(f"Baking: {rel_path}...")

        fn = file.lower()
        ext_id = fn.rfind(".")
        ext = fn[ext_id+1:]
        suffix = fn[fn.rfind("_"):ext_id]

        cmd = [TOKTX_PATH]
        cmd += ["--genmipmap", "--t2"]

        if ext in HDR_FORMATS:
          # HDR Data (Skyboxes) - Use BC6H for GPU HDR
          cmd += ["--encode", "etc1s"]
          cmd += ["--clevel", "5"]
          cmd += ["--qlevel", "128"]
        elif suffix in DATA_MAP_SUFFIXES:
          # Linear data (Normal, Roughness, Metalness)
          cmd += ["--assign_oetf", "linear"]
          cmd += ["--uastc_quality", "2"]
          cmd += ["--zcmp", "1"]
        else:
          # Color data (Albedo) - Use SRGB transfer function
          cmd += ["--assign_oetf", "srgb"]
          cmd += ["--uastc_quality", "2"]
          cmd += ["--zcmp", "1"]

        cmd += [str(out_path), str(src_path)]
        subprocess.run(cmd, check=True)

# -----------------------------------------------------------------------------

if __name__ == "__main__":
  bake_textures()
  print(u"> Cooking completed (｡˃ ᵕ ˂ )⸝ ♨ ")

# -----------------------------------------------------------------------------
