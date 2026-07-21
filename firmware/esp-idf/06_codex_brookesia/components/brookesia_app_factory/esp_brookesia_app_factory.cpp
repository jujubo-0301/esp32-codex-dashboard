#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "esp_brookesia_app_factory.hpp"

#define FACTORY_ICON esp_brookesia_app_icon_launcher_squareline_112_112

using namespace esp_brookesia::systems;
LV_IMG_DECLARE(esp_brookesia_app_icon_launcher_squareline_112_112);

namespace {

lv_obj_t *make_screen(const char *title)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x080D15), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF1F6FF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_26, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 18);
    return screen;
}

lv_obj_t *make_button(lv_obj_t *parent, const char *label, int x, int y, int w, int h)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x17263A), 0);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_t *text = lv_label_create(button);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_color(text, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(text);
    return button;
}

void set_value_label(lv_obj_t *label, const char *prefix, int value)
{
    char text[32];
    snprintf(text, sizeof(text), "%s%d%%", prefix, value);
    lv_label_set_text(label, text);
}

} // namespace

namespace esp_brookesia::apps {

DrawPanelApp *DrawPanelApp::_instance = nullptr;
DrawPanelApp *DrawPanelApp::requestInstance(bool status, bool nav) { if (!_instance) _instance = new DrawPanelApp(status, nav); return _instance; }
DrawPanelApp::DrawPanelApp(bool status, bool nav): App("DrawPanel", &FACTORY_ICON, true, status, nav) {}
bool DrawPanelApp::run() {
    lv_obj_t *screen = make_screen("DrawPanel");
    _pad = lv_obj_create(screen); lv_obj_set_pos(_pad, 24, 70); lv_obj_set_size(_pad, 432, 330);
    lv_obj_set_style_bg_color(_pad, lv_color_hex(0x101B2A), 0); lv_obj_set_style_radius(_pad, 18, 0);
    lv_obj_set_style_border_width(_pad, 1, 0); lv_obj_set_style_border_color(_pad, lv_color_hex(0x2D4666), 0);
    lv_obj_add_flag(_pad, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(_pad, drawEvent, LV_EVENT_PRESSED, this); lv_obj_add_event_cb(_pad, drawEvent, LV_EVENT_PRESSING, this);
    lv_obj_t *hint = lv_label_create(screen); lv_label_set_text(hint, "Touch to draw"); lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24); return true;
}
void DrawPanelApp::drawEvent(lv_event_t *event) {
    auto *app = static_cast<DrawPanelApp *>(lv_event_get_user_data(event)); if (!app || !app->_pad) return;
    lv_indev_t *indev = lv_indev_get_act(); lv_point_t point; lv_indev_get_point(indev, &point);
    int x = point.x - lv_obj_get_x(app->_pad) - 7, y = point.y - lv_obj_get_y(app->_pad) - 7;
    if (x < 0 || y < 0 || x > lv_obj_get_width(app->_pad) - 14 || y > lv_obj_get_height(app->_pad) - 14) return;
    lv_obj_t *dot = lv_obj_create(app->_pad); lv_obj_remove_style_all(dot); lv_obj_set_size(dot, 14, 14); lv_obj_set_pos(dot, x, y);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x50E6FF), 0); lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0); lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
}
bool DrawPanelApp::back() { return notifyCoreClosed(); }
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, DrawPanelApp, "DrawPanel", [](){ return std::shared_ptr<DrawPanelApp>(DrawPanelApp::requestInstance(), [](DrawPanelApp*){}); })

