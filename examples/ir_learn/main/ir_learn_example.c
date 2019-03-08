// Copyright 2015-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/* C includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* FreeRTOS includes */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

/* ESP32 includes */
#include "esp_log.h"
#include "sys/param.h"
#include "soc/rmt_reg.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"

/* IR learn includes */
#include "ir_learn.h"
static const char* TAG = "ir_learn_example";

#define IR_ERR_CHECK(con, format, ...) if (con) { \
        ESP_LOGE(TAG, format , ##__VA_ARGS__); break;}

#define IR_SEND_NEC_CHN     RMT_CHANNEL_0
#define IR_SEND_RC5_CHN     RMT_CHANNEL_1
#define IR_SEND_RC6_CHN     RMT_CHANNEL_2
#define IR_SEND_NEC_GPIO    GPIO_NUM_16
#define IR_SEND_RC5_GPIO    GPIO_NUM_17
#define IR_SEND_RC6_GPIO    GPIO_NUM_18
#define IR_NEC_FREQ         (38000)
#define IR_RC5_FREQ         (36000)
#define IR_RC6_FREQ         (36000)

#define IR_LEARN_RECV_GPIO  GPIO_NUM_19
#define IR_LEARN_SEND_GPIO  GPIO_NUM_21
#define IR_LEARN_SEND_CHN   RMT_CHANNEL_3
#define IR_LEARN_BUF_LEN    (250)

static void ir_send_nec_task(void *arg)
{
    // nec: header(1) | addr(8 LSB<-->MSB) | ~addr(8 LSB<-->MSB) | cmd(8 LSB<-->MSB) | ~cmd(8 LSB<-->MSB)
    uint16_t nec_addr = 0x33;
    uint16_t nec_cmd  = 0x9b;

    ir_learn_send_init_t send_init = {
        .channel = IR_SEND_NEC_CHN,
        .gpio = IR_SEND_NEC_GPIO,
        .carrier_en = true,
        .freq = IR_NEC_FREQ,
        .duty = 50,
    };
    ir_learn_send_init(&send_init);

    for (;;) {
        ESP_LOGD(TAG, "ir_nec_send, nec_addr: %2x, nec_cmd: %2x", nec_addr, nec_cmd);

        ir_nec_send(IR_SEND_NEC_CHN, nec_addr, nec_cmd, 100 / portTICK_PERIOD_MS);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

static void ir_send_rc5_task(void *arg)
{
    //   rc5 bit: start(1+1) | field(1) | addr(5 MSB<-->LSB) | cmd(6 MSB<-->LSB)
    // rc5 value: field + addr + cmd
    bool rc5_toggle = true;
    uint32_t rc5_addr = 0x14;
    uint32_t rc5_cmd = 0x25;

    ir_learn_send_init_t send_init = {
        .channel = IR_SEND_RC5_CHN,
        .gpio = IR_SEND_RC5_GPIO,
        .carrier_en = true,
        .freq = IR_RC5_FREQ,
        .duty = 50,
    };
    ir_learn_send_init(&send_init);

    for (;;) {
        ESP_LOGD(TAG, "ir_rc5_send, toggle: %d, rc5_addr: %2x, rc5_cmd: %2x", rc5_toggle, rc5_addr, rc5_cmd);

        ir_rc5_send(IR_SEND_RC5_CHN, rc5_toggle, rc5_addr, rc5_cmd, 100 / portTICK_PERIOD_MS);
        rc5_toggle = !rc5_toggle;

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

static void ir_send_rc6_task(void *arg)
{
    //   rc6 bit: header(1+1) | field(3+1) | addr(8 MSB<-->LSB) | cmd(8 MSB<-->LSB)
    // rc6 value: field + addr + cmd
    bool rc6_toggle = true;
    uint8_t rc6_mode = 0;
    uint32_t rc6_addr = 0x34;
    uint32_t rc6_cmd = 0x29;

    ir_learn_send_init_t send_init = {
        .channel = IR_SEND_RC6_CHN,
        .gpio = IR_SEND_RC6_GPIO,
        .carrier_en = true,
        .freq = IR_RC6_FREQ,
        .duty = 50,
    };
    ir_learn_send_init(&send_init);

    for (;;) {
        ESP_LOGD(TAG, "ir_rc6_send, mode: %d, toggle: %d, rc6_addr: %2x, rc6_cmd: %2x",
                 rc6_mode, rc6_toggle, rc6_addr, rc6_cmd);

        ir_rc6_send(IR_SEND_RC6_CHN, rc6_mode, rc6_toggle, rc6_addr, rc6_cmd, portMAX_DELAY);
        rc6_toggle = !rc6_toggle;

        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

static void ir_learn_recv_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    ir_learn_result_t *ir_data = NULL;
    ir_learn_handle_t ir_handle = NULL;
    ir_learn_state_t state = IR_LEARN_NONE;

    for ( ;; ) {
        ir_handle = ir_learn_create(IR_LEARN_RECV_GPIO);
        IR_ERR_CHECK(ir_handle == NULL, "ir_learn_create fail");

        ret = ir_learn_start(ir_handle);
        IR_ERR_CHECK(ret != ESP_OK, "ir_learn_start fail");

        // wait 60s
        ir_learn_wait_finish(ir_handle, 60 * 1000 / portTICK_PERIOD_MS);

        ret = ir_learn_stop(ir_handle);
        IR_ERR_CHECK(ret != ESP_OK, "ir_learn_stop fail");

        state = ir_learn_get_state(ir_handle);

        if (state != IR_LEARN_FINISH) {
            ESP_LOGE(TAG, "ir_learn_get_state error, state: %d", state);
        } else {
            ir_data = calloc(1, sizeof(ir_learn_result_t) + IR_LEARN_BUF_LEN * 2);
            ir_data->message_len = IR_LEARN_BUF_LEN;

            ret = ir_learn_get_result(ir_handle, ir_data, true);

            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "IR learn result: freq: [%fHz], duty: [%.2f]", ir_data->freq, ir_data->duty);

                ir_learn_decode(ir_data);
                ESP_LOGI(TAG, "protocol: %d, bits: %d, value: %4x, addr: %2x, cmd: %2x",
                         ir_data->proto, ir_data->bits, ir_data->value, ir_data->addr, ir_data->cmd);

                // Optional to send
                ir_learn_send_init_t send_init = {
                    .channel = IR_LEARN_SEND_CHN,
                    .gpio = IR_LEARN_SEND_GPIO,
                    .carrier_en = true,
                    .freq = ir_data->freq,
                    .duty = ir_data->duty * 100,
                };
                ir_learn_send_init(&send_init);
                ir_learn_send(IR_LEARN_SEND_CHN, ir_data, 100 / portTICK_PERIOD_MS);
                ir_learn_send_deinit(IR_LEARN_SEND_CHN);
            }

            free(ir_data);
            ir_data = NULL;
        }

        ret = ir_learn_delete(ir_handle);
        IR_ERR_CHECK(ret != ESP_OK, "ir_learn_delete, ret: %d", ret);
        ir_handle = NULL;

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

void app_main()
{
    xTaskCreate(ir_send_nec_task, "ir_send_nec_task", 1024 * 3, NULL, 6, NULL);
    xTaskCreate(ir_send_rc5_task, "ir_send_rc5_task", 1024 * 3, NULL, 6, NULL);
    xTaskCreate(ir_send_rc6_task, "ir_send_rc6_task", 1024 * 3, NULL, 6, NULL);
    xTaskCreate(ir_learn_recv_task, "ir_learn_recv_task", 1024 * 3, NULL, 6, NULL);
}
