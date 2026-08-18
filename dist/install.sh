#!/usr/bin/env bash

# Build and deploy the shell.

set -e
cd "$(dirname "$0")/.."

meson setup --prefix=/usr --reconfigure build
ninja -C build
sudo ninja -C build install
