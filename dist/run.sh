#!/usr/bin/env bash

# Build and run the project

set -e
cd "$(dirname "$0")"

pkill -x kokusei 2>/dev/null || true

./install.sh

kokusei
