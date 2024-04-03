/* INCLUDES ------------------------------------------------------------------*/

#include "main.h"
#include "gdltask.h"
#include "ownship.h"
#include "flarm.h"
/* PRIVATE STRUCTURES ---------------------------------------------------------*/

/* VARIABLES -----------------------------------------------------------------*/
static const char *TAG = "main";

long getMsTime(){
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    return currentTime.tv_sec * 1000 + currentTime.tv_usec / 1000;
}
int main(void) {
    openlog("SkyEcho", LOG_PID, LOG_USER);
    initTraffic();
    initOwnship();
    gdlInit();
    flarmInit();
    flarmWait();
    return 0;
}
