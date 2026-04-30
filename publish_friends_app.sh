#!/usr/bin/env bash
# ==========================================================================
#  publish_friends_app.sh  —  Publish Friends Around Me to OTA catalog
#
#  Usage:
#    ./publish_friends_app.sh <path-to-apps-repo>
#
#  Example:
#    ./publish_friends_app.sh ~/Repositories/namebadge-apps
#
#  What it does:
#    1. Reads build/friends_around_me.bin
#    2. Calculates size and SHA-256
#    3. Copies binary to apps repo
#    4. Updates or creates catalog entry
#    5. Commits and pushes to GitHub
# ==========================================================================
set -euo pipefail

APPS_REPO="${1:-}"

if [[ -z "$APPS_REPO" ]]; then
    echo "Usage: $0 <path-to-apps-repo>"
    echo "  Example: $0 ~/Repositories/namebadge-apps"
    exit 1
fi

APP_BIN="build/friends_around_me.bin"
APP_NAME="Friends Around Me"
APP_DIR="$APPS_REPO/apps"
CATALOG="$APPS_REPO/catalog.json"

# Check if binary exists
[[ -f "$APP_BIN" ]] || { 
    echo "ERROR: $APP_BIN not found. Run 'idf.py build' first."; 
    exit 1; 
}

# Create apps directory if needed
mkdir -p "$APP_DIR"

# ── Helpers ───────────────────────────────────────────────────────────
filesize() { stat -c%s "$1" 2>/dev/null || stat -f%z "$1"; }
sha256()   { sha256sum "$1" 2>/dev/null | awk '{print $1}' || shasum -a 256 "$1" | awk '{print $1}'; }

# ── Compute values ────────────────────────────────────────────────────
APP_SIZE=$(filesize "$APP_BIN")
APP_SHA=$(sha256 "$APP_BIN")

echo "================================================================"
echo "  Publishing: $APP_NAME"
echo "================================================================"
echo "  Binary:  $APP_BIN"
echo "  Size:    $APP_SIZE bytes ($(($APP_SIZE / 1024)) KB)"
echo "  SHA-256: $APP_SHA"
echo ""

# ── Get version ───────────────────────────────────────────────────────
if [[ -f "$CATALOG" ]]; then
    # Extract current version for this app from catalog
    OLD_VER=$(python3 -c "
import json
with open('$CATALOG') as f:
    catalog = json.load(f)
    apps = catalog if isinstance(catalog, list) else catalog.get('apps', [])
    for app in apps:
        if app.get('name') == '$APP_NAME':
            print(app.get('version', 0))
            exit()
    print(0)
" 2>/dev/null || echo 0)
else
    OLD_VER=0
fi
NEW_VER=$(( OLD_VER + 1 ))
echo "  Version: $OLD_VER → $NEW_VER"
echo ""

# ── Derive GitHub Pages URL ───────────────────────────────────────────
REMOTE=$(git -C "$APPS_REPO" remote get-url origin 2>/dev/null || true)
if [[ "$REMOTE" =~ github\.com[:/]([^/]+)/([^/.]+) ]]; then
    GH_USER="${BASH_REMATCH[1]}"
    GH_REPO="${BASH_REMATCH[2]}"
    BASE_URL="https://${GH_USER}.github.io/${GH_REPO}"
else
    BASE_URL="https://YOUR_USERNAME.github.io/namebadge-apps"
    echo "WARN: Could not detect GitHub remote. Using placeholder URL."
fi

APP_URL="${BASE_URL}/apps/friends_around_me.bin"
echo "  URL:     $APP_URL"
echo ""

# ── Copy binary ───────────────────────────────────────────────────────
cp "$APP_BIN" "$APP_DIR/friends_around_me.bin"
echo "✓ Copied binary to $APP_DIR/friends_around_me.bin"

# ── Update catalog ────────────────────────────────────────────────────
python3 - <<EOF
import json
import os

catalog_path = "$CATALOG"
new_entry = {
    "name": "$APP_NAME",
    "version": $NEW_VER,
    "url": "$APP_URL",
    "size": $APP_SIZE,
    "sha256": "$APP_SHA"
}

# Load existing catalog or create new
if os.path.exists(catalog_path):
    with open(catalog_path, 'r') as f:
        catalog = json.load(f)
    # Support both array and object formats
    if isinstance(catalog, list):
        apps = catalog
    else:
        apps = catalog.get('apps', [])
    
    # Find and update existing entry, or append new
    found = False
    for i, app in enumerate(apps):
        if app.get('name') == '$APP_NAME':
            apps[i] = new_entry
            found = True
            break
    if not found:
        apps.append(new_entry)
    
    # Save as array format (simpler)
    catalog = apps
else:
    catalog = [new_entry]

# Write catalog
with open(catalog_path, 'w') as f:
    json.dump(catalog, f, indent=2)
    f.write('\n')

print(f"✓ Updated catalog: {catalog_path}")
print(f"  Added/updated '{new_entry['name']}' v{new_entry['version']}")
EOF

# ── Commit and push ───────────────────────────────────────────────────
cd "$APPS_REPO"
git add apps/friends_around_me.bin catalog.json
git commit -m "Publish Friends Around Me v${NEW_VER} (${APP_SIZE} bytes)" || {
    echo ""
    echo "Note: No changes to commit (binary already published)"
    exit 0
}

echo ""
read -p "Push to GitHub? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    git push origin main
    echo ""
    echo "================================================================"
    echo "  ✓ Published successfully!"
    echo "================================================================"
    echo "  Manifest URL: ${BASE_URL}/catalog.json"
    echo ""
    echo "  Wait ~5 minutes for GitHub Pages to update, then:"
    echo "  1. Configure badge manifest URL in WiFi portal"
    echo "  2. Select 'OTA App Download' from loader menu"
    echo "  3. Choose '$APP_NAME' from the list"
    echo "================================================================"
else
    echo "Skipped push. Run 'git push' manually when ready."
fi
EOF
chmod +x publish_friends_app.sh
