#include <sys/cdefs.h>
#include <stdio.h>
#include <sys/socket.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "cJSON.h"
#include "gdltask.h"
#include "traffic.h"
#include "main.h"
#include "ownship.h"
#include "vws/websocket.h"

#define MAX_AGE_MS (10*1000)      // consider traffic disconnected if nothing received for this many ms

static const char *TAG = "GDL90";
static int listenPort = 4000;
static int listeningSocket;
static uint8_t rxBuffer[1600];
static gdl90Status_t gdl90Status;
//static char gdl90StatusText[256];
static uint32_t lastTrafficMs;
static vws_cnx *cnx;
static pthread_t ws_thread;
static bool ws_running = false;
static bool ws_should_exit = false;
static pthread_t gdl_thread;


static size_t utcFromGpsTime(char *buf, size_t len, time_t gpsTime) {
    gpsTime += 315964800 + 18;
    struct tm ts;
    ts = *gmtime(&gpsTime);
    strftime(buf, len, "%a %Y-%m-%d %H:%M:%S %Z", &ts);
    return strlen(buf);
}

static void rxJson(const char *data, size_t len) {
    cJSON *root = cJSON_ParseWithLength(data, len);
    gpsStatus_t status;
    cJSON *value;
    cJSON *sub;

    if (root == NULL) {
        //syslog(LOG_INFO, "root parse failed");
        return;
    }
    memset(&status, 0, sizeof status);
    value = cJSON_GetObjectItem(root, "batteryCapacity_%");
    if (value != NULL)
        status.batteryPercent = value->valueint;
    sub = cJSON_GetObjectItem(root, "host");
    if (value != NULL) {
        cJSON *gps;
        gps = cJSON_GetObjectItem(sub, "gps");
        if (gps != NULL) {
            value = cJSON_GetObjectItem(gps, "positionAccuracy_mm");
            if (value != NULL)
                status.accuracyH = (float)value->valueint / 1000.0f;
            value = cJSON_GetObjectItem(gps, "verticalAccuracy_mm");
            if (value != NULL)
                status.accuracyV = (float)value->valueint / 1000.0f;
            value = cJSON_GetObjectItem(gps, "fixType");
            if (value != NULL)
                status.gpsFix = value->valueint;
            value = cJSON_GetObjectItem(gps, "gnssAlt");
            if (value != NULL)
                status.geoAltitude = (float)value->valueint / 1000.0f;
            value = cJSON_GetObjectItem(gps, "numSats");
            if (value != NULL)
                status.satellitesUsed = value->valueint;
            else
                syslog(LOG_INFO, "numSats parse failed");
            value = cJSON_GetObjectItem(gps, "UTCTime");
            if (value != NULL)
                status.gpsTime = value->valueint;
        }
    }
    sub = cJSON_GetObjectItem(root, "sensor");
    if (value != NULL) {
        cJSON *baro;
        baro = cJSON_GetObjectItem(sub, "baro");
        if (value != NULL) {
            value = cJSON_GetObjectItem(baro, "altitude_m");
            status.baroAltitude = (float)value->valueint;
        }
    }
    cJSON_Delete(root);
    char buf[80];
    utcFromGpsTime(buf, sizeof buf, status.gpsTime);
    //printf("Satellites: %d, gpsFix: %d, baroAltitude: %f, geoAltitude: %f, Time: %s\n", status.satellitesUsed, status.gpsFix, status.baroAltitude, status.geoAltitude, buf);
    setStatus(&status);
}


typedef struct {
    struct in_addr srcAddr;
    int port;
} sock_spec_t;

static void * openWebsocket(void *param) {
    char addr[40];
    char url[128];
    sock_spec_t *sock = (sock_spec_t *)param;
    inet_ntop(AF_INET, &sock->srcAddr, addr, sizeof(addr));
    if (sock->port == 80)
        snprintf(url, sizeof url, "ws://%s/stats", addr);
    else
        snprintf(url, sizeof url, "ws://%s:%d/stats", addr, sock->port);
    cnx = vws_cnx_new();
    vws_socket_set_timeout((vws_socket*)cnx, -1);
    if (vws_connect(cnx, url) == false) {
        syslog(LOG_ERR, "Failed to open websocket");
        vws_cnx_free(cnx);
        return NULL;
    }
    // ReSharper disable once CppDFAEndlessLoop
    for(;;) {
        if(ws_should_exit) {
            ws_running = false;
            pthread_exit(NULL);
        }
        vws_msg* message = vws_msg_recv(cnx);
        if (message == NULL) {
            if(!vws_socket_is_connected((vws_socket*)cnx)) {
                vws_cnx_free(cnx);
                ws_running = false;
                pthread_exit(NULL);
            }
        } else {
            rxJson((const char *)message->data->data, message->data->size);
            vws_msg_free(message);
        }
    }
}

/**
 * Process a GDL 90 packet
 * @param packet The packet
 */
