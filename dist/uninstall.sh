#!/usr/bin/env bash

# Removes system-wide install performed by install.sh

set -e
cd "$(dirname "$0")/.."

sudo ninja -C build uninstall
