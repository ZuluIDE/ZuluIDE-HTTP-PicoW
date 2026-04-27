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

#include <hardware/watchdog.h>
#include <pico/i2c_slave.h>
#include <pico/stdlib.h>
#include <pico/util/queue.h>
#include <pico/multicore.h>


#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

#include "ZuluControlI2CClient.h"
#include "index_html.h"
#include "fw_upgrade.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "pico/cyw43_arch.h"
#include "url_decode.h"

static const uint I2C_SLAVE_ADDRESS = 0x45;
static const uint I2C_BAUDRATE = 400000;  // 100 kHz

static const uint I2C_SLAVE_SDA_PIN = 0;  // PICO_DEFAULT_I2C_SDA_PIN; // 4
static const uint I2C_SLAVE_SCL_PIN = 1;  // PICO_DEFAULT_I2C_SCL_PIN; // 5

static const uint8_t GPIO_BOARD_TYPE = 5; // Determins if the shield is using a Pico or a laid down RP2040
static const uint8_t GPIO_MCU_LED    = 26;

// Board type A uses a Pico W with minimal GPIO break out
// Board type B uses a laid down RP2040 with GPIO break out
static bool g_board_type_b = false;

enum class FilenameCacheState { Idle, Start, Fetching, Full, Overflow};

enum class ImageCacheState { Idle,
                             Fetching,
                             Full,
                             Iterating,
                             IteratingFinished };

enum class IPAddressState { Init, Sending, Received};

static volatile FilenameCacheState filenameState = FilenameCacheState::Idle;

static char filenames_json[FILENAMES_JSON_CACHE_SIZE] = {0};

static volatile ImageCacheState imageState = ImageCacheState::Idle;

static volatile IPAddressState ipAddrState = IPAddressState::Init;

bool static_ip_set = false;

static ip4_addr_t static_ip, static_gw, static_netmask;


char ipBuffer[32] = {0};

static char versionJson[MAX_MSG_SIZE];

static char currentStatus[MAX_MSG_SIZE];

static queue_t imageQueue;

static std::vector<char *> images;

static char *imageJson = NULL;

static std::string wifiPass;

static bool wifiPassSet = false;

static std::string wifiSSID;

static std::string serverAPIVersion;

enum class State { 
                   Unknown,
                   WaitForAPIVersion,
                   WaitingForSSID,
                   WaitingForPassword,
                   WaitingForConnect,
                   WIFIInit,
                   WIFIDown,
                   Normal };

namespace ClientMessage {
   namespace Prefix
   {
      constexpr char Normal  = 'n';
      constexpr char Debug   = 'd';
      constexpr char Unknown = 'u';
   }
   enum class Type {
                     Normal,
                     Debug
   };
}
static State programState = State::WaitForAPIVersion;



void RebuildImageJson();

static uint32_t millis() {
   return to_ms_since_boot(get_absolute_time());
}

/**
 * Resets the client state, including clearing the output queue and any stored static IP information.
 */
static void reset() {
   zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
   static_ip_set = false;
   memset(&static_ip, 0, sizeof(static_ip));
   memset(&static_netmask, 0, sizeof(static_netmask));
   memset(&static_gw, 0, sizeof(static_gw));
}

namespace zuluide::i2c::client {

   /**
 * Send message to i2c server to log
 */
void LogMessageToServer(ClientMessage::Type type, const char* format, ...)
{
   // Prepend a character to indicate the type of message being sent.
   char prefix  = ClientMessage::Prefix::Unknown;
   switch(type)
   {
      case ClientMessage::Type::Normal:
         prefix = ClientMessage::Prefix::Normal;
         break;
      case ClientMessage::Type::Debug:
         prefix = ClientMessage::Prefix::Debug;
         break;
      default:
         prefix = ClientMessage::Prefix::Unknown;
   }
   // Minimize the size need to store format with the prefix in message.
   size_t format_len = strlen(format);
   if (format_len > MAX_MSG_SIZE - 2) {
      format_len = MAX_MSG_SIZE - 2;
   }
   char *message = new char[format_len + 2];
   memset(message, '\0', format_len + 2);
   message[0] = prefix;
   strncpy(message + 1, format, format_len);

   // Use printf formatting to build payload message, then send to server and print to console.
   char payload[MAX_MSG_SIZE] = {0};
   va_list args;
   va_start(args, format);
   vsnprintf(payload, sizeof(payload), message, args);
   va_end(args);
   printf("%s\n", payload + 1);
   zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_LOG_MSG, payload);
   delete[] message;
}

