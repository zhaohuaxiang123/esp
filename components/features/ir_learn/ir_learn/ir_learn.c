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

/* ESP32 includes */
#include "sys/param.h"
#include "soc/rmt_reg.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"
#include "esp_log.h"
#include "esp_err.h"

/* IR learn includes */
#include "ir_learn.h"
#include "ir_codec.h"

static const char *TAG = "ir_learn";
#define IR_ERR_CHECK(a, ret, str)  if(a) {                                          \
            ESP_LOGE(TAG,"%s:%d (%s): %s", __FILE__, __LINE__, __FUNCTION__, str);  \
            return (ret);                                                           \
        }

// buffer length configuration
#define IR_LEARN_MSG_BUF_LEN  (250)
#define IR_LEARN_RPT_BUF_LEN  (10)
#define IR_FILTER_BUF_LEN     (10)
#define IR_CARRIER_BUF_LEN    (100)
#define IR_CARRIER_FILTER_LEN (10)     // filter out the smallest and largest 10 items

// carrier configuration
#define IR_LEARN_FREQ_MIN     (20000)  // 20KHZ
#define IR_LEARN_FREQ_MAX     (80000)  // 80kHZ
#define IR_LEARN_CARRIER_WIDTH_MAX (1000000/IR_LEARN_FREQ_MIN)   // freq: 20kHZ, duty: 100%
#define IR_LEARN_CARRIER_WIDTH_MIN (1000000/2/IR_LEARN_FREQ_MAX) // freq: 80kHZ, duty: 50%

// level period configuration
#define IR_LEARN_PERIOD_MIN   (200)    // 200us
#define IR_LEARN_PERIOD_MAX   (20000)  // 20000us = 20ms
#define IR_LEARN_REPEAT_MAX   (120000) // 120000us = 120ms
#define IR_LEARN_CHECK_PERIOD (100/portTICK_RATE_MS) // 100ms

// IR learn type
#define IR_LEARN_MSG          (0)      // IR learn message data
#define IR_LEARN_RPT          (1)      // IR learn repeat data

typedef struct {
    uint8_t low_cnt;                   // data count in low buffer
    uint8_t high_cnt;                  // data count in high buffer
    uint8_t filter_cnt;                // data count in filter buffer
    uint8_t low[IR_CARRIER_BUF_LEN];   // low level period buffer
    uint8_t high[IR_CARRIER_BUF_LEN];  // high level period buffer
    uint8_t filter[IR_FILTER_BUF_LEN]; // temperate level period buffer to remove jitter
} ir_learn_carrier_t;

typedef struct {
    uint8_t type;                          // learn type: 0 message, 1 repeat
    uint16_t msg_len;                      // data count in message data buffer
    uint16_t repeat_len;                   // data count in repeat data buffer
    uint32_t msg[IR_LEARN_MSG_BUF_LEN];    // message data buffer
    uint32_t repeat[IR_LEARN_RPT_BUF_LEN]; // repeat data buffer
} ir_learn_msg_t;

typedef struct {
    ir_learn_carrier_t carrier;
    ir_learn_msg_t msg;
    ir_learn_state_t state;
    bool finished;
    int64_t pre_time;
    gpio_num_t gpio;
    SemaphoreHandle_t sem;
} ir_learn_t;

bool ir_nec_decode(ir_learn_result_t *result);
bool ir_rc5_decode(ir_learn_result_t *result);
bool ir_rc6_decode(ir_learn_result_t *result);
BaseType_t ir_learn_send_sem_take(TickType_t xTicksToWait);
static SemaphoreHandle_t g_ir_send_sem = NULL;

