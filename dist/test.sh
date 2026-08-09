#!/usr/bin/env bash

# Build and run the unit test suite only

set -e
cd "$(dirname "$0")/.."

meson setup --prefix=/usr --reconfigure build
ninja -C build
meson test -C build --print-errorlogs
