#!/usr/bin/env python3
"""Check LVGL rendering, memory reuse and timing with RAM-only workout data.

Requires the same pyserial/Pillow environment as check-device.py. Does not
reset production progress, save sessions, toggle production settings or set RTC.
"""
import importlib.util
import json
from pathlib import Path
import sys
import time

from PIL import Image

spec = importlib.util.spec_from_file_location("check_device", Path(__file__).with_name("check-device.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
device = module.Device(sys.argv[1])
report = {}
try:
    initial = device.status()
    device.initial_inputs = initial["inputs"]
    device.expect(initial, screen="home", test=0, ui="lvgl", ui_ready=1, storage=1)
    report["initial"] = initial
    device.command("test begin")
    home = device.capture("lvgl-summary-01")
    image = Image.open(home).convert("RGB")
    # Both regions contain white marks on true black. Intermediate coverage
    # colors distinguish anti-aliased outlines/glyphs from binary hard edges.
    font_colors = len(set(image.crop((140, 108, 330, 153)).getdata()))
    circle_colors = len(set(image.crop((65, 320, 80, 336)).getdata()))
    assert font_colors >= 6 and circle_colors >= 4, (font_colors, circle_colors)
    report["edge_colors"] = {"text": font_colors, "circle": circle_colors}

    captures = {3, 6, 9, 12, 13, 14, 16, 17, 21, 26, 27}
    for page in range(1, 29):
        device.expect(device.command("next"), page=page % 28)
        if page in captures:
            device.capture(f"lvgl-summary-{page + 1:02}")
    memory = []
    for cycle in range(4):
        for page in range(28):
            device.command("next")
        device.capture(f"lvgl-memory-cycle-{cycle + 1}")
        state = device.status()
        device.expect(state, page=0, screen="home", test=1)
        memory.append(state["ui_mem_free"])
    assert max(memory) - min(memory) <= 256, memory
    assert min(memory) > 24 * 1024, memory
    report["free_lvgl_bytes_after_page_cycles"] = memory
    print(json.dumps({"page_cycles": "PASS", "free_lvgl_bytes": memory}), flush=True)

    device.command("enter")
    device.expect(device.command("advance 300000"), segment=1)
    device.capture("lvgl-run-start")
    # Begin the timed sample after the pending page paint has completed.
    start = device.status()
    started = time.monotonic()
    time.sleep(5)
    end = device.status()
    elapsed_ms = (time.monotonic() - started) * 1000
    timer_ms = end["total_ms"] - start["total_ms"]
    assert abs(timer_ms - elapsed_ms) < 350, (timer_ms, elapsed_ms)
    transferred = end["ui_pixels"] - start["ui_pixels"]
    full_frame_pixels = int(elapsed_ms / 100) * 468 * 468
    assert 0 < transferred < full_frame_pixels / 2, (transferred, full_frame_pixels)
    report["active_rendering"] = {
        "elapsed_ms": round(elapsed_ms), "timer_ms": timer_ms,
        "transferred_pixels": transferred,
        "previous_full_frame_equivalent": full_frame_pixels,
    }
    device.capture("lvgl-run")
    device.command("enter")
    device.capture("lvgl-paused")
    paused = device.status()
    time.sleep(1)
    after = device.status()
    assert after["total_ms"] == paused["total_ms"]
    assert after["ui_pixels"] == paused["ui_pixels"], (paused, after)
    device.command("enter")
    device.expect(device.command("advance 60000"), segment=2)
    device.capture("lvgl-completed-segments")
    device.expect(device.command("next"), segment=3, partial=1)
    device.capture("lvgl-skipped-segment")
    device.command("escape")
    device.capture("lvgl-cancel")
    device.command("escape")

    # Widest countdown uses four equally spaced digits plus the colon.
    device.command("prev")
    device.command("prev")
    device.command("enter")
    device.command("advance 300000")
    device.command("prev")
    device.capture("lvgl-long-run")
    device.command("enter")
    device.capture("lvgl-long-paused")
finally:
    try:
        if device.status()["test"]:
            device.command("test end")
        final = device.status()
        if "initial" in report:
            device.expect(final, screen="home", test=0,
                          **{key: initial[key] for key in ("logs", "cursor", "sound", "vibration", "storage")})
            report["final"] = final
        (device.output / "lvgl-check.json").write_text(json.dumps(report, indent=2) + "\n")
    finally:
        device.serial.close()
print(json.dumps({"result": "PASS", **report}), flush=True)