static void IRAM_ATTR ir_learn_isr_handle(void *arg)
{
    // udpate time and gpio level upon enter ist in order to avoid diviation
    ir_learn_t *ir_learn = (ir_learn_t *)arg;
    int64_t cur_time = esp_timer_get_time();
    int64_t period   = cur_time - ir_learn->pre_time;
    int gpio_level   = gpio_get_level((gpio_num_t) ir_learn->gpio);

    // just update cur_time and return when:
    // 1. first time enter isr;
    // 2. IR learn is finished;
    if (ir_learn->pre_time == 0 || ir_learn->finished) {
        ir_learn->pre_time = cur_time;
        return;
    }

    ir_learn->pre_time = cur_time;

    // check overflow
    if (ir_learn->msg.msg_len >= IR_LEARN_MSG_BUF_LEN) {
        ir_learn->state = IR_LEARN_OVERFLOW;
        ir_learn->finished = true;
        goto EXIT;
    }

    // save mark data
    if (period >= IR_LEARN_CARRIER_WIDTH_MIN && period <= IR_LEARN_CARRIER_WIDTH_MAX) {
        // save carrier data independently
        if ((ir_learn->carrier.high_cnt < IR_CARRIER_BUF_LEN) && (ir_learn->carrier.low_cnt < IR_CARRIER_BUF_LEN)) {
            if (gpio_level == 1) {
                ir_learn->carrier.high[ir_learn->carrier.high_cnt++] = period;
            } else {
                ir_learn->carrier.low[ir_learn->carrier.low_cnt++] = period;
            }
        } else {
            // carrier is saved, if message data is saved already, the learn process is finished.
            if (ir_learn->state == IR_LEARN_MSG) {
                ir_learn->state = IR_LEARN_FINISH;
                ir_learn->finished = true;
                goto EXIT;
            } else {
                ir_learn->state = IR_LEARN_CARRIER;
            }
        }

        // save data to filter buffer firstly in order to remove jitter
        if (ir_learn->carrier.filter_cnt < IR_FILTER_BUF_LEN) {
            ir_learn->carrier.filter[ir_learn->carrier.filter_cnt++] = period;
        } else {
            // the filter buffer is full, start a new mark
            if (ir_learn->carrier.filter_cnt == IR_FILTER_BUF_LEN) {
                ir_learn->carrier.filter_cnt++;

                if (ir_learn->msg.type == IR_LEARN_MSG && ir_learn->msg.msg_len > 0) {
                    ir_learn->msg.msg_len++;
                } else if (ir_learn->msg.type == IR_LEARN_RPT
                           && ir_learn->msg.repeat_len > 0
                           && ir_learn->msg.repeat_len < IR_LEARN_RPT_BUF_LEN) {
                    ir_learn->msg.repeat_len++;
                }

                for (int i = 0; i < IR_FILTER_BUF_LEN; ++i) {
                    if (ir_learn->msg.type == IR_LEARN_MSG) {
                        ir_learn->msg.msg[ir_learn->msg.msg_len] += ir_learn->carrier.filter[i];
                    } else {
                        ir_learn->msg.repeat[ir_learn->msg.repeat_len] += ir_learn->carrier.filter[i];
                    }
                }
            }

            // the filter buffer is full, accumulate the carrier time to current mark
            if (ir_learn->msg.type == IR_LEARN_MSG) {
                ir_learn->msg.msg[ir_learn->msg.msg_len] += period;
            } else {
                ir_learn->msg.repeat[ir_learn->msg.repeat_len] += period;
            }
        }
    } else if (period > IR_LEARN_CARRIER_WIDTH_MAX && period <= IR_LEARN_PERIOD_MAX) {
        // the previous mark is end, start a new space
        if (ir_learn->carrier.filter_cnt > IR_LEARN_MSG) {
            if (ir_learn->msg.type == IR_LEARN_MSG) {
                ir_learn->msg.msg_len++;
            } else if (ir_learn->msg.repeat_len < IR_LEARN_RPT_BUF_LEN) {
                ir_learn->msg.repeat_len++;
            }
        } else {
            // remove the front jitter data
            if (ir_learn->msg.msg_len > 0) {
                for (int i = 0; i < ir_learn->carrier.filter_cnt; ++i) {
                    if (ir_learn->msg.type == IR_LEARN_MSG) {
                        ir_learn->msg.msg[ir_learn->msg.msg_len] += ir_learn->carrier.filter[i];
                    } else {
                        ir_learn->msg.repeat[ir_learn->msg.repeat_len] += ir_learn->carrier.filter[i];
                    }
                }
            }
        }

        // reset filter buffer length to zero to save next mark data
        ir_learn->carrier.filter_cnt = 0;

        if (ir_learn->msg.type == IR_LEARN_MSG && ir_learn->msg.msg_len > 0) {
            ir_learn->msg.msg[ir_learn->msg.msg_len] += period;
        } else if (ir_learn->msg.type == IR_LEARN_RPT && ir_learn->msg.repeat_len > 0) {
            ir_learn->msg.repeat[ir_learn->msg.repeat_len] += period;
        }
    } else if (period > IR_LEARN_PERIOD_MAX && period <= IR_LEARN_REPEAT_MAX) {
        // save repeat data after message data.
        if (ir_learn->msg.msg_len > 0 && ir_learn->msg.repeat_len == 0) {
            ir_learn->msg.type = IR_LEARN_RPT;
            ir_learn->msg.repeat[ir_learn->msg.repeat_len] = period;
            ir_learn->msg.repeat_len++;
        } else if (ir_learn->msg.type == IR_LEARN_RPT
                   && ir_learn->msg.repeat_len > 0
                   && ir_learn->msg.repeat_len < (IR_LEARN_RPT_BUF_LEN - 1)) {
            ir_learn->msg.repeat_len++;
            ir_learn->msg.repeat[ir_learn->msg.repeat_len] = period;
            ir_learn->msg.repeat_len++;
            // repeat data is saved, the learn process is finished.
            ir_learn->msg.type = IR_LEARN_MSG;
            ir_learn->state = IR_LEARN_FINISH;
            ir_learn->finished = true;
        }
    } else if (period > IR_LEARN_REPEAT_MAX) {
        if (ir_learn->msg.msg_len == 0) {
            // when the code goes to this location, means this is not the first time to enter isr, but msg_len is zero(reseted in ir_learn_start(...)),
            // this indicates the IR learn process has been restarted.
            // Therefor the period is the gap duration between differenet IR learn process, just ignore it.
            ;
        } else if (ir_learn->state == IR_LEARN_CARRIER) {
            // message data is saved, if carrier has been saved already, learn process finished.
            ir_learn->state = IR_LEARN_FINISH;
            ir_learn->finished = true;
        } else {
            ir_learn->state = IR_LEARN_MSG;
        }
    }

EXIT:

    if (ir_learn->finished) {
        if (ir_learn->state == IR_LEARN_FINISH) {
            ir_learn->msg.msg_len++;
        }

        xSemaphoreGiveFromISR(ir_learn->sem, 0);
    }
}

