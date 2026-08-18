#!/usr/bin/env bash

# Build, deploy, and run the shell.

set -e
cd "$(dirname "$0")"

kokusei kill || true

./install.sh

kokusei
