#include "M5Unified.h"

extern "C" {
#include "cJSON.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
}

#include <cstring>
#include <cstdio>
#include <string>
#include <sys/time.h>
#include <time.h>

#include "hermes_chan.h"

namespace {

constexpr char TAG[] = "hardware_buddy";
constexpr int WIFI_CONNECTED_BIT = BIT0;
constexpr int WIFI_FAIL_BIT = BIT1;
constexpr uint16_t COLOR_BG = 0x0000;
constexpr uint16_t COLOR_TEXT = 0xFFFF;
constexpr uint16_t COLOR_OK = 0x07E0;
constexpr uint16_t COLOR_WARN = 0xFD20;
constexpr uint16_t COLOR_ERR = 0xF800;
constexpr uint16_t COLOR_ACCENT = 0x051D;
constexpr uint16_t COLOR_PANEL = 0x1082;
constexpr uint16_t COLOR_SOFT = 0x7BEF;
constexpr size_t HTTP_RESPONSE_CAPACITY = 768;
constexpr int RESULT_DELAY_MS = 2200;
constexpr int MENU_COUNT = 4;
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int FRAME_INTERVAL_MS = 120;

struct ActionDef {
    const char *action;
    const char *label;
};

constexpr ActionDef kActions[MENU_COUNT] = {
    {"secretary.memo.capture", "MEMO"},
    {"secretary.reminder.quick_add", "REMIND"},
    {"secretary.task.next", "NEXT"},
    {"secretary.mode.home", "HOME"},
};

const char *action_display_name(const char *action)
{
    if (std::strcmp(action, "secretary.memo.capture") == 0) {
        return "めも";
    }
    if (std::strcmp(action, "secretary.reminder.quick_add") == 0) {
        return "りまいんど";
    }
    if (std::strcmp(action, "secretary.task.next") == 0) {
        return "つぎのこと";
    }
    if (std::strcmp(action, "secretary.mode.home") == 0) {
        return "おかえり";
    }
    return "しらべる";
}

struct HttpResponseBuffer {
    char data[HTTP_RESPONSE_CAPACITY] = {};
    int len = 0;
};

struct ActionResult {
    bool ok = false;
    int http_status = 0;
    std::string title;
    std::string message;
    std::string body;
};

enum class ViewMode : uint8_t {
    Home,
    Busy,
    Result,
};

static EventGroupHandle_t s_wifi_event_group = nullptr;
static bool s_wifi_connected = false;
static bool s_time_synced = false;
static int s_retry_num = 0;
static int s_request_id = 0;
static int s_selected_index = 0;
static int s_wifi_rssi = 0;
static ViewMode s_view_mode = ViewMode::Home;
static ActionResult s_last_result;
static uint32_t s_view_deadline_ms = 0;
static uint32_t s_last_frame_ms = 0;

int clamp_battery(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

int64_t get_timestamp_ms()
{
    struct timeval tv {};
    if (gettimeofday(&tv, nullptr) == 0 && tv.tv_sec > 1700000000) {
        return static_cast<int64_t>(tv.tv_sec) * 1000LL + (tv.tv_usec / 1000LL);
    }
    return esp_timer_get_time() / 1000LL;
}

void draw_multiline_text(int x, int y, const std::string &text, uint16_t color, int line_height, size_t max_chars)
{
    M5.Display.setTextColor(color, COLOR_BG);
    std::string remaining = text;
    int current_y = y;

    while (!remaining.empty()) {
        if (remaining.size() <= max_chars) {
            M5.Display.setCursor(x, current_y);
            M5.Display.print(remaining.c_str());
            break;
        }

        size_t split = remaining.rfind(' ', max_chars);
        if (split == std::string::npos || split == 0) {
            split = max_chars;
        }

        std::string line = remaining.substr(0, split);
        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }
        M5.Display.setCursor(x, current_y);
        M5.Display.print(line.c_str());

        remaining.erase(0, split);
        while (!remaining.empty() && remaining.front() == ' ') {
            remaining.erase(0, 1);
        }
        current_y += line_height;
    }
}

const char *home_status_text()
{
    if (!s_wifi_connected) {
        return "OFFLINE";
    }
    if (std::strlen(CONFIG_SECRETARY_WEBHOOK_URL) == 0) {
        return "URL?";
    }
    return "READY";
}

hermes_buddy::HermesChanMood current_home_mood()
{
    if (!s_wifi_connected) {
        return hermes_buddy::HermesChanMood::Sleep;
    }
    if (std::strlen(CONFIG_SECRETARY_WEBHOOK_URL) == 0) {
        return hermes_buddy::HermesChanMood::Attention;
    }
    return hermes_buddy::HermesChanMood::Idle;
}

void draw_top_bar(const char *title, const char *status, uint16_t status_color)
{
    M5.Display.fillRoundRect(6, 6, SCREEN_W - 12, 20, 8, COLOR_PANEL);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    M5.Display.setCursor(12, 12);
    M5.Display.print(title);

    M5.Display.setTextColor(status_color, COLOR_PANEL);
    M5.Display.setCursor(146, 12);
    M5.Display.print(status);
}

void draw_home_screen(uint32_t tick)
{
    M5.Display.fillScreen(COLOR_BG);
    draw_top_bar("Hermes-chan", home_status_text(), s_wifi_connected ? COLOR_OK : COLOR_ERR);

    hermes_buddy::draw_hermes_chan(58, 76, current_home_mood(), tick, false);

    M5.Display.drawRoundRect(112, 32, 118, 74, 10, COLOR_PANEL);
    M5.Display.setTextSize(1);
    for (int i = 0; i < MENU_COUNT; ++i) {
        const bool selected = (i == s_selected_index);
        const int y = 40 + (i * 16);
        if (selected) {
            M5.Display.fillRoundRect(118, y - 3, 106, 14, 6, COLOR_PANEL);
            M5.Display.drawRoundRect(118, y - 3, 106, 14, 6, COLOR_WARN);
        }
        M5.Display.setTextColor(selected ? COLOR_WARN : COLOR_TEXT, selected ? COLOR_PANEL : COLOR_BG);
        M5.Display.setCursor(124, y);
        M5.Display.printf("%c %s", selected ? '>' : ' ', kActions[i].label);
    }

    M5.Display.setTextColor(COLOR_SOFT, COLOR_BG);
    M5.Display.setCursor(112, 110);
    M5.Display.printf("WiFi %s", s_wifi_connected ? "GOOD" : "NG");
    M5.Display.setCursor(8, 116);
    M5.Display.print("A:えらぶ  B:おくる");
}

void draw_busy_screen(const char *label, uint32_t tick)
{
    M5.Display.fillScreen(COLOR_BG);
    draw_top_bar("Hermes-chan", "BUSY", COLOR_WARN);
    hermes_buddy::draw_hermes_chan(46, 76, hermes_buddy::HermesChanMood::Busy, tick, false);

    M5.Display.fillRoundRect(96, 34, 132, 66, 10, COLOR_PANEL);
    M5.Display.setTextColor(COLOR_WARN, COLOR_PANEL);
    M5.Display.setCursor(108, 42);
    M5.Display.print("おはなし中...");

    M5.Display.setTextColor(COLOR_TEXT, COLOR_PANEL);
    M5.Display.setCursor(108, 62);
    M5.Display.print(action_display_name(kActions[s_selected_index].action));
    M5.Display.setTextColor(COLOR_SOFT, COLOR_PANEL);
    M5.Display.setCursor(108, 82);
    M5.Display.print("ちょっとまってね");
}

void draw_result_screen(const ActionResult &result, uint32_t tick)
{
    M5.Display.fillScreen(COLOR_BG);
    const auto mood = result.ok
        ? (result.title == "HOME" ? hermes_buddy::HermesChanMood::Heart : hermes_buddy::HermesChanMood::Celebrate)
        : hermes_buddy::HermesChanMood::Error;
    draw_top_bar("Hermes-chan", result.ok ? "OK" : "NG", result.ok ? COLOR_OK : COLOR_ERR);
    hermes_buddy::draw_hermes_chan(42, 74, mood, tick, false);

    M5.Display.fillRoundRect(86, 28, 146, 80, 10, COLOR_PANEL);
    M5.Display.setTextColor(COLOR_ACCENT, COLOR_PANEL);
    M5.Display.setCursor(96, 36);
    if (!result.title.empty()) {
        M5.Display.print(result.title.c_str());
    } else {
        M5.Display.print(kActions[s_selected_index].label);
    }

    M5.Display.setTextColor(COLOR_TEXT, COLOR_PANEL);
    draw_multiline_text(96, 54, result.message, COLOR_TEXT, 14, 18);
    if (!result.body.empty()) {
        draw_multiline_text(96, 82, result.body, COLOR_SOFT, 12, 20);
    }
}

void render_current_view(uint32_t tick)
{
    switch (s_view_mode) {
    case ViewMode::Home:
        draw_home_screen(tick);
        break;
    case ViewMode::Busy:
        draw_busy_screen(kActions[s_selected_index].label, tick);
        break;
    case ViewMode::Result:
        draw_result_screen(s_last_result, tick);
        break;
    }
}

void sync_time_once()
{
    if (s_time_synced || !s_wifi_connected) {
        return;
    }

    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.nict.jp");
        esp_sntp_setservername(1, "pool.ntp.org");
        esp_sntp_init();
    }

