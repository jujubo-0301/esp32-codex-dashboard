#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class DrawPanelApp final : public systems::phone::App {
public: static DrawPanelApp *requestInstance(bool = false, bool = false);
protected: DrawPanelApp(bool, bool); bool run() override; bool back() override;
private: static DrawPanelApp *_instance; static void drawEvent(lv_event_t *); lv_obj_t *_pad = nullptr;
};

class CalculatorApp final : public systems::phone::App {
public: static CalculatorApp *requestInstance(bool = false, bool = false);
protected: CalculatorApp(bool, bool); bool run() override; bool back() override;
private: static CalculatorApp *_instance; static void keyEvent(lv_event_t *); lv_obj_t *_display = nullptr; double _value = 0; char _op = 0; bool _fresh = true;
};

class SettingsApp final : public systems::phone::App {
public: static SettingsApp *requestInstance(bool = false, bool = false);
protected: SettingsApp(bool, bool); bool run() override; bool back() override;
private: static SettingsApp *_instance; static void brightnessEvent(lv_event_t *); static void volumeEvent(lv_event_t *); lv_obj_t *_brightness = nullptr; int _brightnessValue = 100;
};

class GravityBallApp final : public systems::phone::App {
public: static GravityBallApp *requestInstance(bool = false, bool = false);
protected: GravityBallApp(bool, bool); bool run() override; bool back() override;
private: static GravityBallApp *_instance; static void timerEvent(lv_timer_t *); lv_obj_t *_ball = nullptr; int _dx = 3; int _dy = 2;
};

class SpecAnalyzerApp final : public systems::phone::App {
public: static SpecAnalyzerApp *requestInstance(bool = false, bool = false);
protected: SpecAnalyzerApp(bool, bool); bool run() override; bool back() override;
private: static SpecAnalyzerApp *_instance; static void timerEvent(lv_timer_t *); lv_obj_t *_bars[16] = {}; int _phase = 0;
};

class MusicPlayerApp final : public systems::phone::App {
public: static MusicPlayerApp *requestInstance(bool = false, bool = false);
protected: MusicPlayerApp(bool, bool); bool run() override; bool back() override;
private: static MusicPlayerApp *_instance; static void toggleEvent(lv_event_t *); lv_obj_t *_state = nullptr; bool _playing = false;
};

class VideoPlayerApp final : public systems::phone::App {
public: static VideoPlayerApp *requestInstance(bool = false, bool = false);
protected: VideoPlayerApp(bool, bool); bool run() override; bool back() override;
private: static VideoPlayerApp *_instance;
};

class AIChatsApp final : public systems::phone::App {
public: static AIChatsApp *requestInstance(bool = false, bool = false);
protected: AIChatsApp(bool, bool); bool run() override; bool back() override;
private: static AIChatsApp *_instance;
};

} // namespace esp_brookesia::apps
