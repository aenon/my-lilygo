#!/usr/bin/env bash
# Clone LilyGo vendor repos into vendor/.  Idempotent -- safe to re-run.
#
# Each entry below is "<git-url>|<branch>|<target-dir>".  Add another line
# when you start work on a new device.
#
# Notes:
#   - T5 E-Paper S3 Pro: branch H752-01 is the latest hardware revision; SKU
#     H752-02 ships on the same PCB with the LoRa+GPS option populated.
#   - T-Dongle S3: not yet enabled in platformio.ini.  The clone is harmless
#     so we fetch it preemptively.

set -euo pipefail

VENDORS=(
    "https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO.git|H752-01|vendor/T5S3-4.7-e-paper-PRO"
    "https://github.com/Xinyuan-LilyGO/T-Dongle-S3.git|main|vendor/T-Dongle-S3"
)

cd "$(dirname "$0")/.."
mkdir -p vendor

for entry in "${VENDORS[@]}"; do
    IFS='|' read -r REPO_URL BRANCH TARGET <<< "$entry"
    if [ -d "$TARGET/.git" ]; then
        echo "[$(basename "$TARGET")] already cloned; pulling '$BRANCH'..."
        git -C "$TARGET" fetch --quiet origin "$BRANCH"
        git -C "$TARGET" checkout --quiet "$BRANCH"
        git -C "$TARGET" pull --quiet --ff-only
    else
        echo "[$(basename "$TARGET")] cloning $REPO_URL ($BRANCH)..."
        git clone --branch "$BRANCH" --depth 1 "$REPO_URL" "$TARGET"
    fi
done

echo
echo "Vendor repos ready under vendor/."
echo
echo "Next steps:"
echo "  - copy each apps/<device>/<app>/main/secrets.example.h to secrets.h and edit"
echo "  - pick an env in platformio.ini (default: t5_epaper_s3_pro)"
echo "  - pio run -e t5_epaper_s3_pro -t upload"
echo "  - pio device monitor --rts 0 --dtr 0"
