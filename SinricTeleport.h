/*
    Copyright (c) 2023 Sinric. All rights reserved.
    Licensed under Creative Commons Attribution-Share Alike (CC BY-SA)

    This file is part of the Sinric Teleport (https://github.com/sinricpro/teleport-arduino-esp32-library)
*/

#pragma once

#ifdef ENABLE_SINRIC_TELEPORT_LOG
  #ifdef DEBUG_ESP_PORT
    #define DEBUG_TELEPORT(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
  #else
    #define DEBUG_TELEPORT(...) if(Serial) Serial.printf( __VA_ARGS__ )
  #endif
  #else
    #define DEBUG_TELEPORT(x...) if (false) do { (void)0; } while (0)
#endif

#define SINRIC_TELEPORT_VERSION "1.1.1"

#include <libssh2.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include <WiFi.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include "netdb.h" // gethostbyname
#include <string>

class SinricTeleport {
  public:
    SinricTeleport(const std::string &publicKey, const std::string &privateKey,
                   const std::string &localIP, int localPort) : publicKey(publicKey), privateKey(privateKey), localIP(localIP), localPort(localPort) {}

    void begin();

  private:
    std::string privateKey;
    std::string publicKey;

    const char *teleportServerIP = "5.161.193.42";
    int teleportServerPort = 8443;

    const char *teleportServerListenHost = "localhost"; /* determined by server */
    int teleportServerDynaGotPort; /* determined by server */

    std::string localIP = "127.0.0.1";
    int localPort = 80;

    static void teleportTask(void * parameter);

    bool isValidPublicKey();
    bool isValidPrivateKey();
    
    bool isStartingWith(const std::string& str, const std::string& prefix);
    bool isEndingWith(const std::string& str, const std::string& ending);
  
    int forwardTunnel(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel);
};
 
void SinricTeleport::teleportTask(void * pvParameters) {
  SinricTeleport *l_pThis = (SinricTeleport *) pvParameters;

  int rc, i;
  struct sockaddr_in sin;
  const char *fingerprint;
  char *userauthlist;
  int sock = -1;

  LIBSSH2_SESSION *session;
  LIBSSH2_LISTENER *listener = NULL;
  LIBSSH2_CHANNEL *channel = NULL;

  //DEBUG_TELEPORT("[Teleport]: libssh2 version: %s\n", LIBSSH2_VERSION);

  /* Initialize libssh2 */
  rc = libssh2_init(0); // 0 will initialize the crypto library

  if (rc != 0) {
    DEBUG_TELEPORT("[Teleport]: libssh2 initialization failed (%d)\n", rc);
    return;
  }

  DEBUG_TELEPORT("[Teleport]: Connecting to Teleport server ..\n");

  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    Serial.printf("[Teleport]: Error opening socket!\n");
    return;
  }

  sin.sin_family = AF_INET;
  if (INADDR_NONE == (sin.sin_addr.s_addr = inet_addr(l_pThis->teleportServerIP))) {
    Serial.printf("[Teleport]: Invalid server IP address of Teleport server!\n");
    return;
  }

  sin.sin_port = htons(l_pThis->teleportServerPort); /* SSH port */
  if (connect(sock, (struct sockaddr*)(&sin),
              sizeof(struct sockaddr_in)) != 0) {
    Serial.printf("Failed to connect to Teleport server!\n");
    return;
  }

  /* Create a session instance of ssh2*/
  session = libssh2_session_init();
  if (!session) {
    Serial.printf("[Teleport]: Could not initialize session!\n");
    return;
  }

  /* ... start it up. This will trade welcome banners, exchange keys, and setup crypto, compression, and MAC layers */
  rc = libssh2_session_handshake(session, sock);
  if (rc) {
    Serial.printf("[Teleport]: Error when starting up session: %d\n", rc);
    return;
  }

  /* Verify server finger print */
  const char *pubkey_md5 = "1B 89 54 DC F6 52 C9 80 57 91 EB 9C DB A2 F5 4F 6F 6D 14 D9";
  fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);

  DEBUG_TELEPORT("[Teleport]: Fingerprint: ");
  for (i = 0; i < 20; i++) DEBUG_TELEPORT("%02X ", (unsigned char)fingerprint[i]);
  DEBUG_TELEPORT("\n");

  if (fingerprint && strcmp(pubkey_md5, fingerprint)) {
    DEBUG_TELEPORT("[Teleport]: fingerprint matched!\n");
  } else {
    Serial.printf("Teleport server fingerprint match failed!\n");
    return;
  }

  const char* passphrase = NULL;
  const char* user = "libssh2";
  size_t pubkeylen;
  size_t privkeylen;

  rc = libssh2_userauth_publickey_frommemory(session, user, strlen(user), l_pThis->publicKey.c_str(), l_pThis->publicKey.size(), l_pThis->privateKey.c_str(), l_pThis->privateKey.size(), passphrase);

  if (rc != 0) {
    Serial.printf("[Teleport]: Authenticate the session with a public key failed.!\n");
    return;
  }

  //libssh2_trace(session, LIBSSH2_TRACE_SOCKET);

  DEBUG_TELEPORT("[Teleport]: Asking server to listen!\n");
  listener = libssh2_channel_forward_listen_ex(session, l_pThis->teleportServerListenHost, 0, &l_pThis->teleportServerDynaGotPort, 1);

  if (!listener) {
    Serial.printf("[Teleport]: Could not start the tcpip forward listener!\n");
    char *error;
    libssh2_session_last_error(session, &error, NULL, 0);
    DEBUG_TELEPORT("[Teleport]: libssh2_channel_forward_listen_ex error: %s\n", error);
    goto shutdown;
  }

  while (1) {
    Serial.printf("[Teleport]: Waiting for remote connection ..\n");
    channel = libssh2_channel_forward_accept(listener);

    if (!channel) {
      Serial.printf("[Teleport]: Could not accept the remote connection!\n");
      char *error;
      libssh2_session_last_error(session, &error, NULL, 0);
      DEBUG_TELEPORT("[Teleport] libssh2_channel_forward_accept error: %s\n", error);
      goto shutdown;
    }

    l_pThis->forwardTunnel(session, channel);

    libssh2_channel_free(channel);
  }