static void array_sort(uint8_t array[], uint8_t arr_len)
{
    uint8_t i, j, temp;

    for (i = 0; i < arr_len; ++i) {
        for (j = i + 1; j < arr_len; ++j) {
            if (array[j] < array[i]) {
                temp = array[i];
                array[i] = array[j];
                array[j] = temp;
            }
        }
    }
}

esp_err_t ir_learn_get_result(ir_learn_handle_t ir_learn_hdl, ir_learn_result_t *result, bool enable_debug)
{
    IR_ERR_CHECK(ir_learn_hdl == NULL, ESP_ERR_INVALID_STATE, "IR learn handle is NULL");
    ir_learn_t *ir_learn = (ir_learn_t *)ir_learn_hdl;
    IR_ERR_CHECK(ir_learn->state != IR_LEARN_FINISH, ESP_ERR_INVALID_STATE, "IR learn not finished");
    IR_ERR_CHECK(result == NULL, ESP_ERR_INVALID_ARG, "argument error, result is NULL");
    IR_ERR_CHECK(result->message_len < ir_learn->msg.msg_len, ESP_ERR_INVALID_ARG,
                 "argument error, message buffer length is too short");

    int carrier_cnt = 0;
    float high_sum  = 0;
    float low_sum   = 0;
    float time, duty, freq;

    if (enable_debug) {
        ESP_LOGI(TAG, "======= Dump IR learn result data in hex format =======");
        ESP_LOGI(TAG, "============= carrier high level, cnt: %d =============", ir_learn->carrier.high_cnt);
        ESP_LOG_BUFFER_HEX(TAG, ir_learn->carrier.high, ir_learn->carrier.high_cnt);

        ESP_LOGI(TAG, "============= carrier low level, cnt: %d =============", ir_learn->carrier.low_cnt);
        ESP_LOG_BUFFER_HEX(TAG, ir_learn->carrier.low, ir_learn->carrier.low_cnt);

        ESP_LOGI(TAG, "============= IR message data, cnt: %d =============", ir_learn->msg.msg_len);
        ESP_LOG_BUFFER_HEX(TAG, ir_learn->msg.msg, ir_learn->msg.msg_len);

        ESP_LOGI(TAG, "============= IR repeat data, cnt: %d =============", ir_learn->msg.repeat_len);
        ESP_LOG_BUFFER_HEX(TAG, ir_learn->msg.repeat, ir_learn->msg.repeat_len);
    }

    // check the IR message data
    for (int i = 0; i < ir_learn->msg.msg_len; ++i) {
        if (ir_learn->msg.msg[i] < IR_LEARN_PERIOD_MIN
                || ir_learn->msg.msg[i] > IR_LEARN_PERIOD_MAX) {
            ESP_LOGW(TAG, "ir_learn->msg.msg[%d]: %d, min: %d, max: %d",
                     i, ir_learn->msg.msg[i], IR_LEARN_PERIOD_MIN, IR_LEARN_PERIOD_MAX);
            ir_learn->state = IR_LEARN_CHECK_FAIL;
            return ESP_FAIL;
        }
    }

    // check the IR repeat data
    if (ir_learn->msg.repeat_len >= (IR_LEARN_RPT_BUF_LEN - 1)) {
        ir_learn->msg.repeat_len = 0;
    } else {
        for (int i = 0; i < ir_learn->msg.repeat_len; ++i) {
            if (ir_learn->msg.repeat[i] < IR_LEARN_PERIOD_MIN
                    || ir_learn->msg.repeat[i] > IR_LEARN_REPEAT_MAX) {
                ESP_LOGW(TAG, "ir_learn->msg.repeat[%d]: %d, min: %d, max: %d",
                         i, ir_learn->msg.repeat[i], IR_LEARN_PERIOD_MIN, IR_LEARN_PERIOD_MAX);
                ir_learn->state = IR_LEARN_CHECK_FAIL;
                return ESP_FAIL;
            }
        }
    }

    ir_learn->state = IR_LEARN_SUCCESS;

    array_sort(ir_learn->carrier.high, ir_learn->carrier.high_cnt);
    array_sort(ir_learn->carrier.low, ir_learn->carrier.low_cnt);
    carrier_cnt = MIN(ir_learn->carrier.high_cnt, ir_learn->carrier.low_cnt);

    for (int i = IR_CARRIER_FILTER_LEN; i < (carrier_cnt - IR_CARRIER_FILTER_LEN); ++i) {
        high_sum += ir_learn->carrier.high[i];
        low_sum  += ir_learn->carrier.low[i];
    }

    time = (high_sum + low_sum) / (carrier_cnt - (IR_CARRIER_FILTER_LEN * 2));
    duty = high_sum / (high_sum + low_sum);
    freq = (float)1000 * 1000 / time; // 1s = 1000 ms = 1000 * 1000 us

    result->freq = freq;
    result->duty = duty;
    result->repeat_len  = ir_learn->msg.repeat_len;
    result->message_len = ir_learn->msg.msg_len;

    for (int i = 0; i < ir_learn->msg.repeat_len; ++i) {
        result->repeat[i] = (uint16_t)ir_learn->msg.repeat[i];
    }

    for (int i = 0; i < ir_learn->msg.msg_len; ++i) {
        result->message[i] = (uint16_t)ir_learn->msg.msg[i];
    }

    return ESP_OK;
}