    for (int i = 0; i < 10; ++i) {
        time_t now = 0;
        time(&now);
        if (now > 1700000000) {
            s_time_synced = true;
            ESP_LOGI(TAG, "Time sync complete");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGW(TAG, "Time sync not available yet, falling back to monotonic timestamp");
}

void update_wifi_metrics()
{
    if (s_wifi_event_group == nullptr) {
        s_wifi_connected = false;
        s_wifi_rssi = 0;
        return;
    }

    wifi_ap_record_t ap_info {};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_wifi_connected = true;
        s_wifi_rssi = ap_info.rssi;
        return;
    }

    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    s_wifi_connected = (bits & WIFI_CONNECTED_BIT) != 0;
    if (!s_wifi_connected) {
        s_wifi_rssi = 0;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        s_wifi_rssi = 0;
        if (s_retry_num < 10) {
            esp_wifi_connect();
            ++s_retry_num;
            ESP_LOGI(TAG, "Retrying Wi-Fi connection");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_num = 0;
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta()
{
    if (std::strlen(CONFIG_SECRETARY_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty; device will stay offline until configured");
        return;
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        nullptr,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        nullptr,
                                                        &instance_got_ip));

    wifi_config_t wifi_config {};
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), CONFIG_SECRETARY_WIFI_SSID,
                 sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password), CONFIG_SECRETARY_WIFI_PASSWORD,
                 sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGW(TAG, "Wi-Fi initial connection failed");
    } else {
        ESP_LOGW(TAG, "Wi-Fi initial connection timed out");
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    auto *buffer = static_cast<HttpResponseBuffer *>(evt->user_data);
    if (buffer == nullptr) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int remaining = static_cast<int>(HTTP_RESPONSE_CAPACITY - 1 - buffer->len);
        if (remaining > 0) {
            int to_copy = evt->data_len < remaining ? evt->data_len : remaining;
            std::memcpy(buffer->data + buffer->len, evt->data, to_copy);
            buffer->len += to_copy;
            buffer->data[buffer->len] = '\0';
        }
    }

    return ESP_OK;
}

ActionResult parse_response_body(const HttpResponseBuffer &buffer, int http_status)
{
    ActionResult result;
    result.http_status = http_status;

    if (buffer.len == 0) {
        result.ok = (http_status >= 200 && http_status < 300);
        result.message = result.ok ? "No response body" : ("HTTP " + std::to_string(http_status));
        return result;
    }

    cJSON *root = cJSON_Parse(buffer.data);
    if (root == nullptr) {
        result.ok = (http_status >= 200 && http_status < 300);
        result.message = result.ok ? "Non-JSON response" : ("HTTP " + std::to_string(http_status));
        result.body = buffer.data;
        return result;
    }

    cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
    cJSON *display = cJSON_GetObjectItemCaseSensitive(root, "display");

    result.ok = cJSON_IsBool(ok) ? cJSON_IsTrue(ok) : (http_status >= 200 && http_status < 300);
    result.message = cJSON_IsString(message) && message->valuestring ? message->valuestring : "No message";

    if (cJSON_IsObject(display)) {
        cJSON *title = cJSON_GetObjectItemCaseSensitive(display, "title");
        cJSON *body = cJSON_GetObjectItemCaseSensitive(display, "body");
        if (cJSON_IsString(title) && title->valuestring) {
            result.title = title->valuestring;
        }
        if (cJSON_IsString(body) && body->valuestring) {
            result.body = body->valuestring;
        }
    }

    cJSON_Delete(root);
    return result;
}

ActionResult send_selected_action()
{
    ActionResult result;
    const ActionDef &action = kActions[s_selected_index];
    result.title = action.label;

    if (std::strcmp(action.action, "secretary.mode.home") == 0 && std::strlen(CONFIG_SECRETARY_WEBHOOK_URL) == 0) {
        result.ok = true;
        result.message = "おかえりなさい";
        result.body = "エルメスちゃん待機中だよ";
        return result;
    }

    if (std::strlen(CONFIG_SECRETARY_WEBHOOK_URL) == 0) {
        result.message = "URL みつからないよ";
        result.body = "あとで設定してね";
        return result;
    }

    if (std::strlen(CONFIG_SECRETARY_WEBHOOK_TOKEN) < 32) {
        result.message = "認証が未設定だよ";
        result.body = "tokenを設定してね";
        return result;
    }

    const std::string webhook_url(CONFIG_SECRETARY_WEBHOOK_URL);
    if (webhook_url.find("/buddy/") != std::string::npos && webhook_url.find("/buddy/actions") == std::string::npos) {
        result.message = "URLを見直してね";
        result.body = "token入りURLは使わないよ";
        return result;
    }

    if (!s_wifi_connected) {
        result.message = "Wi-Fi まだだよ";
        result.body = "でんぱを確認してね";
        return result;
    }

    if (!s_time_synced) {
        result.message = "時刻あわせ中だよ";
        result.body = "もう少し待ってね";
        return result;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON *request = cJSON_AddObjectToObject(root, "request");
    cJSON *context = cJSON_AddObjectToObject(root, "context");

    cJSON_AddStringToObject(device, "id", CONFIG_SECRETARY_DEVICE_ID);
    cJSON_AddStringToObject(device, "type", "M5StickS3");
    cJSON_AddStringToObject(device, "firmware", CONFIG_SECRETARY_FIRMWARE_LABEL);

    ++s_request_id;
    char request_id[32];
    std::snprintf(request_id, sizeof(request_id), "%llu", static_cast<unsigned long long>(s_request_id));
    cJSON_AddStringToObject(request, "id", request_id);
    cJSON_AddNumberToObject(request, "timestamp", static_cast<double>(get_timestamp_ms() / 1000));
    cJSON_AddStringToObject(request, "action", action.action);

    cJSON_AddNumberToObject(context, "battery", clamp_battery(M5.Power.getBatteryLevel()));
    cJSON_AddNumberToObject(context, "wifi_rssi", s_wifi_rssi);

    char *json_body = cJSON_PrintUnformatted(root);
    if (json_body == nullptr) {
        cJSON_Delete(root);
        result.message = "じゅんびで失敗したよ";
        return result;
    }

    HttpResponseBuffer response_buffer;
    esp_http_client_config_t config = {};
    config.url = CONFIG_SECRETARY_WEBHOOK_URL;
    config.method = HTTP_METHOD_POST;
    config.event_handler = http_event_handler;
    config.user_data = &response_buffer;
    config.timeout_ms = CONFIG_SECRETARY_HTTP_TIMEOUT_MS;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    std::string auth_header = std::string("Bearer ") + CONFIG_SECRETARY_WEBHOOK_TOKEN;
    esp_http_client_set_header(client, "Authorization", auth_header.c_str());
    esp_http_client_set_post_field(client, json_body, std::strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        result.message = "つながらなかったよ";
        result.body = esp_err_to_name(err);
        esp_http_client_cleanup(client);
        cJSON_free(json_body);
        cJSON_Delete(root);
        return result;
    }

    int http_status = esp_http_client_get_status_code(client);
    result = parse_response_body(response_buffer, http_status);
    if (!result.ok && result.body.empty() && http_status > 0) {
        result.body = "HTTP " + std::to_string(http_status);
    }

    esp_http_client_cleanup(client);
    cJSON_free(json_body);
    cJSON_Delete(root);
    return result;
}

void init_device()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Power.begin();
    M5.Display.setRotation(3);
    M5.Display.setTextSize(2);
    M5.Display.setTextWrap(false);
    M5.Display.fillScreen(COLOR_BG);
    s_last_frame_ms = lgfx::v1::millis();
}

}  // namespace

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_device();
    wifi_init_sta();
    update_wifi_metrics();
    sync_time_once();
    render_current_view(lgfx::v1::millis());

    while (true) {
        M5.update();
        update_wifi_metrics();
        if (s_wifi_connected && !s_time_synced) {
            sync_time_once();
        }

        if (M5.BtnA.wasPressed() && s_view_mode == ViewMode::Home) {
            s_selected_index = (s_selected_index + 1) % MENU_COUNT;
            render_current_view(lgfx::v1::millis());
        }

        if (M5.BtnB.wasPressed() && s_view_mode == ViewMode::Home) {
            s_view_mode = ViewMode::Busy;
            render_current_view(lgfx::v1::millis());
            s_last_result = send_selected_action();
            s_view_mode = ViewMode::Result;
            s_view_deadline_ms = lgfx::v1::millis() + RESULT_DELAY_MS;
            render_current_view(lgfx::v1::millis());
        }

        const uint32_t now = lgfx::v1::millis();
        if (s_view_mode == ViewMode::Result && static_cast<int32_t>(now - s_view_deadline_ms) >= 0) {
            s_view_mode = ViewMode::Home;
            render_current_view(now);
        } else if (static_cast<int32_t>(now - s_last_frame_ms) >= FRAME_INTERVAL_MS) {
            s_last_frame_ms = now;
            render_current_view(now);
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
