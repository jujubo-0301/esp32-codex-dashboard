#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

extern "C" void codex_dashboard_start(void);
extern "C" void codex_dashboard_stop(void);

namespace esp_brookesia::apps {

class CodexApp final : public systems::phone::App {
public:
    static CodexApp *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~CodexApp() override = default;

protected:
    CodexApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static CodexApp *_instance;
};

} // namespace esp_brookesia::apps
