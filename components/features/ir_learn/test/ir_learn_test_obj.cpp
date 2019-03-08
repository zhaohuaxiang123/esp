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

/* FreeRTOS includes */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ESP32 includes */
#include "esp_err.h"
#include "esp_log.h"
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"
#include "soc/rmt_reg.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "unity.h"

/* IR learn includes */
#include "ir_learn.h"

#define SEND_RMT_TX_CHN      RMT_CHANNEL_1
#define SEND_RMT_TX_GPIO     GPIO_NUM_19
#define SEND_RMT_TX_CARRIER  (1)
#define RECV_RMT_TX_CHN      RMT_CHANNEL_0
#define RECV_RMT_TX_GPIO     GPIO_NUM_18
#define RECV_RMT_TX_CARRIER  (1)
#define IR_LEARN_GPIO        GPIO_NUM_17
#define IR_LEARN_BUF_LEN     (250)

#define IR_ERR_CHECK(con, err, format, ...) if (con) { \
            ESP_LOGE(TAG, format , ##__VA_ARGS__); \
            return err;}

static const char *TAG = "ir_learn_test";
static bool g_send_task_flag = false;

static void ir_learn_test_tx_init()
{
    rmt_config_t rmt_tx;
    rmt_tx.channel = SEND_RMT_TX_CHN;
    rmt_tx.gpio_num = SEND_RMT_TX_GPIO;
    rmt_tx.mem_block_num = 1;
    rmt_tx.clk_div = 100;
    rmt_tx.tx_config.loop_en = false;
    rmt_tx.tx_config.carrier_en = SEND_RMT_TX_CARRIER;
    rmt_tx.tx_config.carrier_duty_percent = 50;
    rmt_tx.tx_config.carrier_freq_hz = 38000;
    rmt_tx.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
    rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    rmt_tx.tx_config.idle_output_en = true;
    rmt_tx.rmt_mode = RMT_MODE_TX;
    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 0, 0);
}

static void ir_learn_test_tx_deinit()
{
    rmt_driver_uninstall(SEND_RMT_TX_CHN);
}

static void ir_learn_tx_task(void *arg)
{
    // nec: header(1) | addr(8 LSB<-->MSB) | ~addr(8 LSB<-->MSB) | cmd(8 LSB<-->MSB) | ~cmd(8 LSB<-->MSB)
    uint16_t nec_addr = 0x33;
    uint16_t nec_cmd = 0x9b;

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ir_learn_test_tx_init();

    while (g_send_task_flag) {
        ESP_LOGI(TAG, "ir_nec_send, nec_addr: %2x, nec_cmd: %2x", nec_addr, nec_cmd);

        ir_nec_send(SEND_RMT_TX_CHN, nec_addr, nec_cmd, 100 / portTICK_PERIOD_MS);

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    ir_learn_test_tx_deinit();
    vTaskDelete(NULL);
}

void ir_learn_test()
{
    xTaskCreate(ir_learn_tx_task, "ir_learn_tx_task", 1024 * 3, NULL, 6, NULL);
    g_send_task_flag = true;

    ir_learn_result_t *ir_data = (ir_learn_result_t *)calloc(1, sizeof(ir_learn_result_t) + IR_LEARN_BUF_LEN * 2);
    ir_data->message_len = IR_LEARN_BUF_LEN;

    IRLearn *ir_learn = new IRLearn(IR_LEARN_GPIO);
    ir_learn->start();

    // wait 60 second
    ir_learn->wait_finish(60 * 1000 / portTICK_PERIOD_MS);

    ir_learn->stop();
    ir_learn->get_result(ir_data, true);
    ESP_LOGI(TAG, "IR learn result: freq: [%fHz], duty: [%.2f]", ir_data->freq, ir_data->duty);

    ir_learn->decode(ir_data);
    ESP_LOGI(TAG, "protocol: %d, bits: %d, value: %4x, addr: %2x, cmd: %2x",
             ir_data->proto, ir_data->bits, ir_data->value, ir_data->addr, ir_data->cmd);

    ir_learn_send_init_t send_init;
    send_init.channel = RECV_RMT_TX_CHN;
    send_init.gpio = RECV_RMT_TX_GPIO;
    send_init.carrier_en = RECV_RMT_TX_CARRIER;
    send_init.freq = ir_data->freq;
    send_init.duty = ir_data->duty * 100;

    ir_learn->send_init(&send_init);
    ir_learn->send(RECV_RMT_TX_CHN, ir_data, 100 / portTICK_PERIOD_MS);
    ir_learn->send_deinit(RECV_RMT_TX_CHN);

    delete(ir_learn);
    free(ir_data);

    // exit IR learn send task
    g_send_task_flag = false;
}

// please connect gpio17 with gpio19
TEST_CASE("IR learn cpp test", "[IR][Learn]")
{
    ir_learn_test();
}
