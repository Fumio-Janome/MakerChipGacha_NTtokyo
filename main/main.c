#include "driver/ledc.h"
#include "common.h"
#include "lcd_ips.h"

#include "wifi_config.h"

#include "web_config.h"

// オンボードLED PWM制御用チャンネル・タイマー定義
#define ONBOARD_LED_PWM_TIMER      LEDC_TIMER_0
#define ONBOARD_LED_PWM_MODE       LEDC_LOW_SPEED_MODE
#define ONBOARD_LED_PWM_CHANNEL    LEDC_CHANNEL_0
#define ONBOARD_LED_PWM_RES        LEDC_TIMER_10_BIT

// オンボードLEDをPWMで点灯（duty_percent: 0-100, freq_hz: 周波数）
void onboard_led_pwm_start(int duty_percent, int freq_hz) {
    ledc_timer_config_t timer_conf = {
        .speed_mode = ONBOARD_LED_PWM_MODE,
        .timer_num = ONBOARD_LED_PWM_TIMER,
        .duty_resolution = ONBOARD_LED_PWM_RES,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .gpio_num = ONBOARD_LED_PIN,
        .speed_mode = ONBOARD_LED_PWM_MODE,
        .channel = ONBOARD_LED_PWM_CHANNEL,
        .timer_sel = ONBOARD_LED_PWM_TIMER,
        .duty = (duty_percent * ((1 << 10) - 1)) / 100,
        .hpoint = 0,
        .flags.output_invert = 1 // 極性反転（duty=0で最大輝度、duty=maxで消灯）
    };
    ledc_channel_config(&channel_conf);
    // ledc_fade_func_install(0); // 初期化時のみ呼び出す
}

// オンボードLED PWM停止
void onboard_led_pwm_stop(void) {
ledc_stop(ONBOARD_LED_PWM_MODE, ONBOARD_LED_PWM_CHANNEL, 0); // 0=出力Low（output_invert=1時は消灯）
    // ledc_fade_func_uninstall(); // 終了時のみ呼び出す
}

// オンボードLEDフェード用タスク
static void onboard_led_pwm_fade_task(void *pvParameters) {
    const int max_duty = (1 << 10) - 1; // 10bit解像度
    const int fade_time_ms = 2000; // 2秒フェード
    // PWM初期化（duty=0, freq=1000Hz）
    onboard_led_pwm_start(0, 1000);
    // 2秒で最大輝度までフェードアップ
    ledc_set_fade_with_time(ONBOARD_LED_PWM_MODE, ONBOARD_LED_PWM_CHANNEL, max_duty, fade_time_ms);
    ledc_fade_start(ONBOARD_LED_PWM_MODE, ONBOARD_LED_PWM_CHANNEL, LEDC_FADE_NO_WAIT);
    vTaskDelay(pdMS_TO_TICKS(fade_time_ms));
    // 2秒で消灯までフェードダウン
    ledc_set_fade_with_time(ONBOARD_LED_PWM_MODE, ONBOARD_LED_PWM_CHANNEL, 0, fade_time_ms);
    ledc_fade_start(ONBOARD_LED_PWM_MODE, ONBOARD_LED_PWM_CHANNEL, LEDC_FADE_NO_WAIT);
    vTaskDelay(pdMS_TO_TICKS(fade_time_ms));
    // 後始末
    onboard_led_pwm_stop();
    vTaskDelete(NULL);
}

// オンボードLEDを1秒で最大輝度まで上げ、1秒で消灯まで下げる（非ブロッキング）
void onboard_led_pwm_fade_test(void) {
xTaskCreate(onboard_led_pwm_fade_task, "led_fade", 2048, NULL, 5, NULL); // スタックサイズ増加
}

// 1パルス=100円換算用
uint32_t total_pulse_count = 0;
// 入金データの実体定義
coin_data_t bank_data = {0};
uint32_t total_value = 0;

QueueHandle_t coin_event_queue = NULL;
SemaphoreHandle_t nvs_mutex = NULL;  // NVS保存の排他制御用
SemaphoreHandle_t lcd_mutex = NULL;  // LCD表示の排他制御用
QueueHandle_t lcd_update_queue = NULL;  // LCD更新要求用キュー

