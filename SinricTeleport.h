/*
    Copyright (c) 2023 Sinric. All rights reserved.
    Licensed under Creative Commons Attribution-Share Alike (CC BY-SA)

    This file is part of the Sinric Teleport (https://github.com/sinricpro/teleport-arduino-esp32-library)
*/

#pragma once

#define SINRIC_TELEPORT_VERSION "1.1.1"

#ifdef ENABLE_SINRIC_TELEPORT_LOG
  #ifdef DEBUG_ESP_PORT
    #define DEBUG_TELEPORT(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
  #else
    #define DEBUG_TELEPORT(...) if(Serial) Serial.printf( __VA_ARGS__ )
  #endif
#else
  #define DEBUG_TELEPORT(x...) if (false) do { (void)0; } while (0)
#endif

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
#include "netdb.h" // for gethostbyname
#include <string>

/**
 * @brief Callback definition for onConnected function
 *
 * Gets called when device is connected to Sinric Teleport server
 * @param void
 * @return void
 */
using ConnectedCallbackHandler = std::function<void(void)>;

/**
 * @brief Callback definition for onDisconnected function
 *
 * Gets called when device is disconnected from Sinric Teleport server
 * @param char disconnect reason
 * @return void
 */
using DisconnectedCallbackHandler = std::function<void(const char *)>;

/**
 * @class SinricTeleport
 * @ingroup SinricTeleport
 * @brief The main class of this library, handles secure connection between Server and your module.
 **/
class SinricTeleport {
  public:
    SinricTeleport(const char *publicKey, const char *privateKey,
                   const char *localIP, int localPort) : _publicKey(publicKey), _privateKey(privateKey), _localIP(localIP), _localPort(localPort) {}

    void begin();
    void onConnected(ConnectedCallbackHandler callback);
    void onDisconnected(DisconnectedCallbackHandler callback);

  private:
    const char * _privateKey;
    const char * _publicKey;

    const char *teleportServerIP = "connect.sinric.tel";
    const int   teleportServerPort = 8443;
    const char *teleportServerHostKey = "1B 89 54 DC F6 52 C9 80 57 91 EB 9C DB A2 F5 4F 6F 6D 14 D9";
    const char *teleportServerListenHost = "localhost"; /* determined by server */
    int teleportServerDynaGotPort; /* determined by server */

    const char * _localIP = "127.0.0.1";
    int _localPort = 80;

    static void teleportTask(void * parameter);

    bool isValidPublicKey();
    bool isValidPrivateKey();
    
    bool isStartingWith(const std::string& str, const std::string& prefix);
    bool isEndingWith(const std::string& str, const std::string& ending);

    int waitSocket(int socket_fd, LIBSSH2_SESSION *session);    
    int forwardTunnel(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel);
    void end(int sock, LIBSSH2_SESSION *session, LIBSSH2_LISTENER *listener, LIBSSH2_CHANNEL *channel, const char * reason);
    
    DisconnectedCallbackHandler _disconnectedCallback;    
    ConnectedCallbackHandler _connectedCallback;
};

/**
 * @brief Called when connected to the server
 *
 * @param cb Function pointer to a `ConnectedCallbackHandler` function
 * @return void
 **/
void SinricTeleport::onConnected(ConnectedCallbackHandler callback) {
    _connectedCallback = callback;
}

/**
 * @brief Called when disconnected from the server
 *
 * @param cb Function pointer to a `DisconnectedCallbackHandler` function
 * @return void
 **/
void SinricTeleport::onDisconnected(DisconnectedCallbackHandler callback) {
    _disconnectedCallback = callback;
}

