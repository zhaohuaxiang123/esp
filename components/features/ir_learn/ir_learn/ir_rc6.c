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

/* ESP32 includes */
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"
#include "soc/rmt_reg.h"

/* IR learn includes */
#include "ir_learn.h"
#include "ir_codec.h"

//+-------------------------------- rc6 ---------------------------------------
//  rc6 bits: header(1+1) | field(3+1) | addr(8 MSB<-->LSB) | cmd(8 MSB<-->LSB)
// rc6 value: field + addr + cmd
//+----------------------------------------------------------------------------
#define RC6_BIT_LEN       (16)
#define RC6_ADDR_BIT_LEN  (8)
#define RC6_CMD_BIT_LEN   (8)
#define RC6_BIT_MARGIN    (100)
#define RC6_HDR_MARK      (2666)
#define RC6_HDR_SPACE     (889)
#define RC6_BIT_US        (444)

BaseType_t ir_learn_send_sem_take(TickType_t xTicksToWait);

esp_err_t ir_rc6_send(rmt_channel_t channel, uint8_t mode, bool toggle, uint8_t addr, uint8_t cmd, TickType_t xTicksToWait)
{
    static SemaphoreHandle_t s_send_lock = NULL;

    if (s_send_lock == NULL) {
        s_send_lock = xSemaphoreCreateMutex();
    }

    if (xSemaphoreTake(s_send_lock, xTicksToWait) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    rmt_item32_t *item = (rmt_item32_t *) calloc(1, sizeof(rmt_item32_t) * (RC6_BIT_LEN + 6));
    rmt_item32_t *item_tmp = item;

    if (item == NULL) {
        xSemaphoreGive(s_send_lock);
        return ESP_ERR_NO_MEM;
    }

    // Header and Start bit
    ir_encode_set_level(item_tmp++, 1, RC6_HDR_MARK, RC6_HDR_SPACE);
    ir_encode_set_level(item_tmp++, 1, RC6_BIT_US, RC6_BIT_US);

    // Build field
    ir_encode_set_level(item_tmp++, mode & 0x04, RC6_BIT_US, RC6_BIT_US);
    ir_encode_set_level(item_tmp++, mode & 0x02, RC6_BIT_US, RC6_BIT_US);
    ir_encode_set_level(item_tmp++, mode & 0x01, RC6_BIT_US, RC6_BIT_US);

    // Build toggle bit
    ir_encode_set_level(item_tmp++, toggle, RC6_BIT_US * 2, RC6_BIT_US * 2);

    // Build data addr
    for (int i = RC6_ADDR_BIT_LEN - 1; i >= 0; i--) {
        if (addr & (1 << i)) {
            ir_encode_set_level(item_tmp++, 1, RC6_BIT_US, RC6_BIT_US);
        } else {
            ir_encode_set_level(item_tmp++, 0, RC6_BIT_US, RC6_BIT_US);
        }
    }

    // Build data cmd
    for (int i = RC6_CMD_BIT_LEN - 1; i >= 0; i--) {
        if (cmd & (1 << i)) {
            ir_encode_set_level(item_tmp++, 1, RC6_BIT_US, RC6_BIT_US);
        } else {
            ir_encode_set_level(item_tmp++, 0, RC6_BIT_US, RC6_BIT_US);
        }
    }

    // Send data
    rmt_write_items(channel, item, RC6_BIT_LEN + 6, true);
    free(item);

    xSemaphoreGive(s_send_lock);
    return ESP_OK;
}

bool ir_rc6_decode(ir_learn_result_t *result)
{
    int bits;
    int used = 0;
    int offset = 0;
    uint32_t data = 0;

    if (result->message_len < RC6_BIT_LEN) {
        return false;
    }

    // Initial mark
    if (ir_decode_check_range(result->message[offset++],  RC6_HDR_MARK, RC6_BIT_MARGIN) == false
            || ir_decode_check_range(result->message[offset++], RC6_HDR_SPACE, RC6_BIT_MARGIN) == false) {
        return false;
    }

    // Get start bit (1)
    if (ir_decode_get_level(result, &offset, &used, RC6_BIT_US, RC6_BIT_MARGIN) != RC_MARK
            || ir_decode_get_level(result, &offset, &used, RC6_BIT_US, RC6_BIT_MARGIN) != RC_SPACE) {
        return false;
    }

    for (bits = 0; offset < result->message_len;  bits++) {
        // Next two levels
        int levelA, levelB;
        levelA = ir_decode_get_level(result, &offset, &used, RC6_BIT_US, RC6_BIT_MARGIN);

        // T(toggle) bit is double wide; make sure second half matches
        if (bits == 3 && levelA != ir_decode_get_level(result, &offset, &used, RC6_BIT_US, RC6_BIT_MARGIN)) {
            return false;
        }

        levelB = ir_decode_get_level(result, &offset, &used, RC6_BIT_US, RC6_BIT_MARGIN);

        // T(toggle) bit is double wide; make sure second half matches
        if (bits == 3 && levelB != ir_decode_get_level(result, &offset, &used, RC6_BIT_US, RC6_BIT_MARGIN)) {
            return false;
        }

        if ((levelA == RC_MARK) && (levelB == RC_SPACE)) {
            data |= (1 << bits);

            // first 4 bit is field
            if (bits >= 4 && bits < (RC6_ADDR_BIT_LEN + 4)) {
                result->addr |= (1 << ((RC6_ADDR_BIT_LEN  - 1) - (bits - 4)));
            } else if (bits >= (RC6_ADDR_BIT_LEN + 4) && bits <= (RC6_BIT_LEN + 4)) {
                result->cmd |= (1 << ((RC6_BIT_LEN  - 1) - (bits - 4)));
            }
        } else if ((levelA == RC_SPACE) && (levelB == RC_MARK)) {
            data |= (0 << bits);

            // first 4 bit is field
            if (bits >= 4 && bits < (RC6_ADDR_BIT_LEN + 4)) {
                result->addr |= (0 << ((RC6_ADDR_BIT_LEN  - 1) - (bits - 4)));
            } else if (bits >= (RC6_ADDR_BIT_LEN + 4) && bits <= (RC6_BIT_LEN + 4)) {
                result->cmd |= (0 << ((RC6_BIT_LEN  - 1) - (bits - 4)));
            }
        } else {
            return false;
        }
    }

    // Success
    result->bits  = bits;
    result->value = data;
    result->proto = IR_PROTO_RC6;
    return true;
}