// パルスカウント用の変数（精度向上のため追加変数）
static volatile uint32_t pulse_count = 0;
static volatile TickType_t last_pulse_time = 0;
static volatile TickType_t first_pulse_time = 0;
static volatile bool pulse_sequence_active = false;

// パルス品質検証用の変数
static volatile uint32_t pulse_intervals[20];  // パルス間隔履歴（最大20パルス）
static volatile uint8_t interval_index = 0;

// パルス統計用変数（取りこぼし監視）
static volatile uint32_t total_interrupts = 0;     // 総割り込み回数
static volatile uint32_t debounce_filtered = 0;    // デバウンスでフィルタされた回数
static volatile uint32_t valid_pulses = 0;         // 有効パルス数
static volatile uint32_t unknown_pulses = 0;       // 不明パルス数
static volatile uint32_t rejected_quality = 0;     // 品質不良で拒否された回数

// NVSから合計パルス数とWi-Fi情報(SSID/PASS)を読み込む関数
esp_err_t load_bank_data_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVSからの読み込みに失敗（初回起動？）: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_get_u32(nvs_handle, "pulse_cnt", &total_pulse_count);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "パルス数の読み込みに失敗: %s", esp_err_to_name(err));
    }
    size_t len;
    len = sizeof(bank_data.ssid);
    err = nvs_get_str(nvs_handle, "wifi_ssid", bank_data.ssid, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) bank_data.ssid[0] = '\0';
    len = sizeof(bank_data.password);
    err = nvs_get_str(nvs_handle, "wifi_pass", bank_data.password, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) bank_data.password[0] = '\0';
    nvs_close(nvs_handle);
    total_value = total_pulse_count * 100;
    ESP_LOGI(TAG, "NVSから合計パルス数を読み込みました: %luパルス（%lu円）", total_pulse_count, total_value);
    ESP_LOGI(TAG, "NVSからWi-Fi情報を読み込みました: SSID=%s PASS=%s", bank_data.ssid, bank_data.password);
    return ESP_OK;
}

// NVSに合計パルス数とWi-Fi情報(SSID/PASS)を保存する関数
esp_err_t save_bank_data_to_nvs(void)
{
    if (xSemaphoreTake(nvs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "NVS保存処理がタイムアウトしました");
        return ESP_ERR_TIMEOUT;
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVSのオープンに失敗: %s", esp_err_to_name(err));
        xSemaphoreGive(nvs_mutex);
        return err;
    }
    err = nvs_set_u32(nvs_handle, "pulse_cnt", total_pulse_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "パルス数の保存に失敗: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        xSemaphoreGive(nvs_mutex);
        return err;
    }
    err = nvs_set_str(nvs_handle, "wifi_ssid", bank_data.ssid);
    if (err != ESP_OK) ESP_LOGE(TAG, "SSIDの保存に失敗: %s", esp_err_to_name(err));
    err = nvs_set_str(nvs_handle, "wifi_pass", bank_data.password);
    if (err != ESP_OK) ESP_LOGE(TAG, "PASSの保存に失敗: %s", esp_err_to_name(err));
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVSコミットに失敗: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "合計パルス数とWi-Fi情報をNVSに保存しました: %luパルス, SSID=%s PASS=%s", total_pulse_count, bank_data.ssid, bank_data.password);
    }
    nvs_close(nvs_handle);
    xSemaphoreGive(nvs_mutex);
    return err;
}

// 合計パルス数をリセットする関数
esp_err_t reset_bank_data(void)
{
    total_pulse_count = 0;
    total_value = 0;
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "合計パルス数をリセットしました");
    } else {
        ESP_LOGE(TAG, "NVSリセットに失敗: %s", esp_err_to_name(err));
    }
    return err;
}