/**
 * @brief Initialize libssh2, connect to server and start reverse port forwarding.
 **/
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

  /* Initialize libssh2 */
  rc = libssh2_init(0); // 0 will initialize the crypto library

  if (rc != 0) {
    DEBUG_TELEPORT("[Teleport]: libssh2 initialization failed (%d)\n", rc);
    
    const char * reason = "libssh2 initialization failed!";
    l_pThis->end(sock, session, listener, channel, reason);
    return;    
  }

  DEBUG_TELEPORT("[Teleport]: Connecting to Teleport server ..\n");

  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    char buffer[50];
    sprintf(buffer, "%s: (%d)", "Error opening socket!\n" , sock);    
    const char * reason = const_cast<const char *>(buffer);
    l_pThis->end(sock, session, listener, channel, reason);
    return;
  }

  sin.sin_family = AF_INET;
    
  struct hostent *hp;
  hp = gethostbyname(l_pThis->teleportServerIP);
  if (hp == NULL) {
    const char * reason = "Get hostbyname failed. Check the DNS!\n";
    l_pThis->end(sock, session, listener, channel, reason);
    return;    
  }

  struct ip4_addr *ip4_addr;
  ip4_addr = (struct ip4_addr *)hp->h_addr;
  sin.sin_addr.s_addr = ip4_addr->addr;
  sin.sin_port = htons(l_pThis->teleportServerPort);

  if (connect(sock, (struct sockaddr*)(&sin),
              sizeof(struct sockaddr_in)) != 0) {
    const char * reason = "Failed to connect to Teleport server!\n";
    l_pThis->end(sock, session, listener, channel, reason);
    return;
  }

  /* Create a session instance of ssh2*/
  session = libssh2_session_init();
  if (!session) {
    const char * reason = "Could not initialize session!\n";
    l_pThis->end(sock, session, listener, channel, reason);
    return;
  }

  /* ... start it up. This will trade welcome banners, exchange keys, and setup crypto, compression, and MAC layers */
  rc = libssh2_session_handshake(session, sock);
  if (rc) {
    char buffer[100];
    sprintf(buffer, "%s: (%d)", "Error starting up the session!\n" , rc);    
    const char * reason = const_cast<const char *>(buffer);
    l_pThis->end(sock, session, listener, channel, reason);
    return;
  }

  /* Verify server finger print */
  fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);

  #ifdef ENABLE_SINRIC_TELEPORT_LOG
    DEBUG_TELEPORT("[Teleport]: Fingerprint: ");
    for (i = 0; i < 20; i++) DEBUG_TELEPORT("%02X ", (unsigned char)fingerprint[i]);
    DEBUG_TELEPORT("\n");
  #endif

  if (fingerprint && strcmp(l_pThis->teleportServerHostKey, fingerprint)) {
    DEBUG_TELEPORT("[Teleport]: Fingerprint matched!\n");
  } else {
    const char * reason = "Teleport server fingerprint match failed!\n";
    l_pThis->end(sock, session, listener, channel, reason);
    return;
  }

  /* Authenticate with public key */
  const char* passphrase = NULL;
  size_t pubkeylen;
  size_t privkeylen;

  char buffer[32];
  sprintf(buffer, "%s:%d", l_pThis->_localIP, l_pThis->_localPort);
  const char *user = const_cast<const char *>(buffer);

  rc = libssh2_userauth_publickey_frommemory(session, user, strlen(user), l_pThis->_publicKey, strlen(l_pThis->_publicKey), l_pThis->_privateKey, strlen(l_pThis->_privateKey), passphrase);

  if (rc != 0) {
    char buffer[100];
    sprintf(buffer, "%s: (%d)", "Authenticate the session with a public key failed.!\n" , rc);    
    const char * reason = const_cast<const char *>(buffer);
    l_pThis->end(sock, session, listener, channel, reason);
    return;
  }

  //libssh2_trace(session, LIBSSH2_TRACE_SOCKET);

  DEBUG_TELEPORT("[Teleport]: Asking server to listen!\n");
  listener = libssh2_channel_forward_listen_ex(session, l_pThis->teleportServerListenHost, 0, &l_pThis->teleportServerDynaGotPort, 1);

  if (!listener) {
    char *error;
    libssh2_session_last_error(session, &error, NULL, 0);
    DEBUG_TELEPORT("[Teleport]: libssh2_channel_forward_listen_ex error: %s\n", error);
    
    const char * reason = "Port mapping failed.!\n";
    l_pThis->end(sock, session, listener, channel, reason);
    return;
  }

  if(l_pThis->_connectedCallback != nullptr) {
    l_pThis->_connectedCallback();
  }

  while (1) {
    DEBUG_TELEPORT("[Teleport]: Waiting for remote connection ..\n");
    channel = libssh2_channel_forward_accept(listener);

    if (!channel) {
      char *error;
      libssh2_session_last_error(session, &error, NULL, 0);
      DEBUG_TELEPORT("[Teleport] libssh2_channel_forward_accept error: %s\n", error);
      
      const char * reason = "Error accpeting the remote connection.!\n";
      l_pThis->end(sock, session, listener, channel, reason);
      return;
    }

    l_pThis->forwardTunnel(session, channel);

    libssh2_channel_free(channel);
  }
}

