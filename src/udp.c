//
// Created by Clyde Stubbs on 29/11/2022.
//

#include <sys/socket.h>
#include <netinet/in.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include "udp.h"
#include "main.h"

#define TAG "udp"
static int pingPorts[] = {63093, 47578};
static const char *pingPayload = "{\"App\": \"TraffiX\","
                                 "\"GDL90\": {\"port\": 4000}}";

void pingUdp() {
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = INADDR_BROADCAST;
    dest_addr.sin_family = AF_INET;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        syslog(LOG_ERR, "socket() failed with code %d", sock);
        return;
    }
    int err;
    int optval;
    err = setsockopt(sock, IPPROTO_UDP, SO_BROADCAST, &optval, sizeof(int));
    if (err < 0) {
        syslog(LOG_ERR, "setsockopt() failed with code %d", sock);
        return;
    }
    for (int i = 0; i != ARRAY_SIZE(pingPorts); i++) {
        dest_addr.sin_port = htons(pingPorts[i]);
        err = sendto(sock, pingPayload, strlen(pingPayload), 0, (struct sockaddr *) &dest_addr, sizeof(dest_addr));
        if (err < 0) {
            syslog(LOG_ERR, "sendto() failed with code %d", err);
            return;
        }
        syslog(LOG_INFO, "Sent to port %d", dest_addr.sin_port);
    }
    close(sock);
}
