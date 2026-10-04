#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/client"
OUT="$CLIENT_DIR/ds-shop.nds"

cd "$CLIENT_DIR"

if [[ "$1" == "--test" ]]; then
    shift
    make CPPFLAGS=-DTEST_MODE "$@"
else
    make "$@"
fi

echo ""
echo "Output: $OUT"

UPDATE_ROM="$SCRIPT_DIR/roms/ds-shop.nds"
if [[ ! -d "$(dirname $UPDATE_ROM)" ]]; then
    mkdir -p "$(dirname "$UPDATE_ROM")"
fi
cp -f "$OUT" "$UPDATE_ROM"
echo "Update ROM: $UPDATE_ROM"

# Auto-deploy to melonDS's SD card folder (its DLDI "FolderPath"), so the build
# is always on the emulated SD card. The folder is read from the melonDS config
# (Flatpak first, then native); override with DEPLOY_DIR=/some/dir, or skip with
# DEPLOY_DIR= (empty). melonDS only re-reads the ROM on File > Open ROM, not on Reset.
# This only works on Linux, not MSYS. Manually copying is required on non-Linux platforms.
if [[ -z "${DEPLOY_DIR+set}" ]]; then
    for cfg in "$HOME/.var/app/net.kuribo64.melonDS/config/melonDS/melonDS.toml" \
               "$HOME/.config/melonDS/melonDS.toml"; do
        [[ -f "$cfg" ]] || continue
        DEPLOY_DIR=$(awk -F' = ' '/^\[/{sec=$0} sec=="[DLDI]" && $1=="FolderPath"{gsub(/"/,"",$2); print $2}' "$cfg")
        [[ -n "$DEPLOY_DIR" ]] && break
    done
fi
if [[ -n "$DEPLOY_DIR" ]]; then
    mkdir -p "$DEPLOY_DIR"
    cp -f "$OUT" "$DEPLOY_DIR/ds-shop.nds"
    echo "Deployed: $DEPLOY_DIR/ds-shop.nds"
    # first deploy: give the emulated SD card a config that reaches the server on
    # this PC (melonDS's emulated network sees the host as 10.64.0.1)
    if [[ ! -f "$DEPLOY_DIR/ds-shop/config.ini" ]]; then
        mkdir -p "$DEPLOY_DIR/ds-shop"
        printf 'server=10.64.0.1\nport=8888\nssid=\nui=gui\n' > "$DEPLOY_DIR/ds-shop/config.ini"
        echo "Created: $DEPLOY_DIR/ds-shop/config.ini (for the emulator: server=10.64.0.1:8888)"
    fi
else
    echo "No melonDS SD folder found; not deploying."
fi