// リセットボタンチェック機能
void check_reset_button(void)
{
    static TickType_t button_press_start = 0;
    static bool button_pressed = false;
    static int last_button_state = -1;  // 初回の状態変化を検出するため
    
    int button_state = gpio_get_level(RESET_BUTTON_PIN);
    
    // デバッグ：ボタン状態の変化をログ出力
    if (button_state != last_button_state) {
        // ESP_LOGI(TAG, "ボタン状態変化: %d -> %d", last_button_state, button_state);
        last_button_state = button_state;
    }
    
    if (button_state == 0 && !button_pressed) {
        // ボタンが押され始めた
        button_pressed = true;
        button_press_start = xTaskGetTickCount();
        ESP_LOGI(TAG, "リセットボタンが押されました（3秒間長押しでリセット）");
    } else if (button_state == 1 && button_pressed) {
        // ボタンが離された
        TickType_t press_duration = xTaskGetTickCount() - button_press_start;
        button_pressed = false;
        ESP_LOGI(TAG, "リセットボタンが離されました（押下時間: %lu ms）", 
                 (unsigned long)(press_duration * portTICK_PERIOD_MS));
    } else if (button_state == 0 && button_pressed) {
        // ボタンが押し続けられている
        TickType_t press_duration = xTaskGetTickCount() - button_press_start;
        
        // 1秒ごとに進捗を表示
        static TickType_t last_progress_time = 0;
        if ((press_duration - last_progress_time) > pdMS_TO_TICKS(1000)) {
            ESP_LOGI(TAG, "リセットボタン長押し中... %lu / %lu ms", 
                     (unsigned long)(press_duration * portTICK_PERIOD_MS), 
                     (unsigned long)RESET_HOLD_TIME_MS);
            last_progress_time = press_duration;
        }
        
        if (press_duration > pdMS_TO_TICKS(RESET_HOLD_TIME_MS)) {
            // 長押し時間に達した
            ESP_LOGI(TAG, "リセット実行中...");
            
            reset_bank_data();
            // display_bank_status();
            
            button_pressed = false; // リセット後はボタン状態もリセット
            ESP_LOGI(TAG, "リセット完了しました");
        }
    }
}

