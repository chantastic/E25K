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
    const bool running = v.screen == Screen::Workout || v.screen == Screen::CancelConfirm;
    _scene.background(running ? FACE : BLACK);
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
  static constexpr uint32_t BLACK = 0x000000;
  static constexpr uint32_t WHITE = 0xF4F4F2;
  static constexpr uint32_t MUTED = 0x9B9DA2;
  static constexpr uint32_t DIM = 0x53565B;
  static constexpr uint32_t PAUSED = 0x85888D;
  static constexpr uint32_t TRACK = 0x24272B;
  static constexpr uint32_t RUN = 0xFF765E;
  static constexpr uint32_t WALK = 0x52D9CB;
  // Run screen palette.
  static constexpr uint32_t FACE = 0x111214;
  static constexpr uint32_t CREAM = 0xF2EFE6;
  static constexpr uint32_t LABEL = 0x8A8C93;
  static constexpr uint32_t BLUE = 0x6B7CFF;
  static constexpr uint32_t BLUE_DIM = 0x3A4594;
  static constexpr uint32_t SEG_TRACK = 0x2A2B30;
  static constexpr uint32_t PAUSED_DONE = 0x4C5170;
  static constexpr uint32_t PAUSED_NOW = 0x33364A;
  static constexpr unsigned RING_SEGMENTS = 12;
  LvglScene _scene;
  int _cx = 234, _cy = 234;

  LvglScene& surface() { return _scene; }

  void text(const char* str, int y, const lv_font_t* font,
            uint32_t color = WHITE, int dx = 0, int letterSpace = 0) {
    _scene.text(str, _cx + dx, y, font, color, letterSpace);
  }

  static void duration(char* dst, size_t n, uint32_t seconds) {
    snprintf(dst, n, "%lu:%02lu", static_cast<unsigned long>(seconds / 60),
             static_cast<unsigned long>(seconds % 60));
  }

  static void workoutLabel(char* dst, size_t n, uint8_t index) {
    snprintf(dst, n, "W%u D%u", unsigned(index / 3 + 1), unsigned(index % 3 + 1));
  }

  static uint32_t segmentColor(SegmentType type) {
    return type == SegmentType::Run ? RUN : WALK;
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

  void smallHint(const char* hint) { text(hint, 364, &lv_font_montserrat_18, MUTED); }

  void check(int x, int y, uint32_t color, int scale = 1) {
    auto& g = surface();
    g.drawLine(x - 9 * scale, y, x - 2 * scale, y + 7 * scale, color, 3 * scale);
    g.drawLine(x - 2 * scale, y + 7 * scale, x + 12 * scale, y - 8 * scale, color, 3 * scale);
  }

  void arcDots(unsigned count, int current, uint32_t completed = 0,
               const Workout* plan = nullptr, int viewed = -1) {
    if (!count) return;
    auto& g = surface();
    // Keep large histories readable with a moving window around the current page.
    unsigned visible = count > 28 ? 21 : count;
    unsigned first = 0;
    if (count > visible && current > int(visible / 2)) {
      first = unsigned(current) - visible / 2;
      if (first + visible > count) first = count - visible;
    }
    const float span = visible > 18 ? 120.0f : (visible > 8 ? 100.0f : 58.0f);
    for (unsigned j = 0; j < visible; ++j) {
      unsigned i = first + j;
      float angle = visible > 1 ? (-span / 2 + span * j / (visible - 1)) : 0;
      float radians = angle * 0.01745329252f;
      int x = _cx + lroundf(sinf(radians) * 187);
      int y = _cy + lroundf(cosf(radians) * 187);
      const uint32_t color = plan ? segmentColor(plan->segments[i].type) : WHITE;
      const int r = count > 20 ? 4 : 5;
      const bool done = i < 32 && (completed & (uint32_t(1) << i));
      if (int(i) == current) {
        // Current takes precedence while replaying an already finished item.
        g.fillCircle(x, y, r + 2, color);
        g.fillCircle(x, y, r - 1, BLACK);
      } else if (done) {
        g.fillCircle(x, y, r, color);
      } else {
        g.drawCircle(x, y, r, plan ? color : DIM);
      }
      if (int(i) == viewed) {
        // Browsing a Home page must not make that workout look completed or
        // change which workout is next. An outer tick clears the footer text.
        for (int offset = -1; offset <= 1; ++offset) {
          const int dx = lroundf(cosf(radians) * offset);
          const int dy = -lroundf(sinf(radians) * offset);
          g.drawLine(_cx + lroundf(sinf(radians) * 198) + dx,
                     _cy + lroundf(cosf(radians) * 198) + dy,
                     _cx + lroundf(sinf(radians) * 203) + dx,
                     _cy + lroundf(cosf(radians) * 203) + dy, WHITE);
        }
      }
    }
  }

  void ring(int radius, int thickness, float progress, uint32_t color) {
    _scene.ring(_cx, _cy, radius, thickness, progress, color, TRACK);
  }

  void countdown(uint32_t seconds, uint32_t color) {
    char value[12]; duration(value, sizeof(value), seconds);
    text(value, 228, &e25k_timer_font, color);
  }

  void activitySummary(const Workout& plan) {
    const WorkoutSummary summary = summarizeWorkout(plan);
    char label[48], warmup[12], cooldown[12];
    if (summary.repeats > 1)
      snprintf(label, sizeof(label), "%u ROUNDS", unsigned(summary.repeats));
    else if (summary.patternCount == 1)
      snprintf(label, sizeof(label), "CONTINUOUS RUN");
    else
      snprintf(label, sizeof(label), "%u INTERVALS", unsigned(summary.coreCount));
    text(label, 232, &lv_font_montserrat_14, MUTED);

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
        text(tokens[i], y, font, segmentColor(segment.type), x + widths[i] / 2 - _cx);
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
    snprintf(label, sizeof(label), "%s WARM UP + %s COOL DOWN", warmup, cooldown);
    text(label, 327, &lv_font_montserrat_14, MUTED);
  }

  void home(const UiView& v) {
    text(!v.storageOk ? "STORAGE UNAVAILABLE" : v.programComplete ? "PROGRAM COMPLETE" : "COUCH TO 5K",
         v.homePage >= 27 ? 92 : 82, &lv_font_montserrat_14, MUTED);
    if (v.homePage >= 27) {
      historyIcon(174);
      text("History", 250, &lv_font_montserrat_40);
      char count[40];
      snprintf(count, sizeof(count), "%u saved %s", unsigned(v.historyCount),
               v.historyCount == 1 ? "session" : "sessions");
      text(count, 300, &lv_font_montserrat_18, MUTED);
      smallHint("BOTH TO OPEN");
    } else {
      char label[24], time[16], count[48];
      workoutLabel(label, sizeof(label), v.homePage);
      text(label, 132, &lv_font_montserrat_48);
      if (!v.programComplete && v.homePage == v.cursor) {
        surface().fillTriangle(_cx - 5, 175, _cx + 5, 175, _cx, 168, WHITE);
      }
      duration(time, sizeof(time), totalSeconds(workoutAt(v.homePage)));
      snprintf(label, sizeof(label), "%s total", time);
      text(label, 199, &lv_font_montserrat_24);
      activitySummary(workoutAt(v.homePage));
      if (v.completionCount || v.partialCount) {
        if (v.completionCount && v.partialCount) {
          snprintf(count, sizeof(count), "%u complete   ~%u partial",
                   unsigned(v.completionCount), unsigned(v.partialCount));
        } else if (v.partialCount) {
          snprintf(count, sizeof(count), "~%u partial %s", unsigned(v.partialCount),
                   v.partialCount == 1 ? "session" : "sessions");
        } else {
          snprintf(count, sizeof(count), "%u %s", unsigned(v.completionCount),
                   v.completionCount == 1 ? "completion" : "completions");
        }
        text(count, 349, &lv_font_montserrat_14, MUTED);
        if (v.lastDateText && *v.lastDateText)
          text(v.lastDateText, 367, &lv_font_montserrat_14, DIM);
      }
      text("BOTH TO START", 389, &lv_font_montserrat_18, MUTED);
    }
    arcDots(28, v.programComplete ? -1 : v.cursor, v.completedWorkouts, nullptr, v.homePage);
  }

  void workout(const UiView& v) {
    const Workout& plan = workoutAt(v.workoutIndex);
    const unsigned index = v.segmentIndex < plan.count ? v.segmentIndex : plan.count - 1;
    const SegmentType type = plan.segments[index].type;
    // Twelve fixed segments show whole-session progress; fixed topology keeps
    // the retained arcs stable across timer ticks.
    const float progress = fmaxf(0, fminf(v.sessionProgress, 1));
    constexpr int step = 360 / RING_SEGMENTS, gap = 3;
    for (unsigned i = 0; i < RING_SEGMENTS; ++i) {
      const float start = float(i) / RING_SEGMENTS, end = float(i + 1) / RING_SEGMENTS;
      uint32_t color = SEG_TRACK;
      if (progress >= end) color = v.paused ? PAUSED_DONE : BLUE;
      else if (progress > start) color = v.paused ? PAUSED_NOW : BLUE_DIM;
      _scene.arcSegment(_cx, _cy, 222, 8, int(i) * step + gap, int(i + 1) * step - gap, color);
    }
    text(v.paused ? "PAUSED" : segmentLabel(type), 146, &lv_font_montserrat_18,
         v.paused ? CREAM : LABEL, 0, 4);
    countdown(v.remainingSec, v.paused ? PAUSED : CREAM);
    // Montserrat lacks U+00B7, so the separator dot is drawn.
    char week[8], day[8];
    snprintf(week, sizeof(week), "W%u", unsigned(v.workoutIndex / 3 + 1));
    snprintf(day, sizeof(day), "D%u", unsigned(v.workoutIndex % 3 + 1));
    const int y = 318;
    const int left = LvglScene::textWidth(week, &lv_font_montserrat_18) / 2 + 16;
    const int right = LvglScene::textWidth(day, &lv_font_montserrat_18) / 2 + 16;
    text(week, y, &lv_font_montserrat_18, LABEL, -left, 2);
    text(day, y, &lv_font_montserrat_18, LABEL, right, 2);
    surface().fillCircle(_cx, y, 2, LABEL);
  }

  void cancelConfirm() {
    auto& g = surface();
    g.fillRoundRect(_cx - 163, 137, 326, 194, 22, BLACK);
    g.drawRoundRect(_cx - 163, 137, 326, 194, 22, DIM);
    text("Cancel workout?", 184, &lv_font_montserrat_24);
    text("Hold both again", 235, &lv_font_montserrat_18);
    text("to confirm", 262, &lv_font_montserrat_18);
    text("Any press to keep going", 304, &lv_font_montserrat_14, MUTED);
  }

  void metric(const char* label, uint32_t seconds, int y, int dx) {
    char time[16];
    duration(time, sizeof(time), seconds);
    const lv_font_t* font = LvglScene::textWidth(time, &lv_font_montserrat_40) > 148
      ? &lv_font_montserrat_32 : &lv_font_montserrat_40;
    text(time, y, font, WHITE, dx);
    text(label, y + 32, &lv_font_montserrat_14, MUTED, dx);
  }

  void complete(const UiView& v) {
    check(_cx, 96, WHITE, 2);
    text(v.partial ? "DONE ~" : "DONE", 162, &lv_font_montserrat_48);
    char label[16];
    workoutLabel(label, sizeof(label), v.workoutIndex);
    text(label, 225, &lv_font_montserrat_24, MUTED);
    metric("TOTAL TIME", v.elapsedSec, 284, -81);
    metric("RUN TIME", v.runSec, 284, 81);
    smallHint(v.storageOk ? "BOTH TO CONTINUE" : "LOG COULD NOT BE SAVED");
  }

  void historyIcon(int y) {
    auto& g = surface();
    g.drawCircle(_cx, y, 32, WHITE);
    g.drawCircle(_cx, y, 31, WHITE);
    g.fillRoundRect(_cx - 2, y - 20, 4, 22, 2, WHITE);
    for (int i = -1; i <= 1; ++i)
      g.drawLine(_cx, y + i, _cx + 14, y + 9 + i, WHITE);
  }

  void history(const UiView& v) {
    text("HISTORY", 89, &lv_font_montserrat_18, MUTED);
    if (!v.historyCount) {
      historyIcon(174);
      text("No runs yet", 253, &lv_font_montserrat_24);
      text("Your first one starts here.", 295, &lv_font_montserrat_18, MUTED);
      smallHint("HOLD BOTH TO GO BACK");
      return;
    }
    char label[24];
    workoutLabel(label, sizeof(label), v.workoutIndex);
    text(label, 151, &lv_font_montserrat_40);
    text(v.dateText && *v.dateText ? v.dateText : "--", 205, &lv_font_montserrat_18, MUTED);
    if (v.partial) {
      surface().drawRoundRect(_cx - 50, 230, 100, 25, 12, DIM);
      text("PARTIAL", 243, &lv_font_montserrat_14, MUTED);
    }
    metric("TOTAL TIME", v.elapsedSec, 285, -81);
    metric("RUN TIME", v.runSec, 285, 81);
    snprintf(label, sizeof(label), "%u of %u", unsigned(v.historyPage + 1), unsigned(v.historyCount));
    text(label, 362, &lv_font_montserrat_18, MUTED);
    arcDots(v.historyCount, v.historyPage);
  }

  void toggle(bool on, int y) { _scene.toggle(on, _cx, y, WHITE, TRACK, MUTED); }

  void settings(const UiView& v) {
    text("SETTINGS", 94, &lv_font_montserrat_18, MUTED);
    switch (v.settingsPage) {
      case 0:
      case 1: {
        bool on = v.settingsPage == 0 ? v.soundOn : v.vibOn;
        toggle(on, 177);
        text(v.settingsPage == 0 ? "Sound" : "Vibration", 247, &lv_font_montserrat_40);
        text(on ? "On" : "Off", 303, &lv_font_montserrat_24, MUTED);
        smallHint("BOTH TO TOGGLE");
        break;
      }
      case 2: {
        historyIcon(175);
        text("Set Time", 246, &lv_font_montserrat_40);
        char value[16];
        snprintf(value, sizeof(value), "%02u:%02u", unsigned(v.hour), unsigned(v.minute));
        text(v.rtcValid ? value : "Clock not set", 302, &lv_font_montserrat_24, MUTED);
        smallHint("BOTH TO EDIT");
        break;
      }
      case 3:
        text("Reset", 187, &lv_font_montserrat_40);
        text("Progress", 241, &lv_font_montserrat_40);
        text("Start the program over", 302, &lv_font_montserrat_18, MUTED);
        smallHint("BOTH TO OPEN");
        break;
      default: {
        text("E25K", 176, &lv_font_montserrat_48);
        text("Version 1.1 / LVGL", 239, &lv_font_montserrat_18, MUTED);
        text(__DATE__, 269, &lv_font_montserrat_18, MUTED);
        char label[32];
        if (v.battery < 0) snprintf(label, sizeof(label), "Battery --");
        else snprintf(label, sizeof(label), "Battery %d%%", v.battery);
        text(label, 316, &lv_font_montserrat_18, MUTED);
        smallHint("HOLD BOTH TO GO HOME");
        break;
      }
    }
    arcDots(5, v.settingsPage);
  }

  void setTime(const UiView& v) {
    text("SET TIME", 94, &lv_font_montserrat_18, MUTED);
    text(v.timeField == 0 ? "Hour" : v.timeField == 1 ? "Minute" : "Save this time?",
         152, &lv_font_montserrat_24);
    char hours[4], minutes[4];
    snprintf(hours, sizeof(hours), "%02u", unsigned(v.hour));
    snprintf(minutes, sizeof(minutes), "%02u", unsigned(v.minute));
    text(hours, 232, &lv_font_montserrat_48, v.timeField == 1 ? DIM : WHITE, -76);
    text(":", 228, &lv_font_montserrat_48, MUTED);
    text(minutes, 232, &lv_font_montserrat_48, v.timeField == 0 ? DIM : WHITE, 76);
    if (v.timeField < 2) {
      int x = _cx + (v.timeField == 0 ? -76 : 76);
      surface().fillRoundRect(x - 43, 278, 86, 3, 1, WHITE);
      text("YELLOW -     BLUE +", 313, &lv_font_montserrat_18, MUTED);
    } else if (v.dateText && *v.dateText) {
      text(v.dateText, 313, &lv_font_montserrat_18, MUTED);
    }
    smallHint(v.timeField < 2 ? "BOTH FOR NEXT" : "BOTH TO SAVE");
    arcDots(3, v.timeField);
  }

  void resetConfirm(const UiView& v) {
    text("RESET PROGRESS", 102, &lv_font_montserrat_18, MUTED);
    text("Start over?", 176, &lv_font_montserrat_40);
    char label[48];
    snprintf(label, sizeof(label), "Clear %u saved %s", unsigned(v.historyCount),
             v.historyCount == 1 ? "session" : "sessions");
    text(label, 241, &lv_font_montserrat_18);
    text("Return to W1 D1", 270, &lv_font_montserrat_18);
    text("Hold both to cancel", 310, &lv_font_montserrat_18, MUTED);
    text("PRESS BOTH + RELEASE", 354, &lv_font_montserrat_18);
    text("TO RESET", 380, &lv_font_montserrat_14, MUTED);
  }

  void resetResult(const UiView& v) {
    text("RESET PROGRESS", 94, &lv_font_montserrat_18, MUTED);
    if (v.resetSucceeded) {
      check(_cx, 157, WALK, 2);
      text("History cleared", 230, &lv_font_montserrat_24);
      text("W1 D1 is next", 276, &lv_font_montserrat_24);
      text("Sound and vibration kept", 318, &lv_font_montserrat_14, MUTED);
      smallHint("BOTH TO GO HOME");
    } else {
      text("Reset not confirmed", 181, &lv_font_montserrat_24);
      text("Could not verify the save.", 239, &lv_font_montserrat_18);
      text("Please try again.", 275, &lv_font_montserrat_24);
      text("Hold both to go back", 321, &lv_font_montserrat_18, MUTED);
      smallHint("BOTH TO RETRY");
    }
  }
};

}  // namespace c25k
