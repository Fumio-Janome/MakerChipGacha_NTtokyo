// デバッグタスク用: 必要なヘッダ
#include <stdbool.h>
#include <string.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sntp.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <time.h>
#include "freertos/event_groups.h"

#include "common.h"
#include "wifi_config.h"
#include "web_config.h"

EventGroupHandle_t wifi_event_group = NULL;
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
        // ESP_LOGI(TAG, "WiFi切断、再接続試行");
        esp_wifi_connect();
    }
}

esp_err_t wifi_config_softap_start(void) {
    // AP+STA同時モードで両方の設定をセット
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_SOFTAP_SSID,
            .ssid_len = strlen(WIFI_SOFTAP_SSID),
            .channel = WIFI_SOFTAP_CHANNEL,
            .password = WIFI_SOFTAP_PASS,
            .max_connection = WIFI_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 0,
        },
    };
    wifi_config_t sta_config = {0};
    // NVSからSTA設定を取得
    nvs_handle_t nvs;
    char ssid[33] = {0};
    char pass[65] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);
    if (nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_str(nvs, WIFI_CONFIG_SSID_KEY, ssid, &ssid_len);
        nvs_get_str(nvs, WIFI_CONFIG_PASS_KEY, pass, &pass_len);
        nvs_close(nvs);
        strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, pass, sizeof(sta_config.sta.password));
    }
    wifi_country_t country_config = {
        .cc = "JP",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL
    };
    if (strlen(WIFI_SOFTAP_PASS) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
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
    // NVSからSTA設定を取得し、STA設定のみ上書き
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
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "WiFi STA接続開始: SSID=%s", ssid);
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi接続失敗: APのみモードへ切替");
        // STAをstopし、APのみモードへ
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_wifi_set_mode(WIFI_MODE_AP);
        ESP_LOGI(TAG, "APのみモードへ切替完了");
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
        // STA接続済みかどうかをチェック
        if (wifi_event_group == NULL ||
            (xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT) == 0) {
            ESP_LOGW(TAG, "STA未接続のためNTP取得スキップ");
            return ESP_FAIL;
        }

        static bool sntp_initialized = false;
        if (!sntp_initialized) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, NTP_SERVER_NAME);
            esp_sntp_init();
            // 日本時間（JST）にタイムゾーンを設定
            setenv("TZ", "JST-9", 1);
            tzset();
            sntp_initialized = true;
        }
        time_t now = 0;
        struct tm timeinfo = {0};
        int retry = 0;
        const int retry_count = 10;
        while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
            // ESP_LOGI(TAG, "NTP時刻待機中...");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            time(&now);
            localtime_r(&now, &timeinfo);
        }
        if (timeinfo.tm_year < (2016 - 1900)) {
            lcd_show_request(LCD_STATE_WIFI_NG); // LCDにWi-Fi接続失敗を表示
            ESP_LOGE(TAG, "NTP時刻取得失敗");
            return ESP_FAIL;
        }
        strftime(datetime_buf, buf_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
        // ESP_LOGI(TAG, "NTP時刻取得: %s", datetime_buf);
        return ESP_OK;
}

// NTPを定期的に取得し、最新時刻を保持するタスク
static char latest_datetime[32] = "";
static char latest_date[16] = "WIFI";
static char latest_time[16] = "NOT CONNECT";

