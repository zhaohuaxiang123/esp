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
#ifndef __IR_LEARN_H__
#define __IR_LEARN_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IR transmission protocol enumeration
 */
typedef enum {
    NOT_SUPPORT,  /**< IR transmission protocol not supported */
    IR_PROTO_NEC, /**< NEC IR transmission protocol */
    IR_PROTO_RC5, /**< RC5 IR transmission protocol */
    IR_PROTO_RC6, /**< RC6 IR transmission protocol */
    IR_PROTO_MAX,
} ir_proto_t;

/**
 * @brief IR learn state enumeration
 */
typedef enum {
    IR_LEARN_NONE = -1,  /**< No IR learn state find */
    IR_LEARN_IDLE = 0,   /**< The IR learn object is just created */
    IR_LEARN_READY,      /**< The IR learn object is Ready to start */
    IR_LEARN_CARRIER,    /**< Carrier is saved */
    IR_LEARN_MSG,        /**< Message data is saved */
    IR_LEARN_FINISH,     /**< IR learn process is finished */
    IR_LEARN_SUCCESS,    /**< IR learn process is finished and result is correct */
    IR_LEARN_CHECK_FAIL, /**< IR learn process is finished, but result is error */
    IR_LEARN_OVERFLOW,   /**< Message buffer of IR learn object is overflow */
    IR_LEARN_STATE_MAX,
} ir_learn_state_t;

/**
 * @brief Result data of IR learn
 */
typedef struct {
    ir_proto_t proto;     /**< IR transmission protocol of carrier */
    float freq;           /**< Frequence of carrier */
    float duty;           /**< Duty of carrier */
    int bits;             /**< Bit number of decoded value */
    uint32_t value;       /**< Decoded value */
    uint16_t addr;        /**< Address info of IR learn result */
    uint16_t cmd;         /**< Command info of IR learn result */
    uint16_t repeat_len;  /**< Repeat data length of IR learn result */
    uint16_t repeat[10];  /**< Repeat data buffer of IR learn result */
    uint16_t message_len; /**< Message data length of IR learn result */
    uint16_t message[0];  /**< Message data buffer of IR learn result */
} ir_learn_result_t;

/**
 * @brief Parameters to initialize IR learn send process
 */
typedef struct {
    rmt_channel_t channel; /**< RMT channel (0-7) */
    gpio_num_t gpio;       /**< GPIO index of the pin used to send the IR data */
    uint32_t freq;         /**< Frequence of IR carrier */
    uint8_t duty;          /**< Duty of IR carrier */
    bool carrier_en;       /**< Enable IR carrier or not */
} ir_learn_send_init_t;

typedef void* ir_learn_handle_t;

/**
 * @brief  create IR learn object.
 *
 * @param  gpio  GPIO index of the pin that the IR learn uses.
 *
 * @return  handle of the created IR learn object, or NULL in case of error.
 */
ir_learn_handle_t ir_learn_create(gpio_num_t gpio);

/**
 * @brief  deinit IR learn object.
 *
 * @param  ir_learn_hdl  handle of the created IR learn object.
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_INVALID_STATE
 */
esp_err_t ir_learn_delete(ir_learn_handle_t ir_learn_hdl);

/**
 * @brief  start IR learn process.
 *
 * @param  ir_learn_hdl  handle of the created IR learn object.
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_INVALID_STATE
 */
esp_err_t ir_learn_start(ir_learn_handle_t ir_learn_hdl);

/**
 * @brief  stop IR learn process.
 *
 * @param  ir_learn_hdl  handle of the created IR learn object.
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_INVALID_STATE
 */
esp_err_t ir_learn_stop(ir_learn_handle_t ir_learn_hdl);

/**
 * @brief  Wait for finish of the IR learn process in given time.
 *
 * @param  ir_learn_hdl  handle of the created IR learn object.
 * @param  xTicksToWait  system tick number to wait.
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_INVALID_ARG
 *     - ESP_ERR_TIMEOUT
 *     - ESP_FAIL
 */
esp_err_t ir_learn_wait_finish(ir_learn_handle_t ir_learn_hdl, TickType_t xTicksToWait);

/**
 * @brief  get the status of IR learn process.
 *
 * @param  ir_learn_hdl  handle of the created IR learn object.
 *
 * @return
 *     - IR_LEARN_NONE IR learn object was not created.
 *     - other IR learn status
 */
ir_learn_state_t ir_learn_get_state(ir_learn_handle_t ir_learn_hdl);

