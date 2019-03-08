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
#ifndef __IR_CODEC_H__
#define __IR_CODEC_H__

#ifdef __cplusplus
extern "C" {
#endif

#define RC_MARK         (1)
#define RC_SPACE        (0)
#define RMT_CLK_DIV     (100)                         /**< RMT counter clock divider */
#define RMT_TICK_10_US  (80000000/RMT_CLK_DIV/100000) /**< RMT counter value for 10 us.(Source clock is APB clock 80MHZ) */

/**
 * @brief  check the given period is or not in specific IR transmission protocol duration.
 *
 * @param  duration_us  fixed duration of level in IR transmission protocol
 * @param  target_us  target period to check
 * @param  bit_margin  check margin period
 *
 * @return
 *     - true
 *     - false
 */
bool ir_decode_check_range(int duration_us, int target_us, int bit_margin);

/**
 * @brief  get the level of IR learn result.
 *
 * @param  result  pointer of IR learn result
 * @param  offset  index of message buffer in result
 * @param  used  data count used of message buffer in result
 * @param  bit_us  period of bit in protocol
 * @param  bit_margin  check margin period
 */
int ir_decode_get_level(ir_learn_result_t *result, int *offset, int *used, int bit_us, int bit_margin);

/**
 * @brief  set level and period into rmt item.
 *
 * @param  item  pointer of rmt item to set
 * @param  logic_dir  level change direction, should be 1(high to low) or 0(low to high)
 * @param  high_us  high level period
 * @param  low_us  low level period
 */
void ir_encode_set_level(rmt_item32_t *item, int logic_dir, int high_us, int low_us);

#ifdef __cplusplus
}
#endif


#endif // __IR_CODEC_H__
