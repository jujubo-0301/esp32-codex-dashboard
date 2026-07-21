#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "esp_brookesia_app_codex.hpp"

#define APP_NAME "Codex"

using namespace esp_brookesia::systems;



namespace esp_brookesia::apps {

CodexApp *CodexApp::_instance = nullptr;

CodexApp *CodexApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new CodexApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

CodexApp::CodexApp(bool use_status_bar, bool use_navigation_bar):
    /* Codex renders onto the app's active screen. Let Brookesia create and
     * reclaim that screen instead of touching the launcher screen. */
    App(APP_NAME, nullptr,
        true, use_status_bar, use_navigation_bar)
{
}

bool CodexApp::run(void)
{
    ESP_UTILS_LOGI("Start Codex dashboard app");
    codex_dashboard_start();
    return true;
}

bool CodexApp::back(void)
{
    codex_dashboard_stop();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, CodexApp, APP_NAME, []() {
    return std::shared_ptr<CodexApp>(CodexApp::requestInstance(), [](CodexApp *) {});
})

} // namespace esp_brookesia::apps
