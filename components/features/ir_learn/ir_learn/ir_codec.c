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
#include <stdio.h>
#include <string.h>

/* ESP32 includes */
#include "driver/rmt.h"
#include "soc/rmt_reg.h"

/* IR learn includes */
#include "ir_learn.h"
#include "ir_codec.h"

bool ir_decode_check_range(int duration_us, int target_us, int bit_margin)
{
    return (duration_us < (target_us + bit_margin) && duration_us > (target_us - bit_margin));
}

int ir_decode_get_level(ir_learn_result_t *result, int *offset, int *used, int bit_us, int bit_margin)
{
    int value;
    int width;
    int avail;

    // After end of recorded buffer, assume RC_SPACE.
    if (*offset >= result->message_len) {
        return RC_SPACE;
    }

    width = result->message[*offset];
    value = ((*offset) % 2) ? RC_SPACE : RC_MARK;

    if (ir_decode_check_range(width, bit_us, bit_margin)) {
        avail = 1;
    } else if (ir_decode_check_range(width, 2 * bit_us, bit_margin)) {
        avail = 2;
    } else if (ir_decode_check_range(width, 3 * bit_us, bit_margin)) {
        avail = 3;
    } else {
        return -1;
    }

    (*used)++;

    if (*used >= avail) {
        *used = 0;
        (*offset)++;
    }

    return value;
}

void ir_encode_set_level(rmt_item32_t *item, int logic_dir, int high_us, int low_us)
{
    if (logic_dir) {
        item->level0 = 1;
        item->duration0 = (high_us) / 10 * RMT_TICK_10_US;
        item->level1 = 0;
        item->duration1 = (low_us) / 10 * RMT_TICK_10_US;
    } else {
        item->level0 = 0;
        item->duration0 = (low_us) / 10 * RMT_TICK_10_US;
        item->level1 = 1;
        item->duration1 = (high_us) / 10 * RMT_TICK_10_US;
    }
}
