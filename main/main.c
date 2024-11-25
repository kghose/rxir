#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_SIZE 2000
#define SAMPLE_FREQ_HZ 200000
#define ADC_CHAN ADC_CHANNEL_5 // GPIO33
#define RAW_DATA_BUF_LEN 256

static void continuous_adc_init(adc_channel_t channel, uint64_t sample_freq_hz,
                                uint64_t sample_len,
                                adc_continuous_handle_t *out_handle) {
  adc_continuous_handle_t handle = NULL;

  adc_continuous_handle_cfg_t adc_config = {
      .max_store_buf_size = 1024,
      .conv_frame_size = sample_len,
  };
  ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));

  adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
  adc_digi_pattern_config_t the_adc_pattern = {
      .atten = ADC_ATTEN_DB_0,
      .channel = channel & 0x7,
      .unit = ADC_UNIT_1,
      .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
  };
  adc_pattern[0] = the_adc_pattern;
  adc_continuous_config_t dig_cfg = {.sample_freq_hz = sample_freq_hz,
                                     .conv_mode = ADC_CONV_SINGLE_UNIT_1,
                                     .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
                                     .pattern_num = 1,
                                     .adc_pattern = adc_pattern};
  ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

  *out_handle = handle;
};

void app_main() {
  uint32_t *sample = malloc(SAMPLE_SIZE * sizeof(uint32_t));
  memset(sample, 0x0, SAMPLE_SIZE);
  uint8_t raw_data_buf[RAW_DATA_BUF_LEN] = {0};
  memset(sample, 0xcc, RAW_DATA_BUF_LEN);

  adc_continuous_handle_t handle = NULL;
  continuous_adc_init(ADC_CHAN, SAMPLE_FREQ_HZ, RAW_DATA_BUF_LEN, &handle);
  ESP_ERROR_CHECK(adc_continuous_start(handle));

  while (1) {
    uint64_t sample_idx = 0;
    bool triggered = false;
    while (1) {
      uint32_t read_data_size = 0;
      esp_err_t ret = adc_continuous_read(handle, raw_data_buf,
                                          RAW_DATA_BUF_LEN, &read_data_size, 1);
      if (ret == ESP_OK) {
        for (int i = 0; i < read_data_size; i += SOC_ADC_DIGI_RESULT_BYTES) {
          adc_digi_output_data_t *p =
              (adc_digi_output_data_t *)&raw_data_buf[i];
          uint32_t chan_num = p->type1.channel;
          uint32_t data = p->type1.data;
          if (chan_num < SOC_ADC_CHANNEL_NUM(ADC_UNIT_1)) {
            uint32_t this_sample = 1 - (uint32_t)(data / 4095);

            if (!triggered && this_sample > 0) {
              triggered = true;
            }

            if (triggered) {
              sample[sample_idx++] = this_sample;
            }

            if (sample_idx == SAMPLE_SIZE) {
              break;
            }
          }
        }
        vTaskDelay(1);
      } else if (ret == ESP_ERR_TIMEOUT) {
        // break;
      }
      vTaskDelay(1);
      if (sample_idx == SAMPLE_SIZE) {
        break;
      }
    }
    for (int i = 0; i < SAMPLE_SIZE; i++) {
      printf("%" PRIu32 ",", sample[i]);
    }
    printf("\n---\n");
    vTaskDelay(10);
  }
  free(sample);
}