/**
 * @brief  get the result of IR learn.
 * @note  Only when IR learn was finished, user can call the API to get the result data.
 *
 * @param  ir_learn_hdl  handle of the created IR learn object.
 * @param  result  pointer of IR learn result.
 * @param  enable_debug  print the learn result of not.
 *
 * @attention  This IR learn result will only be printed out
 *             when the log level of `ir_learn` module is higher than debug level and enable_debug is set true.
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_INVALID_ARG
 *     - ESP_ERR_INVALID_STATE
 */
esp_err_t ir_learn_get_result(ir_learn_handle_t ir_learn_hdl, ir_learn_result_t *result, bool enable_debug);

/**
 * @brief  decode the IR learn result.
 * @note  When IR learn succeeded, call this API to decode the result into corresponding IR transmission protocol and message.
 *
 * @param  result  pointer of IR learn result.
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_INVALID_STATE
 *     - ESP_FAIL
 */
esp_err_t ir_learn_decode(ir_learn_result_t *result);

/**
 * @brief  init IR learn send function.
 *
 * @param  send_init  pointer of IR learn send init data
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_INVALID_ARG
 *     - ESP_FAIL
 */
esp_err_t ir_learn_send_init(const ir_learn_send_init_t *send_init);

/**
 * @brief  deinit IR learn send function.
 *
 * @param  channel  RMT channel (0-7)
 *
 * @return
 *     - ESP_ERR_INVALID_ARG
 *     - ESP_OK
 */
esp_err_t ir_learn_send_deinit(rmt_channel_t channel);

/**
 * @brief  IR learn send function.
 *
 * @param  channel  RMT channel (0-7)
 * @param  result  pointer of IR learn result
 * @param  xTicksToSend  system tick number to send
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_INVALID_ARG
 *     - ESP_ERR_TIMEOUT
 *     - ESP_ERR_NO_MEM
 */
esp_err_t ir_learn_send(rmt_channel_t channel, const ir_learn_result_t *result, TickType_t xTicksToSend);

/**
 * @brief  stop IR learn send function.
 *
 * @return
 *     - pdTRUE
 *     - pdFALSE
 */
BaseType_t ir_learn_send_stop(void);

/**
 * @brief  send IR data in nec protocol.
 *
 * @param  channel  RMT channel (0-7)
 * @param  addr  address bits in nec protocol
 * @param  cmd  command bits in nec protocol
 * @param  xTicksToSend  system tick number to send
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_TIMEOUT
 *     - ESP_ERR_NO_MEM
 */
esp_err_t ir_nec_send(rmt_channel_t channel, uint16_t addr, uint16_t cmd, TickType_t xTicksToSend);

/**
 * @brief  send IR data in rc5 protocol.
 *
 * @param  channel  RMT channel (0-7)
 * @param  toggle  toggle bit in rc5 protocol
 * @param  addr  address bits in rc5 protocol
 * @param  cmd  command bits in rc5 protocol
 * @param  xTicksToSend  system tick number to send
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_TIMEOUT
 *     - ESP_ERR_NO_MEM
 */
esp_err_t ir_rc5_send(rmt_channel_t channel, bool toggle, uint8_t addr, uint8_t cmd, TickType_t xTicksToSend);

/**
 * @brief  send IR data in rc6 protocol.
 *
 * @param  channel  RMT channel (0-7)
 * @param  mode  control mode in rc6 protocol
 * @param  toggle  toggle bit in rc6 protocol
 * @param  addr  address bits in rc6 protocol
 * @param  cmd  command bits in rc6 protocol
 * @param  xTicksToSend  system tick number to send
 *
 * @return
 *     - ESP_OK
 *     - ESP_ERR_TIMEOUT
 *     - ESP_ERR_NO_MEM
 */
