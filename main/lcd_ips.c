#include <stdbool.h>
#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "common.h"
#include "lcd_ips.h"
#include "esp_err.h"
#include "wifi_config.h"

// LCD関連の変数
static spi_device_handle_t lcd_spi = NULL;
static bool lcd_initialized = false;
extern SemaphoreHandle_t lcd_mutex;

// main.cの合計金額・パルス数を参照
extern uint32_t total_value;
extern uint32_t total_pulse_count;

// LCD描画色
uint16_t color_back;    // 背景色
uint16_t color_title;   // タイトル色
uint16_t color_title_back; // タイトル背景色
uint16_t color_black;
uint16_t color_green;
uint16_t color_red;

static void lcd_draw_ui(lcd_disp_state_t lcd_state) {
// static void lcd_draw_ui(void) {
    if (!lcd_initialized) return;
    if (lcd_mutex && xSemaphoreTake(lcd_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    // LCD描画色
    color_back = rgb565(0, 105, 255);       // 背景色(青緑)
    color_title_back = color_back;          // タイトル背景色
    color_title = rgb565(255, 100, 100);
    color_black = rgb565(255, 255, 255);
    color_green = rgb565(200, 150, 255);
    color_red = rgb565(255, 0, 0);

    // lcd_fill_screen(color_back);
    char buffer[32];
    
    switch (lcd_state) {
    case LCD_STATE_TITLE:
        lcd_fill_screen(color_back);

        lcd_print_string(25, 10, "MAKER CHIP", color_title, color_title_back, 2);
        lcd_print_string(40, 40, "GACHA", color_title, color_title_back, 3);
        break;
    case LCD_STATE_STARTING:
        lcd_fill_message_area(color_back);

        lcd_print_string(20, 90, "STARTING", color_black, color_back, 3);
        lcd_print_string(20, 130, "UP...", color_black, color_back, 3);
        lcd_print_string(20, 170, "PLEASE", color_black, color_back, 3);
        lcd_print_string(20, 210, "WAIT", color_black, color_back, 3);
        break;
    case LCD_STATE_INSERT:
        lcd_fill_message_area(color_back);

        lcd_print_string(20, 90, "PLEASE", color_black, color_back, 3);
        lcd_print_string(20, 130, "INSERT", color_black, color_back, 3);
        lcd_print_string(20, 170, "500", color_green, color_back, 5);
        lcd_print_string(90, 220, "YEN", color_black, color_back, 3);
        break;
    case LCD_STATE_AMOUNT:
        lcd_fill_message_area(color_back);

        lcd_print_string(20, 90, "CURRENT", color_black, color_back, 3);
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)bank_data.total_value);
        {
            int value_len = strlen(buffer);
            int value_x = (value_len <= 2) ? 40 : (value_len <= 3) ? 20 : 5;
            lcd_print_string(value_x, 140, buffer, color_green, color_back, 7);
        }
        lcd_print_string(90, 210, "YEN", color_black, color_back, 3);
        break;
    case LCD_STATE_PRESS_BUTTON:
        lcd_fill_message_area(color_back);

        lcd_print_string(20, 90, "PLEASE", color_black, color_back, 3);
        lcd_print_string(20, 130, "PUSH", color_black, color_back, 3);
        lcd_print_string(20, 170, "BUTTON", color_green, color_back, 3);
        break;
    case LCD_STATE_THANKS:
        lcd_fill_message_area(color_back);

        lcd_print_string(20, 90, "THANK", color_black, color_back, 4);
        lcd_print_string(20, 130, "YOU", color_black, color_back, 4);
        lcd_print_string(20, 170, "SO", color_black, color_back, 4);
        lcd_print_string(20, 210, "MUCH!!", color_black, color_back, 4);
        break;
    case LCD_STATE_DATE_TIME:
        lcd_fill_datetime_area(color_back);

        get_latest_date(buffer, 12);
        lcd_print_string(20, 270, buffer, color_black, color_back, 2);
        get_latest_time(buffer, 6);
        lcd_print_string(20, 295, buffer, color_black, color_back, 2);
        //MakerChip購入数を表示
        snprintf(buffer, sizeof(buffer), "<%d>", get_chip_buy_count());
        lcd_print_string(90, 295, buffer, color_black, color_back, 2);
        break;
    case LCD_STATE_WIFI_WAIT:
        lcd_fill_datetime_area(color_back);

        lcd_print_string(20, 270, "WAIT FOR", color_red, color_back, 2);
        lcd_print_string(20, 295, "WIFI CONNECT", color_red, color_back, 2);
        break;
    case LCD_STATE_WIFI_NG:
        lcd_fill_datetime_area(color_back);

        lcd_print_string(20, 270, "WIFI", color_red, color_back, 2);
        lcd_print_string(20, 295, "CONNECT FAIL", color_red, color_back, 2);
        break;
    }
    
    if (lcd_mutex) xSemaphoreGive(lcd_mutex);
}

