#include "wifi.h"
#include "app.h"
#include <nds.h>
#include <dswifi9.h>

#define WIFI_TIMEOUT_FRAMES 600  /* 10 seconds at 60 fps */

bool wifi_connect(const Config *config) {
    (void)config;  /* ssid field reserved for future calico wlmgr support */

    if (!Wifi_InitDefault(WFC_CONNECT)) {
        return false;
    }

    int status;
    int timeout = 0;
    do {
        app_vblank();
        status = Wifi_AssocStatus();
        timeout++;
    } while (status != ASSOCSTATUS_ASSOCIATED &&
             status != ASSOCSTATUS_CANNOTCONNECT &&
             timeout < WIFI_TIMEOUT_FRAMES);

    return (status == ASSOCSTATUS_ASSOCIATED);
}

void wifi_disconnect(void) {
    Wifi_DisconnectAP();
}

unsigned wifi_get_signal_strength(void) {
    if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) return 0;

    if (isDSiMode()) {
        int dbm = (int)(s8)wlmgrGetRssi();
        if (dbm <= -90) return 0;
        if (dbm >= -52) return 3;
        if (dbm >= -65) return 2;
        if (dbm >= -78) return 1;
        return 0;
    } else {
        return wlmgrGetSignalStrength();
    }
}