/**
 * @brief Free resources and invoke disconnected handler.
 **/
void SinricTeleport::end(int sock, LIBSSH2_SESSION *session, LIBSSH2_LISTENER *listener, LIBSSH2_CHANNEL *channel, const char * reason) {
  if (channel)  libssh2_channel_free(channel);
  if (listener) libssh2_channel_forward_cancel(listener);
  if (session)  libssh2_session_disconnect(session, "Client disconnecting normally");
  if (session)  libssh2_session_free(session);
  if (sock)     close(sock);

  libssh2_exit();

  if(_disconnectedCallback != nullptr) {
    _disconnectedCallback(reason);
  }  
}

/**
 * @brief Wait until socket is read/write able
 **/
int SinricTeleport::waitSocket(int socket_fd, LIBSSH2_SESSION *session) {
    struct timeval timeout;
    int rc;
    fd_set fd;
    fd_set *writefd = NULL;
    fd_set *readfd = NULL;
    int dir;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    FD_ZERO(&fd);
    FD_SET(socket_fd, &fd);

    /* now make sure we wait in the correct direction */
    dir = libssh2_session_block_directions(session);
    if(dir & LIBSSH2_SESSION_BLOCK_INBOUND)
        readfd = &fd;
    if(dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
        writefd = &fd;
    rc = select(socket_fd + 1, readfd, writefd, NULL, &timeout);
    return rc;
}

/**
 * @brief Start reading/writing to remote connection.
 **/
int SinricTeleport::forwardTunnel(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel) {
  int i, rc = 0;
  struct sockaddr_in sin;
  fd_set fds;
  struct timeval tv;
  ssize_t len, wr;
  char buf[16384];
  int forward_socket = -1;

  memset(buf, 0, sizeof(buf));
  memset(&sin, 0, sizeof(sin));

  DEBUG_TELEPORT("[Teleport]: Accepted remote connection. Connecting to %s:%d\n", _localIP, _localPort);
  
  forward_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (forward_socket == -1) {
    DEBUG_TELEPORT("[Teleport]: Error opening socket\n");
    goto shutdown;
  }

  sin.sin_family = AF_INET;
  sin.sin_port = htons(_localPort);
  sin.sin_addr.s_addr = inet_addr(_localIP);

  if (sin.sin_addr.s_addr == 0xffffffff) { // local host is a name.
    struct hostent *hp;
    hp = gethostbyname(_localIP);
    if (hp == NULL) {
      DEBUG_TELEPORT("[Teleport]: gethostbyname fail %s", _localIP);
      while (1) {
        vTaskDelay(1);
      }
    }

    struct ip4_addr *ip4_addr;
    ip4_addr = (struct ip4_addr *)hp->h_addr;
    sin.sin_addr.s_addr = ip4_addr->addr;
  }

  if (INADDR_NONE == sin.sin_addr.s_addr) {
    DEBUG_TELEPORT("[Teleport]: Invalid local IP or host!\n");
    goto shutdown;
  }

  if (-1 == connect(forward_socket, (struct sockaddr *)&sin, sizeof(struct sockaddr_in))) {
    DEBUG_TELEPORT("[Teleport]: Failed to connect to local IP or host!\n");
    goto shutdown;
  }

  DEBUG_TELEPORT("[Teleport]: Forwarding connection to local %s:%d\n", _localIP, _localPort);

  /* Setting session to non-blocking IO */
  libssh2_session_set_blocking(session, 0);

  while (1) {
    FD_ZERO(&fds);
    FD_SET(forward_socket, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    rc = select(forward_socket + 1, &fds, NULL, NULL, &tv);
    memset(buf, 0, sizeof(buf));

    if (-1 == rc) {
      DEBUG_TELEPORT("[Teleport]: Forward socket not ready!\n");
      goto shutdown;
    }

    if (rc && FD_ISSET(forward_socket, &fds)) {
      len = recv(forward_socket, buf, sizeof(buf), 0);

      if (len < 0) {
        DEBUG_TELEPORT("[Teleport]: Error reading from the forward socket!\n");
        goto shutdown;
      } else if (0 == len) {
        DEBUG_TELEPORT("[Teleport]: The local server at %s:%d disconnected!\n", _localIP, _localPort);
        goto shutdown;
      }

      //DEBUG_TELEPORT("data: %.*s\n", len, buf);

      wr = 0;
      while (wr < len) {
          rc = libssh2_channel_write(channel, buf + wr, len - wr);
          if (rc < 0) {
              if (rc == LIBSSH2_ERROR_EAGAIN) {
                  DEBUG_TELEPORT("[Teleport]: Error writing.. so wait for the socket to become writableand try again.\n");
                  waitSocket(forward_socket, session);
              } else {
                  DEBUG_TELEPORT("[Teleport]: error writing to server channel: %d\n", rc);
                  goto shutdown;
              }
          } else {
              wr += rc;
          }
      }
    }

    while (1) {
      memset(buf, 0, sizeof(buf));
      len = libssh2_channel_read(channel, buf, sizeof(buf));

      if (LIBSSH2_ERROR_EAGAIN == len) {
        break;
      }
      else if (len < 0) {
        DEBUG_TELEPORT("[Teleport]: Error reading from the teleport server channel: %d\n", (int)len);
        goto shutdown;
      }

      wr = 0;
      while (wr < len) {
        i = send(forward_socket, buf + wr, len - wr, 0);
        if (i <= 0) {
          DEBUG_TELEPORT("[Teleport]: Error writing to the forward socket!\n");
          goto shutdown;
        }
        wr += i;
      }

      if (libssh2_channel_eof(channel)) {
        DEBUG_TELEPORT("[Teleport]: The remote client at %s:%d disconnected!\n", teleportServerListenHost, teleportServerDynaGotPort);
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

/**
 * @brief Validate public key
 **/
bool SinricTeleport::isValidPublicKey() {
  std::string prefix = "ssh-rsa";
  
  if(!isStartingWith(_publicKey, prefix)) { 
    Serial.printf("[Teleport]: Invalid Public Key. Must starts with ssh-rsa... Cannot continue!\n");
    return false;
  }

  return true;
}

/**
 * @brief Validate private key
 **/
bool SinricTeleport::isValidPrivateKey() {
  std::string prefix = "-----BEGIN PRIVATE KEY-----";
  
  if(!isStartingWith(_privateKey, prefix)) { 
    Serial.printf("[Teleport]: Invalid Private Key. (invalid begin) Cannot continue!\n");
    return false;
  }

  std::string ending = "-----END PRIVATE KEY-----";
  if(!isEndingWith(_privateKey, ending)) {
    Serial.printf("[Teleport]: Invalid Private Key!. (invalid end) Cannot continue!\n");
    return false;
  }

  return true;
}

/**
 * @brief Initialize libssh2 and to connect to the server
 **/
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
