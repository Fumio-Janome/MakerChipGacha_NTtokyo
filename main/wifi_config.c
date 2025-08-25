
// 必要なヘッダを整理し、TAGはcommon.hのものを利用
#include "common.h"
#include "wifi_config.h"
#include "web_config.h"
#include <string.h>
#include <stddef.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

// Wi-Fiセットアップ・NTP取得を別タスクで実行する関数
static void wifi_setup_task(void *pvParameters) {
    ESP_LOGI(TAG, "SoftAP+Webサーバ起動");
    wifi_config_softap_start();
    start_wifi_config_server();

    // SSID/PASSが保存されていればSTA接続＆NTP取得
    vTaskDelay(pdMS_TO_TICKS(5000)); // Web設定待ち猶予（5秒）
    if (wifi_config_sta_connect() == ESP_OK) {
        char ip[16] = {0};
        if (wifi_config_get_ip(ip, sizeof(ip)) == ESP_OK) {
            ESP_LOGI(TAG, "IPアドレス: %s", ip);
        }
        char datetime[32] = {0};
        if (wifi_config_get_ntp_time(datetime, sizeof(datetime)) == ESP_OK) {
            ESP_LOGI(TAG, "NTP時刻: %s", datetime);
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi STA接続失敗。Webで設定してください");
    }
    vTaskDelete(NULL);
}

// main.cから呼び出す用
void start_wifi_setup_task(void) {
    xTaskCreate(wifi_setup_task, "wifi_setup", 4096, NULL, 6, NULL);
}

#include "esp_sntp.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <time.h>

#define WIFI_CONFIG_NAMESPACE "wifi_cfg"
#define WIFI_CONFIG_SSID_KEY "ssid"
#define WIFI_CONFIG_PASS_KEY "pass"
#define WIFI_SOFTAP_SSID "ESP32_SETUP"
#define WIFI_SOFTAP_PASS "12345678"
#define WIFI_SOFTAP_CHANNEL 1
#define WIFI_MAX_STA_CONN 1

// TAGはcommon.hで定義済み
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi STA接続完了");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "IPアドレス取得");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi切断、再接続試行");
        esp_wifi_connect();
    }
}

esp_err_t wifi_config_softap_start(void) {
    // NVS, esp_netif, APインターフェースの初期化はapp_mainで一度だけ行う
        wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_SOFTAP_SSID,
            .ssid_len = strlen(WIFI_SOFTAP_SSID),
            .channel = WIFI_SOFTAP_CHANNEL,
            .password = WIFI_SOFTAP_PASS,
            .max_connection = WIFI_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 0, // SSIDをブロードキャスト

        },
    };
    wifi_country_t country_config = {
        .cc = "JP",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL
    };
    if (strlen(WIFI_SOFTAP_PASS) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_country(&country_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // SoftAP情報の取得と表示
    wifi_config_t current_ap;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_AP, &current_ap));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    esp_netif_ip_info_t ip_info;
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
        esp_wifi_set_max_tx_power(78);

        ESP_LOGI(TAG, "SoftAP開始情報:");
        ESP_LOGI(TAG, "  SSID: %s", (char*)current_ap.ap.ssid);
        ESP_LOGI(TAG, "  PASS: %s", (char*)current_ap.ap.password);
        ESP_LOGI(TAG, "  チャンネル: %d", current_ap.ap.channel);
        ESP_LOGI(TAG, "  最大接続数: %d", current_ap.ap.max_connection);
        ESP_LOGI(TAG, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "  IP: %s", ip4addr_ntoa((const ip4_addr_t*)&ip_info.ip));
    } else {
        ESP_LOGI(TAG, "SoftAP開始 SSID:%s PASS:%s", WIFI_SOFTAP_SSID, WIFI_SOFTAP_PASS);
    }
    return ESP_OK;
}

esp_err_t wifi_config_save(const char* ssid, const char* password) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    nvs_set_str(nvs, WIFI_CONFIG_SSID_KEY, ssid);
    nvs_set_str(nvs, WIFI_CONFIG_PASS_KEY, password);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "WiFi設定保存: SSID=%s", ssid);
    return ESP_OK;
}

esp_err_t wifi_config_load(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    char ssid[33] = {0};
    char pass[65] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);
    nvs_get_str(nvs, WIFI_CONFIG_SSID_KEY, ssid, &ssid_len);
    nvs_get_str(nvs, WIFI_CONFIG_PASS_KEY, pass, &pass_len);
    nvs_close(nvs);
    ESP_LOGI(TAG, "WiFi設定読込: SSID=%s", ssid);
    return ESP_OK;
}

esp_err_t wifi_config_sta_connect(void) {
    wifi_event_group = xEventGroupCreate();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    nvs_handle_t nvs;
    char ssid[33] = {0};
    char pass[65] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);
    if (nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_str(nvs, WIFI_CONFIG_SSID_KEY, ssid, &ssid_len);
        nvs_get_str(nvs, WIFI_CONFIG_PASS_KEY, pass, &pass_len);
        nvs_close(nvs);
    } else {
        ESP_LOGE(TAG, "WiFi設定が保存されていません");
        return ESP_FAIL;
    }
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi STA接続開始: SSID=%s", ssid);
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi接続失敗");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wifi_config_get_ip(char* ip_buf, size_t buf_len) {
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return ESP_FAIL;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return ESP_FAIL;
    strncpy(ip_buf, ip4addr_ntoa((const ip4_addr_t*)&ip_info.ip), buf_len);
    return ESP_OK;
}

esp_err_t wifi_config_get_ntp_time(char* datetime_buf, size_t buf_len) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.nict.jp");
    esp_sntp_init();
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "NTP時刻待機中...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGE(TAG, "NTP時刻取得失敗");
        return ESP_FAIL;
    }
    strftime(datetime_buf, buf_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "NTP時刻取得: %s", datetime_buf);
    return ESP_OK;
}
