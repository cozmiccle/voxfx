#!/bin/sh
# Installs voxfx for the current user (no root needed, works on immutable
# distros like Bazzite/Silverblue): ~/.local/opt/voxfx, a `voxfx` command in
# ~/.local/bin and an app-menu entry. Run it again to update.
set -e
HERE=$(dirname "$(readlink -f "$0")")
DEST="$HOME/.local/opt/voxfx"
if [ "$HERE" != "$DEST" ]; then
    rm -rf "$DEST"
    mkdir -p "$DEST"
    cp -r "$HERE"/. "$DEST"/
fi
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications"
ln -sf "$DEST/voxfx" "$HOME/.local/bin/voxfx"
for size in 256x256 64x64 scalable; do
    src=$(ls "$DEST"/share/icons/hicolor/$size/apps/voxfx.* 2>/dev/null | head -1)
    [ -n "$src" ] || continue
    mkdir -p "$HOME/.local/share/icons/hicolor/$size/apps"
    cp "$src" "$HOME/.local/share/icons/hicolor/$size/apps/"
done
sed "s|^Exec=.*|Exec=$DEST/voxfx|" "$DEST/share/applications/voxfx.desktop" \
    > "$HOME/.local/share/applications/voxfx.desktop"
update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true
gtk-update-icon-cache -q "$HOME/.local/share/icons/hicolor" >/dev/null 2>&1 || true
echo "Installed. Start voxfx from your app menu, or run: voxfx"