// LCD表示要求受付
void lcd_show_request(lcd_disp_state_t request) {
    lcd_draw_ui(request);
}

// RGB565色変換関数（BGR順序対応）
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    // BGR順序で変換を試す
    return ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3);
}

// LCDコマンド送信
esp_err_t lcd_send_command(uint8_t cmd) {
    if (!lcd_spi) {
        ESP_LOGE(TAG, "LCD SPI未初期化");
        return ESP_ERR_INVALID_STATE;
    }
    
    gpio_set_level(LCD_DC_PIN, 0);  // Command mode
    
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .flags = 0
    };
    
    esp_err_t ret = spi_device_transmit(lcd_spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCDコマンド送信失敗 (0x%02X): %s", cmd, esp_err_to_name(ret));
    }
    return ret;
}

// LCDデータ送信
esp_err_t lcd_send_data(const uint8_t *data, int len) {
    if (!lcd_spi) {
        ESP_LOGE(TAG, "LCD SPI未初期化");
        return ESP_ERR_INVALID_STATE;
    }
    if (len == 0) return ESP_OK;
    
    gpio_set_level(LCD_DC_PIN, 1);  // Data mode
    
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0
    };
    
    esp_err_t ret = spi_device_transmit(lcd_spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCDデータ送信失敗 (長さ: %d): %s", len, esp_err_to_name(ret));
    }
    return ret;
}

// LCDカラーデータ送信（高速化）
esp_err_t lcd_send_color_data(uint16_t color, int len) {
    if (!lcd_spi) {
        ESP_LOGE(TAG, "LCD SPI未初期化");
        return ESP_ERR_INVALID_STATE;
    }
    if (len == 0) return ESP_OK;

    gpio_set_level(LCD_DC_PIN, 1);  // Data mode

    // ST7789V3はビッグエンディアンを期待（修正）
    uint16_t color_be = color;  // エンディアン変換を削除

    // 効率的な送信のため、小さなバッファを使用
    const int BUFFER_SIZE = 32;  // バッファサイズを小さく
    uint16_t buffer[BUFFER_SIZE];
    for (int i = 0; i < BUFFER_SIZE; i++) {
        buffer[i] = color_be;
    }

    while (len > 0) {
        int chunk = (len > BUFFER_SIZE) ? BUFFER_SIZE : len;
        spi_transaction_t t = {
            .length = chunk * 16,
            .tx_buffer = buffer,
            .flags = 0
        };
        esp_err_t ret = spi_device_transmit(lcd_spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "カラーデータ送信失敗 (残り: %d): %s", len, esp_err_to_name(ret));
            return ret;
        }
        len -= chunk;
    }

    return ESP_OK;
}

// LCD表示領域設定
void lcd_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // ST7789V3の172x320表示用オフセット調整
    x0 += 34;  // 列オフセット
    x1 += 34;  // 列オフセット
    
    lcd_send_command(0x2A);  // Column Address Set
    uint8_t col_data[] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    lcd_send_data(col_data, 4);
    
    lcd_send_command(0x2B);  // Row Address Set
    uint8_t row_data[] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    lcd_send_data(row_data, 4);
    
    lcd_send_command(0x2C);  // Memory Write
}

