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

//+------------------------------------------------ nec ---------------------------------------------
// nec: header(1) | addr(8 LSB<-->MSB) | ~addr(8 LSB<-->MSB) | cmd(8 LSB<-->MSB) | ~cmd(8 LSB<-->MSB)
//+--------------------------------------------------------------------------------------------------
#define NEC_BITS          (34)
#define NEC_DATA_BITS     (32)
#define NEC_BIT_MARGIN    (100)
#define NEC_HDR_HIGH_US   (9000)
#define NEC_HDR_LOW_US    (4500)
#define NEC_BIT_HIGH_US   (560)
#define NEC_ONE_LOW_US    (1690)
#define NEC_ZERO_LOW_US   (560)
#define NEC_RPT_HIGH_US   (9000)
#define NEC_RPT_LOW_US    (2250)
#define NEC_RPT_INT_MS    (108)     // the interval time when sending data repeatedly
#define NEC_MSG_TRANS_MS  (67.5)    // fully transmit the massage frame
#define NEC_RPT_TRANS_MS  (11.8125) // fully transmit the repeat code

BaseType_t ir_learn_send_sem_take(TickType_t xTicksToWait);

void nec_build_items(rmt_item32_t *item, uint16_t addr, uint16_t cmd)
{
    ir_encode_set_level(item++, 1, NEC_HDR_HIGH_US, NEC_HDR_LOW_US);

    for (int j = 0; j < 16; j++, addr >>= 1) {
        if (addr & 0x1) {
            ir_encode_set_level(item++, 1, NEC_BIT_HIGH_US, NEC_ONE_LOW_US);
        } else {
            ir_encode_set_level(item++, 1, NEC_BIT_HIGH_US, NEC_ZERO_LOW_US);
        }
    }

    for (int j = 0; j < 16; j++, cmd >>= 1) {
        if (cmd & 0x1) {
            ir_encode_set_level(item++, 1, NEC_BIT_HIGH_US, NEC_ONE_LOW_US);
        } else {
            ir_encode_set_level(item++, 1, NEC_BIT_HIGH_US, NEC_ZERO_LOW_US);
        }
    }

    ir_encode_set_level(item, 1, NEC_BIT_HIGH_US, 0x7fff);
}

esp_err_t ir_nec_send(rmt_channel_t channel, uint16_t addr, uint16_t cmd, TickType_t xTicksToSend)
{
    uint32_t start_ticks = xTaskGetTickCount();
    static SemaphoreHandle_t s_send_lock = NULL;

    if (s_send_lock == NULL) {
        s_send_lock = xSemaphoreCreateMutex();
    }

    if (xSemaphoreTake(s_send_lock, xTicksToSend) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    rmt_item32_t *item = (rmt_item32_t *) calloc(1, sizeof(rmt_item32_t) * NEC_BITS);

    if (item == NULL) {
        xSemaphoreGive(s_send_lock);
        return ESP_ERR_NO_MEM;
    }

    nec_build_items(item, ((~addr) << 8) | addr, ((~cmd) << 8) | cmd);
    rmt_write_items(channel, item, NEC_BITS, true);

    // if take semaphore fail, means IR contrl button still been pressed,
    // user should call ir_learn_send_stop() to stop sending.
    xTicksToSend = (xTicksToSend == portMAX_DELAY) ? portMAX_DELAY :
                   xTaskGetTickCount() - start_ticks < xTicksToSend ?
                   xTicksToSend - (xTaskGetTickCount() - start_ticks) : 0;

    if ((xTicksToSend == portMAX_DELAY || xTicksToSend > 0)
            && ir_learn_send_sem_take((NEC_RPT_INT_MS - NEC_MSG_TRANS_MS) / portTICK_RATE_MS) != pdTRUE) {
        rmt_item32_t *item_tmp = item;
        ir_encode_set_level(item_tmp++, 1, NEC_RPT_HIGH_US, NEC_RPT_LOW_US);
        ir_encode_set_level(item_tmp, 1, NEC_BIT_HIGH_US, 0);
        rmt_write_items(channel, item, 2, true);

        while (1) {
            xTicksToSend = (xTicksToSend == portMAX_DELAY) ? portMAX_DELAY :
                           xTaskGetTickCount() - start_ticks < xTicksToSend ?
                           xTicksToSend - (xTaskGetTickCount() - start_ticks) : 0;

            if ((xTicksToSend == portMAX_DELAY || xTicksToSend > 0)
                    && ir_learn_send_sem_take((NEC_RPT_INT_MS - NEC_RPT_TRANS_MS) / portTICK_RATE_MS) != pdTRUE) {
                rmt_write_items(channel, item, 2, true);
            } else {
                break;
            }
        }
    }

    free(item);
    xSemaphoreGive(s_send_lock);
    return ESP_OK;
}

bool ir_nec_decode(ir_learn_result_t *result)
{
    uint32_t data = 0;
    int offset = 0;

    // Check header "mark"
    if (ir_decode_check_range(result->message[offset], NEC_HDR_HIGH_US, NEC_BIT_MARGIN) == false) {
        return false;
    }

    offset++;

    // Check we have enough data
    if (result->message_len < (2 * NEC_DATA_BITS)) {
        return false;
    }

    // Check header "space"
    if (ir_decode_check_range(result->message[offset], NEC_HDR_LOW_US, NEC_BIT_MARGIN) == false) {
        return false;
    }

    offset++;

    // Parse the data
    for (int i = 0; i < NEC_DATA_BITS; i++) {
        // Check data "mark"
        if (ir_decode_check_range(result->message[offset], NEC_BIT_HIGH_US, NEC_BIT_MARGIN) == false) {
            return false;
        }

        offset++;

        // Suppend this bit
        if (ir_decode_check_range(result->message[offset], NEC_ONE_LOW_US, NEC_BIT_MARGIN)) {
            data |= (1 << i);
        } else if (ir_decode_check_range(result->message[offset], NEC_ZERO_LOW_US, NEC_BIT_MARGIN)) {
            data |= (0 << i);
        } else {
            return false;
        }

        offset++;
    }

    // Success
    result->proto = IR_PROTO_NEC;
    result->bits  = NEC_DATA_BITS;
    result->value = data;
    // nec: header | addr | ~addr | cmd | ~cmd | footer
    result->addr  = (uint16_t)(data & 0x000000ff);
    result->cmd   = (uint16_t)((data >> 16) & 0x000000ff);
    return true;
}
