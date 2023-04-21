// libssh2_version is 1.10.1_DEV

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

#include <WebServer.h>

#define BAUD_RATE 115200

const char *ssid = "June-2G";
const char *wifi_password = "wifipassword"; 

const char *username = "foo";
const char *password = "bar";

const char *server_ip = "5.161.193.42"; // Server "192.168.10.157"
int server_ssh_port = 8443;

const char *remote_listenhost = "localhost"; /* dermined by server */
int remote_wantport = 0; // dermined by server
int remote_listenport; // dermined by server

const char *local_destip = "127.0.0.1";
int local_destport = 80;

WebServer server(80);

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

int forward_tunnel(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel) {
    int i, rc = 0;
    struct sockaddr_in sin;
    fd_set fds;
    struct timeval tv;
    ssize_t len, wr;
    char buf[16384];
    int forward_socket = -1;

    memset(&sin, 0, sizeof(sin));

    Serial.printf("Accepted remote connection. Connecting to local server %s:%d\n", local_destip, local_destport);
    forward_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    // unsigned long mode = 1;
    // ioctlsocket(forward_socket, FIONBIO, &mode);    
    if (forward_socket == -1) {
        Serial.printf("Error opening socket\n");
        goto shutdown;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(local_destport);
    sin.sin_addr.s_addr = inet_addr(local_destip);

    if (sin.sin_addr.s_addr == 0xffffffff) { 
      struct hostent *hp;
      hp = gethostbyname(local_destip);
      if (hp == NULL) {
        Serial.printf("gethostbyname fail %s", local_destip);
        while(1) { vTaskDelay(1); }
      }
      
      struct ip4_addr *ip4_addr;
      ip4_addr = (struct ip4_addr *)hp->h_addr;
      sin.sin_addr.s_addr = ip4_addr->addr;      
    }
      
    if (INADDR_NONE == sin.sin_addr.s_addr) {
        Serial.printf("Invalid local IP address\n");
        goto shutdown;
    }

    if (-1 == connect(forward_socket, (struct sockaddr *)&sin, sizeof(struct sockaddr_in))) {
        Serial.printf("Failed to connect!\n");
        goto shutdown;
    }

    Serial.printf("Forwarding connection from remote %s:%d to local %s:%d\n", remote_listenhost, remote_listenport, local_destip, local_destport);

    /* Setting session to non-blocking IO */
    libssh2_session_set_blocking(session, 0);

    while (1) {
        FD_ZERO(&fds);
        FD_SET(forward_socket, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        rc = select(forward_socket + 1, &fds, NULL, NULL, &tv);
        
        if (-1 == rc) {
            Serial.printf("Socket not ready!\n");
            goto shutdown;
        }
        
        if (rc && FD_ISSET(forward_socket, &fds)) {
            len = recv(forward_socket, buf, sizeof(buf), 0);
            if (len < 0) {
                Serial.printf("Error reading from the forward socket!\n");
                goto shutdown;
            } else if (0 == len) {
                Serial.printf("The local server at %s:%d disconnected!\n",
                    local_destip, local_destport);
                goto shutdown;
            }
            wr = 0;
            do {
                i = libssh2_channel_write(channel, buf, len);
                if (i < 0) {
                    Serial.printf("Error writing on the SSH channel: %d\n", i);
                    goto shutdown;
                }
                wr += i;
            } while(i > 0 && wr < len);
        }
        
        while (1) {
            len = libssh2_channel_read(channel, buf, sizeof(buf));
            if (LIBSSH2_ERROR_EAGAIN == len)
                break;                
            else if (len < 0) {
                Serial.printf("Error reading from the SSH channel: %d\n", (int)len);
                goto shutdown;
            }

            wr = 0;
            while (wr < len) {
                i = send(forward_socket, buf + wr, len - wr, 0);
                if (i <= 0) {
                    Serial.printf("Error writing on the forward_socket!\n");
                    goto shutdown;
                }
                wr += i;
            }
            if (libssh2_channel_eof(channel)) {
                Serial.printf("The remote client at %s:%d disconnected!\n",
                    remote_listenhost, remote_listenport);
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

// static int ssh_set_error(LIBSSH2_SESSION *session) {
// 	char *error;
// 	libssh2_session_last_error(session, &error, NULL, 0);
// 	Serial.printf("SSH error: %s", error);
// 	return -1;
// }

void ssh_task(void * parameter){
  doit();
}
 

int doit() {
  int rc, i;
  struct sockaddr_in sin;
  const char *fingerprint;
  char *userauthlist;
  int sock = -1;
    
  LIBSSH2_SESSION *session;
  LIBSSH2_LISTENER *listener = NULL;
  LIBSSH2_CHANNEL *channel = NULL;

  Serial.printf("libssh2_version is %s\n", LIBSSH2_VERSION);

  /* Initialize libssh2 */
  rc = libssh2_init(0); // 0 will initialize the crypto library
  
  if (rc != 0) {
      Serial.printf("libssh2 initialization failed (%d)\n", rc);
      return 1;
  }

  Serial.printf("Connecting to SSH server ..\n", rc);
  
  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
      Serial.printf("Error opening socket\n");
      return -1;
  }
  
  sin.sin_family = AF_INET;
  if (INADDR_NONE == (sin.sin_addr.s_addr = inet_addr(server_ip))) {
      Serial.printf("Invalid remote IP address\n");
      return -1;
  }
  
  sin.sin_port = htons(server_ssh_port); /* SSH port */
  if (connect(sock, (struct sockaddr*)(&sin),
              sizeof(struct sockaddr_in)) != 0) {
      Serial.printf("Failed to connect!\n");
      return -1;
  }

  /* Create a session instance */
  session = libssh2_session_init();
  if(!session) {
      Serial.printf("Could not initialize the SSH session!\n");
      return -1;
  }

  /* ... start it up. This will trade welcome banners, exchange keys,
  * and setup crypto, compression, and MAC layers
  */
  rc = libssh2_session_handshake(session, sock);
  if(rc) {
      Serial.printf("Error when starting up SSH session: %d\n", rc);
      return -1;
  }

  // /* Print banner */
  // const char *banner = libssh2_session_banner_get(session);  
  Serial.printf("=============================================================================================\r\n");
  Serial.printf("                         !! Welcome to Sinric Teleport. !!                                   \r\n");
  Serial.printf("WARNNING ! Unauthorized users are prosecuted and deported naked to the Taliban.\r\n");
  Serial.printf("=============================================================================================\r\n");
  
  
  const char *pubkey_md5 = "1B 89 54 DC F6 52 C9 80 57 91 EB 9C DB A2 F5 4F 6F 6D 14 D9";  
  fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);

  Serial.printf("Fingerprint: ");
  for(i = 0; i < 20; i++) Serial.printf("%02X ", (unsigned char)fingerprint[i]);
  Serial.printf("\n");

  if(fingerprint && strcmp(pubkey_md5, fingerprint)) { 
     Serial.printf("fingerprint matched!\n");
  } else {
    Serial.printf("fingerprint match failed!\n");    
  }

  const char* passphrase = NULL;
  const char* user = "libssh2";
  size_t pubkeylen; 
  size_t privkeylen;

// ssh-keygen -p -f keyfile.pub -m pem
// chmod 400 keyfile
// ssh-keygen -p -f keyfile -m pem

// const std::string privkey =
// "-----BEGIN RSA PRIVATE KEY-----\n"
// "MIIJKQIBAAKCAgEA2rvDWK4lj1nzVSBqvc2SsBD6Ibpwpi/lipvr3aPwACKqiHW/\n"
// "oFWaYTN8m/xiAvW2FTId2tl3/u1ahIeXjoYAqzUKYNrTvyewU8OAbIkP+jVIGP/6\n"
// "mR7i7e07LQxkIXQjJw1D3CbmfALSMa+3foqMNKRc4EMj6VYwps3S1FYQU44ZtX7m\n"
// "FTxVJzEQpRQu9YAiLKbLbHvK9CkUcByNe/P9QgFUk7kNpb1rQPl8Tvf9odmLxtR6\n"
// "sbbYiS1wTLy0xv4l8OSNGmJFJlYCqWyiUsSuNO0AcmVneFMx77xstBGF3d7Jbp1c\n"
// "BQ5gdi/EhuqzkLMMWG7CYCoLKVvvwpq5AQjJhZ8CPoNGB8becWCXGUB8ZMiYAc81\n"
// "p98poo/SIx5Lz9P4i/El9SV0ltEoBCViY8Zkc+gqAeAIulV+S82Z4Ojo1LJxoIxU\n"
// "C9E04qg9IxKCuKTvYZnfVThhWo33Iu1joP3k/4gaQHAdsz2+GTreM+/5BmDA4Zbu\n"
// "leSSSzcfQBJKf0EQ3OXeTa1yLgG2RNb4E2HkqW587ApJP8SdO5btp2+12xeWR02A\n"
// "INRC/fSbnG5N+PQVL27aH8LRUXkb86EUhskDADPHwjcmI4zgtxewdJTjckEyRcEm\n"
// "OELmc2YUl2q95Xa7CQoQhLUOjGgD9EtUI8YBweD9nYdb5H0jSpH+OsR7M8ECAwEA\n"
// "AQKCAgBZlHw0XV3Uj6owOs58XSyuHsXR+mEYLpV7Zs/6PaGU7J2atV59c5F+LW/j\n"
// "EkGxpJ+lnpjLgDS8mshvbniLTiYH9/kAIZ6GsuJr6600xg1dE2Url+oxu4yElJuf\n"
// "n2uCp+WdoLkh+Gx8bUtYPfaQRH2XMZk330dd0IasSa8GdxjIn3G3+viPyd2150pB\n"
// "0TpKIOfeEZFOJITB6fEM2SS4lvwphBH3Tdpg/mpmecaHMNkW59lu3KZDEfcsdwd+\n"
// "5enDdWJkfbWsEILxaFg+utbfvtz5n/aG4zC0/p186VY7iNGiRBOK0bdV+sVWocmC\n"
// "16winrF3piTE7XMUxSo0MNmDqu2codAxqg4xcV1cLvGvryiORxSMO2LY1+ky2vd4\n"
// "avtLBrjSKuJ7nlijpx97fNgWBSOr7de1+s9KyjID+i+tKgC7JIfEd6wFFXZYIVKE\n"
// "2Q5QfFb+GVhcDbfoGpMT3NS1Q4q3CBVTgYgrTUqffiSWSVi3XybGEAfLoyK95I3m\n"
// "+Bkvl8FULE8gdKdRgNYJQIk0mZMjPno+kBqw/qFcxZigfn5qJN7eToojtS4o0baj\n"
// "RZ4QgAxWLyf7pGydHegk3Watnn7M7Q7yXoZK05As/s0hTNgZElOM4T67aDFS3N+H\n"
// "38ujbKUJW6aSIYRqppAk/Z29KJKTrnRj8dV3p7UqVe77lxqa4QKCAQEA8/NttIwX\n"
// "/hdHW+DVvshH+0YdX7RKoQlE3xVvticLKuS6N749x6Ts3aGA4ejsL9WDZH4G5sOZ\n"
// "0slbyOPa0kxHF5HelhisKxnT3xIvFX3UVpi7ozAESIq+eoen9eUJSs43ezS4JM6t\n"
// "/Wv8+wEsCYoEpAFScNiiK83HPRSPw1Nr5vBPGc1WVdjHXsBZAmHj+uuUfw2Lejk7\n"
// "q2FIn7vKu3I5zxEFitTsHTbDQ4/Mf93ahiU41B6YbfGR2VeMuXCqeJzgXmh/UxVn\n"
// "yxafO81xrcrdi54KxHWjyKDKIphfDQweFChFG01CjlG1svd1vB+Qgfau0dM7sCak\n"
// "ZU7ndEvEvlULgwKCAQEA5Yl6txLMJ48ao+cI+TGBJTH3NP0qUlPYRW5093vGF0zP\n"
// "Us7kwmpjrXLNKTEahg+AaMcvN8n+xR8rkl9sxwvobtGhxFMPDsMaKIXFtsoL9XMC\n"
// "l+4QGMabSgnvQUZpr5RG6tLNveHcEkC+Y2HVe54UW1Ni+usIwPKX5hcXuC/ZRpWk\n"
// "onL4rfo/kUMygGZeryi50BkbBwIxysVqnsiBJdCiPnBRu5F4U66fkRR2nXd2Rfv1\n"
// "u1TzB+F9I6WMeEdZ7/wa/VGIMvUZNjNjLkZbs6ddmDv0SKd0l8Gzz4585LgU1Lg/\n"
// "648C5Po5Q3tUiYhoK41cdDD/bOctWWe1896z7MHMawKCAQEAjQv+Lhmh9aN3+sNw\n"
// "UjJyi/HqId+YFqvJSkKWqHbCmjZNBNXV2oyc9zfd0MBfbvjAU2Yaj2ogkiMiEnDL\n"
// "oDPCFvqb+6SgRvtT6PIWjxKFptwAAUZN87NklmvAzQdz6/B3W6ELpxxotNGvH2F6\n"
// "GLnYHQs2o1Bd033S6Jnu1TxycsAWvBBhDpmcDEiiLiJzMizrWtp1/mEBAwHof5KQ\n"
// "kPhmPDbXQYICUpHLLFEEKBoZst3qkZpu/4JglY1tK/rcVYg+oddBZsRFksKtpmIO\n"
// "jDFzDrp990EdRW7R8Faw/lY3PeharIJrLOZJbARv6ilF4B7EzUSYiiyNeJW6dR8p\n"
// "zfJWCQKCAQEAuD9Yr6d5FK/8FGCZhV3FapPm+TPWSuteiK0XWqiR45YWPUQxwBUi\n"
// "GdIy+MOfpMjArFpmfoO364cPtJjAei4Gzx2amjqJtbHKR4jJoeYhH07IYCgJ57lU\n"
// "YWQEFwNsRjHhkxDPcYHg4w3xRVj8whOsB3qx0vPivO5+G4Wh1okiAVSRKIzdLNnZ\n"
// "OMMVwJdrnXI1ZOMiHMgYK8m3wej0MeR8t131XXhxe8qJ7yzb4Z5I4/hR0aNoyYSo\n"
// "rHpwfQPZH5fgGkLd7vlq2WT6UeTMKzNHH7HQbplsL3ye1xZrDjTtE6sqM+1Bx4dL\n"
// "e+19eEB0TbFU6zeBcGtIraFgHnc/OeZ+LQKCAQBFcN/dh4MntKtvZP+I2QIzZPkr\n"
// "YmQ6zR+XM3eKYFH8tZFHkM3z/yS6FMWhk9FLXBmj7mZ6CaCh0tsFxbsDV/eg/0xg\n"
// "Tw9VGkVnYOe8XZ+g9wIVTVqi4o1RTiPe/OvW/eqTu9O5IsqYhdFGM2MIBQZ5tiRz\n"
// "+mtY/3843Z5JTNVNnK/aPJGYGGEpV9R4a2onmOOa9n8d63B5//l8KxxBwjyiFmBg\n"
// "lh0aRheKbRfpYgbaf+EecP97FM0XBM5rbNRHx7qF0RaF6uqQnpzGQHgZb/TUfIyW\n"
// "ihc/opiJ0XVbDFoU0hn+7JkzCMwdptnaR8ryLsBGs4izU0l+3Ei/wVtJ7yDE\n"
// "-----END RSA PRIVATE KEY-----"; 
 
//const std::string pubkey = "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQDau8NYriWPWfNVIGq9zZKwEPohunCmL+WKm+vdo/AAIqqIdb+gVZphM3yb/GIC9bYVMh3a2Xf+7VqEh5eOhgCrNQpg2tO/J7BTw4BsiQ/6NUgY//qZHuLt7TstDGQhdCMnDUPcJuZ8AtIxr7d+iow0pFzgQyPpVjCmzdLUVhBTjhm1fuYVPFUnMRClFC71gCIspstse8r0KRRwHI178/1CAVSTuQ2lvWtA+XxO9/2h2YvG1HqxttiJLXBMvLTG/iXw5I0aYkUmVgKpbKJSxK407QByZWd4UzHvvGy0EYXd3slunVwFDmB2L8SG6rOQswxYbsJgKgspW+/CmrkBCMmFnwI+g0YHxt5xYJcZQHxkyJgBzzWn3ymij9IjHkvP0/iL8SX1JXSW0SgEJWJjxmRz6CoB4Ai6VX5LzZng6OjUsnGgjFQL0TTiqD0jEoK4pO9hmd9VOGFajfci7WOg/eT/iBpAcB2zPb4ZOt4z7/kGYMDhlu6V5JJLNx9AEkp/QRDc5d5NrXIuAbZE1vgTYeSpbnzsCkk/xJ07lu2nb7XbF5ZHTYAg1EL99Jucbk349BUvbtofwtFReRvzoRSGyQMAM8fCNyYjjOC3F7B0lONyQTJFwSY4QuZzZhSXar3ldrsJChCEtQ6MaAP0S1QjxgHB4P2dh1vkfSNKkf46xHszwQ== root@vmi295078.contaboserver.net";

const std::string privkey = 
  "-----BEGIN PRIVATE KEY-----\n"
  "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDOv3mTEaI/vixiLqTd7vZqAun3HsqBERzxNdiQN8f9WDS1OLiKYOyuuCfG47DRTuIqaR6oTBFI/9eA6mi3TqDBmf2SEigPgsHyyZN4Mpt7qAWXXIxD2KYCTRVP2kWuQQD5wTv6R7N9Ix392P0XTbHRsSXllqZB/TTczyQeqDAx8t4ur8ZMG2y37TSiV+pI/1krsQMN3JEXkiOawHBtTPgRWhqE3LmTK9GX6SfNOq0s7NPfYYrzR7lN2P45zFauXiPxfp9Hd53bYyaxCcn4fGCyX4wFtxaTd+J4HGOssgyv8q7HyWaA+OrJnYtI9C68AlT6ywEgAbr8xDr8JCe5w4G9AgMBAAECggEAKY12vg4Efcq3SWlmBdvuBxr8UohL+3pIxpsDomKvrXWxrD2Q63D1CN2m0vS2uC6iSpNTj9AwBiuzcKvMLZFeQmL0gYvoJMvrNqR4YOdM9CKuMwBtCYLMxLgg4Sp1qELU0x2Y9CG+i5dIAxm+LlWVeFdiDj2Dv04IdpukPwwfGZ6BuMOtEqmXtPpAQ04QV+H8n45BBiVLm7krfheoby2PtRNWvUYStpHmbwAeDbb4YXZlQietAoEqYiiA0kBjKHGrahQf4jx4371AlleibIXMPKqYxLXvxOAcFp4RVWkOpfRQ4lpvj2fRX1jPMpIaamRsHbt7CBLGV/fJefwDfeJWOQKBgQDo9Ude5VWg60UTNmz+LUrcPAwWVXyT61Zne/7OI+JI8ZrKeSnR7D9DrdfWyCY9x3STyv9Zi/Ha0kDWhOM92Q5GmHLcip05fT/5Uy9FLzYvBSXgzrvdqx/6TAt6LYu/BSCKRLfHDQqdSp4omLY2LE1Xn0qSeb1ru9nDgxgwUAy9aQKBgQDjMoeWJuPCLW/TdG8NSSTNDuscFijnWebEqW0OCSS84bfI9jE0+7bW3hbpsa+0Klslav6T6zs0qJSBqLrpA/Dwe252/E2eS8wq7jOsjZpQ0qMVY9WmIRU6qq0CcU8MgSqWUD+YLWWWd+yk2C+bvMHA+Rt/lOZsSRm+pTWWjtWTNQKBgBBE+QwOljFb3QIfffMudJj29J0msUGfYPRvO3doGCih/v5/AcWwayat4HIWnl0YLfMYbUbyuBxhLLgOpdQu8YlKolL2t05Jigs+nQGG75DPGjseFQ7BIcWYRADvZ2Aa2o/thqw3I/OiP3N0Xt1fsLMa49lg+TKp9uZppGnTXWBpAoGASWTCXiQUAV9SN5nuYflV9RQzqTATaKEnJjKhMx6LCqVUDIxTWw1RhFncRwQKgYYJSa4lrT0ZNCqdRsFuF+YZCGanSbK5lEBiJSAr+zsHNcLFwhwtIWygggIuv5JA+gYj7sjfslY/8fqtrJbV0laItMEEPBOq2CJJOcf+5rMflV0CgYEA4CG2li33RNO3VLYM+BJCxuj0wOAboJeyZbWGOInw/Fy/6i47x7oXNOW+jRQCb1mtzxJ6cLo+kE9uRCWYGcIxv+xqT6vrC3/0CJJ74aiMxaWfIbiFz9T3vBVbXqx+CNtlm8yf72mwhyt244e4PtAtmEyXgGn5NbSQJlEWUbh295o=\n"
  "-----END PRIVATE KEY-----";

const std::string pubkey = "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDOv3mTEaI/vixiLqTd7vZqAun3HsqBERzxNdiQN8f9WDS1OLiKYOyuuCfG47DRTuIqaR6oTBFI/9eA6mi3TqDBmf2SEigPgsHyyZN4Mpt7qAWXXIxD2KYCTRVP2kWuQQD5wTv6R7N9Ix392P0XTbHRsSXllqZB/TTczyQeqDAx8t4ur8ZMG2y37TSiV+pI/1krsQMN3JEXkiOawHBtTPgRWhqE3LmTK9GX6SfNOq0s7NPfYYrzR7lN2P45zFauXiPxfp9Hd53bYyaxCcn4fGCyX4wFtxaTd+J4HGOssgyv8q7HyWaA+OrJnYtI9C68AlT6ywEgAbr8xDr8JCe5w4G9";
 
  //rc = libssh2_userauth_publickey_frommemory(session, user, strlen(user), pubkey, strlen(pubkey), privkey, strlen(privkey), passphrase);

  rc = libssh2_userauth_publickey_frommemory(session, user, strlen(user), pubkey.c_str(), pubkey.size(), privkey.c_str(), privkey.size(), passphrase);
  
  if (rc != 0) { 
      char * error = NULL;
      int len = 0;
      int errbuf = 0;
      libssh2_session_last_error(session, &error, &len, errbuf);        
      Serial.printf("error: "); 
      Serial.printf(error);
      Serial.printf("\n");
      return -1;
  }

  //libssh2_trace(session, LIBSSH2_TRACE_SOCKET);
  //Serial.printf("Asking server to listen on remote %s:%d\n", remote_listenhost, remote_wantport);
  
  listener = libssh2_channel_forward_listen_ex(session, remote_listenhost, remote_wantport, &remote_listenport, 1);
        
  if (!listener) {
      Serial.printf("Could not start the tcpip-forward listener!\n");
      goto shutdown;
  }

  //Serial.printf("Server is listening on %s:%d\n", remote_listenhost, remote_listenport);

  while (1) {
      Serial.printf("Waiting for remote connection\n");
      
      // int err = LIBSSH2_ERROR_EAGAIN;
      // while (err == LIBSSH2_ERROR_EAGAIN) {
      //   channel = libssh2_channel_forward_accept(listener);
      //   if (channel) break;
      //   err = libssh2_session_last_errno(session);
      // }
            
      channel = libssh2_channel_forward_accept(listener);
      
      if (!channel) {
          Serial.printf("Could not accept the connection. error: %d\n", libssh2_session_last_errno(session));
          goto shutdown;
      }

      forward_tunnel(session, channel);

      libssh2_channel_free(channel);
  } 
    

  shutdown:
    if (channel)
        libssh2_channel_free(channel);
    if (listener)
        libssh2_channel_forward_cancel(listener);
    libssh2_session_disconnect(session, "Client disconnecting normally");
    libssh2_session_free(session);
    close(sock);
    libssh2_exit();
    return 0; 
}

void setup() {
  Serial.begin(BAUD_RATE);
  while (!Serial) {};

  Serial.println(ESP.getFreeHeap()); // 333624

  setup_wifi();

  setup_webserver();

  Serial.println(ESP.getFreeHeap()); // 333624

  // put your setup code here, to run once:
  xTaskCreate(ssh_task, "rewriteTask", 1024 * 24, NULL, 5, NULL );

  Serial.println(ESP.getFreeHeap()); // 279520
}

void loop(){
   server.handleClient();
}