esp_err_t ir_rc6_send(rmt_channel_t channel, uint8_t mode, bool toggle, uint8_t addr, uint8_t cmd, TickType_t xTicksToSend);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class IRLearn
{
private:
    ir_learn_handle_t ir_learn_hdl;

    /**
     * prevent copy constructing
     */
    IRLearn(const IRLearn&);
    IRLearn& operator = (const IRLearn&);
public:

    /**
     * @brief constructor of IRLearn
     *
     * @param gpio GPIO index of the pin that the IR learn uses
     */
    IRLearn(gpio_num_t gpio);

    /**
     * @brief destructor of IRLearn
     */
    ~IRLearn();

    /**
     * @brief  start IR learn process.
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_INVALID_STATE
     */
    esp_err_t start(void);
    /**
     * @brief  stop IR learn process.
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_INVALID_STATE
     */
    esp_err_t stop(void);

    /**
     * @brief  Wait for finish of the IR learn process in given time.
     *
     * @param  xTicksToWait  system tick number to wait.
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_INVALID_ARG
     *     - ESP_ERR_TIMEOUT
     *     - ESP_FAIL
     */
    esp_err_t wait_finish(TickType_t xTicksToWait);

    /**
     * @brief  get the status of IR learn process.
     * @note  User should use timer or in task to get the state of IR learn process periodially.
     *
     * @return
     *     - IR_LEARN_NONE IR learn object was not created.
     *     - other IR learn status
     */
    ir_learn_state_t get_state(void);

    /**
     * @brief  get the result of IR learn.
     * @note  Only when IR learn was finished, user can call the API to get the result data.
     * @param  result  pointer of IR learn result.
     * @param  enable_debug  print the learn result of not.
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_INVALID_ARG
     *     - ESP_ERR_INVALID_STATE
     */
    esp_err_t get_result(ir_learn_result_t *result, bool enable_debug);

    /**
     * @brief  decode the IR learn result.
     * @note  When IR learn succeeded, call this API to decode the result into corresponding IR transmission protocol and message.
     *
     * @param  result  pointer of IR learn result.
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_INVALID_STATE
     *     - ESP_FAIL
     */
    esp_err_t decode(ir_learn_result_t *result);

    /**
     * @brief  init IR learn send function.
     *
     * @param  send_init  pointer of IR learn send init data
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_INVALID_ARG
     *     - ESP_FAIL
     */
    esp_err_t send_init(const ir_learn_send_init_t *send_init);

    /**
     * @brief  deinit IR learn send function.
     *
     * @param  channel  RMT channel (0-7)
     *
     * @return
     *     - ESP_ERR_INVALID_ARG
     *     - ESP_OK
     */
    esp_err_t send_deinit(rmt_channel_t channel);

    /**
     * @brief  IR learn send function.
     *
     * @param  channel  RMT channel (0-7)
     * @param  result  pointer of IR learn result
     * @param  xTicksToSend  system tick number to send
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_INVALID_ARG
     *     - ESP_ERR_TIMEOUT
     *     - ESP_ERR_NO_MEM
     */
    esp_err_t send(rmt_channel_t channel, const ir_learn_result_t *result, TickType_t xTicksToSend);

    /**
     * @brief  stop IR learn send function.
     *
     * @return
     *     - pdTRUE
     *     - pdFALSE
     */
    BaseType_t send_stop(void);

    /**
     * @brief  send IR data in nec protocol.
     *
     * @param  channel  RMT channel (0-7)
     * @param  addr  address bits in nec protocol
     * @param  cmd  command bits in nec protocol
     * @param  xTicksToSend  system tick number to send
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_TIMEOUT
     *     - ESP_ERR_NO_MEM
     */
    esp_err_t nec_send(rmt_channel_t channel, uint16_t addr, uint16_t cmd, TickType_t xTicksToSend);


    /**
     * @brief  send IR data in rc5 protocol.
     *
     * @param  channel  RMT channel (0-7)
     * @param  toggle  toggle bit in rc5 protocol
     * @param  addr  address bits in rc5 protocol
     * @param  cmd  command bits in rc5 protocol
     * @param  xTicksToSend  system tick number to send
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_TIMEOUT
     *     - ESP_ERR_NO_MEM
     */
    esp_err_t rc5_send(rmt_channel_t channel, bool toggle, uint8_t addr, uint8_t cmd, TickType_t xTicksToSend);

    /**
     * @brief  send IR data in rc6 protocol.
     *
     * @param  channel  RMT channel (0-7)
     * @param  mode  control mode in rc6 protocol
     * @param  toggle  toggle bit in rc6 protocol
     * @param  addr  address bits in rc6 protocol
     * @param  cmd  command bits in rc6 protocol
     * @param  xTicksToSend  system tick number to send
     *
     * @return
     *     - ESP_OK
     *     - ESP_ERR_TIMEOUT
     *     - ESP_ERR_NO_MEM
     */
    esp_err_t rc6_send(rmt_channel_t channel, uint8_t mode, bool toggle, uint8_t addr, uint8_t cmd, TickType_t xTicksToSend);
};
#endif // __cplusplus

#endif // __IR_LEARN_H__
