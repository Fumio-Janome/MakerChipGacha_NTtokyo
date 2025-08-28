
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define SERVO_PIN 0
#define SERVO_LEDC_TIMER      LEDC_TIMER_0
#define SERVO_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_CHANNEL    LEDC_CHANNEL_1
#define SERVO_PWM_FREQ        50
#define SERVO_PWM_BITWIDTH    LEDC_TIMER_14_BIT
// #define SERVO_PWM_BITWIDTH    LEDC_TIMER_16_BIT

// 角度→デューティ変換（サーボ用: 0-180度→0.5ms-2.5ms）
static uint32_t angle_to_duty(float deg)
{
  // 0度=0.5ms, 180度=2.5ms（パルス幅広め）
  if (deg < 0.0f) deg = 0.0f;
  if (deg > 180.0f) deg = 180.0f;
  float min_ms = 0.5f;
  float max_ms = 2.5f;
  float ms = min_ms + (max_ms - min_ms) * (deg / 180.0f);
  float duty_ratio = ms / 20.0f; // 20ms周期(50Hz)
  return (uint32_t)(duty_ratio * ((1 << 16) - 1));
}

void servo_task(void *param)
{
  while (1) {
    // 0度
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, angle_to_duty(0.0f));
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
    ESP_LOGI("SERVO", "0 deg");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 90度
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, angle_to_duty(90.0f));
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
    ESP_LOGI("SERVO", "90 deg");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 0度
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, angle_to_duty(0.0f));
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
    ESP_LOGI("SERVO", "0 deg");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // -90度（サーボによっては0未満不可）
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, angle_to_duty(-90.0f));
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
    ESP_LOGI("SERVO", "-90 deg");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void ledc_setup()
{
  // タイマー設定
  ledc_timer_config_t timer_conf = {
    .speed_mode = SERVO_LEDC_MODE,
    .timer_num = SERVO_LEDC_TIMER,
    .duty_resolution = SERVO_PWM_BITWIDTH,
    .freq_hz = SERVO_PWM_FREQ,
    .clk_cfg = LEDC_AUTO_CLK
  };
  ledc_timer_config(&timer_conf);

  // チャンネル設定
  ledc_channel_config_t channel_conf = {
    .gpio_num = SERVO_PIN,
    .speed_mode = SERVO_LEDC_MODE,
    .channel = SERVO_LEDC_CHANNEL,
    .timer_sel = SERVO_LEDC_TIMER,
    .duty = 0,
    .hpoint = 0,
    .flags.output_invert = 0
  };
  ledc_channel_config(&channel_conf);

  xTaskCreate(servo_task, "servo_task", 2048, NULL, 5, NULL);
}