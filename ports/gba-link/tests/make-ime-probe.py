#!/usr/bin/env python3
"""Assemble the original IME test using the shared homebrew ROM builder."""
from pathlib import Path
import subprocess
import sys
HERE = Path(__file__).resolve().parent
output = sys.argv[1] if len(sys.argv) > 1 else 'build/gba-link/ime-probe.gba'
subprocess.run([sys.executable, str(HERE / 'make-probe.py'), output,
                str(HERE / 'ime-probe.S')], check=True)
