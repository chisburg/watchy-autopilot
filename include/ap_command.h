#ifndef AP_COMMAND_H
#define AP_COMMAND_H

#include <stdint.h>

enum class ApCommand : uint8_t {
  None,
  AdjustPlus1,
  AdjustMinus1,
  AdjustPlus10,
  AdjustMinus10,
  SetAuto,
  SetStandby,
  SetWind,
};

#endif
