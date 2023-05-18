# Remote File Browser for ESP32 Example

This example demonstrates how to access your ESP32 file system from anywhere in the world over internet

Original example from https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/FSBrowser/FSBrowser.ino has modified to support Sinric Teleport.

1. Upload the data folder via "ESP32 Sketch Data Upload" in Tools menu in Arduino IDE. If you do not have this: https://github.com/me-no-dev/arduino-esp32fs-plugin
2. Update the WiFI ssid, password.
3. Update the Sinric Teleport credentials if you are a registered user.  
4. Flash. 
5. Endopint is displayed in Arduino Serial Monitor.
6. Goto HTTP : https://<your-endpoint>.sinric.tel/edit

![Sinric Teleport Session](https://github.com/sinricpro/teleport-arduino-esp32-library/blob/main/examples/remote-file-browser/img/teleport-endpoint.png?raw=true)


