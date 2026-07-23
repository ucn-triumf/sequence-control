#!/usr/bin/env bash
# Launcher for the UCN MIDAS sequencer frontend.
# Provisions a Python venv named "env" (if missing), installs pip
# dependencies from requirements.txt, then runs Sequencer.py.
# Derek Fujimoto

set -euo pipefail

# Resolve the directory this script lives in so relative paths (the venv,
# requirements.txt, Sequencer.py) work no matter where it is invoked from.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VENV_DIR="env"

# Create the venv only if it doesn't already exist. --system-site-packages
# exposes the system MIDAS install so "import midas" resolves (midas is not
# a PyPI package and cannot be pip-installed).
if [[ ! -f "$VENV_DIR/bin/activate" ]]; then
    echo "Creating virtual environment '$VENV_DIR'..."
    python3 -m venv --system-site-packages "$VENV_DIR"

    # shellcheck source=/dev/null
    source "$VENV_DIR/bin/activate"

    echo "Installing dependencies from requirements.txt..."
    pip install --upgrade pip
    pip install -r requirements.txt
else
    # shellcheck source=/dev/null
    source "$VENV_DIR/bin/activate"
fi

# Launch the sequencer frontend.
python Sequencer.py