CalculatorApp *CalculatorApp::_instance = nullptr;
CalculatorApp *CalculatorApp::requestInstance(bool status, bool nav) { if (!_instance) _instance = new CalculatorApp(status, nav); return _instance; }
CalculatorApp::CalculatorApp(bool status, bool nav): App("Calculator", &FACTORY_ICON, true, status, nav) {}
bool CalculatorApp::run() {
    lv_obj_t *screen = make_screen("Calculator");
    _display = lv_label_create(screen); lv_label_set_text(_display, "0"); lv_obj_set_style_text_color(_display, lv_color_hex(0x73F7B4), 0); lv_obj_set_style_text_font(_display, &lv_font_montserrat_36, 0); lv_obj_align(_display, LV_ALIGN_TOP_RIGHT, -28, 70);
    const char *keys[] = {"7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+"};
    for (int i = 0; i < 16; ++i) { lv_obj_t *button = make_button(screen, keys[i], 28 + (i % 4) * 108, 140 + (i / 4) * 62, 96, 50); lv_obj_add_event_cb(button, keyEvent, LV_EVENT_CLICKED, this); }
    return true;
}
void CalculatorApp::keyEvent(lv_event_t *event) {
    auto *app = static_cast<CalculatorApp *>(lv_event_get_user_data(event)); auto *button = static_cast<lv_obj_t *>(lv_event_get_target(event)); const char *key = lv_label_get_text(lv_obj_get_child(button, 0));
    if (!app || !key) return;
    if (key[0] >= '0' && key[0] <= '9') { if (app->_fresh) { app->_value = 0; app->_fresh = false; lv_label_set_text(app->_display, ""); } char current[32]; snprintf(current, sizeof(current), "%s%s", lv_label_get_text(app->_display), key); lv_label_set_text(app->_display, current); }
    else if (key[0] == 'C') { app->_value = 0; app->_op = 0; app->_fresh = true; lv_label_set_text(app->_display, "0"); }
    else if (key[0] == '+' || key[0] == '-' || key[0] == '*' || key[0] == '/') { app->_value = atof(lv_label_get_text(app->_display)); app->_op = key[0]; app->_fresh = true; }
    else if (key[0] == '=') { double rhs = atof(lv_label_get_text(app->_display)); if (app->_op == '+') app->_value += rhs; else if (app->_op == '-') app->_value -= rhs; else if (app->_op == '*') app->_value *= rhs; else if (app->_op == '/' && rhs != 0) app->_value /= rhs; char result[32]; snprintf(result, sizeof(result), "%.2f", app->_value); lv_label_set_text(app->_display, result); app->_fresh = true; }
}
bool CalculatorApp::back() { return notifyCoreClosed(); }
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, CalculatorApp, "Calculator", [](){ return std::shared_ptr<CalculatorApp>(CalculatorApp::requestInstance(), [](CalculatorApp*){}); })

SettingsApp *SettingsApp::_instance = nullptr;
SettingsApp *SettingsApp::requestInstance(bool status, bool nav) { if (!_instance) _instance = new SettingsApp(status, nav); return _instance; }
SettingsApp::SettingsApp(bool status, bool nav): App("Settings", &FACTORY_ICON, true, status, nav) {}
bool SettingsApp::run() {
    lv_obj_t *screen = make_screen("Settings");
    _brightness = lv_label_create(screen); lv_obj_set_style_text_color(_brightness, lv_color_hex(0xF1F6FF), 0); lv_obj_set_style_text_font(_brightness, &lv_font_montserrat_24, 0); lv_obj_align(_brightness, LV_ALIGN_TOP_MID, 0, 84); set_value_label(_brightness, "Brightness ", _brightnessValue);
    lv_obj_t *down = make_button(screen, "-", 62, 150, 150, 60); lv_obj_add_event_cb(down, brightnessEvent, LV_EVENT_CLICKED, this); lv_obj_t *up = make_button(screen, "+", 268, 150, 150, 60); lv_obj_add_event_cb(up, brightnessEvent, LV_EVENT_CLICKED, this);
    lv_obj_t *volume = make_button(screen, "Volume", 62, 240, 356, 60); lv_obj_add_event_cb(volume, volumeEvent, LV_EVENT_CLICKED, this);
    lv_obj_t *wifi = lv_label_create(screen); lv_label_set_text(wifi, "Wi-Fi is controlled by Codex bridge"); lv_obj_set_style_text_color(wifi, lv_color_hex(0x8FA6C4), 0); lv_obj_align(wifi, LV_ALIGN_BOTTOM_MID, 0, -26); return true;
}
void SettingsApp::brightnessEvent(lv_event_t *event) { auto *app = static_cast<SettingsApp *>(lv_event_get_user_data(event)); if (!app) return; auto *button = static_cast<lv_obj_t *>(lv_event_get_target(event)); int x = lv_obj_get_x(button); app->_brightnessValue += (x < 200) ? -25 : 25; if (app->_brightnessValue < 25) app->_brightnessValue = 25; if (app->_brightnessValue > 100) app->_brightnessValue = 100; bsp_display_brightness_set(app->_brightnessValue); set_value_label(app->_brightness, "Brightness ", app->_brightnessValue); }
void SettingsApp::volumeEvent(lv_event_t *) { }
bool SettingsApp::back() { return notifyCoreClosed(); }
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, SettingsApp, "Settings", [](){ return std::shared_ptr<SettingsApp>(SettingsApp::requestInstance(), [](SettingsApp*){}); })

