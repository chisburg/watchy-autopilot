#ifndef SETTINGS_H
#define SETTINGS_H

#include <Watchy.h>

watchySettings settings{
    .cityID = "",
    .lat = "",
    .lon = "",
    .weatherAPIKey = "",
    .weatherURL = "",
    .weatherUnit = "metric",
    .weatherLang = "en",
    .weatherUpdateInterval = 30,
    .ntpServer = "pool.ntp.org",
    .gmtOffset = 3600,
    .vibrateOClock = false,
};

#endif
