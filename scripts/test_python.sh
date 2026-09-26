#!/usr/bin/env bash
#
# test_python.sh — run the Python test suite in the project virtual environment.
#
# Uses .venv explicitly rather than whatever `python3` happens to be first on PATH,
# because on this machine the system interpreter has none of the requirements
# installed and the failure mode is a confusing import error rather than a clear
# "environment not set up".

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_PY="$ROOT/.venv/bin/python"

if [ ! -x "$VENV_PY" ]; then
    echo "FAIL no virtual environment at $ROOT/.venv"
    echo "     create one:  python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt"
    exit 1
fi

cd "$ROOT" || exit 1
exec "$VENV_PY" -m pytest "$@"