// LCD画面塗りつぶし
void lcd_fill_screen(uint16_t color) {
    if (!lcd_initialized) {
        ESP_LOGW(TAG, "LCD未初期化のため塗りつぶしをスキップ");
        return;
    }
    
    // ESP_LOGI(TAG, "画面塗りつぶし開始 (色: 0x%04X)", color);
    
    lcd_set_addr_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    
    // 小さなバッファで分割送信（メモリ効率を向上）
    const int CHUNK_SIZE = 1024;  // 1KB単位で送信
    int total_pixels = LCD_WIDTH * LCD_HEIGHT;
    int sent_pixels = 0;
    
    while (sent_pixels < total_pixels) {
        int chunk_pixels = (total_pixels - sent_pixels > CHUNK_SIZE) ? CHUNK_SIZE : (total_pixels - sent_pixels);
        esp_err_t ret = lcd_send_color_data(color, chunk_pixels);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "画面塗りつぶし失敗 (ピクセル位置: %d): %s", sent_pixels, esp_err_to_name(ret));
            return;
        }
        sent_pixels += chunk_pixels;
    }
    
    // ESP_LOGI(TAG, "画面塗りつぶし完了");
}

    // メッセージエリアのみ塗りつぶし（POSI_Y_MESSAGE～POSI_Y_DATE_TIME-1）
    void lcd_fill_message_area(uint16_t color) {
        if (!lcd_initialized) {
            ESP_LOGW(TAG, "LCD未初期化のため塗りつぶしをスキップ");
            return;
        }
        lcd_set_addr_window(0, POSI_Y_MESSAGE, LCD_WIDTH - 1, POSI_Y_DATE_TIME - 1);
        const int CHUNK_SIZE = 1024;
        int total_pixels = LCD_WIDTH * (POSI_Y_DATE_TIME - POSI_Y_MESSAGE);
        int sent_pixels = 0;
        while (sent_pixels < total_pixels) {
            int chunk_pixels = (total_pixels - sent_pixels > CHUNK_SIZE) ? CHUNK_SIZE : (total_pixels - sent_pixels);
            esp_err_t ret = lcd_send_color_data(color, chunk_pixels);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "メッセージエリア塗りつぶし失敗 (ピクセル位置: %d): %s", sent_pixels, esp_err_to_name(ret));
                return;
            }
            sent_pixels += chunk_pixels;
        }
    }

    // 日時エリアのみ塗りつぶし（POSI_Y_DATE_TIME～LCD_HEIGHT-1）
    void lcd_fill_datetime_area(uint16_t color) {
        if (!lcd_initialized) {
            ESP_LOGW(TAG, "LCD未初期化のため塗りつぶしをスキップ");
            return;
        }
        lcd_set_addr_window(0, POSI_Y_DATE_TIME, LCD_WIDTH - 1, LCD_HEIGHT - 1);
        const int CHUNK_SIZE = 1024;
        int total_pixels = LCD_WIDTH * (LCD_HEIGHT - POSI_Y_DATE_TIME);
        int sent_pixels = 0;
        while (sent_pixels < total_pixels) {
            int chunk_pixels = (total_pixels - sent_pixels > CHUNK_SIZE) ? CHUNK_SIZE : (total_pixels - sent_pixels);
            esp_err_t ret = lcd_send_color_data(color, chunk_pixels);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "日時エリア塗りつぶし失敗 (ピクセル位置: %d): %s", sent_pixels, esp_err_to_name(ret));
                return;
            }
            sent_pixels += chunk_pixels;
        }
    }

