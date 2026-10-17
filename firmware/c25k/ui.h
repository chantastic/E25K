#pragma once

#include "lvgl_scene.h"

LV_FONT_DECLARE(e25k_timer_font);
#include <math.h>
#include "workouts.h"
#include "workout_summary.h"

namespace c25k {

enum class Screen : uint8_t {
  Home, Workout, CancelConfirm, Complete, History, Settings, SetTime, ResetConfirm, ResetResult
};

// A frame is a snapshot: drawing never changes the timer or persistent state.
struct UiView {
  Screen screen = Screen::Home;
  uint8_t homePage = 0, cursor = 0, settingsPage = 0, timeField = 0;
  uint8_t hour = 0, minute = 0;
  bool programComplete = false, soundOn = true, vibOn = true;
  bool partial = false, paused = false, rtcValid = true, storageOk = true;
  bool resetSucceeded = false;
  uint8_t workoutIndex = 0, segmentIndex = 0, segmentCount = 0;
  uint32_t remainingSec = 0, elapsedSec = 0, runSec = 0, plannedSec = 0;
  uint32_t completedWorkouts = 0, completedSegments = 0;
  float segmentProgress = 0, sessionProgress = 0;
  uint16_t historyCount = 0, historyPage = 0, completionCount = 0, partialCount = 0;
  int battery = -1;
  const char* dateText = "";
  const char* lastDateText = "";
};

class Ui {
 public:
  bool begin() {
    _cx = M5.Display.width() / 2;
    _cy = M5.Display.height() / 2;
    return _scene.begin();
  }

  void draw(const UiView& v) {
    if (!_scene.ready()) return;
    // Only structural changes rebuild the tree. Timer ticks update retained
    // labels/arcs; they do not allocate a new screen or redraw every pixel.
    const uint32_t layout = uint32_t(v.screen) | uint32_t(v.homePage) << 4 |
      uint32_t(v.settingsPage) << 9 | uint32_t(v.timeField) << 12 |
      uint32_t(v.historyPage) << 14 | uint32_t(v.segmentIndex) << 22 |
      uint32_t(v.resetSucceeded) << 27 | uint32_t(v.historyCount == 0) << 28 |
      uint32_t(v.homePage == v.cursor) << 29 | uint32_t(v.programComplete) << 30;
    _scene.beginFrame(layout);
    _scene.background(BLACK);
    switch (v.screen) {
      case Screen::Home: home(v); break;
      case Screen::Workout: workout(v); break;
      case Screen::CancelConfirm: workout(v); cancelConfirm(); break;
      case Screen::Complete: complete(v); break;
      case Screen::History: history(v); break;
      case Screen::Settings: settings(v); break;
      case Screen::SetTime: setTime(v); break;
      case Screen::ResetConfirm: resetConfirm(v); break;
      case Screen::ResetResult: resetResult(v); break;
    }
    _scene.endFrame();
  }

  void push() { _scene.refresh(); }
  void service() { _scene.service(); }
  M5Canvas& canvas() { return _scene.canvas(); }
  bool buffered() const { return _scene.ready(); }
  uint32_t flushes() const { return _scene.flushes(); }
  uint64_t pixels() const { return _scene.pixels(); }
  size_t freeBytes() const { return _scene.freeBytes(); }

 private:
  // One palette for every screen: true black, off-white numerals, muted
  // letter-spaced labels and periwinkle accents (§9).
  static constexpr uint32_t BLACK = 0x000000;
  static constexpr uint32_t CREAM = 0xF2EFE6;
  static constexpr uint32_t LABEL = 0x8A8C93;
  static constexpr uint32_t DIM = 0x4A4C52;
  static constexpr uint32_t PAUSED = 0x85888D;
  static constexpr uint32_t HALO = 0x1C1B19;
  static constexpr uint32_t HALO_PAUSED = 0x111112;
  static constexpr uint32_t BLUE = 0x6B7CFF;
  static constexpr uint32_t BLUE_DIM = 0x3A4594;
  static constexpr uint32_t SEG_TRACK = 0x2A2B30;
  static constexpr uint32_t PAUSED_DONE = 0x4C5170;
  static constexpr uint32_t PAUSED_NOW = 0x33364A;
  static constexpr int RING_R = 222, RING_W = 8, PAGE_W = 6;
  LvglScene _scene;
  int _cx = 234, _cy = 234;

