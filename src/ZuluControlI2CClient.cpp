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

#include "ZuluControlI2CClient.h"

namespace zuluide::i2c::client {

static volatile Packet* current = NULL;
static queue_t outputQueue;
static queue_t inputQueue;
static queue_t availInputQueue;

static void i2c_slave_handler(i2c_inst_t* i2c, i2c_slave_event_t event) {
   switch (event) {
      case I2C_SLAVE_RECEIVE: {
         if (current == NULL) {
            // Get a buffer.
            if (!queue_try_remove(&availInputQueue, &current)) {
               printf("UNABLE TO GET A FREE BUFFER\n");
            }
         }

         if (current->state == SendState::None) {
            if (i2c_get_read_available(i2c0) > 0) {
               current->command = i2c_read_byte_raw(i2c0);
               current->state = SendState::SentCommand;
            }
         } else if (current->state == SendState::SentCommand) {
            if (current->pos == 0) {
               if (i2c_get_read_available(i2c0) > 1) {
                  // Both length bytes at once.
                  current->lengthBytes[0] = i2c_read_byte_raw(i2c0);
                  current->lengthBytes[1] = i2c_read_byte_raw(i2c0);
                  current->state = SendState::SentLength;
                  current->pos = 0;
                  current->length = (current->lengthBytes[0] << 8) | current->lengthBytes[1];
                  if (current->length == 0) {
                     // We have now received the entire message.
                     queue_try_add(&inputQueue, &current);
                     current = NULL;
                  }
               } else if (i2c_get_read_available(i2c0) > 0) {
                  // Received the first length byte.
                  current->lengthBytes[0] = i2c_read_byte_raw(i2c0);
                  current->pos++;
               }
            } else if (i2c_get_read_available(i2c0) > 0) {
               // Received the second length byte.
               current->lengthBytes[1] = i2c_read_byte_raw(i2c0);
               current->state = SendState::SentLength;
               current->pos = 0;
               current->length = (current->lengthBytes[0] << 8) | current->lengthBytes[1];

               if (current->length == 0) {
                  // We have now received the entire message.
                  queue_try_add(&inputQueue, &current);
                  current = NULL;
               }
            }
         } else if (current->state == SendState::SentLength) {
            // Read string data.
            while (current->pos < current->length && i2c_get_read_available(i2c0) > 0) {
               current->buffer[current->pos++] = i2c_read_byte_raw(i2c0);
            }

            if (current->pos == current->length) {
               // We have now received the entire message.
               queue_try_add(&inputQueue, &current);
               current = NULL;
            }
         }

         break;
      }
      case I2C_SLAVE_REQUEST: {
         if (current != NULL) {
            // Reset if a message wasn't receved.
            current->length = 0;
            current->pos = 0;
            current->state = SendState::None;

            memset((void*)current->buffer, 0, MAX_MSG_SIZE);
            queue_try_add(&availInputQueue, &current);
            current = NULL;
         }

         Packet* toSend;
         if (queue_try_peek(&outputQueue, &toSend)) {
            if (toSend->state == SendState::None) {
               i2c_write_raw_blocking(i2c0, &toSend->command, 1);
               toSend->state = SendState::SentCommand;
            } else if (toSend->state == SendState::SentCommand) {
               i2c_write_raw_blocking(i2c0, toSend->lengthBytes, 2);
               if (toSend->length > 0) {
                  toSend->state = SendState::SentLength;
               } else {
                  // Cleanup, sent a request without a string payload.
                  if (!queue_try_remove(&outputQueue, &toSend)) {
                     printf("Unable to remove from queue.");
                  }

                  delete toSend;
               }
            } else if (toSend->state == SendState::SentLength) {
               // Send out the message.
               if ((toSend->length - toSend->pos) > BUFFER_LENGTH) {
                  i2c_write_raw_blocking(i2c0, toSend->buffer + toSend->pos, BUFFER_LENGTH);
                  toSend->pos += BUFFER_LENGTH;
                  // Leave at the top of the queue for the next I2C_SLAVE_REQUEST
               } else {
                  i2c_write_raw_blocking(i2c0, toSend->buffer + toSend->pos, toSend->length - toSend->pos);

                  // Cleanup.
                  queue_try_remove(&outputQueue, &toSend);
                  delete toSend;
               }
            }
         } else {
            // Send NOOP for the
            i2c_write_byte_raw(i2c0, I2C_CLIENT_NOOP);
         }

         break;
      }
      case I2C_SLAVE_FINISH: {
         break;
      }
      default:
         break;
   }
}

bool EnqueueRequest(uint8_t request) {
   Packet* p = new Packet();
   p->length = 0;
   p->command = request;
   p->pos = 0;
   p->state = SendState::None;
   return queue_try_add(&outputQueue, &p);
}

bool EnqueueRequest(uint8_t request, const char* toSend) {
   Packet* p = new Packet();
   p->command = request;
   p->length = strlen(toSend);
   p->lengthBytes[0] = p->length >> 8;
   p->lengthBytes[1] = p->length;
   p->pos = 0;
   p->state = SendState::None;
   memcpy(p->buffer, toSend, p->length);
   return queue_try_add(&outputQueue, &p);
}

void Init(uint sdaPin, uint sclPin, uint addr, uint baudrate) {
   // Configure pins and I2C.
   gpio_init(sdaPin);
   gpio_set_function(sdaPin, GPIO_FUNC_I2C);
   gpio_pull_up(sdaPin);
   // Maxing the drive strength seem to help the host connect more reliably
   // Maxing out client side to the max sink strength to mirror the host 
   gpio_set_drive_strength(sdaPin, GPIO_DRIVE_STRENGTH_12MA);

   gpio_init(sclPin);
   gpio_set_function(sclPin, GPIO_FUNC_I2C);
   gpio_pull_up(sclPin);
   gpio_set_drive_strength(sclPin, GPIO_DRIVE_STRENGTH_12MA);

   i2c_init(i2c0, baudrate);
   i2c_slave_init(i2c0, addr, &i2c_slave_handler);

   // Initalize data structures for synchronizing between I2C interrupt and the main process.
   queue_init(&outputQueue, sizeof(Packet*), 20);
   queue_init(&inputQueue, sizeof(zuluide::i2c::client::Packet*), INPUT_BUFFER_COUNT);
   queue_init(&availInputQueue, sizeof(zuluide::i2c::client::Packet*), INPUT_BUFFER_COUNT);

   for (int i = 0; i < INPUT_BUFFER_COUNT; i++) {
      auto p = new Packet();
      Cleanup(p);
   }
}

void Cleanup(Packet* packet) {
   // Cleanup buffer and put back into service.
   packet->length = 0;
   packet->pos = 0;
   packet->state = SendState::None;
   memset(packet->buffer, 0, MAX_MSG_SIZE);
   queue_try_add(&availInputQueue, &packet);
}

bool Is(Packet* toCheck, uint8_t messageID) {
   return toCheck->command == messageID;
}

bool TryReceive(Packet** toRecv) {
   return queue_try_remove(&inputQueue, toRecv);
}

void ProcessMessages() {
   zuluide::i2c::client::Packet* toRecv;
   if (TryReceive(&toRecv)) {
      if (Is(toRecv, I2C_SERVER_API_VERSION)) {
         ProcessServerAPIVersion(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_SYSTEM_STATUS_JSON)) {
         ProcessSystemStatus(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_UPDATE_FILENAME_CACHE)) {
         ProcessUpdateFilenames(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_IMAGE_FILENAME)) {
         ProcessFilename(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_IMAGE_JSON)) {
         ProcessImage(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_SSID)) {
         ProcessSSID(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_SSID_PASS)) {
         ProcessPassword(toRecv->buffer, toRecv->length);
      } else if (Is(toRecv, I2C_SERVER_RESET)) {
         ProcessReset();
      }

      // Cleanup buffer and put back into service.
      Cleanup(toRecv);
   }
}
}  // namespace zuluide::i2c::client
