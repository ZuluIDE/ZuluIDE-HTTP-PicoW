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

#include "lwip/opt.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include <pico/util/queue.h>
#include <pico/i2c_slave.h>
#include <cstring>
#include <pico/stdlib.h>
#include <hardware/watchdog.h>
#include <cstdio>
#include "pico/cyw43_arch.h"
#include <vector>
#include <string>
#include "index_html.h"
#include "url_decode.h"
#include "ZuluControlI2CClient.h"

static const uint I2C_SLAVE_ADDRESS = 0x45;
static const uint I2C_BAUDRATE = 100000; // 100 kHz

static const uint I2C_SLAVE_SDA_PIN = 1; //PICO_DEFAULT_I2C_SDA_PIN; // 4
static const uint I2C_SLAVE_SCL_PIN = 0; //PICO_DEFAULT_I2C_SCL_PIN; // 5

enum class ImageCacheState { Idle, Fetching, Full, Iterating, IteratingFinished };

static volatile ImageCacheState imageState = ImageCacheState::Idle;

static char currentStatus[MAX_MSG_SIZE];

static queue_t imageQueue;

static std::vector<char*> images;

static char* imageJson = NULL;

static std::string wifiPass;

static std::string wifiSSID;

enum class State { WaitingForSSID, WaitingForPassword, WIFIInit, WIFIDown, Normal };

static State programState = State::WaitingForSSID;

void RebuildImageJson();

namespace zuluide::i2c::client {

  /**
     Callback function for receiving system status that copies the status
     into a local buffer for use by the web server.
   */
  void ProcessSystemStatus(const uint8_t* message, size_t length) {
    memset(currentStatus, 0, MAX_MSG_SIZE);
    memcpy(currentStatus, message, MAX_MSG_SIZE);
  }

  /**
     Callback function fo rreceiving an image from the I2C server.
     If the web service is iterating, the image is cached for the
     next iterate request from the web server client. If the
     web service is retreiving all fo the images, it is cached in a
     vector until all are received and a single JSON document
     is built for all of the images.
   */
  void ProcessImage(const uint8_t* message, size_t length) {
    if (length > 0) {
      char* image = new char[length + 1];
      memset(image, 0, length + 1);
      memcpy(image, message, length);
      if (imageState == ImageCacheState::Iterating) {
	queue_try_add(&imageQueue, &image);
      } else {
	images.push_back(image);
      }
    } else {
      if (imageState == ImageCacheState::Iterating) {
	imageState = ImageCacheState::IteratingFinished;
      } else {
	// Rebuild the image.
	RebuildImageJson();

	// All images received.
	imageState = ImageCacheState::Full;
      }
    }
  }

  /**
     Handles retreiving the SSID from the server. If one is not provided
     then a compiled constant is used (if avaialble).
   */
  void ProcessSSID(const uint8_t* message, size_t length) {
    if (length > 0) {
      wifiSSID = std::string((const char*)message);
      printf("Using WIFI SSID (%s) from the server.\n", wifiSSID.c_str());
    } else if (sizeof(WIFI_SSID) > 0) {
      wifiSSID = std::string(WIFI_SSID);
      printf("Using WIFI SSID (%s) compiled into the application.\n", wifiSSID.c_str());
    } else {
      printf("No WIFI SSID retrieved from server and none compiled into the application.");
    }

    if (wifiSSID.length() > 0) {
      if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_SSID_PASS)) {
	printf("Failed to add request for SSID password to output queue.");
      }

      programState = State::WaitingForPassword;
    }
  }

  /**
     Handles retreiving the wifi password from the server. If one is not provided
     then a compiled constant is used (if avaialble).
   */
  void ProcessPassword(const uint8_t* message, size_t length) {
    if (length > 0) {
      wifiPass = std::string((const char*)message);
      printf("Using WIFI password (%s) from the server.\n", wifiPass.c_str());
    } else if (sizeof(WIFI_PASSWORD) > 0) {
      wifiPass = std::string(WIFI_PASSWORD);
      printf("Using WIFI password (%s) compiled into the application.\n", wifiPass.c_str());
    } else {
      printf("No WIFI password retrieved from server and none compiled into the application.");
    }

    if (wifiPass.length() > 0) {
      // Put a subscribe message in the queue so when we connect, we immediately subscribe.
      if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_SUBSCRIBE_STATUS_JSON)) {
	printf("Failed to add subscribe to output queue.");
      }

      programState = State::WIFIInit;
    }
  }

  /**
     When the I2C server is started it can send a reset request (probably should). When this
     client receives the reset, it should reset because it may have old data.
   */
  void ProcessReset() {
    printf("Reset Received.\n");
    watchdog_reboot (0, 0, 1000);
  }
}