  LvglScene& surface() { return _scene; }

  void text(const char* str, int y, const lv_font_t* font,
            uint32_t color = CREAM, int dx = 0, int letterSpace = 0) {
    _scene.text(str, _cx + dx, y, font, color, letterSpace);
  }

  // Small uppercase, letter-spaced caption.
  void label(const char* str, int y, uint32_t color = LABEL, int space = 3,
             const lv_font_t* font = &lv_font_montserrat_14, int dx = 0) {
    text(str, y, font, color, dx, space);
  }

  void hint(const char* str) { label(str, 372, BLUE); }

  static void duration(char* dst, size_t n, uint32_t seconds) {
    snprintf(dst, n, "%lu:%02lu", static_cast<unsigned long>(seconds / 60),
             static_cast<unsigned long>(seconds % 60));
  }

  static void workoutLabel(char* dst, size_t n, uint8_t index) {
    snprintf(dst, n, "W%u D%u", unsigned(index / 3 + 1), unsigned(index % 3 + 1));
  }

  // `W3 · D2`. Montserrat lacks U+00B7, so the separator dot is drawn.
  void weekDay(uint8_t index, int y, const lv_font_t* font, uint32_t color,
               int space, int gap, int dot) {
    char week[8], day[8];
    snprintf(week, sizeof(week), "W%u", unsigned(index / 3 + 1));
    snprintf(day, sizeof(day), "D%u", unsigned(index % 3 + 1));
    const int left = (LvglScene::textWidth(week, font) + space * 2) / 2 + gap;
    const int right = (LvglScene::textWidth(day, font) + space * 2) / 2 + gap;
    text(week, y, font, color, -left, space);
    text(day, y, font, color, right, space);
    surface().fillCircle(_cx, y, dot, color);
  }

  static const char* segmentLabel(SegmentType type) {
    switch (type) {
      case SegmentType::Warmup: return "WARM UP";
      case SegmentType::Run: return "RUN";
      case SegmentType::Walk: return "WALK";
      case SegmentType::Cooldown: return "COOL DOWN";
    }
    return "";
  }

  void check(int x, int y, uint32_t color, int scale = 1) {
    auto& g = surface();
    g.drawLine(x - 9 * scale, y, x - 2 * scale, y + 7 * scale, color, 3 * scale);
    g.drawLine(x - 2 * scale, y + 7 * scale, x + 12 * scale, y - 8 * scale, color, 3 * scale);
  }

  void ringArc(int start, int end, uint32_t color, int width = RING_W) {
    _scene.arc(_cx, _cy, RING_R, width, start, end, color);
  }

  // One segment per interval, arc length proportional to its planned share
  // of the workout, separated by constant gaps. `progress >= 0` fills the
  // current segment over a dim track; completed/remaining use `done`.
  void intervalRing(const Workout& plan, int current, float progress, uint32_t done,
                    uint32_t now, uint32_t nowTrack, uint32_t completedMask, bool byMask) {
    constexpr int gap = 4;
    const uint32_t total = totalSeconds(plan);
    if (!total || !plan.count) return;
    const int avail = 360 - plan.count * gap;
    uint32_t elapsed = 0;
    for (unsigned i = 0; i < plan.count; ++i) {
      const int start = gap / 2 + i * gap + int((uint64_t(elapsed) * avail + total / 2) / total);
      elapsed += plan.segments[i].seconds;
      const int end = gap / 2 + i * gap + int((uint64_t(elapsed) * avail + total / 2) / total);
      if (int(i) == current) {
        _scene.arcProgress(_cx, _cy, RING_R, RING_W, start, end, progress, now, nowTrack);
      } else {
        const bool complete = byMask ? (i < 32 && (completedMask & (uint32_t(1) << i)))
                                     : int(i) < current;
        ringArc(start, end, complete ? done : SEG_TRACK);
      }
    }
  }

