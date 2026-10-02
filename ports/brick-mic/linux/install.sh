#!/bin/bash
# Run on Linux as the receiving desktop user; do not run the receiver as root.
set -euo pipefail
SOURCE=$(cd "$(dirname "$0")" && pwd -P)
DEST="$HOME/.local/share/brick-mic"
mkdir -p "$DEST" "$HOME/.config/brick-mic" "$HOME/.config/systemd/user"
chmod 700 "$HOME/.config/brick-mic"
python3 -m venv "$DEST/venv"
"$DEST/venv/bin/pip" install -r "$SOURCE/requirements.txt"
cc -std=gnu11 -O2 -Wall -Wextra -Werror "$SOURCE/keyboard.c" -o "$SOURCE/brick-mic-keyboard.new"
mv "$SOURCE/brick-mic-keyboard.new" "$SOURCE/brick-mic-keyboard"
"$DEST/venv/bin/python" "$SOURCE/receiver.py" --check-config
cp "$SOURCE/brick-mic.service" "$HOME/.config/systemd/user/brick-mic.service"
systemctl --user daemon-reload
systemctl --user enable --now brick-mic.service
printf '\nEnable boot startup once: sudo loginctl enable-linger %q\n' "$USER"