// コインセレクタの軽量化された割り込みハンドラ（パルス取りこぼし防止）
void IRAM_ATTR coin_selector_isr_handler(void* arg)
{
    total_interrupts++; // 統計：総割り込み回数
    
    TickType_t current_time = xTaskGetTickCountFromISR();
    
    // 基本的なデバウンス処理（最小限の処理で高速化）
    TickType_t interval_since_last = current_time - last_pulse_time;
    
    // デバウンス処理（チャタリング除去）
    if (interval_since_last < pdMS_TO_TICKS(DEBOUNCE_TIME_MS)) {
        debounce_filtered++; // 統計：デバウンスフィルタ回数
        return; // チャタリングとして無視
    }
    
    // 最小パルス間隔チェック（ノイズパルス除去）
    if (interval_since_last < pdMS_TO_TICKS(MIN_PULSE_INTERVAL_MS)) {
        debounce_filtered++; // 統計：最小間隔フィルタ回数
        return; // 高速すぎるパルスは無視
    }
    
    valid_pulses++; // 統計：有効パルス数
    
    // 最大パルス間隔チェック（新しいコイン判定のため）
    if (pulse_sequence_active && 
        interval_since_last > pdMS_TO_TICKS(MAX_PULSE_INTERVAL_MS)) {
        // 前のパルス列をタイムアウトとして処理
        uint32_t timeout_event = 2;
        xQueueSendFromISR(coin_event_queue, &timeout_event, NULL);
        
        // 新しいパルス列として開始
        pulse_count = 0;
        interval_index = 0;
    }
    
    // 最初のパルスまたは継続パルス
    if (!pulse_sequence_active) {
        pulse_sequence_active = true;
        first_pulse_time = current_time;
        pulse_count = 0;
        interval_index = 0;
    }
    
    pulse_count++;
    
    // パルス数上限チェック（パルス列の強制分割）
    if (pulse_count >= MAX_PULSE_COUNT) {
        // 連続投入時の安定性向上のため、ログレベルを下げる
        uint32_t split_event = 3;  // 新しいイベント種別
        xQueueSendFromISR(coin_event_queue, &split_event, NULL);
        
        // 現在のパルス列をリセットして新しい列を開始
        pulse_count = 1;  // 現在のパルスを新しい列の最初とする
        interval_index = 0;
        first_pulse_time = current_time;
    }
    
    // パルス間隔記録（軽量化）
    uint32_t interval_ms = (uint32_t)(interval_since_last * portTICK_PERIOD_MS);
    if (pulse_count > 1 && interval_index < 19) {
        pulse_intervals[interval_index] = interval_ms;
        interval_index++;
    }
    
    last_pulse_time = current_time;
    
    // キューに通知（最小限の処理）
    uint32_t event = 1;
    xQueueSendFromISR(coin_event_queue, &event, NULL);
}
// パルス品質検証関数（パルス取りこぼし防止のため緩和版）
bool validate_pulse_sequence(uint32_t pulse_count)
{
    if (pulse_count < 1) {
        ESP_LOGW(TAG, "パルス数が少なすぎます: %lu", pulse_count);
        return false;
    }
    
    // 単一パルス（1パルス）の場合は簡略化して常に受理
    if (pulse_count == 1) {
        // ESP_LOGI(TAG, "単一パルス（1パルス）として受理");
        return true;
    }
    
    // 複数パルスの場合はさらに寛容に（パルス取りこぼし対策）
    if (interval_index >= 1) {
        uint32_t total_interval = 0;
        uint32_t min_interval = pulse_intervals[0];
        uint32_t max_interval = pulse_intervals[0];
        
        // 平均、最小、最大パルス間隔を計算
        for (int i = 0; i < interval_index; i++) {
            total_interval += pulse_intervals[i];
            if (pulse_intervals[i] < min_interval) min_interval = pulse_intervals[i];
            if (pulse_intervals[i] > max_interval) max_interval = pulse_intervals[i];
        }
        
        uint32_t avg_interval = total_interval / interval_index;
        uint32_t variation = max_interval - min_interval;
        
        // ESP_LOGI(TAG, "パルス品質分析 - 平均間隔: %lums, 最小: %lums, 最大: %lums, 変動: %lums", 
        //          avg_interval, min_interval, max_interval, variation);
        
        // パルス間隔変動の許容を実特性に合わせて設定（1.5倍まで）
        if (variation > avg_interval * 1.5) {
            ESP_LOGW(TAG, "パルス間隔の変動が大きすぎます（変動: %lums > 許容値: %lums）", 
                     variation, (uint32_t)(avg_interval * 1.5));
            return false;
        }
        
        // 平均パルス間隔の範囲を実特性に合わせて設定（100-200ms）
        if (avg_interval < 100 || avg_interval > 200) {
            ESP_LOGW(TAG, "平均パルス間隔が範囲外です: %lums（許容範囲: 100-200ms）", avg_interval);
            return false;
        }
    }
    
    // 総パルス時間のチェックを大幅に緩和
    TickType_t total_pulse_time = last_pulse_time - first_pulse_time;
    uint32_t total_time_ms = (uint32_t)(total_pulse_time * portTICK_PERIOD_MS);
    
    // ESP_LOGI(TAG, "パルス列総時間: %lums", total_time_ms);
    
    // 総時間チェックをパルス数1-6に合わせて調整
    if (total_time_ms < (pulse_count * 50)) {  // 最低50ms/パルス（小パルス数対応）
        ESP_LOGW(TAG, "パルス列が短すぎます: %lums（最低: %lums）", total_time_ms, pulse_count * 50);
        return false;
    }
    
    if (total_time_ms > (pulse_count * 300)) {  // 最大300ms/パルス（小パルス数対応）
        ESP_LOGW(TAG, "パルス列が長すぎます: %lums（最大: %lums）", total_time_ms, pulse_count * 300);
        return false;
    }
    
    // ESP_LOGI(TAG, "パルス品質検証合格: %luパルス", pulse_count);
    return true;
}
int get_coin_index(uint32_t pulse_count)
{
    // 1円、10円、500円硬貨のデバッグ強化（パルス数1-6対応）
    if (pulse_count >= 1 && pulse_count <= 3) {
        // ESP_LOGI(TAG, "★ 1円/5円/10円硬貨候補を詳細分析: %luパルス", pulse_count);
    }
    if (pulse_count >= 4 && pulse_count <= 6) {
        // ESP_LOGI(TAG, "★ 50円/100円/500円硬貨候補を詳細分析: %luパルス", pulse_count);
    }
    
    // 厳密なマッチング（最優先）
    switch (pulse_count) {
        case COIN_1_YEN:   return 0;  // 1円
        case COIN_5_YEN:   return 1;  // 5円
        case COIN_10_YEN:  return 2;  // 10円
        case COIN_50_YEN:  return 3;  // 50円
        case COIN_100_YEN: return 4;  // 100円
        case COIN_500_YEN: 
            ESP_LOGI(TAG, "★ 500円硬貨を厳密マッチで認識: %luパルス", pulse_count);
            return 5;  // 500円
    }
    
    // ノイズ対応：最も近い値との距離を計算して最適マッチを判定
    int min_distance = INT_MAX;
    int best_match = -1;
    
    const int coin_pulses[] = {COIN_1_YEN, COIN_5_YEN, COIN_10_YEN, COIN_50_YEN, COIN_100_YEN, COIN_500_YEN};
    const char* coin_names[] = {"1円", "5円", "10円", "50円", "100円", "500円"};
    
    for (int i = 0; i < 6; i++) {
        int distance = abs((int)pulse_count - coin_pulses[i]);
        if (distance <= 1 && distance < min_distance) {  // ±1パルスに戻す（厳密化）
            min_distance = distance;
            best_match = i;
            
            // 1円、10円、500円硬貨の場合は特別にログ出力
            if (i == 0 || i == 2 || i == 5) {
                ESP_LOGI(TAG, "★ %s硬貨を距離ベースで認識: パルス%lu、距離%d", coin_names[i], pulse_count, distance);
            }
        }
    }
    
    if (best_match >= 0) {
        ESP_LOGW(TAG, "%sと推定（パルス数: %lu、期待値: %d、距離: %d）", 
                 coin_names[best_match], pulse_count, coin_pulses[best_match], min_distance);
        return best_match;
    }
    
    // どの硬貨にも近くない場合は、参考情報をログ出力
    ESP_LOGW(TAG, "不明なパルス数: %lu（全硬貨との距離: ", pulse_count);
    for (int i = 0; i < 6; i++) {
        int distance = abs((int)pulse_count - coin_pulses[i]);
        ESP_LOGW(TAG, "%s=%d(距離%d) ", coin_names[i], coin_pulses[i], distance);
    }
    ESP_LOGW(TAG, "）");
    
    return -1; // 不明な硬貨
}

