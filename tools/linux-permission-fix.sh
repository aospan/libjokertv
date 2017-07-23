echo "ATTR{idVendor}==\"2d6b\", ATTR{idProduct}==\"7777\", MODE=\"666\"" > /etc/udev/rules.d/77-joker-tv.rules
systemctl restart systemd-udevd
