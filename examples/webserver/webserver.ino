#define ENABLE_SINRIC_TELEPORT_LOG

#include <WiFi.h>
#include <WebServer.h>
#include "SinricTeleport.h"

#define BAUD_RATE 115200

const char *ssid = "";
const char *wifi_password = ""; 

const char * privkey = "";
const char * pubkey = "";

WebServer server(80);

/* 
   This example exposes a simple WebServer running on the ESP32 port 80 via Sinric Teleport. 
   log in to console.sinric.tel to see the endpoint 
*/

SinricTeleport teleport(pubkey, privkey, "127.0.0.1", 80);

void handle_root() {
  String HTML = "<!DOCTYPE html><html><body><h1>Hello !</h1></body></html>";
  server.send(200, "text/html", HTML);
}

void setup_webserver() {
   server.begin();   
   server.on("/", handle_root);
   Serial.println("HTTP server started");
}

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

void setup_teleport() { 
  teleport.onConnected([] () {
    Serial.printf("[Teleport]: Connected!!!\r\n");
  });

  teleport.onDisconnected([] (const char * reason) {
    Serial.printf("[Teleport]: Disconnected!!!\r\n");
  });

  teleport.begin();   
} 

void setup() {
  Serial.begin(BAUD_RATE);
  while (!Serial) {};
 
  setup_wifi();
  setup_webserver();
  setup_teleport();
}

void loop(){
   server.handleClient();
}
