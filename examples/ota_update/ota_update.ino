#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Update.h>
#include "SinricTeleport.h"

#define BAUD_RATE 115200

const char *ssid = "";
const char *wifi_password = ""; 

const char * pubkey = "";
const char * privkey = "";

WebServer server(80);
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

/* Expose OTA WebServer running on port 80 via Sinric Teleport */
SinricTeleport teleport(pubkey, privkey, "127.0.0.1", 80);

void setup_wifi() {
  Serial.printf("\r\n[Wifi]: Connecting");
  WiFi.begin(ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf(".");
    delay(250);
  }
  
  IPAddress localIP = WiFi.localIP();
  Serial.printf("connected!\r\n[WiFi]: IP-Address is %d.%d.%d.%d\r\n", localIP[0], localIP[1], localIP[2], localIP[3]);
}

void setup_ota_webserver() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.setDebugOutput(true);
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin()) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);
    } else {
      Serial.printf("Update Failed Unexpectedly (likely broken connection): status=%d\n", upload.status);
    }
  });

  server.begin();
}

void setup_teleport() { 
  teleport.onConnected([] (const char * sessionUrl) {
    Serial.printf("[Teleport]: Connected to Teleport!!!\r\n");
    Serial.printf("========================================================\r\n");
    Serial.printf("OTA URL : https://%s\r\n", sessionUrl);
    Serial.printf("========================================================\r\n");
  });

  /*
    You can upload your OTA bin through terminal: curl -F "image=@firmware.bin" <OTA URL>/update
  */

  teleport.onDisconnected([] (const char * reason) {
    Serial.printf("[Teleport]: Disconnected!!!\r\n");
    Serial.printf("%s\r\n", reason);
  });

  teleport.begin();   
} 

void setup(void) {
  Serial.begin(BAUD_RATE);
  Serial.println();
   
  setup_wifi();
  setup_ota_webserver();
  setup_teleport();   
}

void loop(void) {
  server.handleClient(); 
}