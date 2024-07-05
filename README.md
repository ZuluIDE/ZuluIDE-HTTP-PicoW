# ZuluIDE-HTTP-PicoW
ZuluIDE-HTTP-PicoW provides a web-based remote control interface that runs on a separate Pico W and communicates with the ZuluIDE via I2C.

## Building and Installing on PicoW

You can obtain a UF2 image built automatically via GitHub by going to the [releases](https://github.com/ZuluIDE/ZuluIDE-HTTP-PicoW/releases) page.

If you want to build your own image, follow the instructions in [Getting started with Raspberry Pi Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf). Chapter 2 provides instructions on how to obtain the SDK and setup the build tools. **Note:** be sure to correctly set the `PICO_SDK_PATH` environment variable (e.g., `export PICO_SDK_PATH=/home/myusername/repos/pico-sdk/`)

After you have setup the SDK and build tools run the following instructions:

1. Obtain the ZuluIDE-HTTP-PicoW repository by running: `git clone git@github.com:ZuluIDE/ZuluIDE-HTTP-PicoW.git`
2. From within a terminal, change to the directory containing the ZuluIDE-HTTP-PicoW repository
3. Make a build directory ( `mkdir build`) and then change to that directory (`cd build`)
4. Run `cmake ..`
5. Run `make`

After doing this, you should find the `zuluide_http_picow.uf2` file in the build directory.

## Configuring WiFi Settings on ZuluIDE SD Card

The PicoW reads the WiFi SSID and password from the ZuluIDE via I2C. You set the values for these by creating (or editing) the zuluide.ini file on the SD card and adding the `[UI]` section with the `wifipassword` and `wifissid` fields as shown below.


    [UI]
    wifipassword=MY_PASSWORD # Password for the WIFI network.
    wifissid=MY_NETWORK_SSID # SSID for the WIFI network


## Connecting PicoW to ZuluIDE

After installing ZuluIDE-HTTP-PicoW onto a PicoW, the PicoW must be connected to the ZuluIDE via 3 wires. The following diagram shows which pins on the PicoW must be connected to the ZuluIDE. Additionally, you must power the Pico W (e.g., via its USB port or any other methods described by the Raspberry PI Pico documentation).

Lastly, be sure to restart the ZuluIDE with the PicoW connected. ZuluIDE does not support hot plugging on the I2C connection used by the PicoW.

![Wiring PicoW to ZuluIDE [^1] ](pico-pinout-zuluide.svg)

## Using the Web Page

The included web page is a very basic proof-of-concept for how to use the web services. You access the web site by opening a browser and going to `index.html` using the IP address assigned to the PicoW via DHCP. For example, if your DHCP server assigned the PicoW `10.0.0.13` then you would open `http://10.0.0.13` in your browser. Be warned, there is no security of anykind built into this included website.

## Using the Web Service

The web service allows you to build your own interface or custom integration for controlling the ZuluIDE. Be warned, there is no security of any kind build into these web-service endpoints. The included web-page (`index.html`) provides an example of these web service endpoints can be used.

### `/status`

Get request that returns a JSON representation of the current state of the ZuluIDE.

### `/nextImage`

Get request that returns a JSON representation of one of the images on the SD card currently inserted in the ZuluIDE. If will return a `{"status":"wait"}` JSON document when it is in the processes of fetching the next image. When you receive this, try again. When it has finished interating through all of the images it will return a `{"status":"done"}` document.

### `/images`

Get request that returns all of the images in the system in a JSON array. If will return a `{"status":"wait"}` JSON document when it is in the processes of fetching the images. Using this endpoint to retrieve all of the images in a single operation will load all of the images into the PicoW's memory.

### `/eject`

Get request that causes the ZuluIDE to eject an image. Always returns a `{"status":"OK"}` JSON document.

### `/images?imageName=myimage.iso`

Get request that causes the ZuluIDE to load the image passed via the `imageName` query parameter.

[^1]: Pico Pinout image is © 2012-2024 Raspberry Pi Ltd and is licensed under a [Creative Commons Attribution-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-sa/4.0/) (CC BY-SA) licence.