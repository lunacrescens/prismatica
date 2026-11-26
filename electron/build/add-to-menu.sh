#!/bin/sh
# Adds Prismatica to your desktop's app menu (for your user only), pointing at this folder.
# Run it again if you move the folder. To remove: rm ~/.local/share/applications/prismatica.desktop
set -e
here=$(cd "$(dirname "$0")" && pwd)
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$apps"
cat > "$apps/prismatica.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Prismatica
Comment=Psychedelic music visualizer
Exec="$here/Prismatica"
Icon=$here/prismatica.png
Terminal=false
Categories=AudioVideo;Audio;Player;
StartupWMClass=prismatica
DESKTOP
update-desktop-database "$apps" 2>/dev/null || true
echo "Added Prismatica to the app menu."
