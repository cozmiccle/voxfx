#!/bin/sh
# Removes the app installed by install.sh. Your sounds and settings stay in
# ~/.local/share/voxfx and ~/.config/voxfx (delete those too for a clean slate).
rm -rf "$HOME/.local/opt/voxfx"
rm -f "$HOME/.local/bin/voxfx" "$HOME/.local/share/applications/voxfx.desktop"
rm -f "$HOME"/.local/share/icons/hicolor/*/apps/voxfx.png "$HOME"/.local/share/icons/hicolor/scalable/apps/voxfx.svg
echo "voxfx removed (sounds and settings kept)."
