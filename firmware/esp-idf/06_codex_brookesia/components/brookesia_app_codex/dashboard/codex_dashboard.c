#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <unistd.h>
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "codex_config.h"

LV_FONT_DECLARE(lv_font_ui_cjk_20);

#define TAG "codex_status"
#define SCREEN_W 480
#define SCREEN_H 480

typedef enum {
    PAGE_HOME = 0,
    PAGE_RUNNING,
    PAGE_DONE,
    PAGE_ERROR,
    PAGE_ATTENTION,
    PAGE_OFFLINE,
    PAGE_TASKS,
    PAGE_SETTINGS,
    PAGE_COUNT,
} status_page_t;

typedef enum {
    METRIC_CPU = 0,
    METRIC_RAM,
    METRIC_GPU,
} metric_kind_t;

typedef enum {
    CODEX_STATE_OFFLINE = 0,
    CODEX_STATE_IDLE,
    CODEX_STATE_RUNNING,
    CODEX_STATE_WAITING,
    CODEX_STATE_DONE,
    CODEX_STATE_ERROR,
} codex_state_t;

typedef struct {
    codex_state_t state;
    codex_state_t overall_state;
    char task[96];
    char step[96];
    char message[128];
    char error[128];
    char summary[160];
    int progress;
    int overall_progress;
    uint32_t overall_elapsed_s;
    int overall_task_count;
    int step_index;
    int step_total;
    int cpu;
    int ram;
    int gpu;
    char clock[8];
    char recent_task_names[4][96];
    codex_state_t recent_task_states[4];
    int recent_task_progress[4];
    uint32_t recent_task_elapsed_s[4];
    int recent_task_count;
    char quota_remaining[16];
    char reset_date[16];
    uint32_t elapsed_s;
    uint32_t last_update_ms;
    bool connected;
} codex_status_t;

static status_page_t s_page = PAGE_HOME;
static bool s_auto_demo = false;
static status_page_t s_last_page = PAGE_HOME;
static esp_codec_dev_handle_t s_speaker;
static bool s_audio_ready;
static bool s_sound_enabled = true;
static bool s_alt_theme;
static int s_brightness = 100;
static int s_time_offset_hours;
static bool s_touch_tracking;
static bool s_gesture_handled;
static bool s_dashboard_active;
static bool s_dashboard_initialized;
static lv_point_t s_touch_start;
static bool s_manual_page;
static EventGroupHandle_t s_wifi_events;
static bool s_wifi_connected;
static bool s_wifi_initialized;
static bool s_wifi_handlers_registered;
static bool s_factory_wifi_profile_active;
static bool s_wifi_fallback_attempted;
static bool s_bridge_discovery_started;
static bool s_render_queued;
static bool s_provisioning_active;
static bool s_provisioning_stop;
static esp_netif_t *s_wifi_ap_netif;
static uint8_t s_poll_failures;
static char s_status_url[96] = CODEX_STATUS_URL;
static codex_status_t s_status = {
    .state = CODEX_STATE_OFFLINE,
    .overall_state = CODEX_STATE_OFFLINE,
    .message = "等待电脑端状态服务",
    .clock = "--:--",
    .quota_remaining = "--",
    .reset_date = "--",
    .overall_elapsed_s = 0,
    .overall_task_count = 0,
};

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_STATUS_POLL_MS 1000

static lv_color_t color_bg(void) { return lv_color_hex(s_alt_theme ? 0x101318 : 0x050A11); }
static lv_color_t color_panel(void) { return lv_color_hex(s_alt_theme ? 0x18202A : 0x0D1725); }
static lv_color_t color_running_bg(void) { return lv_color_hex(0x050A11); }
static lv_color_t color_running_panel(void) { return lv_color_hex(0x0D1725); }
static lv_color_t color_text(void) { return lv_color_hex(0xF1F6FF); }
static lv_color_t color_muted(void) { return lv_color_hex(0x71829D); }
static lv_color_t color_cyan(void) { return lv_color_hex(0x42E8FF); }
static lv_color_t color_green(void) { return lv_color_hex(0x73F7B4); }
static lv_color_t color_amber(void) { return lv_color_hex(0xFFCA6A); }
static lv_color_t color_red(void) { return lv_color_hex(0xFF6B7D); }

static status_page_t page_for_state(codex_state_t state)
{
    switch (state) {
    case CODEX_STATE_RUNNING: return PAGE_RUNNING;
    case CODEX_STATE_WAITING: return PAGE_ATTENTION;
    case CODEX_STATE_DONE: return PAGE_DONE;
    case CODEX_STATE_ERROR: return PAGE_ERROR;
    case CODEX_STATE_OFFLINE: return PAGE_OFFLINE;
    case CODEX_STATE_IDLE: return PAGE_RUNNING;
    default: return PAGE_HOME;
    }
}

static bool page_available(status_page_t page)
{
    switch (page) {
    case PAGE_RUNNING: return true;
    case PAGE_DONE: return s_status.state == CODEX_STATE_DONE;
    case PAGE_ATTENTION: return s_status.state == CODEX_STATE_WAITING;
    case PAGE_ERROR: return s_status.state == CODEX_STATE_ERROR;
    case PAGE_OFFLINE: return s_status.state == CODEX_STATE_OFFLINE;
    case PAGE_TASKS: return false;
    default: return true;
    }
}

static status_page_t next_available_page(int direction)
{
    status_page_t candidate = s_page;
    for (int i = 0; i < PAGE_COUNT; i++) {
        candidate = (status_page_t)((candidate + (direction > 0 ? 1 : PAGE_COUNT - 1)) % PAGE_COUNT);
        if (page_available(candidate)) return candidate;
    }
    return s_page;
}

static const char *state_label(codex_state_t state)
{
    switch (state) {
    case CODEX_STATE_RUNNING: return "运行中";
    case CODEX_STATE_WAITING: return "等待确认";
    case CODEX_STATE_DONE: return "已完成";
    case CODEX_STATE_ERROR: return "执行失败";
    case CODEX_STATE_IDLE: return "空闲";
    default: return "离线";
    }
}

static void format_elapsed(uint32_t seconds, char *buffer, size_t buffer_size)
{
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t minutes = seconds / 60;
    uint32_t remainder = seconds % 60;
    if (days > 0) {
        snprintf(buffer, buffer_size, "%lud %02luh", (unsigned long)days, (unsigned long)hours);
    } else if (hours > 0) {
        snprintf(buffer, buffer_size, "%luh %02lum", (unsigned long)hours,
                 (unsigned long)(minutes % 60));
    } else {
        snprintf(buffer, buffer_size, "%lum %02lus", (unsigned long)minutes, (unsigned long)remainder);
    }
}

static codex_state_t parse_state(const char *value)
{
    if (!value) return CODEX_STATE_OFFLINE;
    if (strcmp(value, "running") == 0) return CODEX_STATE_RUNNING;
    if (strcmp(value, "waiting") == 0) return CODEX_STATE_WAITING;
    if (strcmp(value, "done") == 0 || strcmp(value, "success") == 0) return CODEX_STATE_DONE;
    if (strcmp(value, "error") == 0 || strcmp(value, "failed") == 0) return CODEX_STATE_ERROR;
    if (strcmp(value, "idle") == 0) return CODEX_STATE_IDLE;
    return CODEX_STATE_OFFLINE;
}

static void render_page_async(void *user_data);
static void pulse_opa_cb(void *var, int32_t value);
static void discover_bridge_task(void *arg);
static void wifi_provision_task(void *arg);

static void schedule_render(void)
{
    if (!s_dashboard_active || s_render_queued) return;
    s_render_queued = true;
    lv_async_call(render_page_async, NULL);
}

static void copy_json_string(char *dst, size_t dst_size, cJSON *root, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    const char *value = cJSON_IsString(item) ? item->valuestring : "";
    strlcpy(dst, value, dst_size);
}

