#!/usr/bin/env bash
# publish.sh — build Friends Around Me, copy binaries to the Pages repo,
# and update apps/manifest.json so the webflash site (Single Program Flash
# dropdown) and the badge OTA menu both pick it up.
#
# Adapted from watsonlr/namebadge_pacman/publish.sh. Key differences:
#   - App lives at 0x20000 (matches our partitions.csv factory slot)
#   - We also publish ota_data_initial.bin (so OTA pointers start clean
#     instead of being read from random flash)
#   - Portable stat/sha helpers for macOS or Linux
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_BIN="${SCRIPT_DIR}/build/friends_around_me.bin"
BL_BIN="${SCRIPT_DIR}/build/bootloader/bootloader.bin"
PT_BIN="${SCRIPT_DIR}/build/partition_table/partition-table.bin"
OD_BIN="${SCRIPT_DIR}/build/ota_data_initial.bin"

GITHUB_PAGES_BASE="https://byu-i-ebadge.github.io/apps"

# Locate the Pages repo — set NAMEBADGE_PAGES_REPO to override.
if [[ -z "${NAMEBADGE_PAGES_REPO:-}" ]]; then
    SIBLING="$(cd "${SCRIPT_DIR}/.." && pwd)/byu-i-ebadge.github.io"
    if [[ -d "${SIBLING}/.git" ]]; then
        NAMEBADGE_PAGES_REPO="${SIBLING}"
    fi
fi
if [[ -z "${NAMEBADGE_PAGES_REPO:-}" ]] || [[ ! -d "${NAMEBADGE_PAGES_REPO}/.git" ]]; then
    echo "ERROR: Cannot find the byu-i-ebadge.github.io Pages repo."
    echo "       Clone it as a sibling of this repo, then re-run, or"
    echo "       export NAMEBADGE_PAGES_REPO=/path/to/byu-i-ebadge.github.io"
    echo
    echo "       git clone git@github.com:BYU-I-eBadge/byu-i-ebadge.github.io.git"
    exit 1
fi
PAGES_REPO="${NAMEBADGE_PAGES_REPO}"
DEST="${PAGES_REPO}/apps"
MANIFEST="${DEST}/manifest.json"

APP_NAME="Friends Around Me"
# Use the same filenames as the existing manifest entry so we update in place.
APP_DEST_NAME="friends.bin"
BL_DEST_NAME="friends_bl.bin"
PT_DEST_NAME="friends_pt.bin"
OD_DEST_NAME="friends_ota_data.bin"

# ── Portable helpers ─────────────────────────────────────────────────────────
filesize() { stat -c%s "$1" 2>/dev/null || stat -f%z "$1"; }
sha256()   { command -v sha256sum >/dev/null && sha256sum "$1" | awk '{print $1}' \
                                            || shasum -a 256 "$1" | awk '{print $1}'; }

# ── Build ────────────────────────────────────────────────────────────────────
echo "Building Friends Around Me..."
if [[ -f "${HOME}/esp/esp-idf/export.sh" ]]; then
    # shellcheck disable=SC1091
    source "${HOME}/esp/esp-idf/export.sh" >/dev/null 2>&1
fi
idf.py -C "${SCRIPT_DIR}" build

for f in "$APP_BIN" "$BL_BIN" "$PT_BIN" "$OD_BIN"; do
    [[ -f "$f" ]] || { echo "ERROR: binary not found at ${f}"; exit 1; }
done

# ── Sync Pages repo ──────────────────────────────────────────────────────────
git -C "${PAGES_REPO}" pull --ff-only

mkdir -p "${DEST}"
cp "${APP_BIN}" "${DEST}/${APP_DEST_NAME}"
cp "${BL_BIN}"  "${DEST}/${BL_DEST_NAME}"
cp "${PT_BIN}"  "${DEST}/${PT_DEST_NAME}"
cp "${OD_BIN}"  "${DEST}/${OD_DEST_NAME}"
echo "Copied ${APP_DEST_NAME}     (app,             0x20000)"
echo "Copied ${BL_DEST_NAME}  (bootloader,      0x0)"
echo "Copied ${PT_DEST_NAME}  (partition table, 0x8000)"
echo "Copied ${OD_DEST_NAME}  (ota_data init,   0xF000)"

APP_SIZE=$(filesize "${DEST}/${APP_DEST_NAME}")
APP_SHA256=$(sha256  "${DEST}/${APP_DEST_NAME}")

# ── Update manifest ──────────────────────────────────────────────────────────
python3 - <<EOF
import json

manifest_path = "${MANIFEST}"
try:
    with open(manifest_path) as f:
        m = json.load(f)
except (FileNotFoundError, json.JSONDecodeError):
    m = {"apps": []}

m.setdefault("apps", [])

old_ver = 0
for app in m["apps"]:
    if app.get("name") == "${APP_NAME}":
        old_ver = int(app.get("version", 0))
        break
new_ver = old_ver + 1

# Entry serves both consumers:
#   webflash site → reads "binaries" (bl + pt + ota_data + app at addresses)
#   badge OTA menu → reads "url" / "size" / "sha256" (app only)
new_entry = {
    "name": "${APP_NAME}",
    "version": new_ver,
    "url": "${GITHUB_PAGES_BASE}/${APP_DEST_NAME}",
    "size": ${APP_SIZE},
    "sha256": "${APP_SHA256}",
    "binaries": [
        {"url": "${GITHUB_PAGES_BASE}/${BL_DEST_NAME}", "address": 0x0},
        {"url": "${GITHUB_PAGES_BASE}/${PT_DEST_NAME}", "address": 0x8000},
        {"url": "${GITHUB_PAGES_BASE}/${OD_DEST_NAME}", "address": 0xF000},
        {"url": "${GITHUB_PAGES_BASE}/${APP_DEST_NAME}", "address": 0x20000},
    ],
}

replaced = False
for i, app in enumerate(m["apps"]):
    if app.get("name") == "${APP_NAME}":
        m["apps"][i] = new_entry
        replaced = True
        break
if not replaced:
    m["apps"].append(new_entry)

with open(manifest_path, "w") as f:
    json.dump(m, f, indent=2)
    f.write("\n")

action = "Updated" if replaced else "Added"
print(f"  {action} ${APP_NAME} v{new_ver} in manifest.json ({${APP_SIZE}} bytes, sha256={'${APP_SHA256}'[:16]}...)")
EOF

# ── Commit & push ────────────────────────────────────────────────────────────
cd "${PAGES_REPO}"
if git diff --quiet apps/; then
    echo "No changes — binaries are identical to what's already committed."
    exit 0
fi

git add apps/
git commit -m "Update ${APP_NAME} ($(date '+%Y-%m-%d %H:%M'))

App:        ${APP_DEST_NAME} (${APP_SIZE} bytes, 0x20000)  sha256=${APP_SHA256}
Bootloader: ${BL_DEST_NAME} (0x0)
Partitions: ${PT_DEST_NAME} (0x8000)
OTA data:   ${OD_DEST_NAME} (0xF000)"
git push

echo
echo "Done. Live at: ${GITHUB_PAGES_BASE}/${APP_DEST_NAME}"
echo "  Webflash: select '${APP_NAME}' in the Single Program Flash dropdown"
echo "  OTA:      badge bootloader will find it in the app catalog"
