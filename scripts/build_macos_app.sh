#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"
cd "$ROOT_DIR"

APP_NAME="Tiny Text Client"
DIST_APP="dist/${APP_NAME}.app"
SRC_ICON="${1:-design/icon.png}"   # optional arg

# 0) Clean old build
rm -rf build dist

# 1) Use existing venv if active; else create .venv
if [ -z "${VIRTUAL_ENV:-}" ]; then
  echo "[i] Creating local venv at .venv"
  python3 -m venv .venv
  # shellcheck disable=SC1091
  source .venv/bin/activate
else
  echo "[i] Using active venv: $VIRTUAL_ENV"
fi

python -m pip install --upgrade pip wheel setuptools
python -m pip install pillow py2app

# 2) Generate icons (from given src if present; else placeholder)
echo "[i] Generating icons…"
python scripts/make_app_assets.py "$SRC_ICON" || python scripts/make_app_assets.py

# 3) Build the .app
python setup.py py2app

# 4) Ad-hoc code sign (so it opens without complaints locally)
codesign --force --deep --sign - "$DIST_APP" || true

echo "Built: $DIST_APP"

# 5) Optional: DMG
# APP_BASENAME="${APP_NAME// /_}"
# hdiutil create -volname "$APP_NAME" -srcfolder dist -ov -format UDZO "dist/${APP_BASENAME}.dmg"
# echo "DMG: dist/${APP_BASENAME}.dmg"