static void apply_status_json(const char *payload, size_t length)
{
    cJSON *root = cJSON_ParseWithLength(payload, length);
    if (!root) {
        ESP_LOGW(TAG, "Status JSON parse failed");
        return;
    }

    cJSON *state_item = cJSON_GetObjectItemCaseSensitive(root, "state");
    const char *state_value = cJSON_IsString(state_item) ? state_item->valuestring : "offline";
    codex_state_t next_state = parse_state(state_value);
    cJSON *overall_state_item = cJSON_GetObjectItemCaseSensitive(root, "overall_state");
    const char *overall_state_value = cJSON_IsString(overall_state_item) ? overall_state_item->valuestring : state_value;
    codex_state_t next_overall_state = parse_state(overall_state_value);
    copy_json_string(s_status.task, sizeof(s_status.task), root, "task");
    copy_json_string(s_status.step, sizeof(s_status.step), root, "step");
    copy_json_string(s_status.message, sizeof(s_status.message), root, "message");
    copy_json_string(s_status.error, sizeof(s_status.error), root, "error");
    copy_json_string(s_status.summary, sizeof(s_status.summary), root, "summary");
    copy_json_string(s_status.clock, sizeof(s_status.clock), root, "clock");
    copy_json_string(s_status.quota_remaining, sizeof(s_status.quota_remaining), root, "quota_remaining");
    copy_json_string(s_status.reset_date, sizeof(s_status.reset_date), root, "reset_date");

    s_status.recent_task_count = 0;
    memset(s_status.recent_task_names, 0, sizeof(s_status.recent_task_names));
    memset(s_status.recent_task_progress, 0, sizeof(s_status.recent_task_progress));
    memset(s_status.recent_task_elapsed_s, 0, sizeof(s_status.recent_task_elapsed_s));
    cJSON *tasks = cJSON_GetObjectItemCaseSensitive(root, "tasks");
    if (cJSON_IsArray(tasks)) {
        cJSON *task_item = NULL;
        cJSON_ArrayForEach(task_item, tasks) {
            if (s_status.recent_task_count >= 4) break;
            copy_json_string(s_status.recent_task_names[s_status.recent_task_count],
                             sizeof(s_status.recent_task_names[0]), task_item, "name");
            cJSON *task_state = cJSON_GetObjectItemCaseSensitive(task_item, "state");
            const char *task_state_value = cJSON_IsString(task_state) ? task_state->valuestring : "idle";
            s_status.recent_task_states[s_status.recent_task_count] = parse_state(task_state_value);
            cJSON *task_progress = cJSON_GetObjectItemCaseSensitive(task_item, "progress");
            s_status.recent_task_progress[s_status.recent_task_count] = cJSON_IsNumber(task_progress)
                ? LV_CLAMP(cJSON_GetNumberValue(task_progress), 0, 100) : 0;
            cJSON *task_elapsed = cJSON_GetObjectItemCaseSensitive(task_item, "elapsed_s");
            s_status.recent_task_elapsed_s[s_status.recent_task_count] = cJSON_IsNumber(task_elapsed)
                ? (uint32_t)cJSON_GetNumberValue(task_elapsed) : 0;
            s_status.recent_task_count++;
        }
    }

    cJSON *progress = cJSON_GetObjectItemCaseSensitive(root, "progress");
    cJSON *overall_progress = cJSON_GetObjectItemCaseSensitive(root, "overall_progress");
    cJSON *overall_elapsed = cJSON_GetObjectItemCaseSensitive(root, "overall_elapsed_s");
    cJSON *overall_task_count = cJSON_GetObjectItemCaseSensitive(root, "overall_task_count");
    cJSON *step_index = cJSON_GetObjectItemCaseSensitive(root, "step_index");
    cJSON *step_total = cJSON_GetObjectItemCaseSensitive(root, "step_total");
    cJSON *cpu = cJSON_GetObjectItemCaseSensitive(root, "cpu");
    cJSON *ram = cJSON_GetObjectItemCaseSensitive(root, "ram");
    cJSON *gpu = cJSON_GetObjectItemCaseSensitive(root, "gpu");
    cJSON *elapsed = cJSON_GetObjectItemCaseSensitive(root, "elapsed_s");
    s_status.progress = cJSON_IsNumber(progress) ? LV_CLAMP(cJSON_GetNumberValue(progress), 0, 100) : 0;
    s_status.overall_progress = cJSON_IsNumber(overall_progress)
        ? LV_CLAMP(cJSON_GetNumberValue(overall_progress), 0, 100) : s_status.progress;
    s_status.overall_elapsed_s = cJSON_IsNumber(overall_elapsed)
        ? (uint32_t)cJSON_GetNumberValue(overall_elapsed)
        : (cJSON_IsNumber(elapsed) ? (uint32_t)cJSON_GetNumberValue(elapsed) : 0);
    s_status.overall_task_count = cJSON_IsNumber(overall_task_count)
        ? LV_CLAMP(cJSON_GetNumberValue(overall_task_count), 0, 4) : s_status.recent_task_count;
    s_status.step_index = cJSON_IsNumber(step_index) ? (int)cJSON_GetNumberValue(step_index) : 0;
    s_status.step_total = cJSON_IsNumber(step_total) ? (int)cJSON_GetNumberValue(step_total) : 0;
    s_status.cpu = cJSON_IsNumber(cpu) ? LV_CLAMP(cJSON_GetNumberValue(cpu), 0, 100) : 0;
    s_status.ram = cJSON_IsNumber(ram) ? LV_CLAMP(cJSON_GetNumberValue(ram), 0, 100) : 0;
    s_status.gpu = cJSON_IsNumber(gpu) ? LV_CLAMP(cJSON_GetNumberValue(gpu), 0, 100) : 0;
    s_status.elapsed_s = cJSON_IsNumber(elapsed) ? (uint32_t)cJSON_GetNumberValue(elapsed) : 0;
    const bool state_changed = next_state != s_status.state;
    s_status.state = next_state;
    s_status.overall_state = next_overall_state;
    s_status.connected = true;
    s_status.last_update_ms = esp_log_timestamp();

    if (state_changed) {
        s_manual_page = false;
        s_page = page_for_state(next_state);
    } else if (!s_manual_page) {
        s_page = page_for_state(next_state);
    }
    cJSON_Delete(root);
    if (s_dashboard_active) {
        schedule_render();
    }
}

typedef struct {
    char data[4096];
    size_t length;
} http_response_t;

// Keep the HTTP receive buffer out of the polling task stack. The ESP HTTP
// client and cJSON parser already use a sizeable stack while performing a
// request; putting another 4 KB array on the task stack caused a reboot.
static http_response_t s_http_response;

static esp_err_t http_event_cb(esp_http_client_event_t *event)
{
    http_response_t *response = (http_response_t *)event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && response && event->data && event->data_len > 0) {
        size_t available = sizeof(response->data) - response->length - 1;
        size_t copy_len = event->data_len < available ? event->data_len : available;
        memcpy(response->data + response->length, event->data, copy_len);
        response->length += copy_len;
        response->data[response->length] = '\0';
    }
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi associated with %s", CODEX_WIFI_SSID);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = (const wifi_event_sta_disconnected_t *)event_data;
        s_wifi_connected = false;
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d; retrying", disconnected ? disconnected->reason : -1);
        if (s_factory_wifi_profile_active && !s_wifi_fallback_attempted && disconnected &&
            disconnected->reason == WIFI_REASON_NO_AP_FOUND && CODEX_WIFI_SSID[0] != '\0') {
            wifi_config_t fallback = {0};
            strlcpy((char *)fallback.sta.ssid, CODEX_WIFI_SSID, sizeof(fallback.sta.ssid));
            strlcpy((char *)fallback.sta.password, CODEX_WIFI_PASSWORD, sizeof(fallback.sta.password));
            fallback.sta.threshold.authmode = WIFI_AUTH_OPEN;
            s_wifi_fallback_attempted = true;
            s_factory_wifi_profile_active = false;
            esp_wifi_set_config(WIFI_IF_STA, &fallback);
            ESP_LOGI(TAG, "Factory WiFi unavailable; trying configured fallback: %s", CODEX_WIFI_SSID);
        }
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi connected");
        if (!s_bridge_discovery_started) {
            s_bridge_discovery_started = true;
            xTaskCreate(discover_bridge_task, "bridge_discovery", 4096, NULL, 3, NULL);
        }
        if (s_dashboard_active) {
            schedule_render();
        }
    }
}

