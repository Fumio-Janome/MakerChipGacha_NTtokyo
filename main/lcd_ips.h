#ifndef LCD_IPS_H
#define LCD_IPS_H

#include <stdint.h>

// bool lcd_thanks_timeout(void);
void lcd_clear(void);
void lcd_draw_string_centered(int row, const char* str);

#endif // LCD_IPS_H
// ST7789V3 LCD設定
#define LCD_WIDTH 172
#define LCD_HEIGHT 320
#define LCD_SPI_HOST SPI2_HOST
#define LCD_SPI_CLOCK_HZ (10 * 1000 * 1000)  // 10MHz（安定性重視）

#define POSI_Y_TITLE    0
#define POSI_Y_MESSAGE  90
#define POSI_Y_DATE_TIME 270

// 関数プロトタイプ宣言
esp_err_t lcd_send_command(uint8_t cmd);
esp_err_t lcd_send_data(const uint8_t *data, int len);
esp_err_t lcd_send_color_data(uint16_t color, int len);
void lcd_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_fill_screen(uint16_t color);
void lcd_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
void lcd_print_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size);
void lcd_display_task(void *pvParameters);  // LCD表示専用タスク
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
// 追加: メッセージエリア・日時エリアのみ塗りつぶし
void lcd_fill_message_area(uint16_t color);
void lcd_fill_datetime_area(uint16_t color);
