/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version.
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#ifndef ZULU_CONTROL_I2C_CLIENT
#define ZULU_CONTROL_I2C_CLIENT

#define MAX_MSG_SIZE 2048
#define BUFFER_LENGTH 8
#define INPUT_BUFFER_COUNT 5

#define I2C_SERVER_SYSTEM_STATUS_JSON  0xA
#define I2C_SERVER_IMAGE_JSON  0xB
#define I2C_SERVER_SSID 0xD
#define I2C_SERVER_SSID_PASS 0xE
#define I2C_SERVER_RESET 0xF

#define I2C_CLIENT_NOOP 0x0
#define I2C_CLIENT_SUBSCRIBE_STATUS_JSON  0xA
#define I2C_CLIENT_LOAD_IMAGE  0xB
#define I2C_CLIENT_EJECT_IMAGE  0xC
#define I2C_CLIENT_FETCH_IMAGES_JSON  0xD
#define I2C_CLIENT_FETCH_SSID 0xE
#define I2C_CLIENT_FETCH_SSID_PASS 0xF
#define I2C_CLIENT_FETCH_ITR_IMAGE 0x10
#define I2C_CLIENT_IP_ADDRESS 0x11
#define I2C_CLIENT_NET_DOWN 0x12

#include <cstdlib>
#include <cstdint>
#include <pico/util/queue.h>
#include <pico/i2c_slave.h>
#include <cstring>
#include <cstdio>
#include <pico/stdlib.h>

namespace zuluide::i2c::client {
  enum class SendState {None, SentCommand, SentLength};

  /**
     Stores the messages received from the I2C server along with the meta data
     used to track the receive progress.
   */
  typedef struct {
    uint16_t pos;
    uint8_t command;
    uint16_t length;
    uint8_t lengthBytes[2];
    uint8_t buffer[MAX_MSG_SIZE];
    SendState state;
  } Packet;

  /**
     Enqueues a request to send to the I2C server with an empty string argument.
   */
  bool EnqueueRequest(uint8_t request);
  
  /**
     Enqueues a request to send to the I2C server with the provided string argument.
   */
  bool EnqueueRequest(uint8_t request, const char* toSend);

  /**
     Called when a system status update is received from the I2C server.
   */
  void ProcessSystemStatus(const uint8_t* message, size_t length);

  /**
     Called when an image is received from the I2C server.
  */  
  void ProcessImage(const uint8_t* message, size_t length);

  /**
     Called when the WiFi SSID is received from the server.
  */
  void ProcessSSID(const uint8_t* message, size_t length);

  /**
     Called when the WiFi password is received from the server.
  */
  void ProcessPassword(const uint8_t* message, size_t length);

  /**
     Called when a reset request is received from the server.
   */
  void ProcessReset();

  /**
     Configures the I2C communication parameters.
  */
  void Init(unsigned int sdaPin, unsigned int sclPin, unsigned int addr, unsigned int buad);

  /**
     Utility method to cleanup a packet and place it back in the available queue.
  */
  void Cleanup(Packet* packet);

  /**
     Predicate for detecting the tyope of message/command received from the I2C server.
  */
  bool Is(Packet* toCheck, uint8_t messageID);

  /**
     Pulls the next message received from the I2C server, returning true if one is available and false if not.
   */
  bool TryReceive(Packet** packet);

  /**
     Executes the message processing and dispatching loop.
   */
  void ProcessMessages();
}

#endif
