// サーボモータ(PWM)テスト用: オンボードLEDをPWM制御する関数プロトタイプ
void onboard_led_pwm_start(int duty_percent, int freq_hz);
void onboard_led_pwm_stop(void);
#ifndef COMMON_H_
#define COMMON_H_
#include <stdio.h>
// オンボードLEDを1秒で最大輝度まで上げ、1秒で消灯まで下げる（非ブロッキング）
void onboard_led_pwm_fade_test(void);
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
// #include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"


static const char *TAG = "MC_GACHA";
// static const char *TAG_LCD_IPS = "LCD_IPS";
// static const char *TAG_LCD_I2C = "LCD_I2C";

// NVS設定(15文字以内で定義)
#define NVS_NAMESPACE "makerchip_gacha"
#define NVS_KEY_TOTAL_COINS "total_coins"
#define NVS_KEY_TOTAL_VALUE "total_value"
#define NVS_KEY_COIN_COUNT "coin_count"

#if 1
// GPIO定義（ESP32-C3 Mini）
#define RESET_BUTTON_PIN  GPIO_NUM_9    // GPIO_NUM_0    // リセットボタンピン（Mini用 BOOTボタン）
#define ONBOARD_LED_PIN   GPIO_NUM_0    // GPIO_NUM_9    // 外部LED用ピン(PWM制御)→モータ制御

#define COIN_SELECTOR_PIN GPIO_NUM_3    // コインセレクタのパルス入力ピン（Mini用）
#define EXT_LED_PIN       GPIO_NUM_1    // 外部LED用ピン（Mini用）
#define EXT_BUTTON_PIN    GPIO_NUM_10    // GPIO_NUM_10   // 外部ボタン用ピン（Mini用）

// LCD(SPI)用ピン（Mini用: lcd_ips.c等で利用）
#define LCD_MOSI_PIN      GPIO_NUM_18   // SDA
#define LCD_SCLK_PIN      GPIO_NUM_19   // SCL
#define LCD_CS_PIN        GPIO_NUM_6    // SPI CS
#define LCD_DC_PIN        GPIO_NUM_5    // Data/Command
#define LCD_RST_PIN       GPIO_NUM_4    // Resetピン
#define LCD_BLK_PIN       GPIO_NUM_7    // バックライト制御ピン
#else
// GPIO定義（ESP32-C3 Super Mini）
#define RESET_BUTTON_PIN  GPIO_NUM_9    // リセットボタンピン（ESP32-C3 Super Mini BOOTボタン）
#define ONBOARD_LED_PIN   GPIO_NUM_8    // オンボードLED用ピン（GPIO08直結）

#define COIN_SELECTOR_PIN GPIO_NUM_4    // コインセレクタのパルス入力ピン
#define EXT_LED_PIN       GPIO_NUM_0   // 外部LED用ピン
#define EXT_BUTTON_PIN    GPIO_NUM_5    // 外部ボタン用ピン

// LCD(SPI)用ピン（参考: lcd_ips.c等で利用）
#define LCD_MOSI_PIN      GPIO_NUM_6    // SDA
#define LCD_SCLK_PIN      GPIO_NUM_7    // SCL
#define LCD_CS_PIN        GPIO_NUM_10   // SPI CS
#define LCD_DC_PIN        GPIO_NUM_2    // Data/Command
#define LCD_RST_PIN       GPIO_NUM_3    // Resetピン
#define LCD_BLK_PIN       GPIO_NUM_1    // バックライト制御ピン
#endif

#define DEBOUNCE_TIME_MS 5              // デバウンス時間（最速化）
#define PULSE_WIDTH_MS 30               // 実際のパルス幅
#define PULSE_TIMEOUT_MS 400            // パルス列タイムアウト（2パルス分）
#define RESET_HOLD_TIME_MS 3000         // リセットボタン長押し時間
#define MIN_PULSE_INTERVAL_MS 10        // 最小パルス間隔（最速化）
#define MAX_PULSE_INTERVAL_MS 250       // 最大パルス間隔（1.25パルス分）
#define MAX_PULSE_COUNT 15              // 最大パルス数（超過で強制分割）