  // Page indicator: short segments along the bottom of the ring. Long
  // histories use a moving window around the current page.
  void pageArcs(unsigned count, unsigned current) {
    if (!count) return;
    const unsigned visible = count > 28 ? 21 : count;
    unsigned first = 0;
    if (count > visible && current > visible / 2) {
      first = current - visible / 2;
      if (first + visible > count) first = count - visible;
    }
    const int pitch = visible <= 6 ? 12 : (126 / int(visible) < 12 ? 126 / int(visible) : 12);
    const int length = pitch - 4 > 2 ? pitch - 4 : 2;
    const int span = int(visible) * pitch - (pitch - length);
    for (unsigned j = 0; j < visible; ++j) {
      // 180° is 6 o'clock; the first page sits at the reader's left.
      const int start = 180 + span / 2 - int(j) * pitch - length;
      ringArc(start, start + length, first + j == current ? BLUE : SEG_TRACK, PAGE_W);
    }
  }

  // Home: 27 workout segments with a wider gap at 12 o'clock where the
  // History page sits between W9 D3 and W1 D1.
  void programRing(const UiView& v) {
    constexpr int pitch = 13, length = 9, offset = 6;
    const int next = v.programComplete ? -1 : v.cursor;
    for (unsigned i = 0; i < kWorkoutCount; ++i) {
      const int start = offset + int(i) * pitch;
      const bool done = v.completedWorkouts & (uint32_t(1) << i);
      // Next-up takes precedence over completed, including replay.
      ringArc(start, start + length, int(i) == next ? BLUE_DIM : done ? BLUE : SEG_TRACK);
    }
    // The viewed page gets a small inner dot; browsing never recolors segments.
    const float angle = v.homePage >= kWorkoutCount ? 0
      : (offset + v.homePage * pitch + length / 2.0f);
    const float radians = angle * 0.01745329252f;
    surface().fillCircle(_cx + lroundf(sinf(radians) * 204),
                         _cy - lroundf(cosf(radians) * 204), 3, CREAM);
  }

