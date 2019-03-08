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

/* ESP32 includes */
#include "soc/rmt_reg.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "esp_system.h"

/* IR learn includes */
#include "ir_learn.h"

IRLearn::IRLearn(gpio_num_t gpio)
{
    ir_learn_hdl = ir_learn_create(gpio);
}

IRLearn::~IRLearn()
{
    if (ir_learn_delete(ir_learn_hdl) == ESP_OK) {
        ir_learn_hdl = NULL;
    }
}

esp_err_t IRLearn::start(void)
{
    return ir_learn_start(ir_learn_hdl);
}

esp_err_t IRLearn::stop(void)
{
    return ir_learn_stop(ir_learn_hdl);
}

esp_err_t IRLearn::wait_finish(TickType_t xTicksToWait)
{
    return ir_learn_wait_finish(ir_learn_hdl, xTicksToWait);
}

ir_learn_state_t IRLearn::get_state(void)
{
    return ir_learn_get_state(ir_learn_hdl);
}

esp_err_t IRLearn::get_result(ir_learn_result_t *result, bool enable_debug)
{
    return ir_learn_get_result(ir_learn_hdl, result, enable_debug);
}

esp_err_t IRLearn::decode(ir_learn_result_t *result)
{
    return ir_learn_decode(result);
}

esp_err_t IRLearn::send_init(const ir_learn_send_init_t *send_init)
{
    return ir_learn_send_init(send_init);
}

esp_err_t IRLearn::send_deinit(rmt_channel_t channel)
{
    return ir_learn_send_deinit(channel);
}

esp_err_t IRLearn::send(rmt_channel_t channel, const ir_learn_result_t *result, TickType_t xTicksToSend)
{
    return ir_learn_send(channel, result, xTicksToSend);
}

BaseType_t IRLearn::send_stop(void)
{
    return ir_learn_send_stop();
}

esp_err_t IRLearn::nec_send(rmt_channel_t channel, uint16_t addr, uint16_t cmd, TickType_t xTicksToSend)
{
    return ir_nec_send(channel, addr, cmd, xTicksToSend);
}

esp_err_t IRLearn::rc5_send(rmt_channel_t channel, bool toggle, uint8_t addr, uint8_t cmd, TickType_t xTicksToSend)
{
    return ir_rc5_send(channel, toggle, addr, cmd, xTicksToSend);
}

esp_err_t IRLearn::rc6_send(rmt_channel_t channel, uint8_t mode, bool toggle, uint8_t addr, uint8_t cmd, TickType_t xTicksToSend)
{
    return ir_rc6_send(channel, mode, toggle, addr, cmd, xTicksToSend);
}