// 硬貨の種類名を取得する関数
const char* get_coin_name(int coin_index)
{
    const char* coin_names[] = {"1円", "5円", "10円", "50円", "100円", "500円"};
    if (coin_index >= 0 && coin_index < 6) {
        return coin_names[coin_index];
    }
    return "不明";
}

// パルス統計情報を表示する関数（パルス取りこぼし監視）
void display_pulse_statistics(void)
{
    ESP_LOGI(TAG, "=== パルス統計情報 ===");
    ESP_LOGI(TAG, "総割り込み回数: %lu回", total_interrupts);
    ESP_LOGI(TAG, "デバウンスフィルタ: %lu回", debounce_filtered);
    ESP_LOGI(TAG, "有効パルス数: %lu回", valid_pulses);
    ESP_LOGI(TAG, "不明パルス数: %lu回", unknown_pulses);
    ESP_LOGI(TAG, "品質不良拒否: %lu回", rejected_quality);
    if (total_interrupts > 0) {
        ESP_LOGI(TAG, "有効パルス率: %.1f%%", (float)valid_pulses * 100.0f / (float)total_interrupts);
    }
    ESP_LOGI(TAG, "=====================");
}

// LCD表示更新要求
void lcd_display_request(lcd_disp_state_t request)
{
    xQueueSend(lcd_update_queue, &request, 0);
}