  void countdown(uint32_t seconds, bool paused) {
    char value[12]; duration(value, sizeof(value), seconds);
    // Halo: four dim copies one step out, drawn behind the numerals. They
    // share the countdown's invalidated area, so the per-second cost stays
    // a few extra glyph blends rather than a blur.
    const uint32_t halo = paused ? HALO_PAUSED : HALO;
    static constexpr int8_t offsets[4][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
    for (const auto& o : offsets) _scene.text(value, _cx + o[0], 228 + o[1], &e25k_timer_font, halo, 0);
    text(value, 228, &e25k_timer_font, paused ? PAUSED : CREAM);
  }

  void activitySummary(const Workout& plan) {
    const WorkoutSummary summary = summarizeWorkout(plan);
    char caption[48], warmup[12], cooldown[12];
    if (summary.repeats > 1)
      snprintf(caption, sizeof(caption), "%u ROUNDS", unsigned(summary.repeats));
    else if (summary.patternCount == 1)
      snprintf(caption, sizeof(caption), "CONTINUOUS RUN");
    else
      snprintf(caption, sizeof(caption), "%u INTERVALS", unsigned(summary.coreCount));
    label(caption, 232);

    // Two large activities for a repeated pair; longer sequences flow across
    // at most three rows. Every token comes from the same table as the timer.
    const unsigned perRow = summary.patternCount <= 4 ? 2 : 3;
    const unsigned rows = (summary.patternCount + perRow - 1) / perRow;
    const lv_font_t* font = summary.patternCount <= 2 ? &lv_font_montserrat_24 : &lv_font_montserrat_18;
    auto& g = surface();
    for (unsigned row = 0; row < rows; ++row) {
      char tokens[3][24]{};
      int widths[3]{}, width = 0;
      unsigned first = row * perRow;
      unsigned count = summary.patternCount - first;
      if (count > perRow) count = perRow;
      for (unsigned i = 0; i < count; ++i) {
        const Segment& segment = plan.segments[summary.firstCore + first + i];
        char time[12]; duration(time, sizeof(time), segment.seconds);
        snprintf(tokens[i], sizeof(tokens[i]), "%s %s",
                 segment.type == SegmentType::Run ? "RUN" : "WALK", time);
        widths[i] = LvglScene::textWidth(tokens[i], font); width += widths[i];
      }
      constexpr int gap = 18;
      width += (count - 1) * gap;
      int x = _cx - width / 2;
      int y = rows == 1 ? 276 : rows == 2 ? 261 + row * 30 : 251 + row * 26;
      for (unsigned i = 0; i < count; ++i) {
        const Segment& segment = plan.segments[summary.firstCore + first + i];
        // Runs carry the accent; walks stay off-white.
        text(tokens[i], y, font, segment.type == SegmentType::Run ? BLUE : CREAM,
             x + widths[i] / 2 - _cx);
        x += widths[i];
        if (i + 1 < count) {
          g.drawLine(x + 6, y - 3, x + 10, y, DIM);
          g.drawLine(x + 10, y, x + 6, y + 3, DIM);
        }
        x += gap;
      }
    }
    duration(warmup, sizeof(warmup), summary.warmupSeconds);
    duration(cooldown, sizeof(cooldown), summary.cooldownSeconds);
    snprintf(caption, sizeof(caption), "%s WARM UP + %s COOL DOWN", warmup, cooldown);
    label(caption, 327, LABEL, 1);
  }

  void home(const UiView& v) {
    programRing(v);
    label(!v.storageOk ? "STORAGE UNAVAILABLE" : v.programComplete ? "PROGRAM COMPLETE" : "COUCH TO 5K",
          v.homePage >= 27 ? 92 : 84, v.storageOk ? LABEL : CREAM);
    if (v.homePage >= 27) {
      clockIcon(174);
      text("History", 250, &lv_font_montserrat_40);
      char count[40];
      snprintf(count, sizeof(count), "%u SAVED %s", unsigned(v.historyCount),
               v.historyCount == 1 ? "SESSION" : "SESSIONS");
      label(count, 298);
      hint("BOTH TO OPEN");
      return;
    }
    weekDay(v.homePage, 134, &lv_font_montserrat_48, CREAM, 0, 18, 4);
    if (!v.programComplete && v.homePage == v.cursor)
      surface().fillTriangle(_cx - 5, 175, _cx + 5, 175, _cx, 168, BLUE);
    char line[48], time[16];
    duration(time, sizeof(time), totalSeconds(workoutAt(v.homePage)));
    snprintf(line, sizeof(line), "%s TOTAL", time);
    text(line, 199, &lv_font_montserrat_18, CREAM, 0, 2);
    activitySummary(workoutAt(v.homePage));
    if (v.completionCount || v.partialCount) {
      if (v.completionCount && v.partialCount) {
        snprintf(line, sizeof(line), "%u COMPLETE   ~%u PARTIAL",
                 unsigned(v.completionCount), unsigned(v.partialCount));
      } else if (v.partialCount) {
        snprintf(line, sizeof(line), "~%u PARTIAL %s", unsigned(v.partialCount),
                 v.partialCount == 1 ? "SESSION" : "SESSIONS");
      } else {
        snprintf(line, sizeof(line), "%u %s", unsigned(v.completionCount),
                 v.completionCount == 1 ? "COMPLETION" : "COMPLETIONS");
      }
      label(line, 349, LABEL, 1);
      if (v.lastDateText && *v.lastDateText) label(v.lastDateText, 367, DIM, 1);
    }
    label("BOTH TO START", 391, BLUE);
  }

  void workout(const UiView& v) {
    const Workout& plan = workoutAt(v.workoutIndex);
    const unsigned index = v.segmentIndex < plan.count ? v.segmentIndex : plan.count - 1;
    const SegmentType type = plan.segments[index].type;
    // Ring topology depends only on the workout, so ticks just move the
    // current segment's fill.
    intervalRing(plan, int(index), v.segmentProgress,
                 v.paused ? PAUSED_DONE : BLUE, v.paused ? PAUSED_DONE : BLUE,
                 v.paused ? PAUSED_NOW : BLUE_DIM, 0, false);
    text(v.paused ? "PAUSED" : segmentLabel(type), 146, &lv_font_montserrat_18,
         v.paused ? CREAM : LABEL, 0, 4);
    countdown(v.remainingSec, v.paused);
    weekDay(v.workoutIndex, 318, &lv_font_montserrat_18, LABEL, 2, 16, 2);
    if (v.paused) hint("BOTH TO RESUME");
  }

  void cancelConfirm() {
    auto& g = surface();
    g.fillRoundRect(_cx - 163, 137, 326, 194, 22, BLACK);
    g.drawRoundRect(_cx - 163, 137, 326, 194, 22, SEG_TRACK);
    text("Cancel workout?", 184, &lv_font_montserrat_24);
    label("HOLD BOTH AGAIN", 231, BLUE);
    label("TO CONFIRM", 253, BLUE);
    label("ANY PRESS TO KEEP GOING", 298, LABEL, 2, &lv_font_montserrat_12);
  }

  void metric(const char* caption, uint32_t seconds, int y, int dx) {
    char time[16];
    duration(time, sizeof(time), seconds);
    const lv_font_t* font = LvglScene::textWidth(time, &lv_font_montserrat_40) > 148
      ? &lv_font_montserrat_32 : &lv_font_montserrat_40;
    text(time, y, font, CREAM, dx);
    label(caption, y + 34, LABEL, 2, &lv_font_montserrat_14, dx);
  }

  void complete(const UiView& v) {
    // Finished segments in accent; skipped ones stay grey.
    intervalRing(workoutAt(v.workoutIndex), -1, 0, BLUE, BLUE, BLUE_DIM,
                 v.completedSegments, true);
    check(_cx, 100, BLUE, 2);
    text(v.partial ? "DONE ~" : "DONE", 164, &lv_font_montserrat_48);
    weekDay(v.workoutIndex, 214, &lv_font_montserrat_14, LABEL, 2, 12, 2);
    metric("TOTAL TIME", v.elapsedSec, 276, -88);
    metric("RUN TIME", v.runSec, 276, 88);
    if (v.storageOk) hint("BOTH TO CONTINUE");
    else label("LOG COULD NOT BE SAVED", 372, CREAM, 2);
  }

  void clockIcon(int y) {
    auto& g = surface();
    g.drawCircle(_cx, y, 32, LABEL);
    g.drawCircle(_cx, y, 31, LABEL);
    g.fillRoundRect(_cx - 2, y - 20, 4, 22, 2, BLUE);
    for (int i = -1; i <= 1; ++i)
      g.drawLine(_cx, y + i, _cx + 14, y + 9 + i, BLUE);
  }

  void history(const UiView& v) {
    label("HISTORY", 88);
    if (!v.historyCount) {
      clockIcon(174);
      text("No runs yet", 250, &lv_font_montserrat_24);
      text("Your first one starts here.", 290, &lv_font_montserrat_18, LABEL);
      hint("HOLD BOTH TO GO BACK");
      return;
    }
    weekDay(v.workoutIndex, 148, &lv_font_montserrat_40, CREAM, 0, 16, 4);
    label(v.dateText && *v.dateText ? v.dateText : "--", 196, LABEL, 1);
    if (v.partial) {
      surface().drawRoundRect(_cx - 50, 222, 100, 24, 12, SEG_TRACK);
      label("PARTIAL", 234, LABEL, 2, &lv_font_montserrat_12);
    }
    metric("TOTAL TIME", v.elapsedSec, 284, -88);
    metric("RUN TIME", v.runSec, 284, 88);
    char page[24];
    snprintf(page, sizeof(page), "%u OF %u", unsigned(v.historyPage + 1), unsigned(v.historyCount));
    label(page, 368);
    pageArcs(v.historyCount, v.historyPage);
  }

  void toggle(bool on, int y) { _scene.toggle(on, _cx, y, BLUE, SEG_TRACK, LABEL, CREAM); }

  void settings(const UiView& v) {
    label("SETTINGS", 92);
    switch (v.settingsPage) {
      case 0:
      case 1: {
        bool on = v.settingsPage == 0 ? v.soundOn : v.vibOn;
        toggle(on, 176);
        text(v.settingsPage == 0 ? "Sound" : "Vibration", 246, &lv_font_montserrat_40);
        text(on ? "ON" : "OFF", 298, &lv_font_montserrat_18, on ? BLUE : LABEL, 0, 4);
        hint("BOTH TO TOGGLE");
        break;
      }
      case 2: {
        clockIcon(176);
        text("Set Time", 246, &lv_font_montserrat_40);
        char value[16];
        snprintf(value, sizeof(value), "%02u:%02u", unsigned(v.hour), unsigned(v.minute));
        if (v.rtcValid) text(value, 298, &lv_font_montserrat_24, CREAM, 0, 2);
        else label("CLOCK NOT SET", 298);
        hint("BOTH TO EDIT");
        break;
      }
      case 3:
        text("Reset", 186, &lv_font_montserrat_40);
        text("Progress", 240, &lv_font_montserrat_40);
        label("START THE PROGRAM OVER", 298, LABEL, 2);
        hint("BOTH TO OPEN");
        break;
      default: {
        text("E25K", 176, &lv_font_montserrat_48);
        label("VERSION 1.1 / LVGL", 236, LABEL, 2);
        label(__DATE__, 262, LABEL, 2);
        char battery[32];
        if (v.battery < 0) snprintf(battery, sizeof(battery), "BATTERY --");
        else snprintf(battery, sizeof(battery), "BATTERY %d%%", v.battery);
        label(battery, 304, CREAM, 2);
        hint("HOLD BOTH TO GO HOME");
        break;
      }
    }
    pageArcs(5, v.settingsPage);
  }

  void setTime(const UiView& v) {
    label("SET TIME", 92);
    text(v.timeField == 0 ? "HOUR" : v.timeField == 1 ? "MINUTE" : "SAVE THIS TIME?",
         132, &lv_font_montserrat_18, CREAM, 0, 3);
    char hours[4], minutes[4];
    snprintf(hours, sizeof(hours), "%02u", unsigned(v.hour));
    snprintf(minutes, sizeof(minutes), "%02u", unsigned(v.minute));
    // Same tabular numerals as the countdown; the field not being edited dims.
    text(hours, 234, &e25k_timer_font, v.timeField == 1 ? DIM : CREAM, -103);
    text(":", 234, &e25k_timer_font, LABEL);
    text(minutes, 234, &e25k_timer_font, v.timeField == 0 ? DIM : CREAM, 103);
    if (v.timeField < 2) {
      const int x = _cx + (v.timeField == 0 ? -103 : 103);
      surface().fillRoundRect(x - 56, 304, 112, 3, 1, BLUE);
      label("YELLOW -     BLUE +", 336, LABEL, 2);
    } else if (v.dateText && *v.dateText) {
      label(v.dateText, 336, LABEL, 1);
    }
    hint(v.timeField < 2 ? "BOTH FOR NEXT" : "BOTH TO SAVE");
    pageArcs(3, v.timeField);
  }

  void resetConfirm(const UiView& v) {
    label("RESET PROGRESS", 96);
    text("Start over?", 168, &lv_font_montserrat_40);
    char line[48];
    snprintf(line, sizeof(line), "Clear %u saved %s", unsigned(v.historyCount),
             v.historyCount == 1 ? "session" : "sessions");
    text(line, 226, &lv_font_montserrat_18);
    text("Return to W1 D1", 254, &lv_font_montserrat_18);
    label("HOLD BOTH TO CANCEL", 298, LABEL, 2);
    label("PRESS BOTH + RELEASE", 346, BLUE);
    label("TO RESET", 370, BLUE);
  }

  void resetResult(const UiView& v) {
    label("RESET PROGRESS", 94);
    if (v.resetSucceeded) {
      check(_cx, 160, BLUE, 2);
      text("History cleared", 228, &lv_font_montserrat_24);
      text("W1 D1 is next", 268, &lv_font_montserrat_24);
      label("SOUND AND VIBRATION KEPT", 314, LABEL, 2, &lv_font_montserrat_12);
      hint("BOTH TO GO HOME");
    } else {
      text("Reset not confirmed", 180, &lv_font_montserrat_24);
      text("Could not verify the save.", 232, &lv_font_montserrat_18, LABEL);
      text("Please try again.", 262, &lv_font_montserrat_18);
      label("HOLD BOTH TO GO BACK", 312, LABEL, 2);
      hint("BOTH TO RETRY");
    }
  }
};

}  // namespace c25k
