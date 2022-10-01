/*

  canbus.c - CAN bus plugin for grblHAL

  Part of grblHAL

  Copyright (c) 2022 Jon Escombe

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "canbus.h"
#include "can.h"

#if CANBUS_ENABLE

#define DEFAULT_BAUDRATE 0        // 125,000

/*
 * Static function prototypes
 */
static on_report_options_ptr on_report_options;
static on_execute_realtime_ptr on_execute_realtime;
static void canbus_settings_restore (void);
static void canbus_settings_load (void);
static void canbus_settings_save (void);
static uint32_t get_baudrate (uint32_t);
static uint32_t canbus_get_baud (setting_id_t);
static status_code_t canbus_set_baud (setting_id_t, uint_fast16_t);
static uint8_t canbus_start (uint32_t);
static uint8_t canbus_stop(void);
static uint8_t canbus_put(canbus_message_t);
static uint8_t queue_get_len(canbus_buffer_t);
static uint8_t queue_get_msg(canbus_buffer_t *);
static uint8_t queue_put_msg(canbus_buffer_t *, canbus_message_t);
static bool canbus_dequeue_rx(canbus_message_t);

/*
 * Static variables
 */
static nvs_address_t nvs_address;
static canbus_settings_t canbus_settings;
static const uint32_t baud[] = { 125000, 250000, 500000, 1000000 };
static bool isEnabled = false;
static canbus_buffer_t tx_buffer = {0, .dir = dir_TX};
static canbus_buffer_t rx_buffer = {0, .dir = dir_RX};
static const char* canbus_direction[] = { "RX", "TX" };

/*
 * Default function pointer for RX handler chain
 */
canbus_t canbus = {.dequeue_rx = canbus_dequeue_rx};


bool canbus_enabled()
{
    return isEnabled;
}

uint8_t canbus_queue_tx(canbus_message_t message)
{
    /* Client has passed us a message to send, add it to the TX buffer to be sent by the main polling loop */
    return (queue_put_msg(&tx_buffer, message));
}

uint8_t canbus_queue_rx(canbus_message_t message)
{
    /* Driver has passed us an incoming message, add it to the RX buffer to be processed by the main polling loop */
    return (queue_put_msg(&rx_buffer, message));
}

static const setting_group_detail_t canbus_groups [] = {
    { Group_Root, Group_CANbus, "CAN bus"}
};

static const setting_detail_t canbus_setting_detail[] = {
    { Setting_CANbus_BaudRate, Group_CANbus, "CAN bus baud rate", NULL, Format_RadioButtons, "125000,250000,500000,1000000", NULL, NULL, Setting_NonCoreFn, canbus_set_baud, canbus_get_baud, NULL },
};

static setting_details_t setting_details = {
    .groups = canbus_groups,
    .n_groups = sizeof(canbus_groups) / sizeof(setting_group_detail_t),
    .settings = canbus_setting_detail,
    .n_settings = sizeof(canbus_setting_detail) / sizeof(setting_detail_t),
    .save = canbus_settings_save,
    .load = canbus_settings_load,
    .restore = canbus_settings_restore
};

static void canbus_settings_restore (void)
{
    printf("canbus_settings_restore()\n");

    canbus_settings.baud_rate = baud[DEFAULT_BAUDRATE];

    hal.nvs.memcpy_to_nvs(nvs_address, (uint8_t *)&canbus_settings, sizeof(canbus_settings_t), true);
}

static void canbus_settings_load (void)
{
    printf("canbus_settings_load()\n");

    if(hal.nvs.memcpy_from_nvs((uint8_t *)&canbus_settings, nvs_address, sizeof(canbus_settings_t), true) != NVS_TransferResult_OK) {
        canbus_settings_restore();
    }

    canbus_start(canbus_settings.baud_rate);
}

static void canbus_settings_save (void)
{
    printf("canbus_settings_save()\n");
    HAL_Delay(100);

    hal.nvs.memcpy_to_nvs(nvs_address, (uint8_t *)&canbus, sizeof(canbus_settings_t), true);
}

static status_code_t canbus_set_baud (setting_id_t id, uint_fast16_t value)
{
    printf("canbus_set_baud(), restarting CAN peripheral...\n");
    canbus_settings.baud_rate = baud[(uint32_t)value];

    canbus_stop();
    canbus_start(canbus_settings.baud_rate);

    return Status_OK;
}

static uint32_t canbus_get_baud (setting_id_t setting)
{
    printf("canbus_get_baud()\n");
    return get_baudrate(canbus_settings.baud_rate);
}

static uint32_t get_baudrate (uint32_t rate)
{
    uint32_t idx = sizeof(baud) / sizeof(uint32_t);

    do {
        if(baud[--idx] == rate)
            return idx;
    } while(idx);

    return DEFAULT_BAUDRATE;
}

