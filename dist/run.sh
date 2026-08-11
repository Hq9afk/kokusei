#!/usr/bin/env bash

# Build and run the project

set -e
cd "$(dirname "$0")"

kokusei kill || true

./install.sh

kokusei