/**
   Redirect a request to /status to /status.json.
 */
static const char * cgi_handler(int index, int numParams, char *pcParam[], char *pcValue[]) {
  return "/status.json";
}

/**
   Fetches the entire set of images. If the images are not yet available then
   a wait response is sent.
 */
static const char * cgi_handler_imgs(int index, int numParams, char *pcParam[], char *pcValue[]) {
  if (imageState == ImageCacheState::Idle) {
    imageState = ImageCacheState::Fetching;
    if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_IMAGES_JSON)) {
      printf("Failed to add fetch images to output queue.");
    }
  }

  if (imageState == ImageCacheState::Fetching) {
    return "/wait.json";
  }

  return "/images.json";
}

/**
   Fetches the next image when iterating the images. A wait message is sent when
   an image is not ready. A done message is sent when the iteration if finished.
 */
static const char * cgi_handler_next_image(int index, int numParams, char *pcParam[], char *pcValue[]) {
  if (imageState == ImageCacheState::Idle) {
    if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_ITR_IMAGE)) {
      printf("Failed to add iterate image to output queue.");
    }

    imageState = ImageCacheState::Iterating;

    return "/wait.json";
  } else if (imageState == ImageCacheState::Iterating) {
    if (queue_is_empty(&imageQueue)) {
      return "/wait.json";
    } else {
      // We have something that we are about to send out, lets fetch the next so we can be ready.
      if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_ITR_IMAGE)) {
	printf("Failed to add iterate image to output queue.");
      }
    }
  } else if (imageState == ImageCacheState::IteratingFinished) {
    imageState = ImageCacheState::Idle;
    return "/done.json";
  }

  return "/nextImage.json";
}

/**
   Processes a user attempting to mount an image with the image JSON provided in the
   query parameter imageName.
 */
static const char * cgi_handler_image(int index, int numParams, char *params[], char *values[]) {
  if (numParams > 0) {
    for (int i = 0; i < numParams; i++) {
      if (strncmp(params[i], "imageName", sizeof("imageName")) == 0) {
	// Decoding parameters that were URL encoded.
	urldecode(values[i]);
        printf("Setting image to: %s\n", values[i]);
        zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_LOAD_IMAGE, values[i]);
        return "/ok.json";
      }
    }
  }

  return "/error.json";
}

/**
   Allows the user to eject the currently mounted image.
*/
static const char * cgi_handler_eject(int index, int numParams, char *params[], char *values[]) {
  zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_EJECT_IMAGE);
  return "/ok.json";
}

static const tCGI cgi_handlers[] = {{"/status", cgi_handler},
                                    {"/images", cgi_handler_imgs},
                                    {"/image", cgi_handler_image},
                                    {"/eject", cgi_handler_eject},
                                    {"/nextImage", cgi_handler_next_image}};

int main() {
  printf("Starting.\n");

  memset(currentStatus, 0, MAX_MSG_SIZE);
  queue_init(&imageQueue, sizeof(char*), 1);

  stdio_init_all();

  zuluide::i2c::client::Init(I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN, I2C_SLAVE_ADDRESS, I2C_BAUDRATE);

  if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_SSID)) {
    printf("Failed to add request for SSID to output queue.");
  }

  bool httpInitialized = false;

  while(true) {
    switch (programState) {
    case State::WaitingForSSID:
    case State::WaitingForPassword: {
      // Waiting to receive the SSID and password via I2C.
      zuluide::i2c::client::ProcessMessages();
      break;
    }

    case State::WIFIInit: {
      if (cyw43_arch_init()) {
        printf("failed to initialize\n");
        return 1;
      }

      cyw43_arch_enable_sta_mode();
      // Disable powersave mode.
      cyw43_wifi_pm(&cyw43_state, cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 20, 1, 1, 1));

      programState = State::WIFIDown;
      break;
    }

    case State::WIFIDown: {
      printf("Connecting to WiFi.\n");
      if (cyw43_arch_wifi_connect_timeout_ms(wifiSSID.c_str(), wifiPass.c_str(), CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Failed to connect to WiFi.\n");
      } else {
        printf("Connected to WiFi.\n");

        extern cyw43_t cyw43_state;
        auto ip_addr = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;
	char* ipBuffer = new char[32];
	memset(ipBuffer, 0, 32);
	sprintf(ipBuffer, "%lu.%lu.%lu.%lu", ip_addr & 0xFF, (ip_addr >> 8) & 0xFF, (ip_addr >> 16) & 0xFF, ip_addr >> 24);
	printf("IP Address: %s\n", ipBuffer);

	// Send the IP address to the I2C server.
	zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_IP_ADDRESS, ipBuffer);

	if (!httpInitialized) {
	  httpd_init();
	  http_set_cgi_handlers(cgi_handlers, 5);
	  printf("Http server initialized.\n");
	  httpInitialized = true;
	}

        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

        programState = State::Normal;
	printf("System Ready\n");
      }

      break;
    }

    case State::Normal: {
      // Allow I2C functions to process messages and make callbacks as appropriate.
      zuluide::i2c::client::ProcessMessages();

      // Test for WIFI going down.
      if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
	programState = State::WIFIInit;
	printf("WiFi connecton down.\n");

	// Notify the I2C server that we have lost our network connection.
	zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_NET_DOWN);
	cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
	cyw43_arch_deinit();
      }
      break;
    }

    default: {
        printf("Error, unkown state.\n");
      break;
    }

    }
  }

  return 0;
}

