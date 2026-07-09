"""Pytest configuration for the verification suite."""

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # make `import vvlib` work
