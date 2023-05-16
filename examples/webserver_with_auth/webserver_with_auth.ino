#include <WiFi.h>
#include <WebServer.h>
#include "SinricTeleport.h"

#define BAUD_RATE 115200

const char *ssid = ""; /* Change this */
const char *wifi_password = "";  /* Change this */

const char * pubkey = ""; /* Change this */
const char * privkey = ""; /* Change this */

WebServer server(80);

const char* www_username = "admin"; // Prompt user name
const char* www_password = "teleport"; // Prompt password

const char* www_realm = "Custom Auth Realm"; // allows you to set the realm of authentication Default:"Login Required"
String authFailResponse = "Authentication Failed"; // the Content of the HTML response in case of Unautherized Access Default:empty

/* Expose WebServer running on port 80 via Sinric Teleport */
SinricTeleport teleport(pubkey, privkey, "127.0.0.1", 80);

void handle_root() {
  /* Authenticate */
  if (!server.authenticate(www_username, www_password)) {
    return server.requestAuthentication(DIGEST_AUTH, www_realm, authFailResponse);
  } 
 
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
  teleport.onConnected([] (const char * sessionUrl) {
    Serial.printf("[Teleport]: Connected to Teleport!!!\r\n");
    Serial.printf("========================================================\r\n");
    Serial.printf("HTTP : https://%s\r\n", sessionUrl);
    Serial.printf("TCP  : %s:443\r\n", sessionUrl);
    Serial.printf("========================================================\r\n");
  });

  teleport.onDisconnected([] (const char * reason) {
    Serial.printf("[Teleport]: Disconnected!!!\r\n");
    Serial.printf("%s\r\n", reason);
  });

  Serial.printf("[Teleport]: Connecting to Teleport..\r\n");
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