/**
   Callback function for receiving I2C Server API version string.
 */

void  ProcessServerAPIVersion(const uint8_t *message, size_t length) {
   memset(versionJson, '\0', sizeof(versionJson));
   strcat(versionJson, "{\"clientAPIVersion\":\"");
   strcat(versionJson, I2C_API_VERSION);
   strcat(versionJson, "\"");
   printf("Client API version: v%s\n", I2C_API_VERSION );
   bool matching_major_version = false;
   unsigned long server_major_version = 0;
   unsigned long client_major_version = 0;
   char* period_location = strchr(I2C_API_VERSION, '.');
   client_major_version = strtoul(I2C_API_VERSION, &period_location, 10);

   strcat(versionJson, ", \"clientFWVersion\":\"");
   strcat(versionJson, FW_VERSION);
   strcat(versionJson, "\"");

   if (length > 0)
   {
      serverAPIVersion = std::string((const char*)message);
      strcat(versionJson, ", \"serverAPIVersion\":\"");
      strcat(versionJson, (const char*)message);
      strcat(versionJson, "\"");
      printf("Server API version: v%s\n", message);

      period_location = strchr((const char*)message, '.');
      if (period_location != NULL)
      {
         server_major_version = strtoul((const char*)message, &period_location, 10);
         if (period_location != NULL)
         {
            if (client_major_version > 0 && client_major_version == server_major_version)
            {
               matching_major_version = true;
            }
         }
      }
      
   }
   else
   {
      strcat(versionJson, ", \"serverAPIVersion\":\"Unknown\"");
      printf("Error: no API version received from server\n");
   }

   if (!matching_major_version)
   {
      strcat(versionJson, ", \"message\":\"API major version mismatch. Please update both devices to the latest firmware. <br/> <a href='https://github.com/ZuluIDE/ZuluIDE-firmware/releases'>ZuluIDE firmware</a><br /><a href='https://github.com/ZuluIDE/ZuluIDE-HTTP-PicoW/releases'>ZuluIDE-HTTP-PicoW firmware</a>\"");
      printf("Warning: major versions between client and sever do not match. Please upgrade both devices to the latest firmware\n");
      printf("https://github.com/ZuluIDE/ZuluIDE-HTTP-PicoW/releases\n");
      printf("https://github.com/ZuluIDE/ZuluIDE-firmware/releases\n");
   }

   strcat(versionJson, "}");
   // Clear unhandled request retries
   EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
   programState = State::WaitingForSSID;
}

void ProcessWiFiConnect()
{
   printf("Wifi Connect Received\n");
   programState = State::WIFIInit;
}

/**
   Callback function for receiving system status that copies the status
   into a local buffer for use by the web server.
 */
void ProcessSystemStatus(const uint8_t *message, size_t length) {
   memset(currentStatus, 0, MAX_MSG_SIZE);
   memcpy(currentStatus, message, MAX_MSG_SIZE);
}

void ProcessUpdateFilenames(const uint8_t *message, size_t length) {
   printf("Begining filename cache update process\n");
   filenameState = FilenameCacheState::Start;
}

/**
   Callback function for receiving a filename from the I2C server.
   It adds the filename to JSON file cached in SRAM.
 */
