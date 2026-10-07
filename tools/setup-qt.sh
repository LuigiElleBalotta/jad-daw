#!/usr/bin/env bash
# Installs Qt into ./.qt with aqtinstall (no account, no GUI). Usage: tools/setup-qt.sh [version]
set -euo pipefail
VERSION="${1:-6.8.3}"
python3 -m venv .qt-venv
# shellcheck disable=SC1091
. .qt-venv/bin/activate 2>/dev/null || . .qt-venv/Scripts/activate
pip install --quiet aqtinstall
case "$(uname -s)" in
  Darwin*) HOST=mac; ARCH=clang_64 ;;
  *) HOST=windows; ARCH=win64_msvc2022_64 ;;
esac
aqt install-qt "$HOST" desktop "$VERSION" "$ARCH" --outputdir .qt -m qtshadertools
echo "Qt installed. Configure with: -DCMAKE_PREFIX_PATH=$(pwd)/.qt/$VERSION/$(ls .qt/$VERSION | head -1)"
