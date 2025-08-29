#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "driver/ledc.h"

#include "common.h"
#include "lcd_ips.h"
#include "wifi_config.h"

#include "web_config.h"
#include <time.h>

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

// 500円認識ログ用実体
log_entry_t log_entries[MAX_LOG_ENTRIES] = {0};
uint16_t log_count = 0;

esp_err_t load_log_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(BUY_LOG_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;
    size_t size = sizeof(log_entries);
    err = nvs_get_blob(nvs_handle, BUY_LOGS, log_entries, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        log_count = 0;
        err = ESP_OK;
    }
    nvs_get_u16(nvs_handle, BUY_COUNT, &log_count);
    nvs_close(nvs_handle);
    return err;
}
esp_err_t save_log_to_nvs(void) {
    if (xSemaphoreTake(nvs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(BUY_LOG_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) { xSemaphoreGive(nvs_mutex); return err; }
    nvs_set_blob(nvs_handle, BUY_LOGS, log_entries, log_count * sizeof(log_entry_t));
    nvs_set_u16(nvs_handle, BUY_COUNT, log_count);
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    xSemaphoreGive(nvs_mutex);
    return err;
}
void add_chip_buy_log(time_t now) {
    if (log_count < MAX_LOG_ENTRIES) {
        log_entries[log_count].timestamp = (uint32_t)now;
        log_count++;
        save_log_to_nvs();
    }
}
uint16_t get_chip_buy_count(void) {
    return log_count;
}
void set_chip_buy_count(uint16_t count) {
    log_count = count;
}

esp_err_t reset_buy_count(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(BUY_LOG_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(nvs_handle, BUY_COUNT, 0); // BUY_COUNTを0に
    if (err == ESP_OK){
        set_chip_buy_count(0); // メモリ上のカウントも0に
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    lcd_display_request(LCD_STATE_DATE_TIME);
    return err;
}

// 最新の500円ログを取得する
// out_entries: 取得先配列, max_entries: 最大取得数
// 戻り値: 実際に取得した件数
uint16_t get_chip_buy_logs(log_entry_t *out_entries, uint16_t max_entries) {
    uint16_t count = (log_count < max_entries) ? log_count : max_entries;
    for (uint16_t i = 0; i < count; i++) {
        out_entries[i] = log_entries[i];
    }
    return count;
}

void log_list_serial_output(void) {
    uint16_t chips = 0;
    if (load_log_from_nvs() == ESP_OK) {
        log_entry_t *tmp_entries = malloc(sizeof(log_entry_t) * MAX_LOG_ENTRIES);
        if (tmp_entries) {
            chips = get_chip_buy_logs(tmp_entries, MAX_LOG_ENTRIES);
            printf("--- Maker Chip購入履歴（購入数:%u）---\n", chips);
            for (uint16_t i = 0; i < chips; i++) {
                time_t t = (time_t)tmp_entries[i].timestamp;
                struct tm tm_info;
                localtime_r(&t, &tm_info);
                char buf[32];
                strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M:%S", &tm_info);
                printf("%2u: %s\n", i+1, buf);
            }
            printf("--------------------------------------\n");
            free(tmp_entries);
        } else {
            printf("購買ログ一時バッファの確保に失敗\n");
        }
    } else {
            printf("--- Maker Chip購入履歴（購入数:%u）---\n", chips);
            printf("--------------------------------------\n");
    }
    printf("・BOOTボタン短押しでログ出力\n");
    printf("・BOOTボタン5秒長押しでログクリア\n");
    printf("・BOOTボタン10秒長押しでNVS初期化\n");
}

// Web用 購入ログ出力関数
// buf: 出力先バッファ, bufsize: バッファサイズ
// 戻り値: 出力した件数
uint16_t log_list_web_output(char *buf, size_t bufsize) {
    if (!buf || bufsize == 0) return 0;
    uint16_t chips = 0;
    size_t pos = 0;
    if (load_log_from_nvs() == ESP_OK) {
        log_entry_t *tmp_entries = malloc(sizeof(log_entry_t) * MAX_LOG_ENTRIES);
        if (tmp_entries) {
            chips = get_chip_buy_logs(tmp_entries, MAX_LOG_ENTRIES);
            int n = snprintf(buf+pos, bufsize-pos, "--- Maker Chip購入履歴（購入数:%u）---\r\n", chips);
            if (n > 0) pos += n;
            for (uint16_t i = 0; i < chips; i++) {
                time_t t = (time_t)tmp_entries[i].timestamp;
                struct tm tm_info;
                localtime_r(&t, &tm_info);
                char datebuf[32];
                strftime(datebuf, sizeof(datebuf), "%Y/%m/%d %H:%M:%S", &tm_info);
                n = snprintf(buf+pos, bufsize-pos, "%2u: %s\r\n", i+1, datebuf);
                if (n > 0) pos += n;
                if (pos >= bufsize-64) break;
            }
            n = snprintf(buf+pos, bufsize-pos, "--------------------------------------\r\n");
            if (n > 0) pos += n;
            free(tmp_entries);
        } else {
            snprintf(buf, bufsize, "ログ一時バッファ確保失敗\r\n");
        }
    } else {
        snprintf(buf, bufsize, "ログ取得失敗\r\n");
    }
    // // 操作説明を追加
    // size_t remain = bufsize - pos;
    // if (remain > 128) {
    //     snprintf(buf+pos, remain,
    //         "・BOOTボタン短押しでログ出力\r\n・BOOTボタン5秒長押しでログクリア\r\n・BOOTボタン10秒長押しでNVS初期化\r\n");
    // }
    return chips;
}

// リセットボタンチェック機能
void check_reset_button(void)
{
    static TickType_t button_press_start = 0;
    static bool button_pressed = false;
    static int last_button_state = -1;  // 初回の状態変化を検出するため
    
    int button_state = gpio_get_level(LOG_BUTTON_PIN);
    
    // デバッグ：ボタン状態の変化をログ出力
    if (button_state != last_button_state) {
        // ESP_LOGI(TAG, "ボタン状態変化: %d -> %d", last_button_state, button_state);
        last_button_state = button_state;
    }
    
    if (button_state == 0 && !button_pressed) {
        // ボタンが押され始めた
        button_pressed = true;
        button_press_start = xTaskGetTickCount();
        // ESP_LOGI(TAG, "BOOTボタン Pushed");

    } else if (button_state == 1 && button_pressed) {
        // ボタンが離された
        TickType_t press_duration = xTaskGetTickCount() - button_press_start;
        button_pressed = false;
        int press_duration_ms = (int)(press_duration * portTICK_PERIOD_MS);
        // ESP_LOGI(TAG, "BOOTボタン Released（押下時間: %lu ms）", (unsigned long)press_duration_ms);

        if(press_duration_ms > LOG_CLEAR_TIME_MS){
            ESP_LOGI(TAG, "購入履歴をクリアします");
            if(reset_buy_count() == ESP_OK){
                ESP_LOGI(TAG, "購入履歴をクリアしました");
            }else{
                ESP_LOGI(TAG, "購入履歴のクリアに失敗しました");
            }

        }else if(press_duration_ms > LOG_LIST_TIME_MS){
            // ログリストをシリアル出力
            log_list_serial_output();
        }
    } else if (button_state == 0 && button_pressed) {
        // ボタンが押し続けられている
        TickType_t press_duration = xTaskGetTickCount() - button_press_start;
        
        // 1秒ごとに進捗を表示
        static TickType_t last_progress_time = 0;
        if ((press_duration - last_progress_time) > pdMS_TO_TICKS(1000)) {
            // ESP_LOGI(TAG, "リセットボタン長押し中... %lu / %lu ms", 
            //          (unsigned long)(press_duration * portTICK_PERIOD_MS), 
            //          (unsigned long)RESET_HOLD_TIME_MS);
            last_progress_time = press_duration;
        }
        
        if (press_duration > pdMS_TO_TICKS(NVS_FORMAT_TIME_MS)) {
            // 長押し時間に達した
            ESP_LOGI(TAG, "NVS初期化...再起動");
           
            nvs_flash_erase();
            esp_restart(); 
            
            // button_pressed = false; // リセット後はボタン状態もリセット
            // ESP_LOGI(TAG, "リセット完了しました");
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

                            // ロギング（日時記録＆カウント更新）
                            add_chip_buy_log(time(NULL));

                            // MakerChip送出
                            servo_0to180();

                            lcd_display_request(LCD_STATE_THANKS);
                            lcd_display_request(LCD_STATE_DATE_TIME);

                            vTaskDelay(pdMS_TO_TICKS(1000));
                            servo_180to0();
                            vTaskDelay(pdMS_TO_TICKS(2000));
                            memset(&bank_data, 0, sizeof(bank_data));
                            // save_bank_data_to_nvs();

                            lcd_display_request(LCD_STATE_INSERT);
                        } else {
                            lcd_display_request(LCD_STATE_AMOUNT);
                            ext_led_off();
                            // save_bank_data_to_nvs();
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
        .pin_bit_mask = (1ULL << LOG_BUTTON_PIN),
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

void app_main(void)
{
    // 日本時間（JST）にタイムゾーン設定
    setenv("TZ", "JST-9", 1);
    tzset();
    ESP_LOGI(TAG, "--Maker Chip Gacha-- 開始");
    
    // NVS初期化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    load_log_from_nvs();
    ESP_LOGI(TAG, "NVSが初期化されました");

    // ネットワーク初期化は一度だけ
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // // 保存された合計パルス数を読み込み
    // load_bank_data_from_nvs();

    // // 購買ログ
    // log_list_serial_output();

    // Wi-Fi/NTP関連は別タスクで実行
    start_wifi_setup_task();
    
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
    lcd_display_request(LCD_STATE_TITLE); // タイトル画面表示要求
    lcd_display_request(LCD_STATE_STARTING);    //起動中画面表示

    // ボタンの初期状態を確認
    int initial_button_state = gpio_get_level(LOG_BUTTON_PIN);
    ESP_LOGI(TAG, "リセットボタン初期状態: %d （0=押下、1=未押下）", initial_button_state);
    
    // // 現在の入金状態をシリアルに表示（復元されたデータ）
    // ESP_LOGI(TAG, "前回の合計パルス数を復元しました");
    // display_bank_status();
    
    ESP_LOGI(TAG, "--Maker Chip Gacha-- 準備完了");
    ESP_LOGI(TAG, "");
    // ESP_LOGI(TAG, "GPIO%d にコインセレクタを接続してください", COIN_SELECTOR_PIN);
    
    // ESP_LOGI(TAG, "*** メインループ開始 - 時刻:%lu ***", xTaskGetTickCount());
    
    // onboard_led_pwm_fade_test(); // フェードテスト

    // PWM初期化
    ledc_setup();

    // メインループ
    int loop_count = 0;
    while (1) {
        // リセットボタンのチェック
        check_reset_button();

        vTaskDelay(pdMS_TO_TICKS(100));  // 10ms間隔での処理

        //for Debug--------------------------
        loop_count++;
        if (loop_count > 10) {
            loop_count = 0;
            // add_chip_buy_log(time(NULL));
        }
    }
    // アプリ終了時にフェード機能アンインストール（必要なら）
    // ledc_fade_func_uninstall();
}