void ntp_update_task(void *pvParameters) {
    time_t now = 0;
    struct tm timeinfo = {0};
    int sec;
    int delay_ms;
    while (1) {
        char buf[32] = "";

        delay_ms = 60000;
        if (wifi_config_get_ntp_time(buf, sizeof(buf)) == ESP_OK) {
            strncpy(latest_datetime, buf, sizeof(latest_datetime));
            // 年月日（YYYY-MM-DD）
            strncpy(latest_date, buf, 10);
            latest_date[10] = '\0';
            // 時分（HH:MM）
            if (strlen(buf) >= 16) {
                strncpy(latest_time, buf + 11, 5);
                latest_time[5] = '\0';
            } else {
                latest_time[0] = '\0';
            }
            lcd_show_request(LCD_STATE_DATE_TIME);  // LCDに日時指示を表示

            // 0秒±5秒の範囲なら60秒待機、それ以外は次の0秒-5秒まで待機
            time(&now);
            localtime_r(&now, &timeinfo);
            sec = timeinfo.tm_sec;
            if (sec > 5) {
                delay_ms = ((60 - sec) + 0) * 1000; // 次の0秒まで
                if (delay_ms < 0) delay_ms = 1000; // 念のため
            }
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// SoftAPのSSIDが消えた場合に自動復旧する監視タスク
static void softap_monitor_task(void *pvParameters) {
    // 必要なマクロ・定数が参照できるようにする
    #include "esp_err.h"
    static const char *softap_ssid = WIFI_SOFTAP_SSID;
    static const char *softap_pass = WIFI_SOFTAP_PASS;
    static const int softap_channel = 1;
    static const int softap_max_conn = 1;
    int empty_count = 0;
    while (1) {
        wifi_mode_t mode;
        wifi_config_t ap_cfg;
        esp_err_t err_mode = esp_wifi_get_mode(&mode);
        esp_err_t err_cfg = esp_wifi_get_config(WIFI_IF_AP, &ap_cfg);
        // ESP_LOGI(TAG, "[SoftAP監視] 取得: err_mode=%d, err_cfg=%d, mode=%d", err_mode, err_cfg, mode);
        if (err_mode == ESP_OK && err_cfg == ESP_OK) {
            // ESP_LOGI(TAG, "[SoftAP監視] 現在のSSID: '%s' (len=%d) channel=%d max_conn=%d authmode=%d hidden=%d", 
            //     (char*)ap_cfg.ap.ssid, ap_cfg.ap.ssid_len, ap_cfg.ap.channel, ap_cfg.ap.max_connection, ap_cfg.ap.authmode, ap_cfg.ap.ssid_hidden);
            // AP/STAモードでない場合のみ再設定
            if (!(mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)) {
                ESP_LOGW(TAG, "SoftAPモードでないため再設定 (mode=%d)", mode);
                esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
                ESP_LOGW(TAG, "esp_wifi_set_mode: %d", ret);
                if (ret == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    ret = esp_wifi_start();
                    ESP_LOGW(TAG, "esp_wifi_start: %d", ret);
                }
            } else if (ap_cfg.ap.ssid[0] == '\0') {
                empty_count++;
                ESP_LOGW(TAG, "SoftAP SSIDが空(%d回目)。ap_cfg.ap.ssid_len=%d", empty_count, ap_cfg.ap.ssid_len);
                // 3回連続で空なら再設定
                if (empty_count >= 3) {
                    ESP_LOGW(TAG, "SoftAP SSIDが3回連続で空。再設定");
                    wifi_config_t ap_config = {0};
                    strncpy((char*)ap_config.ap.ssid, softap_ssid, sizeof(ap_config.ap.ssid));
                    ap_config.ap.ssid_len = strlen(softap_ssid);
                    ap_config.ap.channel = softap_channel;
                    strncpy((char*)ap_config.ap.password, softap_pass, sizeof(ap_config.ap.password));
                    ap_config.ap.max_connection = softap_max_conn;
                    ap_config.ap.authmode = (strlen(softap_pass) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
                    ap_config.ap.ssid_hidden = 0;
                    // ESP_LOGI(TAG, "[SoftAP監視] 再設定: ssid='%s' len=%d channel=%d max_conn=%d authmode=%d hidden=%d", 
                    //     (char*)ap_config.ap.ssid, ap_config.ap.ssid_len, ap_config.ap.channel, ap_config.ap.max_connection, ap_config.ap.authmode, ap_config.ap.ssid_hidden);
                    esp_err_t ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
                    ESP_LOGW(TAG, "esp_wifi_set_config: %d", ret);
                    if (ret == ESP_OK) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        ret = esp_wifi_start();
                        ESP_LOGW(TAG, "esp_wifi_start: %d", ret);
                    }
                    empty_count = 0;
                }
            } else {
                // ESP_LOGI(TAG, "[SoftAP監視] SSIDは正常: '%s'", (char*)ap_cfg.ap.ssid);
                empty_count = 0;
            }
        } else {
            ESP_LOGW(TAG, "SoftAP情報取得失敗: mode=%d cfg=%d", err_mode, err_cfg);
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10秒ごとに監視
    }
}

// 年月日（YYYY-MM-DD）を取得
void get_latest_date(char *buf, size_t len) {
    strncpy(buf, latest_date, len);
    buf[len-1] = '\0';
}
// 時分（HH:MM）を取得
void get_latest_time(char *buf, size_t len) {
    strncpy(buf, latest_time, len);
    buf[len-1] = '\0';
}

// Wi-Fiセットアップ・NTP取得を別タスクで実行する関数
static void wifi_setup_task(void *pvParameters) {
    // イベントグループ初期化（初回のみ）
    if (wifi_event_group == NULL) {
        wifi_event_group = xEventGroupCreate();
        if (wifi_event_group == NULL) {
            ESP_LOGE(TAG, "wifi_event_group作成失敗");
            vTaskDelete(NULL);
            return;
        }
    }
    ESP_LOGI(TAG, "SoftAP+Webサーバ起動");
    wifi_config_softap_start();
    start_wifi_config_server();

    // SoftAP監視タスク起動
    xTaskCreate(softap_monitor_task, "softap_monitor", 4096, NULL, 2, NULL);
    // // SoftAP自己スキャンデバッグタスク起動
    // xTaskCreate(softap_selfscan_task, "softap_selfscan", 4096, NULL, 1, NULL);

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
    lcd_show_request(LCD_STATE_INSERT);     // LCDに挿入指示を表示
    lcd_show_request(LCD_STATE_WIFI_WAIT);  // LCDにWi-Fi接続待ちを表示

    xTaskCreate(ntp_update_task, "ntp_update", 2048, NULL, 3, NULL);
    vTaskDelete(NULL);
}

// main.cから呼び出す用
void start_wifi_setup_task(void) {
    xTaskCreate(wifi_setup_task, "wifi_setup", 4096, NULL, 6, NULL);
}
