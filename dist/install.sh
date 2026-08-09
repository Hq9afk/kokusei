#!/usr/bin/env bash

# Builds and deploy system-wide

set -e
cd "$(dirname "$0")/.."

meson setup --prefix=/usr --reconfigure build
ninja -C build
sudo ninja -C build install
