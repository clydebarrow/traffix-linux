#include <sys/cdefs.h>
//
// Created by Clyde Stubbs on 23/12/2022.
//

#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>


#include "gdltask.h"
#include "flarm.h"

#include <sys/errno.h>

#include "ownship.h"
#include "main.h"


#define TAG "FLARM"

static int sock;

int prefFlarmBaudRate = 19200;

int prefUDPPort = 4353;

#define UART_BUFFER_SIZE (1024 * 2)

static char nmeabuf[160];
static pthread_t flarm_thread;

/**
 * Add a checksum to the buffer. The first charcter ($) is ignored.
 * @param buffer
 */
int checksum() {
    uint8_t sum = 0;
    size_t len = strlen(nmeabuf);
    if (len == 0 || sizeof nmeabuf < len + 6)
        return false;
    for (size_t i = 1; i != len; i++)
        sum ^= nmeabuf[i];
    sprintf(nmeabuf + len, "*%02X\r\n", sum);
    return true;
}

static int accuracyMap[] = {
        -1,
        18520,
        7408,
        3704,
        1852,
        926,
        555,
        185,
        92,
        30,
        10,
        3,
};

static int getAccuracy(int nacP) {

    if (nacP < 0 || nacP > ARRAY_SIZE(accuracyMap))
        return -1;
    return accuracyMap[nacP];
}

void sendData() {
    checksum();
    struct sockaddr_in sendAddr;
    sendAddr.sin_addr.s_addr = INADDR_BROADCAST;
    sendAddr.sin_family = AF_INET;
    sendAddr.sin_port = htons(prefUDPPort);
    //syslog(LOG_INFO, "FLARM data: %s", nmeabuf);
    //printf("FLARM data: %s", nmeabuf);
    sendto(sock, nmeabuf, strlen(nmeabuf), 0, (struct sockaddr *) &sendAddr, sizeof(sendAddr));
}

void gpgga() {
    gdl90Heartbeat_t heartbeat;
    gpsStatus_t status;
    ownship_t ownship;
    bool valid = (int) getHeartbeat(&heartbeat) | getStatus(&status) | getOwnshipPosition(&ownship);

    float absLat = fabsf(ownship.report.latitude);
    int latDeg = (int) absLat;
    float absLon = fabsf(ownship.report.longitude);
    int lonDeg = (int) absLon;

    snprintf(nmeabuf, sizeof nmeabuf,
             "$GPGGA,%02u%02u%02u,%02d%05.2f,%c,%02d%05.2f,%c,%d,%02d,%d,%3.1f,M,%3.1f,M,,",
             (unsigned int) (heartbeat.timeStamp / 3600),
             (unsigned int) ((heartbeat.timeStamp / 60) % 60),
             (unsigned int) (heartbeat.timeStamp % 60),
             latDeg,
             (absLat - latDeg) * 60,
             ownship.report.latitude < 0 ? 'S' : 'N',
             lonDeg,
             (absLon - lonDeg) * 60,
             ownship.report.longitude < 0 ? 'W' : 'E',
             valid ? 1 : 0,
             status.satellitesUsed,
             getAccuracy(ownship.report.NACp),
             ownship.report.altitude,
             status.geoAltitude
    );
    sendData();
}

void gprmc() {
    gdl90Heartbeat_t heartbeat;
    gpsStatus_t status;
    ownship_t ownship;
    bool valid = (int) getHeartbeat(&heartbeat) | getStatus(&status) | getOwnshipPosition(&ownship);

    float absLat = fabsf(ownship.report.latitude);
    int latDeg = (int) absLat;
    float absLon = fabsf(ownship.report.longitude);
    int lonDeg = (int) absLon;
    struct tm ts;
    time_t utc = status.gpsTime + (315964800 + 18);
    ts = *gmtime(&utc);
    snprintf(nmeabuf, sizeof nmeabuf,
             "$GPRMC,%02u%02u%02u,%c,%02d%05.2f,%c,%03d%05.2f,%c,%05.1f,%05.1f,%02d%02d%02d,%05.1f,%c",
             (unsigned int) (heartbeat.timeStamp / 3600),
             (unsigned int) ((heartbeat.timeStamp / 60) % 60),
             (unsigned int) (heartbeat.timeStamp % 60),
             valid ? 'A' : 'V',
             latDeg,
             (absLat - latDeg) * 60,
             ownship.report.latitude < 0 ? 'S' : 'N',
             lonDeg,
             (absLon - lonDeg) * 60,
             ownship.report.longitude < 0 ? 'W' : 'E',
             ownship.report.groundSpeed,
             ownship.report.track,
             ts.tm_mday,
             ts.tm_mon + 1,
             ts.tm_year % 100,
             0.0f, 'E' //if (variation > 0) 'E' else 'W'
    );
    sendData();
}

void pgrmz() {
    gpsStatus_t status;
    getStatus(&status);
    snprintf(nmeabuf, sizeof nmeabuf, "$PGRMZ,%d,F,2", (int) (status.baroAltitude / .3048));
    sendData();
}

void pflaa() {
    traffic_t traffic[MAX_TARGETS_SHOWN];
    ownship_t ourPosition;
    getOwnshipPosition(&ourPosition);
    getTraffic(traffic, MAX_TARGETS_SHOWN);
    for (int i = 0; i != MAX_TARGETS_SHOWN; i++) {
        traffic_t *target = traffic + i;
        if (!target->active)
            continue;

        //PFLAA,<AlarmLevel>,<RelativeNorth>,<RelativeEast>, <RelativeVertical>,<IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>, <ClimbRate>,<AcftType>

        snprintf(nmeabuf, sizeof nmeabuf,
                 "$PFLAA,%d,%d,%d,%d,1,%06X,%d,,%d,%d,%X",
                 0,  // alarm level
                 (int) trafficNorthing(&ourPosition.report, &target->report),
                 (int) trafficEasting(&ourPosition.report, &target->report),
                 (int) (target->report.altitude - ourPosition.report.altitude),
                 (unsigned int)target->report.address,
                 (int) target->report.track,
                 (int) target->report.groundSpeed,
                 (int) target->report.verticalSpeed,
                 target->report.category
        );
        sendData();
    }
}

static void * flarmTask(void *param) {
// IP address.
    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        syslog(LOG_ERR, "socket() failed with code %d", sock);
        //printf("socket() failed with code %d", sock);
        return NULL;
    }
    int optval = 1;
    int err = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(int));
    if (err < 0) {
        syslog(LOG_ERR, "setsockopt() failed with code %d", sock);
        //printf("setsockopt() failed with code %d: errno=%d", sock, errno);
        return NULL;
    }
    syslog(LOG_INFO, "Flarm task started");
    //printf("Flarm task started");
    for (;;) {
        unsigned long started = getMsTime();
        gpgga();
        gprmc();
        pgrmz();
        pflaa();
        usleep((started + 1000 - getMsTime()) * 1000UL);
    }
}

void flarmInit() {
    pthread_create(&flarm_thread, NULL, flarmTask, NULL);
}

void flarmWait()
{
    pthread_join(flarm_thread, 0);
}
