#include "AutopilotWatchy.h"
#include "display_session.h"
#include "settings.h"

#include "esp_sleep.h"

AutopilotWatchy watchy(settings);

AutopilotBootDisplayFn gAutopilotBootDisplay = nullptr;

#if !SIM_MODE
static void autopilotBootDisplay() { watchy.bootSyncBeforeDisplay(); }
#endif

// Earliest hook after deep sleep — before setup(), display init, and Serial.
void initVariant() { AutopilotWatchy::captureWakeHeadingDeltaEarly(); }

void setup() {
  Serial.begin(115200);
  Serial.println("watchy-autopilot boot");
#if SIM_MODE
  Serial.println("SIM_MODE=1 (no Signal K traffic)");
#else
  Serial.println("LIVE: auto WiFi profile + Signal K read");
  gAutopilotBootDisplay = autopilotBootDisplay;
#endif
  watchy.init();
}

void loop() {}