// 簡易5x7フォント（数字と基本文字のみ、1.47インチ用に追加文字）
static const uint8_t font5x7[][5] = {
    // 0-9
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    // space, !, ", #, $, %, &, ', (, ), *, +, ,, -, ., /
    {0x00,0x00,0x00,0x00,0x00}, // space (10)
    {0x00,0x00,0x5F,0x00,0x00}, // ! (11)
    {0x00,0x07,0x00,0x07,0x00}, // " (12)
    {0x14,0x7F,0x14,0x7F,0x14}, // # (13)
    {0x24,0x2A,0x7F,0x2A,0x12}, // $ (14)
    {0x23,0x13,0x08,0x64,0x62}, // % (15)
    {0x36,0x49,0x55,0x22,0x50}, // & (16)
    {0x00,0x05,0x03,0x00,0x00}, // ' (17)
    {0x00,0x1C,0x22,0x41,0x00}, // ( (18)
    {0x00,0x41,0x22,0x1C,0x00}, // ) (19)
    {0x14,0x08,0x3E,0x08,0x14}, // * (20)
    {0x08,0x08,0x3E,0x08,0x08}, // + (21)
    {0x00,0x50,0x30,0x00,0x00}, // , (22)
    {0x08,0x08,0x08,0x08,0x08}, // - (23)
    {0x00,0x60,0x60,0x00,0x00}, // . (24)
    {0x20,0x10,0x08,0x04,0x02}, // / (25)
    // A-Z
    {0x7E,0x11,0x11,0x11,0x7E}, // A (26)
    {0x7F,0x49,0x49,0x49,0x36}, // B (27)
    {0x3E,0x41,0x41,0x41,0x22}, // C (28)
    {0x7F,0x41,0x41,0x22,0x1C}, // D (29)
    {0x7F,0x49,0x49,0x49,0x41}, // E (30)
    {0x7F,0x09,0x09,0x09,0x01}, // F (31)
    {0x3E,0x41,0x49,0x49,0x7A}, // G (32)
    {0x7F,0x08,0x08,0x08,0x7F}, // H (33)
    {0x00,0x41,0x7F,0x41,0x00}, // I (34)
    {0x20,0x40,0x41,0x3F,0x01}, // J (35)
    {0x7F,0x08,0x14,0x22,0x41}, // K (36)
    {0x7F,0x40,0x40,0x40,0x40}, // L (37)
    {0x7F,0x02,0x0C,0x02,0x7F}, // M (38)
    {0x7F,0x04,0x08,0x10,0x7F}, // N (39)
    {0x3E,0x41,0x41,0x41,0x3E}, // O (40)
    {0x7F,0x09,0x09,0x09,0x06}, // P (41)
    {0x3E,0x41,0x51,0x21,0x5E}, // Q (42)
    {0x7F,0x09,0x19,0x29,0x46}, // R (43)
    {0x46,0x49,0x49,0x49,0x31}, // S (44)
    {0x01,0x01,0x7F,0x01,0x01}, // T (45)
    {0x3F,0x40,0x40,0x40,0x3F}, // U (46)
    {0x1F,0x20,0x40,0x20,0x1F}, // V (47)
    {0x3F,0x40,0x38,0x40,0x3F}, // W (48)
    {0x63,0x14,0x08,0x14,0x63}, // X (49)
    {0x07,0x08,0x70,0x08,0x07}, // Y (50)
    {0x61,0x51,0x49,0x45,0x43}, // Z (51)
    // : ; < = > ? @
    {0x00,0x14,0x00,0x00,0x00}, // : (52)
    {0x00,0x40,0x34,0x00,0x00}, // ; (53)
    {0x08,0x14,0x22,0x41,0x00}, // < (54)
    {0x14,0x14,0x14,0x14,0x14}, // = (55)
    {0x00,0x41,0x22,0x14,0x08}, // > (56)
    {0x02,0x01,0x59,0x09,0x06}, // ? (57)
    {0x3E,0x41,0x5D,0x59,0x4E}, // @ (58)
    // _
    {0x40,0x40,0x40,0x40,0x40}, // _ (59)
};

