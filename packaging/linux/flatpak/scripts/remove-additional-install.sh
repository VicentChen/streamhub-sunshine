#!/bin/sh

# User Service
systemctl --user stop app-dev.lizardbyte.app.Sunshine
rm "$HOME/.config/systemd/user/app-dev.lizardbyte.app.Sunshine.service"
systemctl --user daemon-reload
echo "Sunshine User Service has been removed."

# Remove rules
echo "Input rules removed."
