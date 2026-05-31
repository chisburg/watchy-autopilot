#ifndef _BLE_H_
#define _BLE_H_

#include "Arduino.h"

// M1 autopilot: BLE OTA not used — stub keeps Watchy linking without full BLE stack
class BLE {
public:
  BLE(void) {}
  ~BLE(void) {}

  bool begin(const char *) { return false; }
  int updateStatus() { return -1; }
  int howManyBytes() { return 0; }
};

#endif
