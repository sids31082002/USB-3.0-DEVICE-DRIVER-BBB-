#!/bin/bash

set -e

MODULE="usb_mass_storage"

echo "[+] Loading $MODULE.ko"

sudo insmod "$MODULE.ko"

echo "[+] Driver loaded successfully"
echo
echo "[+] Recent kernel messages:"
dmesg | tail -n 30