static void discover_bridge_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGW(TAG, "Bridge discovery socket failed");
        vTaskDelete(NULL);
        return;
    }

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in destination = {0};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(8788);
    destination.sin_addr.s_addr = inet_addr("255.255.255.255");
    const char probe[] = "CODEX_DISCOVER";
    sendto(sock, probe, sizeof(probe) - 1, 0,
           (struct sockaddr *)&destination, sizeof(destination));

    char response[64] = {0};
    struct sockaddr_in sender = {0};
    socklen_t sender_len = sizeof(sender);
    int received = recvfrom(sock, response, sizeof(response) - 1, 0,
                            (struct sockaddr *)&sender, &sender_len);
    if (received > 0 && strncmp(response, "CODEX_BRIDGE", 12) == 0) {
        char ip[INET_ADDRSTRLEN] = {0};
        response[received] = '\0';
        if (sscanf(response, "CODEX_BRIDGE %*d %15s", ip) != 1) {
            inet_ntop(AF_INET, &sender.sin_addr, ip, sizeof(ip));
        }
        snprintf(s_status_url, sizeof(s_status_url), "http://%s:8787/status", ip);
        ESP_LOGI(TAG, "Bridge discovered at %s", s_status_url);
    } else {
        ESP_LOGW(TAG, "Bridge discovery timed out; using %s", s_status_url);
    }
    close(sock);
    vTaskDelete(NULL);
}

/* The original launcher persists its network in this NVS namespace. */
static bool load_factory_wifi_profile(wifi_config_t *wifi_config)
{
    nvs_handle_t handle;
    char ssid[sizeof(wifi_config->sta.ssid)] = {0};
    char password[sizeof(wifi_config->sta.password)] = {0};
    size_t ssid_length = sizeof(ssid);
    size_t password_length = sizeof(password);

    if (nvs_open("wifi", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t ssid_result = nvs_get_str(handle, "ssid", ssid, &ssid_length);
    esp_err_t password_result = nvs_get_str(handle, "password", password, &password_length);
    nvs_close(handle);
    if (ssid_result != ESP_OK || password_result != ESP_OK || ssid[0] == '\0') {
        return false;
    }

    memset(wifi_config, 0, sizeof(*wifi_config));
    strlcpy((char *)wifi_config->sta.ssid, ssid, sizeof(wifi_config->sta.ssid));
    strlcpy((char *)wifi_config->sta.password, password, sizeof(wifi_config->sta.password));
    wifi_config->sta.threshold.authmode = WIFI_AUTH_OPEN;
    return true;
}

static void form_url_decode(char *value)
{
    char *read = value;
    char *write = value;
    while (*read) {
        if (*read == '+') {
            *write++ = ' ';
            read++;
        } else if (*read == '%' && read[1] && read[2]) {
            unsigned int byte = 0;
            if (sscanf(read + 1, "%2x", &byte) == 1) {
                *write++ = (char)byte;
                read += 3;
            } else {
                *write++ = *read++;
            }
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static bool form_value(const char *body, const char *key, char *value, size_t value_size)
{
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "%s=", key);
    const char *start = strstr(body, prefix);
    if (!start) return false;
    start += strlen(prefix);
    size_t length = strcspn(start, "&\r\n");
    if (length >= value_size) return false;
    memcpy(value, start, length);
    value[length] = '\0';
    form_url_decode(value);
    return true;
}

static bool save_factory_wifi_profile(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) >= sizeof(((wifi_config_t *)0)->sta.ssid) ||
        strlen(password) >= sizeof(((wifi_config_t *)0)->sta.password)) {
        return false;
    }
    nvs_handle_t handle;
    if (nvs_open("wifi", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(handle, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "password", password);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static void provisioning_reply(int client, const char *body)
{
    char response[1024];
    int length = snprintf(response, sizeof(response),
                          "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                          "Connection: close\r\n\r\n%s", body);
    if (length > 0) send(client, response, (size_t)length, 0);
}

static void wifi_provision_task(void *arg)
{
    (void)arg;
    const char *form =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Codex Wi-Fi</title><style>body{font:18px system-ui;margin:28px;background:#07111e;color:#eef6ff}"
        "input,button{box-sizing:border-box;width:100%;padding:14px;margin:8px 0;border-radius:8px;border:0;font-size:17px}"
        "button{background:#42e8ff;color:#07111e;font-weight:700}</style>"
        "<h2>Codex Wi-Fi</h2><p>&#35774;&#32622;&#26032;&#30340; 2.4GHz Wi-Fi</p>"
        "<form method=post><input name=ssid placeholder='Wi-Fi name (SSID)' required>"
        "<input name=password type=password placeholder='Password'><button>&#20445;&#23384;&#24182;&#36830;&#25509;</button></form>";

    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, "Codex-Setup", sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, "codex216", sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (!s_wifi_ap_netif) s_wifi_ap_netif = esp_netif_create_default_wifi_ap();
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) goto finish;
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(80), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(server, 2) != 0) {
        close(server);
        goto finish;
    }
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    while (!s_provisioning_stop) {
        struct sockaddr_in client_address = {0};
        socklen_t client_length = sizeof(client_address);
        int client = accept(server, (struct sockaddr *)&client_address, &client_length);
        if (client < 0) continue;
        char request[768] = {0};
        int received = recv(client, request, sizeof(request) - 1, 0);
        if (received > 0 && strncmp(request, "POST ", 5) == 0) {
            char *body = strstr(request, "\r\n\r\n");
            char ssid[33] = {0};
            char password[65] = {0};
            if (body && form_value(body + 4, "ssid", ssid, sizeof(ssid)) &&
                form_value(body + 4, "password", password, sizeof(password)) &&
                save_factory_wifi_profile(ssid, password)) {
                provisioning_reply(client, "<meta charset=utf-8><h2>&#24050;&#20445;&#23384;</h2><p>&#35774;&#22791;&#27491;&#22312;&#36830;&#25509;&#26032;&#32593;&#32476;&#12290;</p>");
                s_provisioning_stop = true;
            } else {
                provisioning_reply(client, "<meta charset=utf-8><h2>&#20445;&#23384;&#22833;&#36133;</h2><p>SSID &#19981;&#33021;&#20026;&#31354;&#12290;</p>");
            }
        } else {
            provisioning_reply(client, form);
        }
        close(client);
    }
    close(server);

finish:
    s_provisioning_active = false;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_connect();
    strlcpy(s_status.message, "WiFi 配置已保存，正在连接", sizeof(s_status.message));
    schedule_render();
    vTaskDelete(NULL);
}

static void start_wifi_provisioning(void)
{
    if (s_provisioning_active || !s_wifi_initialized) return;
    s_provisioning_active = true;
    s_provisioning_stop = false;
    strlcpy(s_status.message, "手机连接 Codex-Setup 后打开 192.168.4.1", sizeof(s_status.message));
    if (xTaskCreate(wifi_provision_task, "wifi_setup", 6144, NULL, 3, NULL) != pdPASS) {
        s_provisioning_active = false;
        strlcpy(s_status.message, "WiFi 设置启动失败", sizeof(s_status.message));
    }
}

static void wifi_init(void)
{
    if (s_wifi_initialized) {
        ESP_LOGI(TAG, "[DEBUG-WIFI] WiFi already initialized");
        return;
    }
    if (!s_wifi_events) {
        s_wifi_events = xEventGroupCreate();
    }
    if (CODEX_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "WiFi credentials are empty; live status is disabled");
        return;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[DEBUG-WIFI] NVS init failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "[DEBUG-WIFI] netif init failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "[DEBUG-WIFI] event loop init failed: %s", esp_err_to_name(err));
        return;
    }
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        if (!esp_netif_create_default_wifi_sta()) {
            ESP_LOGE(TAG, "[DEBUG-WIFI] create STA netif failed");
            return;
        }
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        ESP_LOGE(TAG, "[DEBUG-WIFI] WiFi init failed: %s", esp_err_to_name(err));
        return;
    }
    if (!s_wifi_handlers_registered) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[DEBUG-WIFI] WiFi handler failed: %s", esp_err_to_name(err));
            return;
        }
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[DEBUG-WIFI] IP handler failed: %s", esp_err_to_name(err));
            return;
        }
        s_wifi_handlers_registered = true;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        ESP_LOGE(TAG, "[DEBUG-WIFI] set mode failed: %s", esp_err_to_name(err));
        return;
    }

    /* The factory launcher persists its active profile through esp_wifi.
     * Reading it first prevents the legacy app namespace from overwriting a
     * network that the user has just selected in the factory UI. */
    wifi_config_t wifi_config = {0};
    err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        memset(&wifi_config, 0, sizeof(wifi_config));
    }
    if (wifi_config.sta.ssid[0] == '\0') {
        if (load_factory_wifi_profile(&wifi_config)) {
            s_factory_wifi_profile_active = true;
            s_wifi_fallback_attempted = false;
            ESP_LOGI(TAG, "Using legacy WiFi fallback: %s", (char *)wifi_config.sta.ssid);
        } else {
            strlcpy((char *)wifi_config.sta.ssid, CODEX_WIFI_SSID, sizeof(wifi_config.sta.ssid));
            strlcpy((char *)wifi_config.sta.password, CODEX_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
            wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
            ESP_LOGI(TAG, "Using first-boot WiFi fallback: %s", CODEX_WIFI_SSID);
        }
    } else {
        s_factory_wifi_profile_active = false;
        ESP_LOGI(TAG, "Using saved WiFi profile: %s", (char *)wifi_config.sta.ssid);
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        ESP_LOGE(TAG, "[DEBUG-WIFI] set config failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        ESP_LOGE(TAG, "[DEBUG-WIFI] start failed: %s", esp_err_to_name(err));
        return;
    }
    s_wifi_initialized = true;
    ESP_LOGI(TAG, "[DEBUG-WIFI] WiFi init complete");
}

static bool fetch_live_status(void)
{
    if (!s_wifi_connected || s_status_url[0] == '\0') return false;
    memset(&s_http_response, 0, sizeof(s_http_response));
    esp_http_client_config_t config = {
        .url = s_status_url,
        .timeout_ms = 1500,
        .event_handler = http_event_cb,
        .user_data = &s_http_response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status_code != 200 || s_http_response.length == 0) return false;
    apply_status_json(s_http_response.data, s_http_response.length);
    return true;
}

static void status_poll_task(void *arg)
{
    (void)arg;
    while (true) {
        if (!s_dashboard_active) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_STATUS_POLL_MS));
            continue;
        }
        if (fetch_live_status()) {
            s_poll_failures = 0;
        } else if (++s_poll_failures >= 3) {
            s_status.connected = false;
            s_status.state = CODEX_STATE_OFFLINE;
            s_status.overall_state = CODEX_STATE_OFFLINE;
            s_status.progress = 0;
            s_status.overall_progress = 0;
            s_status.recent_task_count = 0;
            s_status.last_update_ms = 0;
            strlcpy(s_status.message, "无法连接电脑端状态服务", sizeof(s_status.message));
            s_manual_page = false;
            s_page = PAGE_OFFLINE;
            schedule_render();
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_STATUS_POLL_MS));
    }
}

