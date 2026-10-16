#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
# Generated C is committed; Node/npm are only needed when changing this font.
npx --yes lv_font_conv@1.5.3 --font assets/fonts/RobotoCondensed.ttf \
  --size 166 --bpp 4 --range 0x30-0x39 --symbols : --format lvgl \
  --no-compress -o firmware/c25k/e25k_timer_font.c