GravityBallApp *GravityBallApp::_instance = nullptr;
GravityBallApp *GravityBallApp::requestInstance(bool status, bool nav) { if (!_instance) _instance = new GravityBallApp(status, nav); return _instance; }
GravityBallApp::GravityBallApp(bool status, bool nav): App("Gravity Ball", &FACTORY_ICON, true, status, nav) {}
bool GravityBallApp::run() { lv_obj_t *screen = make_screen("Gravity Ball"); _ball = lv_obj_create(screen); lv_obj_remove_style_all(_ball); lv_obj_set_size(_ball, 48, 48); lv_obj_set_style_bg_color(_ball, lv_color_hex(0xFFCA6A), 0); lv_obj_set_style_bg_opa(_ball, LV_OPA_COVER, 0); lv_obj_set_style_radius(_ball, LV_RADIUS_CIRCLE, 0); lv_obj_set_pos(_ball, 216, 216); lv_timer_create(timerEvent, 30, this); return true; }
void GravityBallApp::timerEvent(lv_timer_t *timer) { auto *app = static_cast<GravityBallApp *>(timer->user_data); if (!app || !app->_ball) return; int x = lv_obj_get_x(app->_ball) + app->_dx, y = lv_obj_get_y(app->_ball) + app->_dy; if (x < 20 || x > 412) app->_dx = -app->_dx; if (y < 70 || y > 390) app->_dy = -app->_dy; lv_obj_set_pos(app->_ball, x, y); }
bool GravityBallApp::back() { return notifyCoreClosed(); }
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, GravityBallApp, "Gravity Ball", [](){ return std::shared_ptr<GravityBallApp>(GravityBallApp::requestInstance(), [](GravityBallApp*){}); })

SpecAnalyzerApp *SpecAnalyzerApp::_instance = nullptr;
SpecAnalyzerApp *SpecAnalyzerApp::requestInstance(bool status, bool nav) { if (!_instance) _instance = new SpecAnalyzerApp(status, nav); return _instance; }
SpecAnalyzerApp::SpecAnalyzerApp(bool status, bool nav): App("SpecAnalyzer", &FACTORY_ICON, true, status, nav) {}
bool SpecAnalyzerApp::run() {
    lv_obj_t *screen = make_screen("SpecAnalyzer");
    for (int i = 0; i < 16; ++i) { _bars[i] = lv_obj_create(screen); lv_obj_remove_style_all(_bars[i]); lv_obj_set_size(_bars[i], 16, 40); lv_obj_set_style_bg_color(_bars[i], lv_color_hsv_to_rgb((i * 20) % 360, 80, 95), 0); lv_obj_set_style_bg_opa(_bars[i], LV_OPA_COVER, 0); lv_obj_set_pos(_bars[i], 30 + i * 27, 360); }
    lv_obj_t *hint = lv_label_create(screen); lv_label_set_text(hint, "Live audio spectrum"); lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24); lv_timer_create(timerEvent, 80, this); return true;
}
void SpecAnalyzerApp::timerEvent(lv_timer_t *timer) { auto *app = static_cast<SpecAnalyzerApp *>(timer->user_data); if (!app) return; app->_phase++; for (int i = 0; i < 16; ++i) { int height = 30 + ((app->_phase * (i + 3) * 7) % 170); lv_obj_set_size(app->_bars[i], 16, height); lv_obj_set_pos(app->_bars[i], 30 + i * 27, 360 - height); } }
bool SpecAnalyzerApp::back() { return notifyCoreClosed(); }
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, SpecAnalyzerApp, "SpecAnalyzer", [](){ return std::shared_ptr<SpecAnalyzerApp>(SpecAnalyzerApp::requestInstance(), [](SpecAnalyzerApp*){}); })