void processPacket(gdl90Data_t *packet, struct in_addr *srcAddr) {
    switch (packet->id) {
        case GDL90_HEARTBEAT:
            setHeartbeat(&packet->heartbeat);
            //snprintf(gdl90StatusTexta sizeof(gdl90StatusText), "Heartbeat @%d GPSvalid %s", packet->heartbeat.timeStamp, //packet->heartbeat.gpsPosValid ? "true" : "false");
            break;

        case GDL90_OWNSHIP_REPORT:
            setOwnshipPosition(&packet->positionReport);
            break;
        case GDL90_TRAFFIC_REPORT:
            lastTrafficMs = getMsTime();
            processTraffic(&packet->positionReport);
            break;

        case GDL90_OWNSHIP_GEO_ALT:
            //snprintf(gdl90StatusText, sizeof(gdl90StatusText), "OwnGeoAlt @ %f", packet->ownshipAltitude.geoAltitude / 0.3048f);
            break;

        case GDL90_FOREFLIGHT_ID:
            if (!ws_running) {
                sock_spec_t sock;
                sock.srcAddr = *srcAddr;
                sock.port = 0;
                if (strncmp(packet->foreflightId.name, "SkyEcho", 7) == 0) {
                    sock.port = 80;
                } else if (strncmp(packet->foreflightId.name, "Sim:", 4) == 0) {
                    sock.port = atoi(packet->foreflightId.name + 4);
                }
                if (sock.port != 0) {
                    ws_running = true;
                    ws_should_exit = false;
                    pthread_create(&ws_thread, NULL, openWebsocket, &sock);
                }
            }
            break;

        default:
            syslog(LOG_INFO, "Received unhandled packet type %d", packet->id);
            break;
    }
    if (gdl90Status != GDL90_STATE_RX) {
        gdl90Status = GDL90_STATE_RX;
    }
}

/**
 * Receive an unescaped GDL90 packet from the input and process it.
 * @param packet The packet to process
 * @return
 */

static void gCallback(const gdlDataPacket_t *packet, struct in_addr *srcAddr) {
    gdl90Data_t data;
    if (packet->err != GDL90_ERR_NONE) {
        syslog(LOG_INFO, "DecodeBlock failed - %s: id %d, len %d",
                 gdl90ErrorMessage(packet->err), packet->data[0], packet->len);
    } else {
        memset(&data, 0, sizeof(data));
        gdl90Err_t err = gdl90DecodeBlock(packet->data, packet->len, &data);
        if (err == GDL90_ERR_NONE) {
            processPacket(&data, srcAddr);
        } else
            syslog(LOG_INFO, "Decode packet id %d failed with err %s", data.id, gdl90ErrorMessage(err));
    }
}


void * gdlTask(void *param) {
    // ReSharper disable once CppDFAEndlessLoop
    for (;;) {
        sleep(2);
        syslog(LOG_INFO, "GDL UDP Socket opening");
        listeningSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (listeningSocket <= 0) {
            syslog(LOG_INFO, "Error: listenForPackets - socket() failed.");
            continue;
        }
        struct timeval timeV;
        timeV.tv_sec = 2;
        timeV.tv_usec = 0;
        if (setsockopt(listeningSocket, SOL_SOCKET, SO_RCVTIMEO, &timeV, sizeof(timeV)) == -1) {
            syslog(LOG_INFO, "Error: setsockopt failed.");
            close(listeningSocket);
            continue;
        }
        struct sockaddr_in sockaddr;
        memset(&sockaddr, 0, sizeof(sockaddr));

        sockaddr.sin_family = AF_INET;
        sockaddr.sin_port = htons(listenPort);
        sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        int status = bind(listeningSocket, (struct sockaddr *) &sockaddr, sizeof(sockaddr));
        if (status == -1) {
            close(listeningSocket);
            syslog(LOG_INFO, "Error: bind failed.");
            continue;
        }
        struct sockaddr_in sourceAddr;
        socklen_t sourceAddrLen = sizeof(sourceAddr);

        for (;;) {
            // close websocket if disconnected
            ssize_t result = recvfrom(listeningSocket, rxBuffer, sizeof(rxBuffer), 0,
                                      (struct sockaddr *) &sourceAddr, &sourceAddrLen);
            if (result > 0) {
                //syslog(LOG_INFO, "UDP received: %d bytes.", result);
                gdl90GetBlocks(rxBuffer, result, gCallback, &sourceAddr.sin_addr);
            } else if (result < 0) {
                close(listeningSocket);
                if (ws_running) {
                    ws_should_exit = true;
                }
                syslog(LOG_INFO, "UDP task closed socket: errno=%d", errno);
                break;
            }
        }
    }
}


bool isGdl90TrafficConnected() {
    if (getMsTime() > lastTrafficMs + MAX_AGE_MS)
        gdl90Status = GDL90_STATE_WAITING;
    return gdl90Status == GDL90_STATE_RX;
}

void gdlInit() {
    pthread_create(&gdl_thread, NULL, gdlTask, NULL);
}