// 文字描画（1.47インチ用に最適化）
void lcd_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {

    int font_index = -1;
    if (c >= '0' && c <= '9') {
        font_index = c - '0';
    } else if (c == ' ') {
        font_index = 10;
    } else if (c == '!') {
        font_index = 11;
    } else if (c == '"') {
        font_index = 12;
    } else if (c == '#') {
        font_index = 13;
    } else if (c == '$') {
        font_index = 14;
    } else if (c == '%') {
        font_index = 15;
    } else if (c == '&') {
        font_index = 16;
    } else if (c == '\'') {
        font_index = 17;
    } else if (c == '(') {
        font_index = 18;
    } else if (c == ')') {
        font_index = 19;
    } else if (c == '*') {
        font_index = 20;
    } else if (c == '+') {
        font_index = 21;
    } else if (c == ',') {
        font_index = 22;
    } else if (c == '-') {
        font_index = 23;
    } else if (c == '.') {
        font_index = 24;
    } else if (c == '/') {
        font_index = 25;
    } else if (c >= 'A' && c <= 'Z') {
        font_index = 26 + (c - 'A');
    } else if (c == ':') {
        font_index = 52;
    } else if (c == ';') {
        font_index = 53;
    } else if (c == '<') {
        font_index = 54;
    } else if (c == '=') {
        font_index = 55;
    } else if (c == '>') {
        font_index = 56;
    } else if (c == '?') {
        font_index = 57;
    } else if (c == '@') {
        font_index = 58;
    } else if (c == '_') {
        font_index = 59;
    } else if (c >= 'a' && c <= 'z') {
        font_index = 26 + (c - 'a'); // 小文字は大文字で代用
    } else {
        font_index = 10; // 未定義文字はspace
    }
    if (font_index < 0) return;
    
    // より大きなサイズ用のピクセル描画を最適化
    for (int i = 0; i < 5; i++) {
        uint8_t line = font5x7[font_index][i];
        for (int j = 0; j < 7; j++) {
            uint16_t pixel_color = (line & (1 << j)) ? color : bg;
            
            // サイズに応じてピクセルを拡大（効率化）
            if (size > 3) {
                // 大きなサイズの場合は一括描画
                int block_x = x + i * size;
                int block_y = y + j * size;
                
                if (block_x >= 0 && block_x + size <= LCD_WIDTH && 
                    block_y >= 0 && block_y + size <= LCD_HEIGHT) {
                    lcd_set_addr_window(block_x, block_y, block_x + size - 1, block_y + size - 1);
                    lcd_send_color_data(pixel_color, size * size);
                }
            } else {
                // 小さなサイズの場合は個別描画
                for (int dx = 0; dx < size; dx++) {
                    for (int dy = 0; dy < size; dy++) {
                        int px = x + i * size + dx;
                        int py = y + j * size + dy;
                        if (px >= 0 && px < LCD_WIDTH && py >= 0 && py < LCD_HEIGHT) {
                            lcd_set_addr_window(px, py, px, py);
                            lcd_send_color_data(pixel_color, 1);
                        }
                    }
                }
            }
        }
    }
}

// 文字列描画（1.47インチ用に最適化）
void lcd_print_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size) {
    int cursor_x = x;
    while (*str) {
        lcd_draw_char(cursor_x, y, *str, color, bg, size);
        // サイズに応じて文字間隔を調整
        if (size >= 4) {
            cursor_x += 6 * size + 2;  // 大きなフォント用の間隔
        } else if (size >= 3) {
            cursor_x += 6 * size + 1;  // 中サイズフォント用の間隔
        } else {
            cursor_x += 6 * size;      // 小サイズフォント用の間隔
        }
        str++;
    }
}

