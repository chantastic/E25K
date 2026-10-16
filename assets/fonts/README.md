# E25K fonts

The countdown uses Roboto Condensed, converted to an LVGL font with 4-bit
coverage (16 opacity levels). Its digits have equal advances, so the countdown
does not shift horizontally. Only `0123456789:` are included in firmware.

The 166 px source size produces a 122 px glyph line. Regenerate the committed
`firmware/c25k/e25k_timer_font.c` with `scripts/generate-font.sh` (requires npm).
Normal firmware builds use the generated C and do not need Node or a font tool.

`RobotoCondensed.ttf` is the variable upright font from
[Google Fonts](https://github.com/google/fonts/tree/main/ofl/robotocondensed),
retrieved September 20, 2026. The default weight is used by the converter.
See [OFL-RobotoCondensed.txt](OFL-RobotoCondensed.txt) for its SIL Open Font License.

Other text uses LVGL's bundled 4-bit Montserrat fonts. LVGL supplies those font
assets and their licensing; no copy of the LVGL library is vendored here.
