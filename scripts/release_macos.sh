#!/usr/bin/env bash
set -euo pipefail

# --- Config ---
APP_NAME="Tiny Text Client"
PKG_NAME="${APP_NAME// /_}"            # Tiny_Text_Client
REPO_ROOT="$(cd "$(dirname "$0")/.."; pwd)"
cd "$REPO_ROOT"

# Which bump? default=patch | minor | major
BUMP_KIND="${1:-patch}"

# Tools (pin py2app & deps to known-good)
PY2APP_VER="0.28.8"
SETUPTOOLS_PIN="<81"
WHEEL_PIN="<0.45"

# --- Helpers ---
die(){ echo "ERROR: $*" >&2; exit 1; }
have(){ command -v "$1" >/dev/null 2>&1; }

read_version() { cat VERSION 2>/dev/null || echo "1.0.0"; }

bump_version() {
  local v="$1" kind="${2:-patch}"
  IFS=. read -r MA MI PA <<<"$v"
  case "$kind" in
    major) MA=$((MA+1)); MI=0; PA=0 ;;
    minor) MI=$((MI+1)); PA=0 ;;
    patch) PA=$((PA+1)) ;;
    *) die "unknown bump kind: $kind (use: patch|minor|major)" ;;
  esac
  echo "${MA}.${MI}.${PA}"
}

# --- Ensure version file exists ---
if [ ! -f VERSION ]; then
  echo "1.0.0" > VERSION
fi

# --- Compute next version ---
CURR="$(read_version)"
NEXT="$(bump_version "$CURR" "$BUMP_KIND")"
echo "[i] Bumping version: $CURR -> $NEXT"
echo "$NEXT" > VERSION

# --- venv ---
if [ -z "${VIRTUAL_ENV:-}" ]; then
  echo "[i] Creating/using .venv"
  python3 -m venv .venv
  # shellcheck disable=SC1091
  source .venv/bin/activate
else
  echo "[i] Using active venv: $VIRTUAL_ENV"
fi
python -m pip install --upgrade "setuptools${SETUPTOOLS_PIN}" "wheel${WHEEL_PIN}" "py2app==${PY2APP_VER}" pillow

# --- Icons ---
if [ ! -f assets/icon.icns ]; then
  echo "[i] Generating icons (no assets/icon.icns found)"
  python scripts/make_app_assets.py design/icon.png || python scripts/make_app_assets.py
else
  echo "[i] Using existing assets/icon.icns"
fi

# --- Clean build dirs ---
rm -rf build dist

# --- Build .app ---
python setup.py py2app

APP_PATH="dist/${APP_NAME}.app"
[ -d "$APP_PATH" ] || die "App bundle not found: $APP_PATH"

# --- Ad-hoc sign (local run friendliness) ---
codesign --force --deep --sign - "$APP_PATH" || true

# --- Package artifacts (versioned names) ---
ZIP="dist/${PKG_NAME}-${NEXT}.zip"
DMG="dist/${PKG_NAME}-${NEXT}.dmg"

# zip
( cd dist && ditto -c -k --keepParent "${APP_NAME}.app" "${PKG_NAME}-${NEXT}.zip" )
# dmg
hdiutil create -volname "${APP_NAME}" -srcfolder "$APP_PATH" -ov -format UDZO "$DMG"

# checksums (optional)
if have shasum; then
  echo "[i] Checksums:"
  (cd dist && shasum -a 256 "$(basename "$ZIP")" "$(basename "$DMG")")
fi

# --- Commit, tag, push ---
git add VERSION setup.py assets/icon.icns 2>/dev/null || true
git add scripts/release_macos.sh scripts/make_app_assets.py 2>/dev/null || true
git add gui_client.py 2>/dev/null || true
git commit -m "release v${NEXT} — macOS app bundle + artifacts" || echo "[i] Nothing to commit."
git tag -a "v${NEXT}" -m "Tiny Text Client v${NEXT}" || echo "[i] Tag exists?"
git push || true
git push --tags || true

# --- GitHub Release (if gh is available) ---
if have gh; then
  echo "[i] Creating GitHub Release v${NEXT} (via gh)…"
  gh release create "v${NEXT}" "$ZIP" "$DMG" \
    --title "Tiny Text Client v${NEXT}" \
    --notes "GUI client for Tiny Text Service.
- Lists files (LIST), previews text and images (GET), Save…, Head, Open in Preview.
- Built with py2app; self-contained. If macOS warns, right-click → Open once."
else
  echo "[i] 'gh' not found; upload manually:"
  echo "   https://github.com/<your-user>/<your-repo>/releases/new?tag=v${NEXT}"
  echo "   Upload: $(basename "$ZIP"), $(basename "$DMG")"
fi

echo "[✓] Release ready:"
echo "    $APP_PATH"
echo "    $ZIP"
echo "    $DMG"