void ProcessFilename(const uint8_t *message, size_t length) {
   const size_t cache_size = sizeof(filenames_json);
   printf("Process filename length: %d\n", length);
   if (filenameState == FilenameCacheState::Start) {
      memset(filenames_json, '\0', cache_size);
      if (cache_size < sizeof("{\"filenames\":[")) {
         printf("Filename cache overflowed after init, increase cache size\n");
         filenameState = FilenameCacheState::Overflow;
         return;
      } else {
         strcat(filenames_json, "{\"filenames\":[");
      }
   }

   if (length > 0) {
      if (filenameState == FilenameCacheState::Start)
      {
         if (strlen(filenames_json) + strlen("\"") + length + strlen("\"") + 1 > cache_size) {
            printf("Filename cache overflowed adding the first filename JSON cache\n");
            filenameState = FilenameCacheState::Overflow;
            return;
         }
         strcat(filenames_json, "\"");
         memcpy(filenames_json + strlen(filenames_json), message, length);
         strcat(filenames_json, "\"");
         filenameState = FilenameCacheState::Fetching;
      } else if (filenameState == FilenameCacheState::Fetching) {
         if (strlen(filenames_json) + strlen(",\"") + length + strlen("\"") + 1 > cache_size) {
            printf("Filename cache overflowed adding a filename JSON cache\n");
            filenameState = FilenameCacheState::Overflow;
            return;
         }
         strcat(filenames_json, ",\"");
         memcpy(filenames_json + strlen(filenames_json), message, length);
         strcat(filenames_json, "\"");
      }
   } else {
      if (filenameState == FilenameCacheState::Start || filenameState == FilenameCacheState::Fetching)
      {
         if (strlen(filenames_json) + strlen("]}") + 1 > cache_size) {
            printf("Filename cache overflowed adding closing characters\n");
            filenameState = FilenameCacheState::Overflow;
         } else {
            printf("Received filename of length zero, setting state to Full\n");
            // All images received.
            strcat(filenames_json, "]}");
            filenameState = FilenameCacheState::Full;
         }
      }
   }
}

/**
   Callback function fo receiving an image from the I2C server.
   If the web service is iterating, the image is cached for the
   next iterate request from the web server client. If the
   web service is retrieving all fo the images, it is cached in a
   vector until all are received and a single JSON document
   is built for all of the images.
 */
void ProcessImage(const uint8_t *message, size_t length) {
   if (length > 0) {
      char *image = new char[length + 1];
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
void ProcessSSID(const uint8_t *message, size_t length) {
   if (length > 0) {
      wifiSSID = std::string((const char *)message);
      printf("Using WIFI SSID (%s) from the server.\n", wifiSSID.c_str());
   } else if (sizeof(WIFI_SSID) > 0) {
      wifiSSID = std::string(WIFI_SSID);
      printf("Using WIFI SSID (%s) compiled into the application.\n", wifiSSID.c_str());
   } else {
      printf("No WIFI SSID retrieved from server and none compiled into the application.\n");
   }

   // Clear retries
   EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
   programState = State::WaitingForPassword;
}

/**
   Handles retrieving the wifi password from the server. If one is not provided
   then a compiled constant is used (if available). If a password is not provided
   then it is assumed to be an open network.
 */
void ProcessPassword(const uint8_t *message, size_t length) {
   if (length > 0) {
      printf("Using WIFI password from the server.\n");
   } else {
      printf("WiFi password cleared, assuming open WiFi network.\n");
   }
   wifiPass = std::string((const char *)message);
   wifiPassSet = true;
   // Clear retries
   EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
   programState = State::WaitingForConnect;
}

/**
   When the I2C server is started it can send a reset request (probably should). When this
   client receives the reset, it should reset because it may have old data.
 */
void ProcessReset() {
   printf("Reset Received.\n");
   reset();
   programState = State::WaitForAPIVersion;
}

void ProcessStaticIP(const uint8_t* message, size_t length)
{
   if (length <= 3)
   {
      static_ip_set = false;
   }
   else
   {
      static_ip_set = false;
      const char *ip_data =  (const char*) &message[2];
      if (strncmp("ip",(const char*) message, 2) == 0)
      {
         printf("Setting static IP to %s\n", ip_data);
         static_ip_set = ip4addr_aton(ip_data, &static_ip);
      }
      else if (strncmp("nm", (const char*)message, 2) == 0)
      {
         printf("Setting static IP netmask to %s\n", ip_data);
         static_ip_set = ip4addr_aton(ip_data, &static_netmask);
      }
      else if (strncmp("gw", (const char*)message, 2) == 0)
      {
         printf("Setting static IP gateway to %s\n", ip_data);
         static_ip_set = ip4addr_aton(ip_data, &static_gw);
      }
   }
}

void ProcessIPAddressAck() {
   ipAddrState = IPAddressState::Received;
   printf("Server received IP Address.\n");

}
}  // namespace zuluide::i2c::client

/**
   Redirect a request to /version to /version.json.
 */
static const char *cgi_handler_version(int index, int numParams, char *pcParam[], char *pcValue[]) {
   return "/version.json";
}

/**
   Redirect a request to /status to /status.json.
 */
static const char *cgi_handler_status(int index, int numParams, char *pcParam[], char *pcValue[]) {
   return "/status.json";
}

static const char *cgi_handler_filenames(int index, int numParams, char *pcParam[], char *pcValue[]) {
   printf("Sending filenames cached JSON\n");
   if (filenameState == FilenameCacheState::Full) {
      if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_FILENAMES)) {
         printf("Failed to add fetch filenames to output queue.\n");
      }
   }

   if (filenameState == FilenameCacheState::Start ||  filenameState == FilenameCacheState::Fetching) {
      return "/wait.json";
   }

   if (filenameState == FilenameCacheState::Overflow) {
      return "/overflow.json";
   }

   return "/filenames.json";
}

