#!/usr/bin/env bash
set -euo pipefail
DAY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"
FQBN='esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi'
mkdir -p "$DAY_ROOT/.build/firmware"
LVGL_FLAGS="-DLV_CONF_INCLUDE_SIMPLE=1 -DLV_LVGL_H_INCLUDE_SIMPLE=1 -I\"$DAY_ROOT/firmware/c25k\""
"$ARDUINO_CLI" compile --fqbn "$FQBN" --warnings default --export-binaries=false \
  --build-property "compiler.c.extra_flags=$LVGL_FLAGS" \
  --build-property "compiler.cpp.extra_flags=$LVGL_FLAGS" \
  --output-dir "$DAY_ROOT/.build/firmware" "$DAY_ROOT/firmware/c25k"
