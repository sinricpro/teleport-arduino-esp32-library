#include <Arduino.h>
#include "WebMonitor.h" // Install WebMonitor via Library manager or get it from https://github.com/sivar2311/WebMonitor
#include "SinricTeleport.h" // Install SinricTeleport via Library manager get it from https://github.com/sinricpro/teleport-arduino-esp32-library
#include <WiFi.h>

const char*    wifi_ssid = "";
const char*    wifi_pass = "";

const int      port      = 80;
const char*    url       = "/webmonitor";

AsyncWebServer server(port);

/* Expose AsyncWebServer running on port 80 via Sinric Teleport */

// If you do not have a Teleport account.
SinricTeleport teleport("127.0.0.1", 80); 

// If you have an account get the keys from console.sinric.tel and update below.
//const char * pubkey = "";
//const char * privkey = "";
//SinricTeleport teleport(pubkey, privkey, "localhost", 80);


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
    Serial.println("Restarting ESP in 5 secs..");
    delay(5000);
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