// LCD初期化
esp_err_t lcd_ips_init(void) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "LCD初期化開始...");
    
    // GPIO設定
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_DC_PIN) | (1ULL << LCD_RST_PIN) | (1ULL << LCD_BLK_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD GPIO設定失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "LCD GPIO設定完了");
    
    // 初期状態設定
    gpio_set_level(LCD_BLK_PIN, 0);  // バックライト一時OFF
    gpio_set_level(LCD_RST_PIN, 1);   // リセット解除
    gpio_set_level(LCD_DC_PIN, 0);    // コマンドモード
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // SPI設定
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,
        .flags = SPICOMMON_BUSFLAG_MASTER
    };
    
    ret = spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus初期化失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus初期化完了");
    
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_CLOCK_HZ,
        .mode = 0,                    // SPI Mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = LCD_CS_PIN,
        .queue_size = 7,
        .flags = SPI_DEVICE_HALFDUPLEX
    };
    
    ret = spi_bus_add_device(LCD_SPI_HOST, &devcfg, &lcd_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device追加失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI device追加完了");
    
    // ハードウェアリセット（確実な実行）
    ESP_LOGI(TAG, "LCDハードウェアリセット開始");
    gpio_set_level(LCD_RST_PIN, 0);   // リセット実行
    vTaskDelay(pdMS_TO_TICKS(20));    // リセット期間延長
    gpio_set_level(LCD_RST_PIN, 1);   // リセット解除
    vTaskDelay(pdMS_TO_TICKS(150));   // 安定化待機延長
    ESP_LOGI(TAG, "LCDハードウェアリセット完了");
    
    // ST7789V3初期化シーケンス（改良版）
    ESP_LOGI(TAG, "ST7789V3初期化シーケンス開始");
    
    // ソフトウェアリセット
    ret = lcd_send_command(0x01);  // Software Reset
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ソフトウェアリセットコマンド失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_LOGI(TAG, "ソフトウェアリセット完了");
    
    // スリープアウト
    ret = lcd_send_command(0x11);  // Sleep Out
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "スリープアウトコマンド失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_LOGI(TAG, "スリープアウト完了");
    
    // カラーモード設定
    ret = lcd_send_command(0x3A);  // Pixel Format Set
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "カラーモード設定コマンド失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    uint8_t pixel_format = 0x55;  // 16-bit RGB565
    ret = lcd_send_data(&pixel_format, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "カラーモード設定データ失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "カラーモード設定完了 (RGB565)");
    
    // メモリアクセス制御（ST7789V3 172x320用）
    ret = lcd_send_command(0x36);  // Memory Data Access Control
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "メモリアクセス制御コマンド失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    // MY=0, MX=0, MV=0, ML=0, RGB=1, MH=0 (縦長表示、RGB順序)
    uint8_t madctl = 0x08;  // RGB bit設定
    ret = lcd_send_data(&madctl, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "メモリアクセス制御データ失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "メモリアクセス制御設定完了 (RGB順序)");
    
    // 表示領域設定（ST7789V3の172x320に最適化）
    ret = lcd_send_command(0x2A);  // Column Address Set
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "列アドレス設定コマンド失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    // ST7789V3の172x320表示用オフセット設定
    uint8_t col_data[] = {0x00, 0x22, 0x00, 0x22 + LCD_WIDTH - 1};  // 34ピクセルオフセット
    ret = lcd_send_data(col_data, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "列アドレス設定データ失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = lcd_send_command(0x2B);  // Row Address Set  
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "行アドレス設定コマンド失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    uint8_t row_data[] = {0x00, 0x00, (LCD_HEIGHT - 1) >> 8, (LCD_HEIGHT - 1) & 0xFF};
    ret = lcd_send_data(row_data, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "行アドレス設定データ失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "表示領域設定完了 (172x320 with offset)");
    
    // 反転表示OFF
    ret = lcd_send_command(0x20);  // Display Inversion Off
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "反転表示OFF失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // ノーマル表示モード
    ret = lcd_send_command(0x13);  // Normal Display Mode On
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ノーマル表示モード失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 表示ON
    ret = lcd_send_command(0x29);  // Display On
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "表示ONコマンド失敗: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "表示ON完了");
    
    // バックライトON
    gpio_set_level(LCD_BLK_PIN, 1);
    ESP_LOGI(TAG, "バックライトON");
    
    lcd_initialized = true;
    
    ESP_LOGI(TAG, "ST7789V3 LCD初期化完了");
    return ESP_OK;
    return ESP_OK;
}

// LCD入金状況表示（1.47インチ用に最適化・2列表示）
void lcd_show_message(const char* msg) {
    // 画面クリア（黒）
    lcd_fill_screen(0x0000);
    // メッセージを中央に表示（1行のみ、中央寄せ風）
    int len = strlen(msg);
    int char_width = 8; // フォント幅（例: 8ピクセル）
    int x = (LCD_WIDTH - len * char_width) / 2;
    int y = LCD_HEIGHT / 2 - 8;
    lcd_print_string(x, y, msg, 0xFFFF, 0x0000, 1); // 白文字・黒背景
}