static void audio_init(void)
{
    s_speaker = bsp_audio_codec_speaker_init();
    if (!s_speaker) {
        ESP_LOGW(TAG, "Speaker codec unavailable; visual alerts remain enabled");
        return;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 22050,
        .channel = 1,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_set_out_vol(s_speaker, 65) != ESP_OK ||
        esp_codec_dev_open(s_speaker, &fs) != ESP_OK) {
        ESP_LOGW(TAG, "Speaker codec open failed; visual alerts remain enabled");
        return;
    }
    s_audio_ready = true;
    ESP_LOGI(TAG, "Speaker alert channel ready");
}

static void play_tone(uint16_t frequency, uint16_t duration_ms)
{
    if (!s_audio_ready) {
        return;
    }

    const uint32_t sample_rate = 22050;
    const size_t sample_count = (sample_rate * duration_ms) / 1000;
    int16_t *samples = heap_caps_malloc(sample_count * sizeof(int16_t), MALLOC_CAP_8BIT);
    if (!samples) {
        ESP_LOGW(TAG, "Tone buffer allocation failed");
        return;
    }

    for (size_t i = 0; i < sample_count; i++) {
        const float phase = 2.0f * (float)M_PI * (float)frequency * (float)i / (float)sample_rate;
        const float envelope = (i < 220) ? (float)i / 220.0f
                                         : (i > sample_count - 220 ? (float)(sample_count - i) / 220.0f : 1.0f);
        samples[i] = (int16_t)(12000.0f * envelope * sinf(phase));
    }

    esp_codec_dev_write(s_speaker, samples, sample_count * sizeof(int16_t));
    heap_caps_free(samples);
}

static void play_alert(status_page_t page)
{
    if (!s_sound_enabled) return;
    if (page == PAGE_DONE) {
        play_tone(880, 90);
        vTaskDelay(pdMS_TO_TICKS(35));
        play_tone(1175, 130);
    } else if (page == PAGE_ERROR) {
        play_tone(520, 150);
        vTaskDelay(pdMS_TO_TICKS(35));
        play_tone(320, 180);
    } else if (page == PAGE_ATTENTION) {
        play_tone(660, 85);
        vTaskDelay(pdMS_TO_TICKS(45));
        play_tone(660, 85);
    }
}

static void style_text(lv_obj_t *obj, lv_color_t color, int32_t size)
{
    lv_obj_set_style_text_color(obj, color, 0);
    if (size >= 44) {
        lv_obj_set_style_text_font(obj, &lv_font_montserrat_44, 0);
    } else if (size >= 34) {
        lv_obj_set_style_text_font(obj, &lv_font_montserrat_36, 0);
    } else if (size >= 26) {
        lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, 0);
    } else if (size >= 22) {
        lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, 0);
    } else if (size >= 18) {
        lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, 0);
    } else if (size >= 16) {
        lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, 0);
    } else {
        lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, 0);
    }
}

static lv_obj_t *panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, s_page == PAGE_RUNNING ? color_running_panel() : color_panel(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x223149), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *text(lv_obj_t *parent, const char *value, int32_t x, int32_t y,
                      int32_t w, int32_t h, lv_color_t color, int32_t size)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, value);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    bool has_utf8 = false;
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p) {
        if (*p >= 0x80) { has_utf8 = true; break; }
    }
    if (has_utf8) {
        lv_obj_set_style_text_font(label, &lv_font_ui_cjk_20, 0);
        lv_obj_set_style_text_color(label, color, 0);
    } else {
        style_text(label, color, size);
    }
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    return label;
}

