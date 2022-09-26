## CAN bus plugin for grblHAL
##### This plugin adds support for CAN 2.0A (11bit identifiers, speed up to 1Mb/s)

#### $399 - Baud rate (setting number tbc)
0 = 125,000  
1 = 250,000  
2 = 500,000  
3 = 1,000,000  

---
#### Dependencies:
Supporting CAN driver code is required for each hardware platform

|Platform|Current status|
|-|-|
|STM32F4xx|HAL driver in progress|
|STM32H7xx|HAL driver planned|
|iMXRT1062|Teensy 4.x driver planned|
|Other|MCP2510 SPI driver planned|

---
#### Usage:
Clients should use the `canbus_queue_tx()` function to queue a message for transmission, and add a
function pointer into the `canbus.dequeue_rx `chain to act on received messages.

Please see the Examples folder for implementation examples.

A number of CAN message id's, and their content (system state, button/encoder values etc), will be added to the `canbus_ids.h` file.

##### To enable the plugin for testing;
Copy the source to a new "canbus" folder in the grblHAL source tree, and define as a source folder in your IDE if necessary. 
Define `CANBUS_ENABLE=1` to enable the plugin.

---

Note: This plugin is under development - implementation details may change as drivers for additional platforms are developed.
