#!/bin/bash

set -e

MODULE="usb_mass_storage"

echo "[+] Unloading $MODULE"

sudo rmmod "$MODULE"

echo "[+] Driver unloaded successfully"
