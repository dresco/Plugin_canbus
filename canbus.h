/*

  canbus.h - CAN bus plugin for grblHAL

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

#ifndef _CANBUS_H_
#define _CANBUS_H_

#ifdef ARDUINO
#include "../driver.h"
#include "../grbl/nvs_buffer.h"
#include "../grbl/protocol.h"
#else
#include "driver.h"
#include "grbl/nvs_buffer.h"
#include "grbl/protocol.h"
#endif

#include <stdio.h>

#define CANBUS_BUFFER_LEN     8

typedef enum {
    dir_RX,
    dir_TX
} canbus_direction_t;

typedef struct {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[8];
} canbus_message_t;

typedef struct {
    uint32_t baud_rate;
} canbus_settings_t;

typedef struct {
    volatile uint8_t head;
    volatile uint8_t tail;
    const canbus_direction_t dir;
    volatile canbus_message_t message[CANBUS_BUFFER_LEN];
} canbus_buffer_t;

/* function pointer for RX handler chain */
typedef bool (*dequeue_rx_ptr)(canbus_message_t);

typedef struct {
    dequeue_rx_ptr dequeue_rx;
} canbus_t;

extern canbus_t canbus;

/*
 * Function prototypes
 */
void    canbus_init ();
bool    canbus_enabled(void);
uint8_t canbus_queue_tx(canbus_message_t);
uint8_t canbus_queue_rx(canbus_message_t);

#endif /* _CANBUS_H_ */
