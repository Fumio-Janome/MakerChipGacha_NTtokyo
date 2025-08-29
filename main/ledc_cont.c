#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "common.h"

#define SERVO_PIN             SERVO_MOTOR_PIN
#define SERVO_LEDC_TIMER      LEDC_TIMER_0
#define SERVO_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_CHANNEL    LEDC_CHANNEL_1
#define SERVO_PWM_FREQ        50
#define SERVO_PWM_BITWIDTH    LEDC_TIMER_14_BIT

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
  return (uint32_t)(duty_ratio * ((1 << SERVO_PWM_BITWIDTH) - 1));
}

void servo_to0(void)
{
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, angle_to_duty(0.0f));
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
}

void servo_0to180(void)
{
    for (float deg = 0.0f; deg <= 180.0f; deg += 5.0f) {
        ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, angle_to_duty(deg));
        ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void servo_180to0(void)
{
    for (float deg = 180.0f; deg >= 0.0f; deg -= 5.0f) {
        ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, angle_to_duty(deg));
        ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void servo_demo_180(void)
{
    servo_0to180();
    vTaskDelay(pdMS_TO_TICKS(1000));
    servo_180to0();
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void servo_task(void *param)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "servo_task start");
    
    while (1) {
      servo_demo_180();
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

    servo_to0();

    // for Demo
    // xTaskCreate(servo_task, "servo_task", 2048, NULL, 5, NULL);
}