// LCD表示専用タスク（パルス検知に影響を与えないように分離）
void lcd_display_task(void *pvParameters)
{   
    lcd_disp_state_t update_request;
    while (1) {
        if (xQueueReceive(lcd_update_queue, &update_request, portMAX_DELAY)) {
            // LCD表示更新要求を処理
            lcd_show_request(update_request);
            // ESP_LOGI(TAG, "LCD表示更新要求: %d", update_request);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// 1パルス=100円換算のコインセレクタタスク
void coin_selector_task(void *pvParameters)
{
    uint32_t event;
    // ESP_LOGI(TAG, "*** コインセレクタタスクが開始されました ***");
    vTaskDelay(pdMS_TO_TICKS(2000));
    // ESP_LOGI(TAG, "コインセレクタタスク - 検知を開始します");

    while (1) {
        if (xQueueReceive(coin_event_queue, &event, pdMS_TO_TICKS(PULSE_TIMEOUT_MS))) {
            // event==1: パルス受信、event==2/3: 無視
        } else {
            // タイムアウト - パルス列の終了と判定
            if (pulse_count > 0 && pulse_sequence_active) {
                // ESP_LOGI(TAG, "パルス列終了: %luパルス", pulse_count);
                bool is_valid = validate_pulse_sequence(pulse_count);
                if (is_valid) {
                    int coin_index = get_coin_index(pulse_count);
                    if (coin_index >= 0 && coin_index < 6) {
                        static const uint32_t coin_values[6] = {1, 5, 10, 50, 100, 500};
                        bank_data.coin_count[coin_index]++;
                        bank_data.total_coins++;
                        bank_data.total_value += coin_values[coin_index];
                        ESP_LOGI(TAG, "コイン認識: %s (%lu円) 合計: %lu円", get_coin_name(coin_index), coin_values[coin_index], bank_data.total_value);
                        // lcd_show_amount(bank_data.total_value);
                        

                        if (bank_data.total_value >= 500) {
                            lcd_display_request(LCD_STATE_PRESS_BUTTON);

                            while (!ext_button_pressed()) {
                                ext_led_on();
                                vTaskDelay(pdMS_TO_TICKS(200));
                                ext_led_off();
                                vTaskDelay(pdMS_TO_TICKS(200));
                            }
                            ext_led_off();

                            // サーボモータの代用: オンボードLEDを4秒間PWMでフェードアップ・ダウン
                            onboard_led_pwm_fade_test(); // フェードテスト
                            vTaskDelay(pdMS_TO_TICKS(1000));

                            lcd_display_request(LCD_STATE_THANKS);

                            vTaskDelay(pdMS_TO_TICKS(4000));
                            memset(&bank_data, 0, sizeof(bank_data));
                            save_bank_data_to_nvs();

                            lcd_display_request(LCD_STATE_INSERT);
                        } else {
                            lcd_display_request(LCD_STATE_AMOUNT);
                            ext_led_off();
                            save_bank_data_to_nvs();
                        }
                    } else {
                        ESP_LOGW(TAG, "不明なコイン: %luパルス", pulse_count);
                    }
                } else {
                    ESP_LOGW(TAG, "✗ パルス品質が不正のため無視: %luパルス", pulse_count);
                }
                pulse_count = 0;
                pulse_sequence_active = false;
                interval_index = 0;
            }
        }
    }
}

// 改良されたGPIO初期化
esp_err_t init_gpio(void)
{
    esp_err_t ret = ESP_OK;
    
    // コインセレクタピンの設定（プルアップ付き入力、ノイズフィルタ有効）
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << COIN_SELECTOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // 立ち下がりエッジで割り込み
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // GPIO4にグリッチフィルタを設定（ノイズ除去強化）
    ret = gpio_set_intr_type(COIN_SELECTOR_PIN, GPIO_INTR_NEGEDGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO interrupt type set failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // リセットボタンピンの設定（プルアップ付き入力）
    gpio_config_t reset_conf = {
        .pin_bit_mask = (1ULL << RESET_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&reset_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reset button GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 割り込みハンドラのインストール
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 割り込みハンドラの追加
    ret = gpio_isr_handler_add(COIN_SELECTOR_PIN, coin_selector_isr_handler, (void*) COIN_SELECTOR_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO ISR handler add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // thank you LED用GPIOの初期化（旧）
    // gpio_set_direction(THANKYOU_LED_PIN, GPIO_MODE_OUTPUT);
    // thankyou_led_off();
    // 外部LED用GPIOの初期化
    gpio_set_direction(EXT_LED_PIN, GPIO_MODE_OUTPUT);
    ext_led_off();

    // オンボードLEDはPWM（LEDC）専用とし、通常GPIO初期化は行わない

    // 外部ボタン用GPIOの初期化（プルアップ付き入力）
    gpio_config_t ext_btn_conf = {
        .pin_bit_mask = (1ULL << EXT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&ext_btn_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ext button GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "GPIO initialized successfully");
    return ESP_OK;
}

// メイン関数
// 必要なESP-IDFヘッダを追加
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"

void app_main(void)
{
    ESP_LOGI(TAG, "--Maker Chip Gacha-- 開始");
    
    // NVS初期化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVSが初期化されました");

        // ネットワーク初期化は一度だけ
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
        esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 保存された合計パルス数を読み込み
    load_bank_data_from_nvs();

    // --- Wi-Fi設定・接続自動実行 ---
    ESP_LOGI("wifi", "SoftAP+Webサーバ起動");
    wifi_config_softap_start();
    start_wifi_config_server();

    // SSID/PASSが保存されていればSTA接続＆NTP取得
    vTaskDelay(pdMS_TO_TICKS(5000)); // Web設定待ち猶予（5秒）
    if (wifi_config_sta_connect() == ESP_OK) {
        char ip[16] = {0};
        if (wifi_config_get_ip(ip, sizeof(ip)) == ESP_OK) {
            ESP_LOGI("wifi", "IPアドレス: %s", ip);
        }
        char datetime[32] = {0};
        if (wifi_config_get_ntp_time(datetime, sizeof(datetime)) == ESP_OK) {
            ESP_LOGI("wifi", "NTP時刻: %s", datetime);
        }
    } else {
        ESP_LOGW("wifi", "Wi-Fi STA接続失敗。Webで設定してください");
    }
    
    // パルス検出関連変数の初期化
    pulse_count = 0;
    pulse_sequence_active = false;
    interval_index = 0;
    last_pulse_time = 0;
    first_pulse_time = 0;
    
    // パルス統計変数の初期化
    total_interrupts = 0;
    debounce_filtered = 0;
    valid_pulses = 0;
    unknown_pulses = 0;
    rejected_quality = 0;
    
    // キューの作成
    coin_event_queue = xQueueCreate(10, sizeof(uint32_t));
    if (coin_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create coin event queue");
        return;
    }
    
    // LCD更新要求キューの作成
    lcd_update_queue = xQueueCreate(5, sizeof(uint8_t));
    if (lcd_update_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create LCD update queue");
        return;
    }
    
    // NVS保存用ミューテックスの作成
    nvs_mutex = xSemaphoreCreateMutex();
    if (nvs_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create NVS mutex");
        return;
    }
    
    // LCD表示用ミューテックスの作成
    lcd_mutex = xSemaphoreCreateMutex();
    if (lcd_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create LCD mutex");
        return;
    }
    
    // GPIO初期化
    ret = init_gpio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO initialization failed");
        return;
    }
    // LEDCフェード機能インストール（初期化時のみ）
    ledc_fade_func_install(0);
    
    // コインセレクタタスクを早期に作成（最高優先度）
    xTaskCreate(coin_selector_task, "coin_selector", 2048, NULL, 20, NULL);  // 最高優先度20
    // ESP_LOGI(TAG, "コインセレクタタスクを開始しました（最高優先度）");
    
    // LCD表示タスクを作成（低優先度）
    xTaskCreate(lcd_display_task, "lcd_display", 2048, NULL, 5, NULL);  // 低優先度5
    // ESP_LOGI(TAG, "LCD表示タスクを開始しました（低優先度）");
    
    // LCD初期化
    ret = lcd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD initialization failed: %s", esp_err_to_name(ret));
        // LCDエラーでも処理を続行
    } else {
        // ESP_LOGI(TAG, "LCD初期化が完了しました");
        // 初期画面表示要求はdisplay_bank_status()に統一
    }
    lcd_display_request(LCD_STATE_INSERT); // 初期画面表示要求
    
    // ボタンの初期状態を確認
    int initial_button_state = gpio_get_level(RESET_BUTTON_PIN);
    ESP_LOGI(TAG, "リセットボタン初期状態: %d （0=押下、1=未押下）", initial_button_state);
    
    // // 現在の入金状態をシリアルに表示（復元されたデータ）
    // ESP_LOGI(TAG, "前回の合計パルス数を復元しました");
    // display_bank_status();
    
    ESP_LOGI(TAG, "--Maker Chip Gacha-- 準備完了");
    ESP_LOGI(TAG, "");
    // ESP_LOGI(TAG, "GPIO%d にコインセレクタを接続してください", COIN_SELECTOR_PIN);
    
    // ESP_LOGI(TAG, "*** メインループ開始 - 時刻:%lu ***", xTaskGetTickCount());
    
    // onboard_led_pwm_fade_test(); // フェードテスト

    // メインループ
    while (1) {
        // // リセットボタンのチェック
        // check_reset_button();

        vTaskDelay(pdMS_TO_TICKS(100));  // 10ms間隔での処理
    }
    // アプリ終了時にフェード機能アンインストール（必要なら）
    // ledc_fade_func_uninstall();
}
