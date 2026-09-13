#!/bin/sh

# User Service
mkdir -p ~/.config/systemd/user
cp "/app/share/sunshine/systemd/user/app-dev.lizardbyte.app.Sunshine.service" "$HOME/.config/systemd/user/app-dev.lizardbyte.app.Sunshine.service"
echo "Sunshine User Service has been installed."
echo "Use [systemctl --user enable app-dev.lizardbyte.app.Sunshine] once to autostart Sunshine on login."