// 硬貨の種類定義
typedef enum {
    COIN_1_YEN = 100,    // 1円
    COIN_5_YEN = 105,    // 5円
    COIN_10_YEN = 110,   // 10円
    COIN_50_YEN = 150,   // 50円
    COIN_100_YEN = 1,  // 100円
    COIN_500_YEN = 5   // 500円
} coin_type_t;
//※メーカーチップガチャとして100円と500円のみを有効

// 入金データ構造
typedef struct {
    uint32_t total_coins;
    uint32_t total_value;
    uint32_t coin_count[6];
    uint32_t coin_values[6];
    char ssid[33];      // Wi-Fi SSID
    char password[65];  // Wi-Fiパスワード
} coin_data_t;
// 入金データ
extern coin_data_t bank_data;

// キューとセマフォのハンドル
extern QueueHandle_t coin_event_queue;
extern SemaphoreHandle_t nvs_mutex;  // NVS保存の排他制御用
extern SemaphoreHandle_t lcd_mutex;  // LCD表示の排他制御用
extern QueueHandle_t lcd_update_queue;  // LCD更新要求用キュー

// LCD表示状態管理
typedef enum {
    LCD_STATE_TITLE,        // 画面：タイトル表示
    LCD_STATE_STARTING,        // 画面：スタート表示
    LCD_STATE_INSERT,       // 画面：コイン挿入待ち
    LCD_STATE_AMOUNT,       // 画面：入金額表示
    LCD_STATE_PRESS_BUTTON, // 画面：ボタン押下待ち
    LCD_STATE_THANKS,       // 画面：サンキュー表示
    LCD_STATE_DATE_TIME,    // 画面：日付・時刻表示
    LCD_STATE_WIFI_WAIT,    // 画面：Wi-Fi接続待ち
    LCD_STATE_WIFI_NG       // 画面：Wi-Fi接続失敗
} lcd_disp_state_t;

// 関数プロトタイプ宣言
esp_err_t load_bank_data_from_nvs(void);
esp_err_t save_bank_data_to_nvs(void);
esp_err_t reset_bank_data(void);
void check_reset_button(void);
void coin_selector_isr_handler(void* arg);
int get_coin_index(uint32_t pulse_count);
const char* get_coin_name(int coin_index);
void display_bank_status(void);
void display_pulse_statistics(void);  // パルス統計表示関数
void coin_selector_task(void *pvParameters);
esp_err_t init_gpio(void);
bool validate_pulse_sequence(uint32_t pulse_count);  // パルス品質検証関数

// LCD関連関数
#define LCD_IF_SPI
// #define LCD_IF_I2C

#ifdef LCD_IF_SPI
    #define lcd_init lcd_ips_init  // LCD初期化関数の定義（IPS用に変更）
    // #define lcd_display_bank_status lcd_ips_display_bank_status  // LCD表示関数の定義（IPS用に変更）
    void lcd_show_message(const char* msg); // 任意メッセージ表示用プロトタイプ追加

    void lcd_show_request(lcd_disp_state_t request);
    // void lcd_show_insert(void);
    // void lcd_show_amount(uint32_t amount);
    // void lcd_show_thanks(void);
    // void lcd_show_press_button(void); // PRESS BUTTON画面表示用プロトタイプ追加
#endif // COMMON_H_
#ifdef LCD_IF_I2C
    #define lcd_init lcd_i2c_init  // LCD初期化関数の定義（I2C用に変更）
    #define lcd_display_bank_status lcd_i2c_display_bank_status  // LCD表示関数の定義（I2C用に変更）
#endif
esp_err_t lcd_init(void);
void lcd_display_bank_status(void);


// 外部LED制御関数（アクティブロー: 点灯0, 消灯1）
static inline void ext_led_on(void)  { gpio_set_level(EXT_LED_PIN, 0); }
static inline void ext_led_off(void) { gpio_set_level(EXT_LED_PIN, 1); }

// オンボードLED制御関数（アクティブロー: 点灯0, 消灯1）
static inline void onboard_led_on(void)  { gpio_set_level(ONBOARD_LED_PIN, 0); }
static inline void onboard_led_off(void) { gpio_set_level(ONBOARD_LED_PIN, 1); }

// 外部ボタン状態取得
static inline int ext_button_pressed(void) { return gpio_get_level(EXT_BUTTON_PIN) != 0; }

#endif // COMMON_H_
