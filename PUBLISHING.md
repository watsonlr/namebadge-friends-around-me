# Publishing Friends Around Me to OTA Catalog

This guide explains how to publish the Friends Around Me app to your BYUI eBadge OTA catalog so users can download it via the bootloader's "OTA App Download" menu.

## Prerequisites

- Built `friends_around_me.bin` (from `build/friends_around_me.bin`)
- Access to your GitHub Pages repository hosting the app catalog
- Git configured for pushing to GitHub

## Step 1: Build the Application

```bash
cd /path/to/namebadge-friends-around-me
source ~/esp/esp-idf/export.sh
idf.py build
```

The output binary will be at: `build/friends_around_me.bin`

## Step 2: Calculate Binary Size and SHA-256

```bash
# Get file size in bytes
stat -c%s build/friends_around_me.bin

# Calculate SHA-256 hash
sha256sum build/friends_around_me.bin
```

Example output:
```
564208
73bc9ef8a1234567890abcdef1234567890abcdef1234567890abcdef123456  build/friends_around_me.bin
```

## Step 3: Upload Binary to GitHub

Assuming you have a `namebadge-apps` repository for hosting app binaries:

```bash
# Navigate to your apps repository
cd /path/to/namebadge-apps

# Create apps directory if it doesn't exist
mkdir -p apps

# Copy the binary
cp /path/to/namebadge-friends-around-me/build/friends_around_me.bin apps/

# Commit and push
git add apps/friends_around_me.bin
git commit -m "Add Friends Around Me app binary v1"
git push origin main
```

The binary will be accessible at:
```
https://raw.githubusercontent.com/<username>/namebadge-apps/main/apps/friends_around_me.bin
```

## Step 4: Update the Manifest JSON

Edit your app catalog JSON file (e.g., `catalog.json` or `apps.json`) and add the Friends Around Me entry:

```json
[
  {
    "name": "Friends Around Me",
    "version": 1,
    "url": "https://raw.githubusercontent.com/<username>/namebadge-apps/main/apps/friends_around_me.bin",
    "size": 564208,
    "sha256": "73bc9ef8a1234567890abcdef1234567890abcdef1234567890abcdef123456"
  }
]
```

**Important Fields:**
- `name`: Display name shown in the OTA menu (max 48 chars)
- `version`: Integer version number (increment for updates)
- `url`: Direct URL to the binary file (must be HTTPS for GitHub Pages)
- `size`: Exact file size in bytes (use `stat` command)
- `sha256`: SHA-256 hash (use `sha256sum` command)

**Note:** The `icon` field is optional and no longer used since the bootloader now displays text-only listings.

## Step 5: Publish the Updated Manifest

Commit and push the updated manifest:

```bash
cd /path/to/namebadge-apps
git add catalog.json
git commit -m "Add Friends Around Me to catalog"
git push origin main
```

Wait a few minutes for GitHub Pages to update.

## Step 6: Configure Badge to Use Your Manifest

Users need to configure their badges to point to your manifest URL during the initial WiFi setup portal:

**Manifest URL Example:**
```
https://<username>.github.io/namebadge-apps/catalog.json
```

This is entered in the captive portal's "Manifest URL" field.

## Testing the OTA Download

1. Press RESET + BOOT on the badge to enter the factory loader
2. Navigate to "OTA App Download"
3. Wait for WiFi connection and manifest fetch
4. You should see "Friends Around Me" in the text list
5. Select it and press A or Right to download
6. Badge will download, verify, and reboot into the app

## Updating the App

To publish a new version:

1. Make your code changes
2. Build: `idf.py build`
3. Calculate new size and SHA-256
4. Upload new binary with version suffix: `friends_around_me_v2.bin`
5. Update manifest with new `version`, `url`, `size`, and `sha256`
6. Users can re-download to update

## Troubleshooting

### "Manifest not found"
- Verify the manifest URL is correct in the badge's NVS configuration
- Check that GitHub Pages is enabled for your repository
- Ensure the JSON file is accessible via the raw GitHub URL

### "SHA-256 mismatch"
- Recalculate the hash: `sha256sum build/friends_around_me.bin`
- Ensure you're uploading the exact same binary file
- Wait for GitHub Pages cache to update (can take 5-10 minutes)

### "Download failed"
- Check that the binary URL is correct and accessible
- Verify the binary size matches the `size` field
- Ensure WiFi connection is stable

### App won't boot after OTA
- Verify you built for the correct target: `idf.py set-target esp32s3`
- Check partition table matches (960 KB OTA slots)
- Review serial monitor output for boot errors

## Binary Size Limits

The OTA partition is **960 KB (983,040 bytes)**. Your app binary must fit within this limit.

Current Friends Around Me app size: ~565 KB (well within limits)

To reduce size if needed:
- Enable compiler optimizations for size: `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`
- Remove debug symbols
- Minimize included components

## GitHub Pages Setup

If you don't have a GitHub Pages repository yet:

1. Create a new repository: `namebadge-apps`
2. Go to Settings → Pages
3. Source: Deploy from branch `main` / root
4. Wait for deployment
5. Your manifest will be at: `https://<username>.github.io/namebadge-apps/catalog.json`

---

**Related Documentation:**
- [Bootloader OTA Manager](https://github.com/watsonlr/BYUI-Namebadge4-OTA)
- [Hardware Reference](doc/HARDWARE.md)
- [Architecture Guide](doc/ARCHITECTURE.md)