/**
   Fetches the entire set of images. If the images are not yet available then
   a wait response is sent.
 */
static const char *cgi_handler_imgs(int index, int numParams, char *pcParam[], char *pcValue[]) {
   if (imageState == ImageCacheState::Idle) {
      imageState = ImageCacheState::Fetching;
      if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_IMAGES_JSON)) {
         printf("Failed to add fetch images to output queue.\n");
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
static const char *cgi_handler_next_image(int index, int numParams, char *pcParam[], char *pcValue[]) {
   if (imageState == ImageCacheState::Idle) {
      if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_ITR_IMAGE)) {
         printf("Failed to add iterate image to output queue.\n");
      }

      imageState = ImageCacheState::Iterating;

      return "/wait.json";
   } else if (imageState == ImageCacheState::Iterating) {
      if (queue_is_empty(&imageQueue)) {
         return "/wait.json";
      } else {
         // We have something that we are about to send out, lets fetch the next so we can be ready.
         if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_ITR_IMAGE)) {
            printf("Failed to add iterate image to output queue.\n");
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
static const char *cgi_handler_image(int index, int numParams, char *params[], char *values[]) {
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
static const char *cgi_handler_eject(int index, int numParams, char *params[], char *values[]) {
   zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_EJECT_IMAGE);
   return "/ok.json";
}


static const tCGI cgi_handlers[] = {
                                    {"/version", cgi_handler_version},
                                    {"/status", cgi_handler_status},
                                    {"/filenames", cgi_handler_filenames},
                                    {"/images", cgi_handler_imgs},
                                    {"/image", cgi_handler_image},
                                    {"/eject", cgi_handler_eject},
                                    {"/nextImage", cgi_handler_next_image}
};

/* Handlers for POST requests */

err_t (*g_httpd_post_receive_data_handler)(void *connection, struct pbuf *p);
void (*g_httpd_post_finished_handler)(void *connection, char *response_uri, u16_t response_uri_len);

err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd)
{
   if (strcmp(uri, "/fw_upgrade.cgi") == 0)
   {
      g_httpd_post_receive_data_handler = &fwupgrade_post_receive_data;
      g_httpd_post_finished_handler = &fwupgrade_post_finished;
      return fwupgrade_post_begin(connection, uri, http_request, http_request_len, content_len,
         response_uri, response_uri_len, post_auto_wnd);
   }

   g_httpd_post_receive_data_handler = nullptr;
   g_httpd_post_finished_handler = nullptr;
   return ERR_VAL;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
   err_t result = ERR_VAL;
   if (g_httpd_post_receive_data_handler)
      result = g_httpd_post_receive_data_handler(connection, p);

   return result;
}

void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
   if (g_httpd_post_finished_handler)
      g_httpd_post_finished_handler(connection, response_uri, response_uri_len);

   g_httpd_post_receive_data_handler = nullptr;
   g_httpd_post_finished_handler = nullptr;
}

void core1_main() {
   zuluide::i2c::client::Init(I2C_SLAVE_SDA_PIN, I2C_SLAVE_SCL_PIN, I2C_SLAVE_ADDRESS, I2C_BAUDRATE);
   multicore_fifo_push_blocking(0xbeef);
   while(true)
   {
      tight_loop_contents();
   }
}

