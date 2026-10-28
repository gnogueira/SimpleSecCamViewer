#ifndef CAMXP_WATCHDOG_H
#define CAMXP_WATCHDOG_H
#include <stdint.h>

typedef struct {
    uint32_t began, progress, stable_since;
    int frames, seen, stable, healthy;
} CamWatchdog;
enum { CAM_OK, CAM_DISCONNECTED, CAM_NO_IMAGE, CAM_FROZEN };

static void cam_watchdog_start(CamWatchdog *w, uint32_t now) {
    w->began=w->progress=now; w->stable_since=now;
    w->frames=0; w->seen=w->stable=w->healthy=0;
}
static int cam_watchdog_sample(CamWatchdog *w, uint32_t now, int state,
        int has_stats, int frames, int has_video, int minimized) {
    if(state==5 || state==6 || state==7)return CAM_DISCONNECTED;
    /* Rendering can legitimately stop while minimized. Connection errors
     * above are still handled, but do not mistake this for frozen video. */
    if(minimized) {
        w->began=w->progress=now; w->stable=0;
        if(has_stats)w->frames=frames;
        return CAM_OK;
    }
    if(has_stats && frames>0 && frames!=w->frames) {
        w->frames=frames; w->progress=now; w->seen=1;
        if(!w->stable) {w->stable_since=now;w->stable=1;}
        if((uint32_t)(now-w->stable_since)>=15000u)w->healthy=1;
    }
    if(!has_stats && state==3 && has_video) {
        /* An unavailable counter cannot prove a freeze. Avoid endless
         * reconnections solely because statistics are unavailable. */
        w->began=w->progress=now;
        return CAM_OK;
    }
    if(!w->seen && (uint32_t)(now-w->began)>=45000u)return CAM_NO_IMAGE;
    if(w->seen && (uint32_t)(now-w->progress)>=30000u)return CAM_FROZEN;
    return CAM_OK;
}
static unsigned cam_retry_delay(unsigned attempts) {
    if(attempts==0)return 60;
    if(attempts==1)return 120;
    if(attempts==2)return 240;
    return 300;
}
static int cam_connection_due(uint32_t now,uint32_t due,uint32_t last,int has_last) {
    return (int32_t)(now-due)>=0 && (!has_last || (uint32_t)(now-last)>=3000u);
}
#endif