shutdown:
  if (channel) libssh2_channel_free(channel);
  if (listener) libssh2_channel_forward_cancel(listener);
  libssh2_session_disconnect(session, "Client disconnecting normally");
  libssh2_session_free(session);
  close(sock);
  libssh2_exit();
  vTaskDelete( NULL );
  return;
}

int SinricTeleport::forwardTunnel(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel) {
  int i, rc = 0;
  struct sockaddr_in sin;
  fd_set fds;
  struct timeval tv;
  ssize_t len, wr;
  char buf[16384];
  int forward_socket = -1;

  memset(&sin, 0, sizeof(sin));

  Serial.printf("[Teleport]: Accepted remote connection. Connecting to %s:%d\n", localIP, localPort);
  
  forward_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (forward_socket == -1) {
    Serial.printf("[Teleport]: Error opening socket\n");
    goto shutdown;
  }

  sin.sin_family = AF_INET;
  sin.sin_port = htons(localPort);
  sin.sin_addr.s_addr = inet_addr(localIP.c_str());

  if (sin.sin_addr.s_addr == 0xffffffff) {
    struct hostent *hp;
    hp = gethostbyname(localIP.c_str());
    if (hp == NULL) {
      Serial.printf("[Teleport]: gethostbyname fail %s", localIP.c_str());
      while (1) {
        vTaskDelay(1);
      }
    }

    struct ip4_addr *ip4_addr;
    ip4_addr = (struct ip4_addr *)hp->h_addr;
    sin.sin_addr.s_addr = ip4_addr->addr;
  }

  if (INADDR_NONE == sin.sin_addr.s_addr) {
    Serial.printf("[Teleport]: Invalid local IP or host!\n");
    goto shutdown;
  }

  if (-1 == connect(forward_socket, (struct sockaddr *)&sin, sizeof(struct sockaddr_in))) {
    Serial.printf("[Teleport]: Failed to connect to local IP or host!\n");
    goto shutdown;
  }

  DEBUG_TELEPORT("[Teleport]: Forwarding connection from remote %s:%d to local %s:%d\n", teleportServerListenHost, teleportServerDynaGotPort, localIP, localPort);

  /* Setting session to non-blocking IO */
  libssh2_session_set_blocking(session, 0);

  while (1) {
    FD_ZERO(&fds);
    FD_SET(forward_socket, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    rc = select(forward_socket + 1, &fds, NULL, NULL, &tv);

    if (-1 == rc) {
      Serial.printf("[Teleport]: Forward socket not ready!\n");
      goto shutdown;
    }

    if (rc && FD_ISSET(forward_socket, &fds)) {
      len = recv(forward_socket, buf, sizeof(buf), 0);
      if (len < 0) {
        Serial.printf("[Teleport]: Error reading from the forward socket!\n");
        goto shutdown;
      } else if (0 == len) {
        Serial.printf("[Teleport]: The local server at %s:%d disconnected!\n", localIP, localPort);
        goto shutdown;
      }
      wr = 0;
      do {
        i = libssh2_channel_write(channel, buf, len);
        if (i < 0) {
          char *error;
          libssh2_session_last_error(session, &error, NULL, 0);
          DEBUG_TELEPORT("[Teleport] libssh2_channel_write error: %s\n", error);          
          Serial.printf("[Teleport]: Error writing to the server channel: %d\n", i);
          goto shutdown;
        }
        wr += i;
      } while (i > 0 && wr < len);
    }

    while (1) {
      len = libssh2_channel_read(channel, buf, sizeof(buf));
      if (LIBSSH2_ERROR_EAGAIN == len)
        break;
      else if (len < 0) {
        Serial.printf("[Teleport]: Error reading from the server channel: %d\n", (int)len);
        goto shutdown;
      }

      DEBUG_TELEPORT("[Teleport]: %d bytes read from from the server channel.\n", i);

      wr = 0;
      while (wr < len) {
        i = send(forward_socket, buf + wr, len - wr, 0);
        if (i <= 0) {
          Serial.printf("[Teleport]: Error writing to the forward socket!\n");
          goto shutdown;
        }
        wr += i;
      }

      DEBUG_TELEPORT("[Teleport]: %d bytes written to forward_socket.\n", wr);
      
      if (libssh2_channel_eof(channel)) {
        Serial.printf("[Teleport]: The remote client at %s:%d disconnected!\n", teleportServerListenHost, teleportServerDynaGotPort);
        goto shutdown;
      }
    }
  }

shutdown:
  close(forward_socket);
  /* Setting the session back to blocking IO */
  libssh2_session_set_blocking(session, 1);
  return rc;
}

bool SinricTeleport::isStartingWith(const std::string& str, const std::string& prefix) {
  if (str.size() < prefix.size()) {
    return false;
  }
  return str.compare(0, prefix.size(), prefix) == 0;
}

bool SinricTeleport::isEndingWith(const std::string& str, const std::string& ending) {
  if (str.size() < ending.size()) {
    return false;
  }
  return str.compare(str.size() - ending.size(), ending.size(), ending) == 0;
}

bool SinricTeleport::isValidPublicKey() {
  std::string prefix = "ssh-rsa";
  
  if(!isStartingWith(publicKey, prefix)) { 
    Serial.printf("[Teleport]: Invalid Public Key. Must starts with ssh-rsa... Cannot continue!\n");
    return false;
  }

  return true;
}

bool SinricTeleport::isValidPrivateKey() {
  std::string prefix = "-----BEGIN PRIVATE KEY-----";
  
  if(!isStartingWith(privateKey, prefix)) { 
    Serial.printf("[Teleport]: Invalid Private Key. (invalid begin) Cannot continue!\n");
    return false;
  }

  std::string ending = "-----END PRIVATE KEY-----";
  if(!isEndingWith(privateKey, ending)) {
    Serial.printf("[Teleport]: Invalid Private Key!. (invalid end) Cannot continue!\n");
    return false;
  }

  return true;
}

void SinricTeleport::begin() {
  if(!isValidPublicKey()) return;
  if(!isValidPrivateKey()) return;
    
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[Teleport]: WiFi is disconnected! Cannot continue!\n");
    return;
  }

  DEBUG_TELEPORT("=============================================================================================\r\n");
  DEBUG_TELEPORT("                         !! Welcome to Sinric Teleport. (v%s) !!                           \r\n", SINRIC_TELEPORT_VERSION);
  DEBUG_TELEPORT("   WARNNING ! Unauthorized users are prosecuted and deported naked to the Taliban.           \r\n");
  DEBUG_TELEPORT("=============================================================================================\r\n");
  
  xTaskCreate(teleportTask, "teleportTask", 1024 * 32, this, 5, NULL );
}