esp_err_t ir_learn_decode(ir_learn_result_t *result)
{
    IR_ERR_CHECK(result == NULL, ESP_ERR_INVALID_ARG, "argument error, result is NULL");
    IR_ERR_CHECK(result->message_len == 0 && result->repeat_len == 0, ESP_ERR_INVALID_ARG,
                 "argument error, message_len and repeat_len are all zero");

    if (ir_nec_decode(result) || ir_rc5_decode(result) || ir_rc6_decode(result)) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "IR learn decode fail, protocol not support");
    return ESP_FAIL;
}

static esp_err_t ir_learn_send_raw(rmt_channel_t channel, const ir_learn_result_t *result, TickType_t xTicksToSend)
{
    static SemaphoreHandle_t s_send_lock = NULL;

    if (s_send_lock == NULL) {
        s_send_lock = xSemaphoreCreateMutex();
    }

    if (xSemaphoreTake(s_send_lock, xTicksToSend) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    int item_num = (result->message_len + 1) / 2;
    rmt_item32_t *item = (rmt_item32_t *)malloc(item_num * sizeof(rmt_item32_t));
    rmt_item32_t *item_tmp = item;

    if (item == NULL) {
        xSemaphoreGive(s_send_lock);
        return ESP_ERR_NO_MEM;
    }

    // in case message_len is an odd number
    if (item_num * 2 > result->message_len) {
        ir_encode_set_level(item + item_num - 1, 1, result->message[result->message_len - 1], 0);
        item_num--;
    }

    // build data
    for (int i = 0; i < item_num; ++i) {
        ir_encode_set_level(item_tmp++, 1, result->message[i * 2], result->message[i * 2 + 1]);
    }

    // send data
    rmt_write_items(channel, item, (result->message_len + 1) / 2, true);

    // repeat length(>=3) equals repreat code length(>=1 odd number) plus the beginning(1) and the end(1) space
    if ((result->message_len >= result->repeat_len) && (result->repeat_len >= 3)
            && (ir_learn_send_sem_take((result->repeat[0] / 1000) / portTICK_RATE_MS) != pdTRUE)) {
        // take semaphore fail, means control button still been pressed,
        // user should call ir_learn_send_stop() to stop sending
        // first space time is stored repeat[0] (us), the later is stored in last item in repeat[n-1] (us)
        int item_repeat_num = result->repeat_len / 2;
        item_tmp = item;

        // build data
        for (int i = 0; i < item_repeat_num; ++i) {
            ir_encode_set_level(item_tmp++, 1, result->repeat[i * 2 + 1], result->repeat[i * 2 + 2]);
        }

        ir_encode_set_level(item_tmp, 1, result->repeat[result->repeat_len - 2], 0);
        rmt_write_items(channel, item, item_repeat_num + 1, true);

        while (1) {
            if (ir_learn_send_sem_take((result->repeat[result->repeat_len - 1] / 1000) / portTICK_RATE_MS) != pdTRUE) {
                rmt_write_items(channel, item, item_repeat_num + 1, true);
            } else {
                break;
            }
        }
    }

    free(item);
    xSemaphoreGive(s_send_lock);
    return ESP_OK;
}

esp_err_t ir_learn_send(rmt_channel_t channel, const ir_learn_result_t *result, TickType_t xTicksToSend)
{
    IR_ERR_CHECK(result == NULL, ESP_ERR_INVALID_ARG, "argumemt error, result is NULL");

    switch (result->proto) {
        case IR_PROTO_NEC:
            return ir_nec_send(channel, result->addr, result->cmd, xTicksToSend);

        case IR_PROTO_RC5:
            return ir_rc5_send(channel, true, result->addr, result->cmd, xTicksToSend);

        case IR_PROTO_RC6:
            // rc6 mode 0 for customer electronics
            return ir_rc6_send(channel, 0, true, result->addr, result->cmd, xTicksToSend);

        default:
            return ir_learn_send_raw(channel, result, xTicksToSend);
    }
}

BaseType_t ir_learn_send_sem_take(TickType_t xTicksToWait)
{
    if (g_ir_send_sem == NULL) {
        g_ir_send_sem = xSemaphoreCreateBinary();
    }

    return xSemaphoreTake(g_ir_send_sem, xTicksToWait);
}

BaseType_t ir_learn_send_stop(void)
{
    if (g_ir_send_sem == NULL) {
        g_ir_send_sem = xSemaphoreCreateBinary();
    }

    return xSemaphoreGive(g_ir_send_sem);
}

esp_err_t ir_learn_send_deinit(rmt_channel_t channel)
{
    return rmt_driver_uninstall(channel);
}

esp_err_t ir_learn_send_init(const ir_learn_send_init_t *send_init)
{
    IR_ERR_CHECK(send_init == NULL, ESP_ERR_INVALID_ARG, "argument error, send_init is NULL");

    esp_err_t ret;
    rmt_config_t rmt_tx;
    rmt_tx.rmt_mode = RMT_MODE_TX;
    rmt_tx.channel = send_init->channel;
    rmt_tx.gpio_num = send_init->gpio;
    rmt_tx.mem_block_num = 1;
    rmt_tx.clk_div = 100;

    rmt_tx.tx_config.loop_en = false;
    rmt_tx.tx_config.carrier_en = send_init->carrier_en;
    rmt_tx.tx_config.carrier_freq_hz = send_init->freq;
    rmt_tx.tx_config.carrier_duty_percent = send_init->duty;
    rmt_tx.tx_config.carrier_level = 1;
    rmt_tx.tx_config.idle_level = 0;
    rmt_tx.tx_config.idle_output_en = true;

    ret = rmt_config(&rmt_tx);
    IR_ERR_CHECK(ret != ESP_OK, ESP_ERR_INVALID_ARG, "rmt comfig error");

    ret = rmt_driver_install(rmt_tx.channel, 0, 0);
    IR_ERR_CHECK(ret != ESP_OK, ESP_FAIL, "rmt driver install error");

    return ESP_OK;
}

esp_err_t ir_learn_wait_finish(ir_learn_handle_t ir_learn_hdl, TickType_t xTicksToWait)
{
    IR_ERR_CHECK(ir_learn_hdl == NULL, ESP_ERR_INVALID_STATE, "IR learn handle is NULL");
    ir_learn_t *ir_learn = (ir_learn_t *)ir_learn_hdl;

    if (xSemaphoreTake(ir_learn->sem, xTicksToWait) != pdTRUE) {
        ESP_LOGW(TAG, "IR learn wait finish timeout: %d", xTicksToWait);
        return ESP_ERR_TIMEOUT;
    }

    if (ir_learn->state != IR_LEARN_FINISH) {
        ESP_LOGE(TAG, "IR learn fail, state: %d", ir_learn->state);
        return ESP_FAIL;
    }

    return ESP_OK;
}

ir_learn_state_t ir_learn_get_state(ir_learn_handle_t ir_learn_hdl)
{
    if (ir_learn_hdl == NULL) {
        return IR_LEARN_NONE;
    } else {
        ir_learn_t *ir_learn = (ir_learn_t *)ir_learn_hdl;
        return ir_learn->state;
    }
}

esp_err_t ir_learn_stop(ir_learn_handle_t ir_learn_hdl)
{
    IR_ERR_CHECK(ir_learn_hdl == NULL, ESP_ERR_INVALID_STATE, "IR learn is not created");
    ir_learn_t *ir_learn = (ir_learn_t *)ir_learn_hdl;

    ir_learn->finished = true;
    gpio_isr_handler_remove(ir_learn->gpio);
    gpio_uninstall_isr_service();
    xSemaphoreTake(ir_learn->sem, 0);
    return ESP_OK;
}

esp_err_t ir_learn_start(ir_learn_handle_t ir_learn_hdl)
{
    IR_ERR_CHECK(ir_learn_hdl == NULL, ESP_ERR_INVALID_STATE, "IR learn is not created");
    ir_learn_t *ir_learn = (ir_learn_t *)ir_learn_hdl;
    IR_ERR_CHECK(ir_learn->state != IR_LEARN_IDLE && ir_learn->finished != true,
                 ESP_ERR_INVALID_STATE, "IR learn neither ready nor finished");

    ir_learn->state = IR_LEARN_READY;
    ir_learn->finished = false;
    memset((void *)&ir_learn->carrier, 0, sizeof(ir_learn_carrier_t));
    memset((void *)&ir_learn->msg, 0, sizeof(ir_learn_msg_t));

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ir_learn->gpio, ir_learn_isr_handle, (void *)ir_learn);
    return ESP_OK;
}