static bool has_elapsed(uint32_t start, uint32_t elapsed) {
   return (uint32_t)(millis() - start) > elapsed;
}

void start_multicore_i2c() {
   multicore_launch_core1(core1_main);
   uint32_t g = multicore_fifo_pop_blocking();
   if (g == 0xbeef)
      printf("Core 1 successfully launched");
   else {
      printf("Core 1 failed to launch");
   }
}

using zuluide::i2c::client::LogMessageToServer;

int main() {
   gpio_set_pulls(GPIO_BOARD_TYPE, true, false);
   // wait for pulls
   busy_wait_us(1);
   g_board_type_b = !gpio_get(GPIO_BOARD_TYPE);
   if (g_board_type_b)
   {
      // init Led connected directly to the MCU
      gpio_set_function(GPIO_MCU_LED, GPIO_FUNC_SIO);
      gpio_set_pulls(GPIO_MCU_LED, false, false);
      gpio_put(GPIO_MCU_LED, true);
      gpio_set_drive_strength(GPIO_MCU_LED, GPIO_DRIVE_STRENGTH_12MA);
      gpio_set_slew_rate(GPIO_MCU_LED, GPIO_SLEW_RATE_SLOW);
      gpio_set_dir(GPIO_MCU_LED, true);
   }

   stdio_init_all();
   printf("Starting.\n");

   memset(currentStatus, 0, MAX_MSG_SIZE);
   memset(versionJson, '\0', MAX_MSG_SIZE);
   sprintf(versionJson,"{\"clientAPIVersion\":\"%s\", \"serverAPIVersion\": \"server failed to send version\"}", I2C_API_VERSION);
   queue_init(&imageQueue, sizeof(char *), 1);

   start_multicore_i2c();

   if (cyw43_arch_init()) {
      LogMessageToServer(ClientMessage::Type::Normal, "Failed to initialize WiFi interface. WiFi client halting.");
      return 1;
   }

   bool httpInitialized = false;
   bool started_blink = true;
   bool blink_on = false;
   uint32_t start_time = millis();
   uint32_t send_ip_start_time = millis();
   int number_of_blinks = 3;
   uint32_t waiting_start = millis();
   State last_state = State::Unknown;
   while (true) {
      // blink number_of_blink when board is powered on
      if (started_blink)
      {
         if ((uint32_t)(millis() - start_time) > 500)
         {
            blink_on = !blink_on;
            gpio_put(GPIO_MCU_LED, blink_on);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, blink_on);
            start_time = millis();
            if (!blink_on && --number_of_blinks <= 0)
            {
               started_blink = false;
            }
         }
      }

      switch (programState) {
         case State::WaitForAPIVersion:
            if (programState != last_state || has_elapsed(waiting_start, I2C_CMD_RETRY_MS))
            {
               zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_API_VERSION, I2C_API_VERSION);
               waiting_start = millis();
            }
            last_state = programState;
            zuluide::i2c::client::ProcessMessages();
            break;
         case State::WaitingForSSID:
            if (programState != last_state || has_elapsed(waiting_start, I2C_CMD_RETRY_MS))
            {
               printf("Waiting for SSID\n");
               if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_SSID)) {
                     printf("Failed to add request for SSID to output queue\n");
               }
               waiting_start = millis();
            }
            last_state = programState;
            zuluide::i2c::client::ProcessMessages();
            break;
         case State::WaitingForPassword: {
            if (programState != last_state || has_elapsed(waiting_start, I2C_CMD_RETRY_MS))
            {
               printf("Waiting for Password\n");
               if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_FETCH_SSID_PASS)) {
                  printf("Failed to add request for Password to output queue\n");
               }
            }
            last_state = programState;
            zuluide::i2c::client::ProcessMessages();
            break;
         }
         case State::WaitingForConnect:
         {
            last_state = programState;
            printf("Waiting for Connect\n");
            zuluide::i2c::client::ProcessMessages();
            break;
         }

         case State::WIFIInit: {
            last_state = programState;
            printf("Initializing to WiFi.\n");
            started_blink = false;
            gpio_put(GPIO_MCU_LED, false);

            cyw43_arch_enable_sta_mode();
            // Disable powersave mode.
            cyw43_wifi_pm(&cyw43_state, cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 20, 1, 1, 1));

            if (static_ip_set)
            {
               printf("Setting up static IP\n");
               cyw43_arch_lwip_begin();
               dhcp_stop(cyw43_state.netif);     // turn off DHCP
               netif_set_addr(cyw43_state.netif, &static_ip,&static_netmask,&static_gw);
               cyw43_arch_lwip_end();
            }
            else
            {
               printf("Setting up DHCP\n");
               cyw43_arch_lwip_begin();
               dhcp_start(cyw43_state.netif);     // turn on DHCP
               cyw43_arch_lwip_end();
            }

            programState = State::WIFIDown;
            break;
         }

         case State::WIFIDown: {
            last_state = programState;

            if (wifiSSID.empty() || sizeof(WIFI_SSID) == 0) {
               reset();
               programState = State::WaitingForSSID;
            } else {
               if (wifiSSID.empty() && sizeof(WIFI_SSID) > 0) {
                  wifiSSID = WIFI_SSID;
               }
               bool open_network = (wifiPassSet && wifiPass.empty()) || (!wifiPassSet && sizeof(WIFI_PASSWORD) == 0);

               if (open_network) {
                  LogMessageToServer(ClientMessage::Type::Normal, "Connecting to open WiFi network: %s", wifiSSID.c_str());
               } else {
                  LogMessageToServer(ClientMessage::Type::Normal, "Connecting to secured WiFi network: %s", wifiSSID.c_str());
               }

               int connection_result;  
               if (open_network) {
                     connection_result = cyw43_arch_wifi_connect_timeout_ms(
                        wifiSSID.c_str(),
                        nullptr,
                        CYW43_AUTH_OPEN,
                        WIFI_CONNECT_TIMEOUT_MS);
               } else {
                     connection_result = cyw43_arch_wifi_connect_timeout_ms(
                        wifiSSID.c_str(), wifiPassSet ? wifiPass.c_str() : WIFI_PASSWORD,
                        CYW43_AUTH_WPA2_AES_PSK,
                        WIFI_CONNECT_TIMEOUT_MS);
               }
               if (PICO_ERROR_NONE != connection_result) {
                  reset();
                  LogMessageToServer(ClientMessage::Type::Normal, "Failed to connect to WiFi.");
                  programState = State::WaitingForSSID;
               } else {
                  zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_RESET_QUEUE);
                  LogMessageToServer(ClientMessage::Type::Normal, "Connected to WiFi.");
                  extern cyw43_t cyw43_state;
                  auto ip_addr = cyw43_state.netif[CYW43_ITF_STA].ip_addr.addr;

                  memset(ipBuffer, 0, 32);
                  sprintf(ipBuffer, "%lu.%lu.%lu.%lu", ip_addr & 0xFF, (ip_addr >> 8) & 0xFF, (ip_addr >> 16) & 0xFF, ip_addr >> 24);
                  printf("IP Address: %s\n", ipBuffer);

                  ipAddrState = IPAddressState::Sending;

                  if (!httpInitialized) {
                     httpd_init();
                     http_set_cgi_handlers(cgi_handlers, sizeof(cgi_handlers)/sizeof(cgi_handlers[0]));
                     LogMessageToServer(ClientMessage::Type::Debug, "Http server initialized.");
                     httpInitialized = true;
                  }

                  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

                  // Put a subscribe message in the queue so when we connect, we immediately subscribe.
                  if (!zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_SUBSCRIBE_STATUS_JSON)) {
                     printf("Failed to add subscribe to output queue.\n");
                  }
                  programState = State::Normal;
                  LogMessageToServer(ClientMessage::Type::Debug, "System Ready");
               }
            }
            break;
         }

         case State::Normal: {
            last_state = programState;
            // Allow I2C functions to process messages and make callbacks as appropriate.
            zuluide::i2c::client::ProcessMessages();

            // Test for WIFI going down.
            if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
               programState = State::WIFIDown;
               LogMessageToServer(ClientMessage::Type::Normal, "WiFi connection down.\n");

               cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
               gpio_put(GPIO_MCU_LED, false);
               started_blink = false;
            }
            break;
         }

         default: {
            last_state = State::Unknown;
            printf("Error, unkown state.\n");
            break;
         }
      }

      if ((uint32_t)(millis() - send_ip_start_time) > 3000)
      {

         if (IPAddressState::Sending == ipAddrState)
         {
            printf("Sending ip address %s\n", ipBuffer);
            // Send the IP address to the I2C server.
            zuluide::i2c::client::EnqueueRequest(I2C_CLIENT_IP_ADDRESS, ipBuffer);
         }
         send_ip_start_time = millis();
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
   for (auto item : images) {
      totalSize += strlen(item) + 1;
   }

   if (imageJson != NULL) {
      delete[] imageJson;
   }

   imageJson = new char[totalSize + 3];
   imageJson[0] = '[';
   int pos = 1;
   for (auto item : images) {
      if (pos > 1) {
         strcat(imageJson, ",");
         pos++;
      }

      strcat(imageJson, item);
      pos += strlen(item);
   }

   imageJson[pos] = ']';
   imageJson[pos + 1] = 0;

   // Delete the images prior to clearing them.
   for (auto item : images) {
      delete[] item;
   }

   images.clear();
}

