#include <WebServer.h>
#include "OV2640.h"
#include "SinricTeleport.h"

#define BAUD_RATE 115200

const char *ssid = "";
const char *wifi_password = "";

OV2640 cam;
WebServer server(80);

const char HEADER[] = "HTTP/1.1 200 OK\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
const char BOUNDARY[] = "\r\n--123456789000000000000987654321\r\n";
const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";
const char JHEADER[] = "HTTP/1.1 200 OK\r\n"
                       "Content-disposition: inline; filename=capture.jpg\r\n"
                       "Content-type: image/jpeg\r\n\r\n";

const int hdrLen = strlen(HEADER);
const int bdrLen = strlen(BOUNDARY);
const int cntLen = strlen(CTNTTYPE);
const int jhdLen = strlen(JHEADER);


/* 
// If you are a registered user, get your public/private keys from console.sinric.tel.

const char * pubkey = "";
const char * privkey = "";
SinricTeleport teleport(pubkey, privkey, "127.0.0.1", 80);
*/

// Unregistered user.
SinricTeleport teleport("127.0.0.1", 80);

void handle_jpg_stream(void) {
  char buf[32];
  int s;

  WiFiClient client = server.client();

  client.write(HEADER, hdrLen);
  client.write(BOUNDARY, bdrLen);

  while (true) {
    if (!client.connected()) break;
    cam.run();
    s = cam.getSize();
    client.write(CTNTTYPE, cntLen);
    sprintf(buf, "%d\r\n\r\n", s);
    client.write(buf, strlen(buf));
    client.write((char *)cam.getfb(), s);
    client.write(BOUNDARY, bdrLen);
  }
}

void handle_jpg(void) {
  WiFiClient client = server.client();

  if (!client.connected()) return;
  cam.run();
  client.write(JHEADER, jhdLen);
  client.write((char *)cam.getfb(), cam.getSize());
}

void handle_not_found() {
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text / plain", message);
}

void setup_camera() {
  /* 
    FRAMESIZE_96X96,    // 96x96
    FRAMESIZE_QQVGA,    // 160x120
    FRAMESIZE_QCIF,     // 176x144
    FRAMESIZE_HQVGA,    // 240x176
    FRAMESIZE_240X240,  // 240x240
    FRAMESIZE_QVGA,     // 320x240
    FRAMESIZE_CIF,      // 400x296
    FRAMESIZE_HVGA,     // 480x320
    FRAMESIZE_VGA,      // 640x480
    FRAMESIZE_SVGA,     // 800x600
    FRAMESIZE_XGA,      // 1024x768
    FRAMESIZE_HD,       // 1280x720
    FRAMESIZE_SXGA,     // 1280x1024
    FRAMESIZE_UXGA,     // 1600x1200
  */

  camera_config_t cam_config = esp32cam_ttgo_t_config; /* available: esp32cam_ttgo_t_config , esp32cam_config, esp32cam_aithinker_config, */
  cam_config.frame_size = FRAMESIZE_HVGA; /* Select from above */
  cam_config.jpeg_quality = 32;  /* 0-63 lower numbers are higher quality. default 12 */
  cam.init(cam_config);

  IPAddress ip;
  ip = WiFi.localIP();
  
  server.on("/mjpeg/1", HTTP_GET, handle_jpg_stream);
  server.on("/jpg", HTTP_GET, handle_jpg);

  // Uncomment to print local WiFi streaming details.
  // Serial.print("WiFI LAN streaming Link: http://");
  // Serial.print(ip);
  // Serial.println("/mjpeg/1");

  // Serial.print("WiFI LAN snapshot link: http://");
  // Serial.print(ip);
  // Serial.println("/jpg");
   
  
  server.onNotFound(handle_not_found);
  server.begin();
}

void setup_teleport() {
  teleport.onConnected([](const char *sessionUrl) {
    Serial.printf("[Teleport]: Connected to Teleport!!!\r\n");
    Serial.printf("=============================================================================================\r\n");
    Serial.printf("Public Camera Streaming URL : https://%s/mjpeg/1\r\n", sessionUrl);
    Serial.printf("Public Camera Snapshot  URL : https://%s/jpg\r\n", sessionUrl);
    Serial.printf("=============================================================================================\r\n");
  });

  teleport.onDisconnected([](const char *reason) {
    Serial.printf("[Teleport]: Disconnected!!!\r\n");
    Serial.printf("%s\r\n", reason);
    Serial.println("Restarting ESP in 5 secs..");
    delay(5000);
    ESP.restart();  
  });

  Serial.printf("[Teleport]: Connecting to Teleport..\r\n");
  teleport.begin();  
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

// Softreboot device
void system_reboot() {
  Serial.printf("Rebooting system now.\n\n");
  esp_restart();
}

void setup() {
  Serial.begin(BAUD_RATE);
  while (!Serial) {};

  setup_wifi();
  setup_camera();
  setup_teleport();
}

void loop() {
  server.handleClient();
}
