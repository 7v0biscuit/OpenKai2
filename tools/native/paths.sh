#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Shared workspace configuration for the native build and launcher.
native_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
native_repo=$(cd -- "$native_source/../.." && pwd)
export OPENKAI2_WORK_DIR=${OPENKAI2_WORK_DIR:-$native_repo/.local/openkai2}
mkdir -p "$OPENKAI2_WORK_DIR"
OPENKAI2_WORK_DIR=$(cd -- "$OPENKAI2_WORK_DIR" && pwd)
export OPENKAI2_WORK_DIR
native_base=$OPENKAI2_WORK_DIR
