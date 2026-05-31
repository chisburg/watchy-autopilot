#include "AutopilotWatchy.h"
#include "settings.h"

AutopilotWatchy watchy(settings);

void setup() {
  Serial.begin(115200);
  Serial.println("watchy-autopilot boot");
#if SIM_MODE
  Serial.println("SIM_MODE=1 (no Signal K traffic)");
#endif
  watchy.init();
}

void loop() {}