static void header(lv_obj_t *screen, const char *title)
{
    char display_clock[8];
    int hour = 0;
    int minute = 0;
    if (sscanf(s_status.clock, "%d:%d", &hour, &minute) == 2) {
        hour = (hour + s_time_offset_hours + 24) % 24;
        snprintf(display_clock, sizeof(display_clock), "%02d:%02d", hour, minute);
    } else {
        strlcpy(display_clock, "--:--", sizeof(display_clock));
    }
    lv_obj_t *wifi_dot = lv_obj_create(screen);
    lv_obj_remove_style_all(wifi_dot);
    lv_obj_set_pos(wifi_dot, 44, 35);
    lv_obj_set_size(wifi_dot, 6, 6);
    lv_obj_set_style_bg_color(wifi_dot, color_text(), 0);
    lv_obj_set_style_bg_opa(wifi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(wifi_dot, LV_RADIUS_CIRCLE, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *wave = lv_obj_create(screen);
        lv_obj_remove_style_all(wave);
        lv_obj_set_pos(wave, 40 - i * 3, 32 - i * 4);
        lv_obj_set_size(wave, 14 + i * 6, 2);
        lv_obj_set_style_bg_color(wave, color_text(), 0);
        lv_obj_set_style_bg_opa(wave, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(wave, LV_RADIUS_CIRCLE, 0);
    }
    lv_obj_t *title_obj = text(screen, title, 90, 17, 300, 30, color_text(), 18);
    lv_obj_set_style_text_align(title_obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *time_obj = text(screen, display_clock, 370, 17, 70, 30, color_text(), 18);
    lv_obj_set_style_text_align(time_obj, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *line = lv_obj_create(screen);
    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, 28, 56);
    lv_obj_set_size(line, 424, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x233044), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

static void format_update_age(char *buffer, size_t buffer_size)
{
    if (!s_status.last_update_ms) {
        strlcpy(buffer, "等待更新", buffer_size);
        return;
    }
    uint32_t now = esp_log_timestamp();
    uint32_t age = now >= s_status.last_update_ms ? (now - s_status.last_update_ms) / 1000 : 0;
    snprintf(buffer, buffer_size, "日志更新: %lus前", (unsigned long)age);
}

static void segmented_bar(lv_obj_t *parent, int32_t x, int32_t y, int32_t w,
                          int32_t h, int32_t percent, lv_color_t accent)
{
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_remove_style_all(track);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_size(track, w, h);
    lv_obj_set_style_bg_color(track, lv_color_hex(0x0F2930), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, 6, 0);
    const int32_t filled = (w * LV_CLAMP(percent, 0, 100)) / 100;
    const int32_t segments = 12;
    const int32_t gap = 3;
    const int32_t seg_w = (w - (segments - 1) * gap) / segments;
    const int32_t filled_segments = (segments * LV_CLAMP(percent, 0, 100) + 99) / 100;
    const int32_t sweep = (s_status.overall_state == CODEX_STATE_RUNNING && filled_segments > 0)
                              ? ((esp_log_timestamp() / 700) % filled_segments) : -1;
    for (int32_t i = 0; i < segments; i++) {
        lv_obj_t *seg = lv_obj_create(track);
        lv_obj_remove_style_all(seg);
        lv_obj_set_pos(seg, i * (seg_w + gap), 0);
        lv_obj_set_size(seg, seg_w, h);
        lv_color_t segment_color = (i * (seg_w + gap) < filled) ? accent : lv_color_hex(0x12333A);
        if (i == sweep) segment_color = color_cyan();
        lv_obj_set_style_bg_color(seg, segment_color, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(seg, 2, 0);
    }
}

static void divider(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x253246), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

static void app_icon(lv_obj_t *parent, int32_t x, int32_t y, lv_color_t accent)
{
    lv_obj_t *outer = lv_obj_create(parent);
    lv_obj_remove_style_all(outer);
    lv_obj_set_pos(outer, x, y);
    lv_obj_set_size(outer, 46, 46);
    lv_obj_set_style_bg_color(outer, lv_color_hex(0x14372D), 0);
    lv_obj_set_style_bg_opa(outer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(outer, 10, 0);
    lv_obj_set_style_border_width(outer, 1, 0);
    lv_obj_set_style_border_color(outer, accent, 0);
    lv_obj_t *inner = lv_obj_create(outer);
    lv_obj_remove_style_all(inner);
    lv_obj_set_pos(inner, 10, 9);
    lv_obj_set_size(inner, 26, 28);
    lv_obj_set_style_bg_color(inner, accent, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(inner, 6, 0);
    lv_obj_t *screen = lv_obj_create(inner);
    lv_obj_remove_style_all(screen);
    lv_obj_set_pos(screen, 5, 5);
    lv_obj_set_size(screen, 16, 12);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xD9FFE9), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(screen, 3, 0);
    lv_obj_t *indicator = lv_obj_create(inner);
    lv_obj_remove_style_all(indicator);
    lv_obj_set_pos(indicator, 8, 21);
    lv_obj_set_size(indicator, 10, 3);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(0x0D6B46), 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(indicator, 2, 0);
}

static void hourglass_icon(lv_obj_t *parent, int32_t x, int32_t y, lv_color_t accent)
{
    lv_obj_t *top = lv_obj_create(parent);
    lv_obj_remove_style_all(top);
    lv_obj_set_pos(top, x + 5, y);
    lv_obj_set_size(top, 38, 4);
    lv_obj_set_style_bg_color(top, accent, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(top, 2, 0);
    lv_obj_t *bottom = lv_obj_create(parent);
    lv_obj_remove_style_all(bottom);
    lv_obj_set_pos(bottom, x + 5, y + 46);
    lv_obj_set_size(bottom, 38, 4);
    lv_obj_set_style_bg_color(bottom, accent, 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bottom, 2, 0);
    lv_obj_t *neck = lv_obj_create(parent);
    lv_obj_remove_style_all(neck);
    lv_obj_set_pos(neck, x + 20, y + 12);
    lv_obj_set_size(neck, 8, 26);
    lv_obj_set_style_bg_color(neck, accent, 0);
    lv_obj_set_style_bg_opa(neck, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(neck, 3, 0);
    lv_obj_t *sand_top = lv_obj_create(parent);
    lv_obj_remove_style_all(sand_top);
    lv_obj_set_pos(sand_top, x + 16, y + 12);
    lv_obj_set_size(sand_top, 16, 7);
    lv_obj_set_style_bg_color(sand_top, accent, 0);
    lv_obj_set_style_bg_opa(sand_top, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sand_top, 4, 0);
    lv_obj_t *sand_bottom = lv_obj_create(parent);
    lv_obj_remove_style_all(sand_bottom);
    lv_obj_set_pos(sand_bottom, x + 16, y + 31);
    lv_obj_set_size(sand_bottom, 16, 7);
    lv_obj_set_style_bg_color(sand_bottom, accent, 0);
    lv_obj_set_style_bg_opa(sand_bottom, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sand_bottom, 4, 0);
}

static void metric_icon(lv_obj_t *parent, int32_t x, int32_t y, lv_color_t accent, metric_kind_t kind)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, 32, 32);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x10263A), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, accent, 0);
    lv_obj_set_style_radius(box, 8, 0);
    if (kind == METRIC_CPU) {
        lv_obj_t *core = lv_obj_create(box);
        lv_obj_remove_style_all(core);
        lv_obj_set_size(core, 12, 12);
        lv_obj_center(core);
        lv_obj_set_style_bg_color(core, accent, 0);
        lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(core, 3, 0);
        for (int i = 0; i < 3; i++) {
            lv_obj_t *pin_top = lv_obj_create(box);
            lv_obj_remove_style_all(pin_top);
            lv_obj_set_pos(pin_top, 7 + i * 6, 2);
            lv_obj_set_size(pin_top, 3, 5);
            lv_obj_set_style_bg_color(pin_top, accent, 0);
            lv_obj_set_style_bg_opa(pin_top, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(pin_top, 1, 0);
            lv_obj_t *pin_bottom = lv_obj_create(box);
            lv_obj_remove_style_all(pin_bottom);
            lv_obj_set_pos(pin_bottom, 7 + i * 6, 25);
            lv_obj_set_size(pin_bottom, 3, 5);
            lv_obj_set_style_bg_color(pin_bottom, accent, 0);
            lv_obj_set_style_bg_opa(pin_bottom, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(pin_bottom, 1, 0);
        }
    } else if (kind == METRIC_RAM) {
        for (int i = 0; i < 3; i++) {
            lv_obj_t *slot = lv_obj_create(box);
            lv_obj_remove_style_all(slot);
            lv_obj_set_pos(slot, 7, 7 + i * 7);
            lv_obj_set_size(slot, 18, 3);
            lv_obj_set_style_bg_color(slot, accent, 0);
            lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(slot, 1, 0);
        }
    } else {
        lv_obj_t *core = lv_obj_create(box);
        lv_obj_remove_style_all(core);
        lv_obj_set_size(core, 8, 8);
        lv_obj_center(core);
        lv_obj_set_style_bg_color(core, accent, 0);
        lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
        const int32_t points[][2] = {{7, 7}, {22, 7}, {7, 22}, {22, 22}};
        for (int i = 0; i < 4; i++) {
            lv_obj_t *fan = lv_obj_create(box);
            lv_obj_remove_style_all(fan);
            lv_obj_set_pos(fan, points[i][0], points[i][1]);
            lv_obj_set_size(fan, 4, 4);
            lv_obj_set_style_bg_color(fan, accent, 0);
            lv_obj_set_style_bg_opa(fan, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(fan, LV_RADIUS_CIRCLE, 0);
        }
    }
}

static void status_icon(lv_obj_t *parent, int32_t x, int32_t y, lv_color_t accent, const char *symbol)
{
    lv_obj_t *outer = lv_obj_create(parent);
    lv_obj_remove_style_all(outer);
    lv_obj_set_pos(outer, x, y);
    lv_obj_set_size(outer, 58, 58);
    lv_obj_set_style_bg_color(outer, lv_color_hex(0x10313B), 0);
    lv_obj_set_style_bg_opa(outer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(outer, 2, 0);
    lv_obj_set_style_border_color(outer, accent, 0);
    lv_obj_set_style_shadow_width(outer, 16, 0);
    lv_obj_set_style_shadow_color(outer, accent, 0);
    lv_obj_t *inner = lv_obj_create(outer);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, 30, 30);
    lv_obj_center(inner);
    lv_obj_set_style_bg_color(inner, accent, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    if (s_status.overall_state == CODEX_STATE_RUNNING) {
        lv_anim_t pulse;
        lv_anim_init(&pulse);
        lv_anim_set_var(&pulse, inner);
        lv_anim_set_exec_cb(&pulse, pulse_opa_cb);
        lv_anim_set_values(&pulse, LV_OPA_50, LV_OPA_COVER);
        lv_anim_set_duration(&pulse, 700);
        lv_anim_set_playback_duration(&pulse, 700);
        lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&pulse);
    }
    lv_obj_t *symbol_obj = text(outer, symbol, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT, color_bg(), 18);
    lv_obj_set_style_pad_all(symbol_obj, 0, 0);
    lv_obj_set_style_text_align(symbol_obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(symbol_obj);
}

static void mini_status_icon(lv_obj_t *parent, int32_t x, int32_t y, lv_color_t accent, const char *symbol)
{
    lv_obj_t *outer = lv_obj_create(parent);
    lv_obj_remove_style_all(outer);
    lv_obj_set_pos(outer, x, y);
    lv_obj_set_size(outer, 28, 28);
    lv_obj_set_style_bg_color(outer, lv_color_hex(0x10263A), 0);
    lv_obj_set_style_bg_opa(outer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(outer, 1, 0);
    lv_obj_set_style_border_color(outer, accent, 0);
    if (symbol[0] == '.' && symbol[1] == '\0') {
        lv_obj_t *dot = lv_obj_create(outer);
        lv_obj_remove_style_all(dot);
        lv_obj_set_pos(dot, 10, 10);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_bg_color(dot, accent, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    } else {
        lv_obj_t *symbol_obj = text(outer, symbol, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT, accent, 16);
        lv_obj_set_style_pad_all(symbol_obj, 0, 0);
        lv_obj_set_style_text_align(symbol_obj, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(symbol_obj);
    }
}

static void render_page(void);

static void pulse_opa_cb(void *var, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)value, 0);
}

static void render_page_async(void *user_data)
{
    (void)user_data;
    s_render_queued = false;
    if (!s_dashboard_active) return;
    render_page();
}

static void swipe_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    if (!indev) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_GESTURE) {
        const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
            s_auto_demo = false;
            s_manual_page = true;
            s_gesture_handled = true;
            s_page = next_available_page(dir == LV_DIR_LEFT ? 1 : -1);
            schedule_render();
        }
    } else if (code == LV_EVENT_PRESSED) {
        s_touch_start = point;
        s_touch_tracking = true;
        s_gesture_handled = false;
    } else if (code == LV_EVENT_RELEASED && s_touch_tracking) {
        s_touch_tracking = false;
        if (s_gesture_handled) {
            return;
        }
        const int32_t dx = point.x - s_touch_start.x;
        const int32_t dy = point.y - s_touch_start.y;
        if (LV_ABS(dx) > 55 && LV_ABS(dx) > LV_ABS(dy)) {
            s_auto_demo = false;
            s_manual_page = true;
            if (dx < 0) {
                s_page = next_available_page(1);
            } else {
                s_page = next_available_page(-1);
            }
            schedule_render();
        }
    }
}

static void nav_event_cb(lv_event_t *event)
{
    status_page_t requested = (status_page_t)(uintptr_t)lv_event_get_user_data(event);
    if (!page_available(requested)) return;
    s_auto_demo = false;
    s_manual_page = true;
    s_page = requested;
    schedule_render();
}

static void nav_button(lv_obj_t *screen, const char *label, int32_t x, status_page_t page)
{
    lv_obj_t *button = lv_button_create(screen);
    lv_obj_set_pos(button, x, 420);
    lv_obj_set_size(button, 126, 38);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x111C2C), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x2A3B55), 0);
    lv_obj_add_event_cb(button, nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)page);
    lv_obj_t *label_obj = lv_label_create(button);
    lv_label_set_text(label_obj, label);
    bool has_utf8 = false;
    for (const unsigned char *p = (const unsigned char *)label; *p != '\0'; ++p) {
        if (*p >= 0x80) { has_utf8 = true; break; }
    }
    if (has_utf8) lv_obj_set_style_text_font(label_obj, &lv_font_ui_cjk_20, 0);
    else style_text(label_obj, color_muted(), 16);
    lv_obj_center(label_obj);
}

static void setting_event_cb(lv_event_t *event)
{
    const int setting = (int)(intptr_t)lv_event_get_user_data(event);
    if (setting == 0) {
        s_status.message[0] = '\0';
        if (!s_wifi_initialized) {
            wifi_init();
            start_wifi_provisioning();
        } else if (!s_wifi_connected) {
            start_wifi_provisioning();
        } else {
            esp_wifi_disconnect();
            esp_wifi_connect();
        }
        strlcpy(s_status.message, "正在重新连接 WiFi", sizeof(s_status.message));
    } else if (setting == 1) {
        if (s_wifi_connected) {
            s_bridge_discovery_started = true;
            xTaskCreate(discover_bridge_task, "bridge_discovery", 4096, NULL, 3, NULL);
        }
        strlcpy(s_status.message, "正在刷新电脑端连接", sizeof(s_status.message));
    } else if (setting == 2) {
        s_alt_theme = !s_alt_theme;
    } else if (setting == 3) {
        s_sound_enabled = !s_sound_enabled;
    } else if (setting == 4) {
        static const int levels[] = {25, 50, 75, 100};
        int next = 0;
        for (int i = 0; i < 4; i++) {
            if (s_brightness == levels[i]) {
                next = (i + 1) % 4;
                break;
            }
        }
        s_brightness = levels[next];
        bsp_display_brightness_set(s_brightness);
    } else if (setting == 5) {
        s_time_offset_hours++;
        if (s_time_offset_hours > 2) s_time_offset_hours = -1;
    } else {
        return;
    }
    schedule_render();
}

static const char *recent_state_text(codex_state_t state)
{
    switch (state) {
    case CODEX_STATE_RUNNING: return "RUNNING";
    case CODEX_STATE_DONE: return "DONE";
    case CODEX_STATE_ERROR: return "ERROR";
    case CODEX_STATE_WAITING: return "WAITING";
    case CODEX_STATE_OFFLINE: return "OFFLINE";
    default: return "IDLE";
    }
}

static lv_color_t recent_state_color(codex_state_t state)
{
    if (state == CODEX_STATE_ERROR) return color_red();
    if (state == CODEX_STATE_WAITING) return color_amber();
    if (state == CODEX_STATE_RUNNING) return color_amber();
    if (state == CODEX_STATE_OFFLINE) return color_muted();
    if (state == CODEX_STATE_DONE) return color_green();
    return color_cyan();
}

static const char *recent_state_symbol(codex_state_t state)
{
    if (state == CODEX_STATE_ERROR) return "X";
    if (state == CODEX_STATE_DONE) return "V";
    if (state == CODEX_STATE_WAITING) return "!";
    return ".";
}

static void render_page(void)
{
    lv_obj_t *screen = lv_screen_active();
    char elapsed_text[32];
    char update_age[32];
    format_elapsed(s_status.overall_elapsed_s, elapsed_text, sizeof(elapsed_text));
    format_update_age(update_age, sizeof(update_age));
    if (s_page != s_last_page) {
        play_alert(s_page);
        s_last_page = s_page;
    }
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, s_page == PAGE_RUNNING ? color_running_bg() : color_bg(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    if (s_page == PAGE_HOME) {
        header(screen, "Agent Dashboard");
        lv_color_t overall_color = recent_state_color(s_status.overall_state);
        lv_obj_t *card = panel(screen, 24, 72, 432, 304);
        app_icon(card, 16, 18, overall_color);
        text(card, "Codex", 84, 18, 180, 28, color_text(), 26);
        text(card, state_label(s_status.overall_state), 84, 52, 160, 22, overall_color, 16);
        text(card, elapsed_text, 300, 22, 110, 28, color_text(), 18);
        text(card, "Codex总时长", 292, 54, 120, 20, color_muted(), 16);
        divider(card, 14, 82, 404);
        text(card, "整体项目进度", 14, 96, 160, 22, color_muted(), 16);
        segmented_bar(card, 14, 124, 300, 12, s_status.overall_progress, overall_color);
        char progress_text[12];
        snprintf(progress_text, sizeof(progress_text), "%d%%", s_status.overall_progress);
        text(card, progress_text, 348, 116, 58, 28, color_text(), 18);
        divider(card, 14, 148, 404);
        metric_icon(card, 14, 162, color_cyan(), METRIC_CPU);
        metric_icon(card, 142, 162, color_cyan(), METRIC_RAM);
        metric_icon(card, 270, 162, color_cyan(), METRIC_GPU);
        char cpu_text[8];
        char ram_text[8];
        char gpu_text[8];
        snprintf(cpu_text, sizeof(cpu_text), "%d%%", s_status.cpu);
        snprintf(ram_text, sizeof(ram_text), "%d%%", s_status.ram);
        snprintf(gpu_text, sizeof(gpu_text), "%d%%", s_status.gpu);
        text(card, "CPU", 56, 158, 74, 20, color_muted(), 14);
        text(card, cpu_text, 56, 180, 74, 28, color_text(), 18);
        text(card, "RAM", 184, 158, 74, 20, color_muted(), 14);
        text(card, ram_text, 184, 180, 74, 28, color_text(), 18);
        text(card, "GPU", 312, 158, 74, 20, color_muted(), 14);
        text(card, gpu_text, 312, 180, 74, 28, color_text(), 18);
        divider(card, 14, 220, 404);
        text(card, "额度剩余", 14, 228, 90, 24, color_muted(), 16);
        text(card, s_status.quota_remaining[0] ? s_status.quota_remaining : "--", 108, 228, 82, 24, color_text(), 16);
        text(card, "下次重置", 220, 228, 90, 24, color_muted(), 16);
        text(card, s_status.reset_date[0] ? s_status.reset_date : "--", 320, 228, 86, 24, color_amber(), 16);
        divider(card, 14, 260, 404);
        char task_count_text[16];
        snprintf(task_count_text, sizeof(task_count_text), "%d", s_status.overall_task_count);
        text(card, "任务数", 14, 270, 72, 22, color_muted(), 16);
        text(card, task_count_text, 84, 270, 44, 22, color_text(), 16);
        text(card, update_age, 190, 270, 216, 22, color_muted(), 14);
        lv_obj_t *online_dot = lv_obj_create(screen);
        lv_obj_remove_style_all(online_dot);
        lv_obj_set_pos(online_dot, 24, 386);
        lv_obj_set_size(online_dot, 10, 10);
        lv_color_t link_color = s_status.connected ? color_green() : color_muted();
        lv_obj_set_style_bg_color(online_dot, link_color, 0);
        lv_obj_set_style_bg_opa(online_dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(online_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_t *online_label = text(screen, s_status.connected ? "PC Online" : "PC Offline", 40, 378, 140, LV_SIZE_CONTENT, link_color, 16);
        lv_obj_set_style_pad_all(online_label, 0, 0);
        lv_obj_align_to(online_label, online_dot, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
        text(screen, "自动发现桥接", 292, 378, 164, 24, color_muted(), 16);
        nav_button(screen, "状态", 24, PAGE_HOME);
        nav_button(screen, "监控", 177, PAGE_RUNNING);
        nav_button(screen, "设置", 330, PAGE_SETTINGS);
        return;
    }

    if (s_page == PAGE_RUNNING) {
        header(screen, "Codex");
        lv_obj_t *card = panel(screen, 24, 72, 432, 304);
        lv_color_t overall_color = recent_state_color(s_status.overall_state);
        status_icon(card, 16, 14, overall_color, recent_state_symbol(s_status.overall_state));
        lv_obj_t *running_label = text(card, recent_state_text(s_status.overall_state), 86, 22, 250, 36, overall_color, 36);
        if (s_status.overall_state == CODEX_STATE_RUNNING) {
            lv_anim_t pulse_text;
            lv_anim_init(&pulse_text);
            lv_anim_set_var(&pulse_text, running_label);
            lv_anim_set_exec_cb(&pulse_text, pulse_opa_cb);
            lv_anim_set_values(&pulse_text, LV_OPA_70, LV_OPA_COVER);
            lv_anim_set_duration(&pulse_text, 700);
            lv_anim_set_playback_duration(&pulse_text, 700);
            lv_anim_set_repeat_count(&pulse_text, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&pulse_text);
        }
        text(card, "最近任务", 86, 60, 250, 24, color_text(), 16);
        divider(card, 14, 96, 404);
        int32_t task_y = 104;
        int shown_tasks = 0;
        for (int i = 0; i < s_status.recent_task_count && shown_tasks < 4; i++) {
            lv_color_t task_color = recent_state_color(s_status.recent_task_states[i]);
            lv_obj_t *task_row = lv_obj_create(card);
            lv_obj_remove_style_all(task_row);
            lv_obj_set_pos(task_row, 10, task_y);
            lv_obj_set_size(task_row, 404, 46);
            lv_obj_set_style_bg_color(task_row, lv_color_hex(0x101A28), 0);
            lv_obj_set_style_bg_opa(task_row, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(task_row, 7, 0);
            mini_status_icon(task_row, 6, 4, task_color, recent_state_symbol(s_status.recent_task_states[i]));
            lv_obj_t *task_name = text(task_row, s_status.recent_task_names[i], 48, 1, 228, 22, color_text(), 18);
            lv_label_set_long_mode(task_name, LV_LABEL_LONG_DOT);
            lv_obj_t *task_state = text(task_row, recent_state_text(s_status.recent_task_states[i]), 282, 1, 88, 22, task_color, 16);
            lv_label_set_long_mode(task_state, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_align(task_state, LV_TEXT_ALIGN_RIGHT, 0);
            char task_elapsed[32];
            format_elapsed(s_status.recent_task_elapsed_s[i], task_elapsed, sizeof(task_elapsed));
            text(task_row, task_elapsed, 48, 24, 96, 18, color_muted(), 14);
            lv_obj_t *task_track = lv_obj_create(task_row);
            lv_obj_remove_style_all(task_track);
            lv_obj_set_pos(task_track, 150, 35);
            lv_obj_set_size(task_track, 224, 5);
            lv_obj_set_style_bg_color(task_track, lv_color_hex(0x1B1A12), 0);
            lv_obj_set_style_bg_opa(task_track, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(task_track, 2, 0);
            lv_obj_t *task_fill = lv_obj_create(task_track);
            lv_obj_remove_style_all(task_fill);
            lv_obj_set_pos(task_fill, 0, 0);
            lv_obj_set_size(task_fill, (224 * s_status.recent_task_progress[i]) / 100, 5);
            lv_obj_set_style_bg_color(task_fill, task_color, 0);
            lv_obj_set_style_bg_opa(task_fill, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(task_fill, 2, 0);
            lv_obj_t *task_light = lv_obj_create(task_row);
            lv_obj_remove_style_all(task_light);
            lv_obj_set_pos(task_light, 382, 18);
            lv_obj_set_size(task_light, 10, 10);
            lv_obj_set_style_bg_color(task_light, task_color, 0);
            lv_obj_set_style_bg_opa(task_light, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(task_light, LV_RADIUS_CIRCLE, 0);
            task_y += 48;
            shown_tasks++;
        }
        if (!shown_tasks) {
            text(card, s_status.task[0] ? s_status.task : "等待电脑端任务", 14, 138, 360, 24, color_text(), 18);
        }
        text(screen, update_age, 24, 386, 190, 24, color_muted(), 14);
        char running_elapsed[48];
        snprintf(running_elapsed, sizeof(running_elapsed), "运行时长: %s", elapsed_text);
        text(screen, running_elapsed, 286, 386, 170, 24, color_muted(), 14);
        nav_button(screen, "状态", 24, PAGE_HOME);
        nav_button(screen, "监控", 177, PAGE_RUNNING);
        nav_button(screen, "设置", 330, PAGE_SETTINGS);
        return;
    }

    if (s_page == PAGE_DONE) {
        header(screen, "Codex");
        lv_obj_t *card = panel(screen, 24, 72, 432, 286);
        status_icon(card, 16, 14, color_green(), "+");
        text(card, "DONE", 86, 22, 250, 36, color_green(), 36);
        text(card, "任务已完成", 86, 60, 250, 24, color_green(), 16);
        divider(card, 14, 96, 404);
        text(card, "结果结论", 14, 116, 180, 24, color_text(), 18);
        lv_obj_t *summary_label = text(card, s_status.summary[0] ? s_status.summary : "任务已完成",
                                       14, 154, 400, 58, color_text(), 16);
        lv_label_set_long_mode(summary_label, LV_LABEL_LONG_WRAP);
        text(card, "结果已验证", 14, 224, 200, 24, color_muted(), 16);
        text(card, elapsed_text, 300, 224, 106, 24, color_text(), 16);
        nav_button(screen, "首页", 24, PAGE_HOME);
        nav_button(screen, "监控", 177, PAGE_RUNNING);
        nav_button(screen, "设置", 330, PAGE_SETTINGS);
        return;
    }

    if (s_page == PAGE_ATTENTION) {
        header(screen, "Codex");
        lv_obj_t *card = panel(screen, 24, 72, 432, 304);
        hourglass_icon(card, 16, 18, color_amber());
        text(card, "WAITING", 86, 22, 250, 36, color_amber(), 36);
        text(card, "需要你的确认", 86, 60, 250, 24, color_amber(), 16);
        divider(card, 14, 96, 404);
        text(card, "当前任务", 14, 110, 120, 22, color_muted(), 16);
        text(card, s_status.task[0] ? s_status.task : "执行下一步操作:", 14, 138, 320, 24, color_text(), 18);
        lv_obj_t *command = panel(card, 14, 170, 404, 54);
        text(command, s_status.message[0] ? s_status.message : "查看电脑上的请求", 16, 14, 330, 24, color_amber(), 16);
        text(card, "请在电脑端完成确认", 14, 244, 380, 24, color_amber(), 16);
        text(screen, update_age, 24, 386, 190, 24, color_muted(), 14);
        nav_button(screen, "状态", 24, PAGE_HOME);
        nav_button(screen, "监控", 177, PAGE_RUNNING);
        nav_button(screen, "设置", 330, PAGE_SETTINGS);
        return;
    }

    if (s_page == PAGE_ERROR) {
        header(screen, "Codex");
        lv_obj_t *card = panel(screen, 24, 72, 432, 304);
        status_icon(card, 16, 14, color_red(), "X");
        text(card, "ERROR", 86, 22, 250, 36, color_red(), 36);
        text(card, "任务执行失败", 86, 60, 250, 24, color_red(), 16);
        divider(card, 14, 96, 404);
        text(card, "当前任务", 14, 110, 120, 22, color_muted(), 16);
        text(card, s_status.task[0] ? s_status.task : "任务执行失败", 14, 138, 320, 24, color_text(), 18);
        text(card, "错误信息", 14, 178, 120, 22, color_muted(), 16);
        lv_obj_t *error_box = panel(card, 14, 206, 404, 72);
        text(error_box, "exit code 1", 16, 10, 370, 22, color_red(), 16);
        text(error_box, s_status.error[0] ? s_status.error : "未收到错误详情", 16, 38, 370, 22, color_red(), 12);
        text(screen, "错误详情已同步到电脑", 254, 386, 202, 24, color_red(), 14);
        nav_button(screen, "首页", 24, PAGE_HOME);
        nav_button(screen, "监控", 177, PAGE_RUNNING);
        nav_button(screen, "设置", 330, PAGE_SETTINGS);
        return;
    }

    if (s_page == PAGE_TASKS) {
        header(screen, "任务列表");
        const int task_count = s_status.recent_task_count > 0 ? s_status.recent_task_count : 1;
        for (int i = 0; i < task_count; i++) {
            lv_obj_t *row = panel(screen, 24, 72 + i * 60, 432, 52);
            const codex_state_t state = s_status.recent_task_count > 0
                                            ? s_status.recent_task_states[i] : CODEX_STATE_IDLE;
            const char *name = s_status.recent_task_count > 0
                                  ? s_status.recent_task_names[i] : "暂无最近任务";
            lv_color_t state_color = recent_state_color(state);
            mini_status_icon(row, 10, 12, state_color, recent_state_symbol(state));
            lv_obj_t *name_label = text(row, name, 50, 0, 270, LV_SIZE_CONTENT, color_text(), 16);
            lv_obj_set_style_pad_all(name_label, 0, 0);
            lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
            lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 50, 0);
            lv_obj_t *state_label = text(row, recent_state_text(state), 326, 0, 92, LV_SIZE_CONTENT, state_color, 16);
            lv_obj_set_style_pad_all(state_label, 0, 0);
            lv_label_set_long_mode(state_label, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(state_label, LV_ALIGN_RIGHT_MID, -14, 0);
        }
        text(screen, "自动更新: 2s", 24, 370, 180, 24, color_muted(), 14);
        nav_button(screen, "首页", 24, PAGE_HOME);
        nav_button(screen, "监控", 177, PAGE_RUNNING);
        nav_button(screen, "设置", 330, PAGE_SETTINGS);
        return;
    }

    if (s_page == PAGE_OFFLINE) {
        header(screen, "Codex");
        lv_obj_t *card = panel(screen, 24, 72, 432, 300);
        status_icon(card, 16, 14, color_muted(), "-");
        text(card, "OFFLINE", 86, 22, 250, 36, color_muted(), 36);
        text(card, "桥接不可用", 86, 60, 250, 24, color_muted(), 16);
        divider(card, 14, 96, 404);
        text(card, "正在重连", 14, 118, 200, 24, color_text(), 18);
        text(card, s_status.message[0] ? s_status.message : "请检查电脑端服务和 WiFi", 14, 158, 380, 24, color_muted(), 16);
        segmented_bar(card, 14, 208, 360, 12, 24, color_muted());
        nav_button(screen, "首页", 24, PAGE_HOME);
        nav_button(screen, "监控", 177, PAGE_RUNNING);
        nav_button(screen, "设置", 330, PAGE_SETTINGS);
        return;
    }

    header(screen, "设置");
    lv_obj_t *settings = panel(screen, 24, 72, 432, 320);
    char brightness_text[8];
    char time_text[12];
    snprintf(brightness_text, sizeof(brightness_text), "%d%%", s_brightness);
    if (s_time_offset_hours == 0) strlcpy(time_text, "电脑同步", sizeof(time_text));
    else snprintf(time_text, sizeof(time_text), "%+dh", s_time_offset_hours);
    const char *setting_names[] = {"WiFi 状态", "电脑端桥接", "主题颜色", "运行提示", "屏幕亮度", "时间调整"};
    const char *setting_values[] = {
        s_wifi_connected ? "已连接" : "未连接",
        s_status.connected ? "在线" : "离线",
        s_alt_theme ? "柔和黑" : "深色",
        s_sound_enabled ? "声音+画面" : "仅画面",
        brightness_text,
        time_text,
    };
    const char *setting_symbols[] = {"W", "S", "C", "!", "B", "T"};
    lv_color_t setting_colors[] = {color_cyan(), color_text(), color_amber(), color_amber(), color_cyan(), color_cyan()};
    for (int i = 0; i < 6; i++) {
        const int32_t y = 12 + i * 48;
        mini_status_icon(settings, 16, y - 1, setting_colors[i], setting_symbols[i]);
        text(settings, setting_names[i], 58, y, 190, 30, color_text(), 16);
        text(settings, setting_values[i], 274, y, 122, 30,
             (i == 0 && !s_wifi_connected) || (i == 1 && !s_status.connected) ? color_muted() : color_text(), 16);
        if (i < 5) divider(settings, 14, y + 34, 404);
        /* Create the transparent hit target last so labels/icons cannot steal
         * the touch event from the settings row. */
        lv_obj_t *row = lv_button_create(settings);
        lv_obj_set_pos(row, 8, y - 6);
        lv_obj_set_size(row, 416, 44);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_add_event_cb(row, setting_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    nav_button(screen, "首页", 24, PAGE_HOME);
        nav_button(screen, "监控", 177, PAGE_RUNNING);
    nav_button(screen, "状态", 330, PAGE_HOME);
}

void codex_dashboard_start(void)
{
    ESP_LOGI(TAG, "Starting Codex status UI v0.1");
    s_dashboard_active = true;
    if (s_dashboard_initialized) {
        render_page();
        return;
    }
    s_brightness = bsp_display_brightness_get();
    if (s_brightness <= 0) s_brightness = 100;
    audio_init();
    wifi_init();
    xTaskCreate(status_poll_task, "status_poll", 8192, NULL, 4, NULL);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, swipe_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen, swipe_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(screen, swipe_event_cb, LV_EVENT_RELEASED, NULL);
    lv_font_glyph_dsc_t probe_glyph;
    const bool probe_ok = lv_font_get_glyph_dsc(&lv_font_ui_cjk_20, &probe_glyph, 0x4E2D, 0);
    ESP_LOGI(TAG, "CJK font probe U+4E2D: %s, advance=%d, box=%dx%d",
             probe_ok ? "OK" : "MISSING", probe_glyph.adv_w, probe_glyph.box_w, probe_glyph.box_h);
    render_page();
    s_dashboard_initialized = true;
    ESP_LOGI(TAG, "UI ready: waiting for live status bridge");
}

void codex_dashboard_stop(void)
{
    /* Keep the bridge task alive, but stop it from touching the launcher while
     * the Codex app is not active. */
    s_dashboard_active = false;
}
