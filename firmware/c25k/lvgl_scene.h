#pragma once

#include <M5Unified.h>
#include <lvgl.h>
#include <math.h>
#include <string.h>

namespace c25k {

// Retained LVGL widgets for a snapshot-driven UI. A page creates its objects
// once; subsequent snapshots update the same labels, arcs and shapes. LVGL
// owns invalidation, anti-aliasing and drawing, not a full-screen paint loop.
class LvglScene {
 public:
  bool begin() {
    _canvas.setColorDepth(16);
    _canvas.setPsram(true);
    if (!_canvas.createSprite(M5.Display.width(), M5.Display.height())) return false;
    _canvas.fillScreen(0);
    _buffer = heap_caps_malloc(M5.Display.width() * 24 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!_buffer) { _canvas.deleteSprite(); return false; }
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    lv_log_register_print_cb([](lv_log_level_t, const char* message) { Serial.print(message); });
    _display = lv_display_create(M5.Display.width(), M5.Display.height());
    if (!_display) { free(_buffer); _buffer = nullptr; _canvas.deleteSprite(); return false; }
    lv_display_set_color_format(_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(_display, _buffer, nullptr, M5.Display.width() * 24 * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(_display, this);
    lv_display_set_flush_cb(_display, flush);
    _root = lv_display_get_screen_active(_display);
    plain(_root);
    lv_obj_set_style_bg_color(_root, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
    return true;
  }

  void beginFrame(uint32_t layout) {
    if (layout != _layout) {
      lv_obj_clean(_root);
      memset(_labels, 0, sizeof(_labels)); memset(_labelState, 0, sizeof(_labelState));
      memset(_boxes, 0, sizeof(_boxes)); memset(_boxState, 0, sizeof(_boxState));
      memset(_lines, 0, sizeof(_lines)); memset(_triangles, 0, sizeof(_triangles));
      memset(_arcs, 0, sizeof(_arcs)); _switch = nullptr;
      _layout = layout;
    }
    _label = _box = _line = _triangle = _arc = 0;
  }

  void endFrame() {
    hideRemainder(_labels, _label); hideRemainder(_boxes, _box);
    for (unsigned i = _arc; i < kArcs; ++i)
      if (_arcs[i].obj) lv_obj_add_flag(_arcs[i].obj, LV_OBJ_FLAG_HIDDEN);
    // Line/triangle topology stays fixed for a layout. Changes to page,
    // segment, time field or result rebuild the scene through beginFrame().
  }
  void refresh() { if (_display) lv_refr_now(_display); }
  void service() { if (_display) lv_timer_handler(); }
  bool ready() const { return _display != nullptr; }
  M5Canvas& canvas() { return _canvas; }
  uint32_t flushes() const { return _flushes; }
  uint64_t pixels() const { return _pixels; }
  size_t freeBytes() const {
    lv_mem_monitor_t memory{};
    if (_display) lv_mem_monitor(&memory);
    return memory.free_size;
  }

  static int textWidth(const char* value, const lv_font_t* font) {
    lv_point_t size;
    lv_text_get_size(&size, value, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
  }

  void background(uint32_t color) {
    if (color == _background) return;
    _background = color;
    lv_obj_set_style_bg_color(_root, lv_color_hex(color), 0);
  }

  void text(const char* value, int cx, int cy, const lv_font_t* font, uint32_t color,
            int letterSpace = 0) {
    LV_ASSERT(_label < 32);
    auto& cached = _labelState[_label];
    auto*& obj = _labels[_label++];
    const bool created = !obj;
    if (created) {
      obj = lv_label_create(_root); plain(obj);
      lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
      lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    }
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (created || cached.font != font) lv_obj_set_style_text_font(obj, font, 0);
    if (created || cached.space != letterSpace)
      lv_obj_set_style_text_letter_space(obj, letterSpace, 0);
    if (created || cached.color != color) lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    if (strcmp(lv_label_get_text(obj), value ? value : "")) lv_label_set_text(obj, value ? value : "");
    if (created || cached.x != cx || cached.y != cy || cached.font != font ||
        cached.space != letterSpace)
      lv_obj_align(obj, LV_ALIGN_TOP_MID, cx - M5.Display.width() / 2, cy - font->line_height / 2);
    cached = {font,cx,cy,color,letterSpace};
  }

  void fillRect(int x,int y,int w,int h,uint32_t color) { box(x,y,w,h,0,color,0,true); }
  void fillRoundRect(int x,int y,int w,int h,int r,uint32_t color) { box(x,y,w,h,r,color,0,true); }
  void drawRoundRect(int x,int y,int w,int h,int r,uint32_t color) { box(x,y,w,h,r,color,1,false); }
  void fillCircle(int x,int y,int r,uint32_t color) { box(x-r,y-r,2*r+1,2*r+1,LV_RADIUS_CIRCLE,color,0,true); }
  void drawCircle(int x,int y,int r,uint32_t color) { box(x-r,y-r,2*r+1,2*r+1,LV_RADIUS_CIRCLE,color,1,false); }

  void drawLine(int x1,int y1,int x2,int y2,uint32_t color,int width=1) {
    LV_ASSERT(_line < 32);
    auto& line = _lines[_line++];
    const bool created = !line.obj;
    if (created) { line.obj = lv_line_create(_root); plain(line.obj); }
    lv_point_precise_t points[2] = {{x1,y1},{x2,y2}};
    if (created || memcmp(points, line.points, sizeof(points))) {
      memcpy(line.points, points, sizeof(points));
      lv_line_set_points(line.obj, line.points, 2);
    }
    if (created || line.width != width) lv_obj_set_style_line_width(line.obj,width,0);
    if (created) lv_obj_set_style_line_rounded(line.obj,true,0);
    if (created || line.color != color) lv_obj_set_style_line_color(line.obj,lv_color_hex(color),0);
    line.width = width; line.color = color;
  }

  void fillTriangle(int x1,int y1,int x2,int y2,int x3,int y3,uint32_t color) {
    LV_ASSERT(_triangle < 4);
    auto& triangle = _triangles[_triangle++];
    if (!triangle.obj) {
      triangle.obj = lv_obj_create(_root); plain(triangle.obj);
      lv_obj_add_event_cb(triangle.obj,drawTriangle,LV_EVENT_DRAW_MAIN,&triangle);
    }
    int left = min(x1,min(x2,x3)), top = min(y1,min(y2,y3));
    lv_point_precise_t points[3]={{x1-left,y1-top},{x2-left,y2-top},{x3-left,y3-top}};
    if (memcmp(points,triangle.points,sizeof(points)) || color != triangle.color) {
      memcpy(triangle.points,points,sizeof(points)); triangle.color=color;
      lv_obj_invalidate(triangle.obj);
    }
    lv_obj_set_pos(triangle.obj,left,top);
    lv_obj_set_size(triangle.obj,max(x1,max(x2,x3))-left+1,max(y1,max(y2,y3))-top+1);
  }

  // A fixed ring segment, angles in degrees clockwise from 12 o'clock.
  void arc(int cx,int cy,int radius,int thickness,int start,int end,uint32_t color) {
    arcShape(cx,cy,radius,thickness,start,end,-1,color,color);
  }

  // A ring segment whose track fills clockwise with progress (0..1).
  void arcProgress(int cx,int cy,int radius,int thickness,int start,int end,float progress,
                   uint32_t fill,uint32_t track) {
    arcShape(cx,cy,radius,thickness,start,end,
             int(lroundf(fmaxf(0,fminf(progress,1))*1000)),fill,track);
  }

  void toggle(bool on,int cx,int cy,uint32_t foreground,uint32_t track,uint32_t muted,
              uint32_t knob) {
    if(!_switch) {
      _switch=lv_switch_create(_root); plain(_switch);
      lv_obj_set_size(_switch,104,50);
      lv_obj_set_style_radius(_switch,LV_RADIUS_CIRCLE,LV_PART_MAIN);
      lv_obj_set_style_radius(_switch,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
      lv_obj_set_style_radius(_switch,LV_RADIUS_CIRCLE,LV_PART_KNOB);
      lv_obj_set_style_bg_opa(_switch,LV_OPA_COVER,LV_PART_MAIN);
      lv_obj_set_style_bg_opa(_switch,LV_OPA_COVER,static_cast<lv_style_selector_t>(LV_PART_INDICATOR)|LV_STATE_CHECKED);
      lv_obj_set_style_bg_opa(_switch,LV_OPA_COVER,LV_PART_KNOB);
      lv_obj_set_style_bg_color(_switch,lv_color_hex(track),LV_PART_MAIN);
      lv_obj_set_style_bg_color(_switch,lv_color_hex(foreground),static_cast<lv_style_selector_t>(LV_PART_INDICATOR)|LV_STATE_CHECKED);
      lv_obj_set_style_bg_color(_switch,lv_color_hex(muted),LV_PART_KNOB);
      lv_obj_set_style_bg_color(_switch,lv_color_hex(knob),static_cast<lv_style_selector_t>(LV_PART_KNOB)|LV_STATE_CHECKED);
      lv_obj_set_style_pad_all(_switch,-7,LV_PART_KNOB);
      lv_obj_set_style_anim_duration(_switch,120,0);
      lv_obj_set_pos(_switch,cx-52,cy-25);
    }
    if(on) lv_obj_add_state(_switch,LV_STATE_CHECKED);
    else lv_obj_remove_state(_switch,LV_STATE_CHECKED);
  }

 private:
  struct LabelState { const lv_font_t* font; int x,y; uint32_t color; int space; };
  struct BoxState { int x,y,w,h,radius; uint32_t color; int border,fill; };
  struct Line { lv_obj_t* obj; lv_point_precise_t points[2]; int width; uint32_t color; };
  struct Triangle { lv_obj_t* obj; lv_point_precise_t points[3]; uint32_t color; };
  // Custom-drawn arc sized to its own bounds, so timer-label invalidations do
  // not make every ring segment run the arc mask over the screen center.
  struct Arc { lv_obj_t* obj; int cx,cy,radius,width,start,end,value; uint32_t fill,track; };
  M5Canvas _canvas{&M5.Display};
  void* _buffer=nullptr;
  lv_display_t* _display=nullptr;
  lv_obj_t* _root=nullptr;
  lv_obj_t* _labels[32]{};
  LabelState _labelState[32]{};
  lv_obj_t* _boxes[64]{};
  BoxState _boxState[64]{};
  Line _lines[32]{};
  Triangle _triangles[4]{};
  // Home uses 27 workout segments; workouts have at most 18 intervals.
  static constexpr unsigned kArcs = 32;
  Arc _arcs[kArcs]{};
  lv_obj_t* _switch=nullptr;
  uint32_t _layout=UINT32_MAX, _flushes=0, _background=0;
  uint64_t _pixels=0;
  unsigned _label=0,_box=0,_line=0,_triangle=0,_arc=0;

  static void plain(lv_obj_t* obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_pad_all(obj,0,0);
    lv_obj_set_style_border_width(obj,0,0);
  }
  template<size_t N> static void hideRemainder(lv_obj_t* (&objects)[N],unsigned used) {
    for(unsigned i=used;i<N;++i) if(objects[i]) lv_obj_add_flag(objects[i],LV_OBJ_FLAG_HIDDEN);
  }
  void box(int x,int y,int w,int h,int radius,uint32_t color,int border,bool fill) {
    LV_ASSERT(_box<64);
    auto& cached=_boxState[_box];
    auto*& obj=_boxes[_box++];
    const bool created=!obj;
    if(created) {
      obj=lv_obj_create(_root); plain(obj);
      lv_obj_set_style_border_opa(obj,LV_OPA_COVER,0);
    }
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);
    if(created || cached.x!=x || cached.y!=y) lv_obj_set_pos(obj,x,y);
    if(created || cached.w!=w || cached.h!=h) lv_obj_set_size(obj,w,h);
    if(created || cached.radius!=radius) lv_obj_set_style_radius(obj,radius,0);
    if(created || cached.color!=color) {
      lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);
      lv_obj_set_style_border_color(obj,lv_color_hex(color),0);
    }
    if(created || cached.fill!=fill) lv_obj_set_style_bg_opa(obj,fill?LV_OPA_COVER:LV_OPA_TRANSP,0);
    if(created || cached.border!=border) lv_obj_set_style_border_width(obj,border,0);
    cached={x,y,w,h,radius,color,border,fill};
  }

  void arcShape(int cx,int cy,int radius,int thickness,int start,int end,int value,
                uint32_t fill,uint32_t track) {
    LV_ASSERT(_arc < kArcs);
    auto& a=_arcs[_arc++];
    if(end<=start) end=start+1;
    const bool created=!a.obj;
    if(created) {
      a.obj=lv_obj_create(_root); plain(a.obj);
      lv_obj_add_event_cb(a.obj,drawArc,LV_EVENT_DRAW_MAIN,&a);
    }
    lv_obj_remove_flag(a.obj,LV_OBJ_FLAG_HIDDEN);
    const bool moved=created || a.cx!=cx || a.cy!=cy || a.radius!=radius ||
      a.width!=thickness || a.start!=start || a.end!=end;
    if(moved) {
      if(!created) lv_obj_invalidate(a.obj);
      a.cx=cx; a.cy=cy; a.radius=radius; a.width=thickness; a.start=start; a.end=end;
      lv_area_t area;
      lv_draw_arc_get_area(cx,cy,radius,(start+270)%360,(end+270)%360,thickness,true,&area);
      lv_area_increase(&area,2,2);  // anti-aliased edge
      lv_obj_set_pos(a.obj,area.x1,area.y1);
      lv_obj_set_size(a.obj,lv_area_get_width(&area),lv_area_get_height(&area));
    }
    if(moved || a.fill!=fill || a.track!=track) {
      a.value=value; a.fill=fill; a.track=track;
      lv_obj_invalidate(a.obj);
    } else if(a.value!=value) {
      // Progress ticks repaint only the swept sliver, not the whole segment.
      const int before=fillEnd(a), after=fillEnd(a.start,a.end,value);
      a.value=value;
      if(before!=after) {
        lv_area_t area;
        lv_draw_arc_get_area(cx,cy,radius,(min(before,after)+270)%360,(max(before,after)+270)%360,
                             thickness,true,&area);
        lv_area_increase(&area,2,2);
        lv_obj_invalidate_area(a.obj,&area);
      }
    }
  }

  static void drawArc(lv_event_t* event) {
    auto* a=static_cast<Arc*>(lv_event_get_user_data(event));
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.center={a->cx,a->cy}; d.radius=a->radius; d.width=a->width; d.rounded=1;
    d.opa=LV_OPA_COVER;
    auto draw=[&](int start,int end,uint32_t color) {
      if(end<=start) return;
      d.start_angle=(start+270)%360; d.end_angle=(end+270)%360; d.color=lv_color_hex(color);
      lv_draw_arc(lv_event_get_layer(event),&d);
    };
    if(a->value<0) { draw(a->start,a->end,a->fill); return; }
    draw(a->start,a->end,a->track);
    draw(a->start,fillEnd(*a),a->fill);
  }

  static int fillEnd(int start,int end,int value) {
    return value<0 ? end : start+(end-start)*value/1000;
  }
  static int fillEnd(const Arc& a) { return fillEnd(a.start,a.end,a.value); }

  static void drawTriangle(lv_event_t* event) {
    auto* triangle=static_cast<Triangle*>(lv_event_get_user_data(event));
    lv_area_t area; lv_obj_get_coords(triangle->obj,&area);
    lv_draw_triangle_dsc_t descriptor; lv_draw_triangle_dsc_init(&descriptor);
    descriptor.color=lv_color_hex(triangle->color); descriptor.opa=LV_OPA_COVER;
    for(unsigned i=0;i<3;++i) descriptor.p[i]={triangle->points[i].x+area.x1,triangle->points[i].y+area.y1};
    lv_draw_triangle(lv_event_get_layer(event),&descriptor);
  }
  static void flush(lv_display_t* display,const lv_area_t* area,uint8_t* pixels) {
    auto* self=static_cast<LvglScene*>(lv_display_get_user_data(display));
    int w=area->x2-area->x1+1,h=area->y2-area->y1+1;
    // Explicit native RGB565 avoids the uint16_t overload's byte swapping.
    auto* rgb=reinterpret_cast<const lgfx::rgb565_t*>(pixels);
    self->_canvas.pushImage(area->x1,area->y1,w,h,rgb);
    M5.Display.pushImage(area->x1,area->y1,w,h,rgb);
    ++self->_flushes; self->_pixels+=uint64_t(w)*h;
    lv_display_flush_ready(display);
  }
};

} // namespace c25k