int get_file_contents(struct fs_file *file, const char *fileContents, int fileLen) {
   memset(file, 0, sizeof(struct fs_file));
   if (fileContents) {
      file->pextension = (void*)fileContents;
      file->data = NULL;
      file->len = fileLen;
      file->index = 0;
      file->flags = FS_FILE_FLAGS_HEADER_PERSISTENT;
      return 1;
   } else {
      return 0;
   }
}

int fs_open_custom(struct fs_file *file, const char *name) {
   printf("open custom name: %s\n", name);
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
   } else if (strncmp(name, "/overflow.json", sizeof("/overflow.json")) == 0) {
      auto waitMessage = "{\"status\": \"overflow\"}";
      return get_file_contents(file, waitMessage, strlen(waitMessage));
   } else if (strncmp(name, "/done.json", sizeof("/done.json")) == 0) {
      auto doneMessage = "{\"status\": \"done\"}";
      return get_file_contents(file, doneMessage, strlen(doneMessage));
   } else if (strncmp(name, "/index.html", sizeof("/index.html")) == 0) {
      return get_file_contents(file, index_html, strlen(index_html));
   } else if (strncmp(name, "/fw_upgrade.html", sizeof("/fw_upgrade.html")) == 0) {
      return get_file_contents(file, fw_upgrade_html, strlen(fw_upgrade_html));
   } else if (strncmp(name, "/control.js", sizeof("/control.js")) == 0) {
      return get_file_contents(file, control_js, strlen(control_js));
   } else if (strncmp(name, "/style.css", sizeof("/style.css")) == 0) {
      return get_file_contents(file, style_css, strlen(style_css));
   } else if (strncmp(name, "/filenames.json", sizeof("/filenames.json")) == 0) {
      return get_file_contents(file, filenames_json, strlen(filenames_json));
   } else if (strncmp(name, "/nextImage.json", sizeof("/nextImage.json")) == 0) {
      char *image;
      if (queue_try_remove(&imageQueue, &image)) {
         int retVal = get_file_contents(file, image, strlen(image));
         delete[] image;
         return retVal;
      }

      return 0;
      
   } else if (strncmp(name, "/version.js", sizeof("/version.js")) == 0) {
      return get_file_contents(file, version_js, strlen(version_js));
   } else if (strncmp(name, "/version.json", sizeof("/version.json")) == 0) {
      return get_file_contents(file, versionJson, strlen(versionJson));
   } else {
      printf("Unable to find %s\n", name);
      return 0;
   }
}

void fs_close_custom(struct fs_file *file) {
   printf("close custom closing file\n");
}

int fs_read_custom(struct fs_file *file, char *buffer, int count) 
{
   if (file->index >= file->len)
      return FS_READ_EOF;
   int read = (file->len - file->index < count) ? file->len - file->index : count; 
   memcpy(buffer, (char*) file->pextension + file->index, read);
   file->index += read;
   return read;
}
