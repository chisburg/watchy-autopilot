#ifndef DISPLAY_SESSION_H
#define DISPLAY_SESSION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile bool gDisplayWifiSession;

// Autopilot: WiFi + Signal K sync before first full refresh after reset (nullptr = stock).
typedef void (*AutopilotBootDisplayFn)(void);
extern AutopilotBootDisplayFn gAutopilotBootDisplay;

#ifdef __cplusplus
}
#endif

#endif