esp_err_t ir_learn_delete(ir_learn_handle_t ir_learn_hdl)
{
    IR_ERR_CHECK(ir_learn_hdl == NULL, ESP_ERR_INVALID_STATE, "IR learn handle is NULL");
    ir_learn_t *ir_learn = (ir_learn_t *)ir_learn_hdl;
    IR_ERR_CHECK(ir_learn->state != IR_LEARN_IDLE && ir_learn->finished != true,
                 ESP_ERR_INVALID_STATE, "IR learn neither ready nor finished");

    vQueueDelete(ir_learn->sem);
    ir_learn->sem = NULL;

    free(ir_learn);
    ir_learn = NULL;

    return ESP_OK;
}

ir_learn_handle_t ir_learn_create(gpio_num_t gpio)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio,
        .mode         = GPIO_MODE_INPUT,
        .pull_down_en = 0,
        .pull_up_en   = 1,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    IR_ERR_CHECK(ret != ESP_OK, NULL, "configure gpio fail");

    ir_learn_t *ir_learn = calloc(1, sizeof(ir_learn_t));
    IR_ERR_CHECK(ir_learn == NULL, NULL, "calloc fail");

    ir_learn->state = IR_LEARN_IDLE;
    ir_learn->gpio  = gpio;
    ir_learn->sem   = xSemaphoreCreateBinary();

    return (ir_learn_handle_t)ir_learn;
}
