#include <Arduino.h>
#include "WebMonitor.h" // Install WebMonitor via Library manager or get it from https://github.com/sivar2311/WebMonitor
#include "SinricTeleport.h"
#include <WiFi.h>

const char*    wifi_ssid = "";
const char*    wifi_pass = "";

const int      port      = 80;
const char*    url       = "/webmonitor";

AsyncWebServer server(port);

/* Expose AsyncWebServer running on port 80 via Sinric Teleport */

/* 
// Registered user.
const char * pubkey = "";
const char * privkey = "";
SinricTeleport teleport(pubkey, privkey, "127.0.0.1", port);
*/

// Unregistered user.
SinricTeleport teleport("127.0.0.1", port);


void setup_teleport() { 
  teleport.onConnected([] (const char * sessionUrl) {
    Serial.printf("[Teleport]: Connected to Teleport!!!\r\n");
    Serial.printf("=============================================================================================\r\n");
    Serial.printf("HTTP : https://%s/webmonitor to see the logs\r\n", sessionUrl);
    Serial.printf("=============================================================================================\r\n");
  });

  teleport.onDisconnected([] (const char * reason) {
    Serial.printf("[Teleport]: Disconnected!!!\r\n");
    Serial.printf("%s\r\n", reason);
  });

  Serial.printf("[Teleport]: Connecting to Teleport..\r\n");
  teleport.begin();   
} 

void setup_wifi() { 
  Serial.printf("Connecting to %s\n", wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
}

void setup_webmonitor() {
  WebMonitor.begin(&server, url);
  server.begin(); 
}

void setup() {
    Serial.begin(115200);
   
    setup_wifi();
    setup_teleport();
    setup_webmonitor();
}

void loop() {
    WebMonitor.printf("uptime: %d\r\n", millis() / 1000); 
    delay(1000);
} 