MusicPlayerApp *MusicPlayerApp::_instance = nullptr;
MusicPlayerApp *MusicPlayerApp::requestInstance(bool status, bool nav) { if (!_instance) _instance = new MusicPlayerApp(status, nav); return _instance; }
MusicPlayerApp::MusicPlayerApp(bool status, bool nav): App("MusicPlayer", &FACTORY_ICON, true, status, nav) {}
bool MusicPlayerApp::run() { lv_obj_t *screen = make_screen("MusicPlayer"); lv_obj_t *album = lv_obj_create(screen); lv_obj_remove_style_all(album); lv_obj_set_size(album, 220, 220); lv_obj_set_style_bg_color(album, lv_color_hex(0x2B425F), 0); lv_obj_set_style_radius(album, 24, 0); lv_obj_align(album, LV_ALIGN_TOP_MID, 0, 72); lv_obj_t *track = lv_label_create(screen); lv_label_set_text(track, "Codex Demo Track"); lv_obj_align(track, LV_ALIGN_TOP_MID, 0, 310); _state = lv_label_create(screen); lv_obj_set_style_text_color(_state, lv_color_hex(0x73F7B4), 0); lv_obj_align(_state, LV_ALIGN_TOP_MID, 0, 340); lv_obj_t *button = make_button(screen, "Play / Pause", 110, 380, 260, 54); lv_obj_add_event_cb(button, toggleEvent, LV_EVENT_CLICKED, this); lv_label_set_text(_state, "Paused"); return true; }
void MusicPlayerApp::toggleEvent(lv_event_t *event) { auto *app = static_cast<MusicPlayerApp *>(lv_event_get_user_data(event)); if (!app) return; app->_playing = !app->_playing; lv_label_set_text(app->_state, app->_playing ? "Playing" : "Paused"); }
bool MusicPlayerApp::back() { return notifyCoreClosed(); }
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, MusicPlayerApp, "MusicPlayer", [](){ return std::shared_ptr<MusicPlayerApp>(MusicPlayerApp::requestInstance(), [](MusicPlayerApp*){}); })

VideoPlayerApp *VideoPlayerApp::_instance = nullptr;
VideoPlayerApp *VideoPlayerApp::requestInstance(bool status, bool nav) { if (!_instance) _instance = new VideoPlayerApp(status, nav); return _instance; }
VideoPlayerApp::VideoPlayerApp(bool status, bool nav): App("VideoPlayer", &FACTORY_ICON, true, status, nav) {}
bool VideoPlayerApp::run() { lv_obj_t *screen = make_screen("VideoPlayer"); lv_obj_t *box = lv_obj_create(screen); lv_obj_set_pos(box, 24, 72); lv_obj_set_size(box, 432, 280); lv_obj_set_style_bg_color(box, lv_color_hex(0x111923), 0); lv_obj_set_style_radius(box, 16, 0); lv_obj_t *text = lv_label_create(box); lv_label_set_text(text, "AVI files from SD card\nPlace videos in /sdcard/avi"); lv_obj_set_style_text_color(text, lv_color_hex(0xB9C9DF), 0); lv_obj_center(text); lv_obj_t *scan = make_button(screen, "Scan SD card", 110, 380, 260, 54); (void)scan; return true; }
bool VideoPlayerApp::back() { return notifyCoreClosed(); }
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, VideoPlayerApp, "VideoPlayer", [](){ return std::shared_ptr<VideoPlayerApp>(VideoPlayerApp::requestInstance(), [](VideoPlayerApp*){}); })

AIChatsApp *AIChatsApp::_instance = nullptr;
AIChatsApp *AIChatsApp::requestInstance(bool status, bool nav) { if (!_instance) _instance = new AIChatsApp(status, nav); return _instance; }
AIChatsApp::AIChatsApp(bool status, bool nav): App("AIChats", &FACTORY_ICON, true, status, nav) {}
bool AIChatsApp::run() { lv_obj_t *screen = make_screen("AIChats"); lv_obj_t *box = lv_obj_create(screen); lv_obj_set_pos(box, 24, 72); lv_obj_set_size(box, 432, 280); lv_obj_set_style_bg_color(box, lv_color_hex(0x111923), 0); lv_obj_set_style_radius(box, 16, 0); lv_obj_t *text = lv_label_create(box); lv_label_set_text(text, "Xiaozhi AI\nConfigure Wi-Fi and service account first"); lv_obj_set_style_text_color(text, lv_color_hex(0xB9C9DF), 0); lv_obj_center(text); lv_obj_t *connect = make_button(screen, "Connect", 110, 380, 260, 54); (void)connect; return true; }
bool AIChatsApp::back() { return notifyCoreClosed(); }
ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AIChatsApp, "AIChats", [](){ return std::shared_ptr<AIChatsApp>(AIChatsApp::requestInstance(), [](AIChatsApp*){}); })

} // namespace esp_brookesia::apps
