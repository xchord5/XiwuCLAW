#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FACTORY = ROOT / "tones" / "factory"
OUT = ROOT / "faust" / "generated"
SCRIPT = Path(__file__).resolve().parent / "tone_json_to_faust.py"


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    tone_files = sorted(FACTORY.glob("*.json"))
    if not tone_files:
        print("no tone json files found")
        return 0

    for tone in tone_files:
        out_file = OUT / (tone.stem + ".dsp")
        cmd = [sys.executable, str(SCRIPT), "--in", str(tone), "--out", str(out_file)]
        subprocess.check_call(cmd)

    print(f"generated {len(tone_files)} dsp files into {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