static uint8_t queue_get_len(canbus_buffer_t ringbuffer)
{
    uint8_t head = ringbuffer.head;
    uint8_t tail = ringbuffer.tail;

    if (head > tail)
        return head - tail;

    if (tail > head)
        return head + CANBUS_BUFFER_LEN - tail;

    return 0;
}

static uint8_t queue_get_msg(canbus_buffer_t *ringbuffer)
{
    /* get next available message from the ring buffer, either send it out or pass to the RX handler chain */

    canbus_message_t message;

    if (ringbuffer->head  != ringbuffer->tail ) {

        printf("queue_get_msg(), getting message from %s ring buffer, idx:%i\n",
                   canbus_direction[ringbuffer->dir], ringbuffer->tail);

        message = ringbuffer->message[ringbuffer->tail];

        if (ringbuffer->dir == dir_TX) {
            /* don't increment tail unless we are able to pass message to CAN driver */
            if (!canbus_put(message)) {
                printf("queue_get_msg(), unable to send TX message\n");
                return 0;
            }
        } else {
            /* call the RX handler chain */
            canbus.dequeue_rx(message);
        }

        ringbuffer->tail = (ringbuffer->tail + 1) % CANBUS_BUFFER_LEN;
        return 1;
    }

    return 0;
}

static uint8_t queue_put_msg(canbus_buffer_t *ringbuffer, canbus_message_t message)
{
    /* add a new message to the ring buffer
     * note: may be called from driver in interrupt context, SWV printf appears unaffected
     * */

    uint8_t next_head = (ringbuffer->head + 1) % CANBUS_BUFFER_LEN;

    if (next_head != ringbuffer->tail) {

       printf("queue_put_msg(), adding message to %s ring buffer, idx:%i\n",
               canbus_direction[ringbuffer->dir], ringbuffer->head);

        ringbuffer->message[ringbuffer->head] = message;

        ringbuffer->head = next_head;
        return 1;
    } else {
        /* no room left in the ring buffer */
        printf("queue_put_msg(), %s ring buffer is full!\n", canbus_direction[ringbuffer->dir]);
        return 0;
    }
}

static uint8_t canbus_put(canbus_message_t message)
{
    return can_put(message);
}

static uint8_t canbus_stop(void)
{
    can_stop();
    return 1;
}

static uint8_t canbus_start (uint32_t baud)
{
    if (!can_start(canbus_settings.baud_rate)) {
        isEnabled = false;
        printf("canbus_start(), ERROR starting can peripheral!\n");
        return(1);
    } else {
        printf("canbus_start(), can peripheral started at %lu baud..\n", canbus_settings.baud_rate);
        isEnabled = true;
        return(0);
    }
}

static bool canbus_dequeue_rx (canbus_message_t message)
{
    printf("canbus_dequeue_rx(), CAN message id:%lx\n", message.id);
    return(1);
}

static void warning_msg(uint_fast16_t state)
{
    report_message("CAN bus plugin failed to initialise!", Message_Warning);
}

static void onReportOptions (bool newopt)
{
    on_report_options(newopt);

    if(!newopt) {
        hal.stream.write("[PLUGIN:CANBUS v0.01]" ASCII_EOL);
    }
}

static void canbus_poll_realtime (sys_state_t state)
{
    static uint32_t last_ms;

    on_execute_realtime(state);

    uint32_t ms = hal.get_elapsed_ticks();

    /* The platform specific CAN driver may either insert received CAN messages directly
     * into the RX ring buffer, or just flag that RX data is available.
     *
     * Check the flag and retrieve data if necessary..
     */
    if (can_rx_pending()) {
        can_get();
    }

    /* Don't process buffers more than once every ms */
    if(ms == last_ms)
        return;

    if (queue_get_len(tx_buffer)) {
        /* have TX data, sends one message per iteration.. */
        queue_get_msg(&tx_buffer);
    }

    if (queue_get_len(rx_buffer)) {
        /* have RX data, process one message per iteration.. */
        queue_get_msg(&rx_buffer);
    }

    last_ms = ms;
}

void canbus_init()
{
    printf("canbus_init()\n");

    if ((nvs_address = nvs_alloc(sizeof(modbus_settings_t)))) {

        settings_register(&setting_details);

        on_report_options = grbl.on_report_options;
        grbl.on_report_options = onReportOptions;

        on_execute_realtime = grbl.on_execute_realtime;
        grbl.on_execute_realtime = canbus_poll_realtime;

    }  else {
         protocol_enqueue_rt_command(warning_msg);
    }
}

#endif