/**
   Builds a JSON document using the individual image JSON items sotred in images.
   After it is finished, it clears images and puts the result in imageJson
 */
void RebuildImageJson() {
  size_t totalSize = 2;
  for(auto item : images) {
    totalSize += strlen(item) + 1;
  }

  if (imageJson != NULL) {
    delete[] imageJson;
  }

  imageJson = new char[totalSize + 3];
  imageJson[0] = '[';
  int pos = 1;
  for(auto item : images) {
    if (pos > 1) {
      strcat(imageJson, ",");
      pos++;
    }

    strcat(imageJson, item);
    pos += strlen(item);
  }

  imageJson[pos] = ']';
  imageJson[pos+1] = 0;

  // Delete the images prior to clearing them.
  for (auto item: images) {
    delete[] item;
  }

  images.clear();
}

int get_file_contents(struct fs_file *file, const char* fileContents, int fileLen) {
  memset(file, 0, sizeof(struct fs_file));
  file->pextension = mem_malloc(fileLen + 1);

  if (file->pextension) {
    memcpy(file->pextension, fileContents, fileLen + 1);

    file->data = (const char *)file->pextension;
    file->len = fileLen;
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_PERSISTENT;

    return 1;
  } else {
    return 0;
  }
}

int fs_open_custom(struct fs_file *file, const char *name) {
  if (strncmp(name, "/status.json", sizeof("/status.json")) == 0) {
    return get_file_contents(file, currentStatus, strlen(currentStatus));
  } else if (strncmp(name, "/images.json", sizeof("/images.json")) == 0) {
    return get_file_contents(file, imageJson, strlen(imageJson));
  } else if (strncmp(name, "/ok.json", sizeof("/ok.json")) == 0) {
    auto okMessage = "{\"status\": \"ok\"}";
    return get_file_contents(file, okMessage, strlen(okMessage));
  } else if (strncmp(name, "/wait.json", sizeof("/wait.json")) == 0) {
    auto waitMessage = "{\"status\": \"wait\"}";
    return get_file_contents(file, waitMessage, strlen(waitMessage));
  } else if (strncmp(name, "/done.json", sizeof("/done.json")) == 0) {
    auto doneMessage = "{\"status\": \"done\"}";
    return get_file_contents(file, doneMessage, strlen(doneMessage));
  } else if (strncmp(name, "/index.html", sizeof("/index.html")) == 0) {
    return get_file_contents(file, index_html, strlen(index_html));
  } else if (strncmp(name, "/control.js", sizeof("/control.js")) == 0) {
    return get_file_contents(file, control_js, strlen(control_js));
  } else if (strncmp(name, "/control2.js", sizeof("/control2.js")) == 0) {
    return get_file_contents(file, control_2_js, strlen(control_2_js));
  } else if (strncmp(name, "/style.css", sizeof("/style.css")) == 0) {
    return get_file_contents(file, style_css, strlen(style_css));
  } else if (strncmp(name, "/nextImage.json", sizeof("/nextImage.json")) == 0) {
    char* image;
    if (queue_try_remove(&imageQueue, &image)) {
      int retVal = get_file_contents(file, image, strlen(image));
      delete[] image;
      return retVal;
    }

    return 0;
  } else {
    printf("Unable to find %s\n", name);
    return 0;
  }
}

void fs_close_custom(struct fs_file *file) {
  if (file && file->pextension) {
    mem_free(file->pextension);
    file->pextension = NULL;
  }
}

int fs_read_custom(struct fs_file *file, char *buffer, int count) {
  return FS_READ_EOF;
}

