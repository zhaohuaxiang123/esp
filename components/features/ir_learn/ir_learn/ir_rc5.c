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

/* FreeRTOS includes */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ESP32 includes */
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"
#include "soc/rmt_reg.h"

/* IR learn includes */
#include "ir_learn.h"
#include "ir_codec.h"

//+-------------------------------- rc5 ------------------------------------
//  First two bit must be a one (start bit)
//
//  rc5 bits: start(1+1) | toggle(1) | addr(5 MSB<-->LSB) | cmd(6 MSB<-->LSB)
// rc5 value: toggle + addr + cmd
//+-------------------------------------------------------------------------
#define RC5_BIT_LEN       (11)
#define RC5_ADDR_BIT_LEN  (5)
#define RC5_CMD_BIT_LEN   (6)
#define RC5_BIT_MARGIN    (100)
#define RC5_BIT_US        (889)
#define RC5_INTERVAL_MS   (114) // the interval time when sending data repeatedly
#define RC5_TRAMSMIT_MS   (25)  // fully transmit the message frame

BaseType_t ir_learn_send_sem_take(TickType_t xTicksToWait);

esp_err_t ir_rc5_send(rmt_channel_t channel, bool toggle, uint8_t addr, uint8_t cmd, TickType_t xTicksToSend)
{
    TickType_t start_ticks = xTaskGetTickCount();
    static SemaphoreHandle_t s_send_lock = NULL;

    if (s_send_lock == NULL) {
        s_send_lock = xSemaphoreCreateMutex();
    }

    if (xSemaphoreTake(s_send_lock, xTicksToSend) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    rmt_item32_t *item = (rmt_item32_t *) calloc(1, sizeof(rmt_item32_t) * (RC5_BIT_LEN + 3));
    rmt_item32_t *item_tmp = item;

    if (item == NULL) {
        xSemaphoreGive(s_send_lock);
        return ESP_ERR_NO_MEM;
    }

    // Build start two bits
    ir_encode_set_level(item_tmp++, 0, RC5_BIT_US, RC5_BIT_US);
    ir_encode_set_level(item_tmp++, 0, RC5_BIT_US, RC5_BIT_US);

    // Build toggle bit
    ir_encode_set_level(item_tmp++, toggle, RC5_BIT_US, RC5_BIT_US);

    // Build data addr
    for (int i = RC5_ADDR_BIT_LEN - 1; i >= 0; i--) {
        if (addr & (1 << i)) {
            ir_encode_set_level(item_tmp++, 0, RC5_BIT_US, RC5_BIT_US);
        } else {
            ir_encode_set_level(item_tmp++, 1, RC5_BIT_US, RC5_BIT_US);
        }
    }

    // Build data cmd
    for (int i = RC5_CMD_BIT_LEN - 1; i >= 0; i--) {
        if (cmd & (1 << i)) {
            ir_encode_set_level(item_tmp++, 0, RC5_BIT_US, RC5_BIT_US);
        } else {
            ir_encode_set_level(item_tmp++, 1, RC5_BIT_US, RC5_BIT_US);
        }
    }

    // Send data
    rmt_write_items(channel, item, RC5_BIT_LEN + 3, true);

    // Send repeat if button still been pressed,
    while (1) {
        xTicksToSend = (xTicksToSend == portMAX_DELAY) ? portMAX_DELAY :
                       xTaskGetTickCount() - start_ticks < xTicksToSend ?
                       xTicksToSend - (xTaskGetTickCount() - start_ticks) : 0;

        if ((xTicksToSend == portMAX_DELAY || xTicksToSend > 0)
                && ir_learn_send_sem_take((RC5_INTERVAL_MS - RC5_TRAMSMIT_MS) / portTICK_RATE_MS) != pdTRUE) {
            toggle = !toggle;
            ir_encode_set_level(item + 2, toggle, RC5_BIT_US, RC5_BIT_US);
            rmt_write_items(channel, item, RC5_BIT_LEN + 3, true);
        } else {
            break;
        }
    }

    free(item);
    xSemaphoreGive(s_send_lock);
    return ESP_OK;
}

bool ir_rc5_decode(ir_learn_result_t *result)
{
    int bits;
    int used = 0;
    int offset = 0;
    uint32_t data = 0;

    if (result->message_len < (RC5_BIT_LEN + 1) / 2 + 2) {
        return false;
    }

    // Get start bits
    if (ir_decode_get_level(result, &offset, &used, RC5_BIT_US, RC5_BIT_MARGIN) != RC_MARK
            || ir_decode_get_level(result, &offset, &used, RC5_BIT_US, RC5_BIT_MARGIN) != RC_SPACE
            || ir_decode_get_level(result, &offset, &used, RC5_BIT_US, RC5_BIT_MARGIN) != RC_MARK) {
        return false;
    }

    // Get data bits
    for (bits = 0; offset < result->message_len; bits++) {
        int levelA = ir_decode_get_level(result, &offset, &used, RC5_BIT_US, RC5_BIT_MARGIN);
        int levelB = ir_decode_get_level(result, &offset, &used, RC5_BIT_US, RC5_BIT_MARGIN);

        if ((levelA == RC_SPACE) && (levelB == RC_MARK)) {
            data |= (1 << bits);

            if (bits && bits <= RC5_ADDR_BIT_LEN) {
                result->addr |= (1 << (RC5_ADDR_BIT_LEN - bits));
            } else if (bits > RC5_ADDR_BIT_LEN && bits <= RC5_BIT_LEN) {
                result->cmd |= (1 << (RC5_BIT_LEN - bits));
            }
        } else if ((levelA == RC_MARK) && (levelB == RC_SPACE)) {
            data |= (0 << bits);

            if (bits && bits <= RC5_ADDR_BIT_LEN) {
                result->addr |= (0 << (RC5_ADDR_BIT_LEN - bits));
            } else if (bits > RC5_ADDR_BIT_LEN && bits <= RC5_BIT_LEN) {
                result->cmd |= (0 << (RC5_BIT_LEN - bits));
            }
        } else {
            return false;
        }
    }

    // Success
    result->bits  = bits;
    result->value = data;
    result->proto = IR_PROTO_RC5;
    return true;
}
