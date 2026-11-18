#!/usr/bin/env bash
# Source this file from WSL: source scripts/env.sh
export WORKSPACE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export PACKAGES_PATH="$WORKSPACE/edk2"
export EDK_TOOLS_PATH="$WORKSPACE/edk2/BaseTools"
export CONF_PATH="$WORKSPACE/Conf"
export PYTHON_COMMAND=python3
mkdir -p "$CONF_PATH"
cd "$WORKSPACE" || return 1
source "$WORKSPACE/edk2/edksetup.sh" BaseTools
