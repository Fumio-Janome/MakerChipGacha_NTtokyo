// Wi-Fiセットアップ・NTP取得を別タスクで実行するAPI
void start_wifi_setup_task(void);
#include <stdint.h>
#include "esp_event.h"
#ifdef __cplusplus
extern "C" {
#endif

// 他の宣言...

// Wi-Fiイベントグループを他ファイルから参照できるようにextern宣言
#include "freertos/event_groups.h"
extern EventGroupHandle_t wifi_event_group;

// Wi-Fiイベントハンドラをmain.cから参照できるようにextern宣言
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

#ifdef __cplusplus
}
#endif
#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include "esp_err.h"

#define NTP_SERVER_NAME "ntp.nict.jp"

#define WIFI_SOFTAP_SSID "Cerevo_MakerChipGacha"
#define WIFI_SOFTAP_PASS "12345678"


esp_err_t wifi_config_softap_start(void);
esp_err_t wifi_config_sta_connect(void);
esp_err_t wifi_config_load(void);
esp_err_t wifi_config_save(const char* ssid, const char* password);
esp_err_t wifi_config_get_ip(char* ip_buf, size_t buf_len);
esp_err_t wifi_config_get_ntp_time(char* datetime_buf, size_t buf_len);
void get_latest_date(char *buf, size_t len);
void get_latest_time(char *buf, size_t len);
void ntp_update_task(void *pvParameters);

#endif // WIFI_CONFIG_H
