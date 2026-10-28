#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "watchdog.h"
int main(void) {
    CamWatchdog w;
    cam_watchdog_start(&w,1000);
    assert(cam_watchdog_sample(&w,1001,7,0,0,0,0)==CAM_DISCONNECTED);
    assert(cam_watchdog_sample(&w,1001,6,0,0,0,0)==CAM_DISCONNECTED);
    assert(cam_watchdog_sample(&w,1001,5,0,0,0,0)==CAM_DISCONNECTED);
    cam_watchdog_start(&w,1000);
    assert(cam_watchdog_sample(&w,45999,1,1,0,0,0)==CAM_OK);
    assert(cam_watchdog_sample(&w,46000,1,1,0,0,0)==CAM_NO_IMAGE);
    cam_watchdog_start(&w,0);
    assert(cam_watchdog_sample(&w,1000,3,1,1,1,0)==CAM_OK);
    assert(cam_watchdog_sample(&w,30999,3,1,1,1,0)==CAM_OK);
    assert(cam_watchdog_sample(&w,31000,3,1,1,1,0)==CAM_FROZEN);
    /* Quiet scene: it still produces frames, so it must not reconnect. */
    cam_watchdog_start(&w,0);
    for(int sec=1;sec<=600;sec++)
        assert(cam_watchdog_sample(&w,(uint32_t)sec*1000,3,1,sec*25,1,0)==CAM_OK);
    assert(w.healthy);
    /* Rendering stopped by minimization is not a stalled camera. */
    cam_watchdog_start(&w,0);
    assert(cam_watchdog_sample(&w,1000,3,1,25,1,0)==CAM_OK);
    assert(cam_watchdog_sample(&w,300000,3,1,25,1,1)==CAM_OK);
    assert(cam_watchdog_sample(&w,300001,3,1,25,1,0)==CAM_OK);
    assert(cam_watchdog_sample(&w,300002,7,0,0,0,1)==CAM_DISCONNECTED);
    /* No optional statistics: a playing video is not proof of a freeze. */
    cam_watchdog_start(&w,0);
    assert(cam_watchdog_sample(&w,120000,3,0,0,1,0)==CAM_OK);
    /* GetTickCount wraps every ~49 days. */
    cam_watchdog_start(&w,UINT32_MAX-999u);
    assert(cam_watchdog_sample(&w,44000,1,1,0,0,0)==CAM_NO_IMAGE);
    assert(cam_retry_delay(0)==60);
    assert(cam_retry_delay(1)==120);
    assert(cam_retry_delay(2)==240);
    assert(cam_retry_delay(3)==300 && cam_retry_delay(100)==300);
    assert(!cam_connection_due(59999,60000,0,0));
    assert(cam_connection_due(60000,60000,0,0));
    /* A second simultaneous failure may reconnect only 3s after the first. */
    assert(!cam_connection_due(60001,60000,60000,1));
    assert(!cam_connection_due(62999,60000,60000,1));
    assert(cam_connection_due(63000,60000,60000,1));
    assert(cam_connection_due(2000,1000,UINT32_MAX-1000u,1));
    assert(!cam_connection_due(1000,1000,UINT32_MAX-1000u,1));
    puts("OK: failures, missing or stalled frames, static scenes, minimization, unavailable stats, backoff, spacing and clock rollover.");
    return 0;
}
