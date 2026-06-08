#include "signalk_client.h"

#include "ap_power.h"
#include "config.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <math.h>
#include <string.h>

namespace {

RTC_DATA_ATTR int8_t rtcCachedProfileIndex = -1;

int currentProfileIndex = -1;
bool wifiPersistentInit = false;
bool wifiStale = false;
bool sessionMode = false;

int httpPutTimeoutMs() {
  return sessionMode ? SK_PUT_SESSION_TIMEOUT_MS : SK_PUT_TIMEOUT_MS;
}

int httpPutPollTimeoutMs() {
  return sessionMode ? SK_PUT_POLL_TIMEOUT_SESSION_MS : SK_PUT_POLL_TIMEOUT_MS;
}

constexpr char AP_API_ADJUST[] =
    "/signalk/v1/api/vessels/self/steering/autopilot/actions/adjustHeading";
constexpr char AP_API_STATE[] =
    "/signalk/v1/api/vessels/self/steering/autopilot/state";

bool tokenConfigured() { return SK_DEVICE_TOKEN[0] != '\0'; }

// Signal K v1 API: find "key":{..."value":X...} and read string value (no JSON library).
bool extractSkObjectStringValue(const char *body, const char *objectKey, char *out,
                                size_t outLen) {
  if (!body || !objectKey || !out || outLen == 0) {
    return false;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", objectKey);
  const char *obj = strstr(body, needle);
  if (!obj) {
    return false;
  }

  const char *valueKey = strstr(obj, "\"value\"");
  if (!valueKey) {
    return false;
  }
  const char *colon = strchr(valueKey, ':');
  if (!colon) {
    return false;
  }
  const char *p = colon + 1;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (*p != '"') {
    return false;
  }
  p++;
  const char *end = strchr(p, '"');
  if (!end) {
    return false;
  }
  const size_t len = (size_t)(end - p);
  if (len == 0 || len >= outLen) {
    return false;
  }
  memcpy(out, p, len);
  out[len] = '\0';
  return true;
}

bool extractSkObjectNumberValue(const char *body, const char *objectKey, double &out) {
  if (!body || !objectKey) {
    return false;
  }
  char needle[48];
  snprintf(needle, sizeof(needle), "\"%s\":", objectKey);
  const char *obj = strstr(body, needle);
  if (!obj) {
    return false;
  }

  const char *valueKey = strstr(obj, "\"value\"");
  if (!valueKey) {
    return false;
  }
  const char *colon = strchr(valueKey, ':');
  if (!colon) {
    return false;
  }
  const char *p = colon + 1;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  char *end = nullptr;
  const double v = strtod(p, &end);
  if (end == p) {
    return false;
  }
  out = v;
  return true;
}

const SkProfile *activeProfile() {
  if (currentProfileIndex < 0 ||
      currentProfileIndex >= (int)SK_PROFILE_COUNT) {
    return nullptr;
  }
  return &SK_PROFILES[currentProfileIndex];
}

constexpr size_t SK_REQUEST_ID_MAX = 48;
constexpr size_t SK_POLL_PATH_MAX = 96;

enum class PutParseResult { CompletedOk, CompletedFail, Pending, Failed, Invalid };

// Copy HTTP body into stack buffer and release heap String before parsing (ESP32 heap).
void captureHttpBody(HTTPClient &http, char *buf, size_t bufLen, bool *truncated) {
  if (bufLen == 0) {
    return;
  }
  buf[0] = '\0';
  if (truncated) {
    *truncated = false;
  }
  String tmp = http.getString();
  const size_t rawLen = tmp.length();
  const size_t n = rawLen < bufLen - 1 ? rawLen : bufLen - 1;
  if (n > 0) {
    memcpy(buf, tmp.c_str(), n);
    buf[n] = '\0';
  }
  if (truncated && rawLen >= bufLen - 1) {
    *truncated = true;
  }
  tmp = String();
}

bool extractJsonStringField(const char *body, const char *key, char *out,
                            size_t outLen) {
  if (!body || !key || !out || outLen == 0) {
    return false;
  }
  char needle[32];
  snprintf(needle, sizeof(needle), "\"%s\":\"", key);
  const char *start = strstr(body, needle);
  if (!start) {
    return false;
  }
  start += strlen(needle);
  const char *end = strchr(start, '"');
  if (!end) {
    return false;
  }
  const size_t len = (size_t)(end - start);
  if (len == 0 || len >= outLen) {
    return false;
  }
  memcpy(out, start, len);
  out[len] = '\0';
  return true;
}

bool extractJsonIntField(const char *body, const char *key, int &out) {
  if (!body || !key) {
    return false;
  }
  char needle[32];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *start = strstr(body, needle);
  if (!start) {
    return false;
  }
  start += strlen(needle);
  out = atoi(start);
  return true;
}

PutParseResult parseBodyRequestState(const char *body, int &statusCode) {
  statusCode = -1;
  if (!body || body[0] == '\0') {
    return PutParseResult::Invalid;
  }
  extractJsonIntField(body, "statusCode", statusCode);

  if (strstr(body, "\"state\":\"COMPLETED\"") != nullptr) {
    return (statusCode == 200) ? PutParseResult::CompletedOk
                               : PutParseResult::CompletedFail;
  }
  if (strstr(body, "\"state\":\"PENDING\"") != nullptr) {
    return PutParseResult::Pending;
  }
  if (strstr(body, "\"state\":\"FAILED\"") != nullptr) {
    return PutParseResult::Failed;
  }
  return PutParseResult::Invalid;
}

bool fillPollPathFromBody(const char *body, char *pollPath, size_t pollPathLen) {
  if (!body || pollPathLen == 0) {
    return false;
  }

  char href[SK_POLL_PATH_MAX];
  if (extractJsonStringField(body, "href", href, sizeof(href))) {
    const char *path = href;
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
      const char *schemeEnd = strstr(href, "://");
      if (schemeEnd) {
        path = strchr(schemeEnd + 3, '/');
        if (!path) {
          path = nullptr;
        }
      }
    }
    if (path && path[0] == '/') {
      strncpy(pollPath, path, pollPathLen - 1);
      pollPath[pollPathLen - 1] = '\0';
      return true;
    }
  }

  char requestId[SK_REQUEST_ID_MAX];
  if (!extractJsonStringField(body, "requestId", requestId, sizeof(requestId))) {
    return false;
  }
  snprintf(pollPath, pollPathLen, "/signalk/v1/requests/%s", requestId);
  return true;
}

void logRequestMessageFromBody(const char *body) {
  char message[96];
  if (extractJsonStringField(body, "message", message, sizeof(message))) {
    AP_LOG("SK message: %s", message);
  }
}

bool httpGetPath(const SkProfile &profile, const char *apiPath, char *outBody,
                 size_t outBodyLen, int timeoutMs, bool useToken) {
  const String url = String("http://") + profile.skHost + ":" +
                     String(profile.skPort) + apiPath;

  WiFiClient client;
  client.setTimeout(timeoutMs / 1000);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(timeoutMs);
  if (!http.begin(client, url)) {
    AP_LOG("HTTP GET begin failed");
    return false;
  }

  http.addHeader("Connection", "close");
  if (useToken && tokenConfigured()) {
    http.addHeader("Authorization", String("Bearer ") + SK_DEVICE_TOKEN);
  }

  const int code = http.GET();
  if (outBody && outBodyLen > 0) {
    bool truncated = false;
    captureHttpBody(http, outBody, outBodyLen, &truncated);
    if (truncated) {
      AP_LOG("HTTP GET %s body truncated (max %u)", apiPath, (unsigned)outBodyLen);
    }
  }
  http.end();
  client.stop();

  if (code != HTTP_CODE_OK) {
    AP_LOG("HTTP GET %s -> %d", apiPath, code);
    return false;
  }
  return outBody && outBody[0] != '\0';
}

bool pollPendingRequest(const SkProfile &profile, const char *pollPath) {
  const unsigned long deadline =
      millis() + (unsigned long)httpPutPollTimeoutMs();
  int pollNum = 0;

  while ((long)(deadline - millis()) > 0) {
    delay(SK_PUT_POLL_INTERVAL_MS);
    pollNum++;

    char body[SK_HTTP_BODY_MAX];
    if (!httpGetPath(profile, pollPath, body, sizeof(body), SK_LINK_TIMEOUT_MS,
                     true)) {
      AP_LOG("HTTP PUT poll #%d GET failed", pollNum);
      continue;
    }

    int statusCode = -1;
    const PutParseResult result = parseBodyRequestState(body, statusCode);

    if (result == PutParseResult::CompletedOk) {
      AP_LOG("HTTP PUT poll #%d COMPLETED 200", pollNum);
      return true;
    }
    if (result == PutParseResult::CompletedFail) {
      AP_LOG("HTTP PUT poll #%d COMPLETED status=%d", pollNum, statusCode);
      logRequestMessageFromBody(body);
      return false;
    }
    if (result == PutParseResult::Failed) {
      AP_LOG("HTTP PUT poll #%d FAILED status=%d", pollNum, statusCode);
      logRequestMessageFromBody(body);
      return false;
    }
    if (result == PutParseResult::Pending) {
      AP_LOG("HTTP PUT poll #%d PENDING", pollNum);
      continue;
    }

    AP_LOG("HTTP PUT poll #%d bad body", pollNum);
  }

  AP_LOG("HTTP PUT poll timeout %s", pollPath);
  return false;
}

bool httpPut(const SkProfile &profile, const char *apiPath,
             const char *jsonBody) {
  if (!tokenConfigured()) {
    AP_LOG("HTTP put: missing SK_DEVICE_TOKEN");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    SignalKClient::recoverWifi();
    wifiStale = false;
  }

  const String url = String("http://") + profile.skHost + ":" +
                     String(profile.skPort) + apiPath;

  for (int attempt = 0; attempt < 2; attempt++) {
    if (attempt > 0) {
      AP_LOG("HTTP PUT retry");
      delay(100);
    }

    WiFi.setSleep(false);

    const int putTimeoutMs = httpPutTimeoutMs();
    WiFiClient client;
    client.setTimeout(putTimeoutMs / 1000);

    HTTPClient http;
    http.setReuse(false);
    http.setTimeout(putTimeoutMs);
    if (!http.begin(client, url)) {
      AP_LOG("HTTP PUT begin failed");
      continue;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    http.addHeader("Authorization", String("Bearer ") + SK_DEVICE_TOKEN);

    AP_LOG("HTTP PUT %s", apiPath);
    const int code = http.PUT(jsonBody);
    char body[SK_HTTP_BODY_MAX];
    captureHttpBody(http, body, sizeof(body), nullptr);
    http.end();
    client.stop();

    if (code < 0) {
      AP_LOG("HTTP PUT -> %d wifi=%d", code, WiFi.status());
      if (attempt == 0) {
        SignalKClient::recoverWifi();
      }
      continue;
    }

    AP_LOG("HTTP PUT -> %d", code);

    if (code != HTTP_CODE_OK && code != HTTP_CODE_ACCEPTED) {
      return false;
    }

    int statusCode = -1;
    const PutParseResult result = parseBodyRequestState(body, statusCode);

    if (result == PutParseResult::CompletedOk) {
      AP_LOG("HTTP PUT COMPLETED 200");
      apLogPower("put ok");
      wifiStale = false;
      return true;
    }
    if (result == PutParseResult::CompletedFail) {
      AP_LOG("HTTP PUT COMPLETED status=%d", statusCode);
      logRequestMessageFromBody(body);
      return false;
    }
    if (result == PutParseResult::Failed) {
      AP_LOG("HTTP PUT FAILED status=%d", statusCode);
      logRequestMessageFromBody(body);
      return false;
    }
    if (result == PutParseResult::Pending) {
      char pollPath[SK_POLL_PATH_MAX];
      char requestId[SK_REQUEST_ID_MAX];
      if (!fillPollPathFromBody(body, pollPath, sizeof(pollPath))) {
        AP_LOG("HTTP PUT PENDING missing requestId/href");
        return false;
      }
      if (extractJsonStringField(body, "requestId", requestId,
                                 sizeof(requestId))) {
        AP_LOG("HTTP PUT PENDING requestId=%s", requestId);
      } else {
        AP_LOG("HTTP PUT PENDING poll=%s", pollPath);
      }
      if (pollPendingRequest(profile, pollPath)) {
        apLogPower("put ok");
        wifiStale = false;
        return true;
      }
      apLogPower("put fail");
      return false;
    }

    AP_LOG("HTTP PUT bad response");
    apLogPower("put fail");
    return false;
  }

  return false;
}

} // namespace

bool SignalKClient::connectProfile(const SkProfile &profile) {
  AP_LOG("connect %s ssid=%s host=%s", profile.label, profile.ssid,
         profile.skHost);

  if (!wifiPersistentInit) {
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);
    wifiPersistentInit = true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == profile.ssid) {
    AP_LOG("wifi already on ssid=%s ip=%s", profile.ssid,
           WiFi.localIP().toString().c_str());
    return true;
  }

  WiFi.disconnect(false, false);
  delay(50);
  WiFi.begin(profile.ssid, profile.password);

  const unsigned long connectStart = millis();
  const unsigned long deadline = millis() + (unsigned long)WIFI_CONNECT_MS;
  while (WiFi.status() != WL_CONNECTED && (long)(deadline - millis()) > 0) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    AP_LOG("wifi failed status=%d connect_ms=%lu", WiFi.status(),
           millis() - connectStart);
    apLogPower("wifi fail");
    return false;
  }

  AP_LOG("wifi ok ip=%s rssi=%d connect_ms=%lu",
         WiFi.localIP().toString().c_str(), WiFi.RSSI(),
         millis() - connectStart);
  apLogPower("wifi ok");
  return true;
}

bool SignalKClient::connectBestProfile() {
  if (rtcCachedProfileIndex >= 0 &&
      rtcCachedProfileIndex < (int)SK_PROFILE_COUNT) {
    const SkProfile &cached = SK_PROFILES[rtcCachedProfileIndex];
    if (connectProfile(cached)) {
      currentProfileIndex = rtcCachedProfileIndex;
      AP_LOG("wifi cached profile %s", cached.label);
      return true;
    }
    AP_LOG("cached profile %s failed, trying all", cached.label);
  }

  for (size_t i = 0; i < SK_PROFILE_COUNT; i++) {
    if (connectProfile(SK_PROFILES[i])) {
      currentProfileIndex = (int)i;
      rtcCachedProfileIndex = (int8_t)i;
      return true;
    }
    WiFi.disconnect(false, false);
    delay(100);
  }

  currentProfileIndex = -1;
  return false;
}

bool SignalKClient::ensureConnected() {
  if (WiFi.status() == WL_CONNECTED && currentProfileIndex >= 0) {
    return true;
  }
  return connectBestProfile();
}

void SignalKClient::disconnect() {
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  currentProfileIndex = -1;
  wifiStale = false;
}

bool SignalKClient::isWifiConnected() {
  return WiFi.status() == WL_CONNECTED && currentProfileIndex >= 0;
}

const char *SignalKClient::connectedProfileLabel() {
  const SkProfile *profile = activeProfile();
  return profile ? profile->label : "";
}

void SignalKClient::markWifiStale() { wifiStale = true; }

void SignalKClient::setSessionMode(bool active) { sessionMode = active; }

void SignalKClient::warmAfterDisplay() {
  if (currentProfileIndex < 0 && rtcCachedProfileIndex >= 0 &&
      rtcCachedProfileIndex < (int)SK_PROFILE_COUNT) {
    currentProfileIndex = rtcCachedProfileIndex;
  }
  const SkProfile *profile = activeProfile();
  if (profile == nullptr) {
    connectBestProfile();
    return;
  }

  AP_LOG("wifi warm after display");
  WiFi.setSleep(false);

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false, false);
    delay(50);
  }

  WiFi.begin(profile->ssid, profile->password);

  const unsigned long deadline = millis() + 1500UL;
  while (WiFi.status() != WL_CONNECTED && (long)(deadline - millis()) > 0) {
    delay(50);
  }

  if (WiFi.status() == WL_CONNECTED) {
    AP_LOG("wifi warm ok ip=%s rssi=%d",
           WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    AP_LOG("wifi warm failed status=%d — full recover", WiFi.status());
    recoverWifi();
  }
}

void SignalKClient::recoverWifi() {
  const SkProfile *profile = activeProfile();
  if (profile == nullptr) {
    connectBestProfile();
    return;
  }

  AP_LOG("wifi recover %s", profile->label);
  WiFi.setSleep(false);

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false, false);
    delay(100);
  }

  WiFi.begin(profile->ssid, profile->password);

  const unsigned long deadline = millis() + (unsigned long)WIFI_RECOVER_MS;
  while (WiFi.status() != WL_CONNECTED && (long)(deadline - millis()) > 0) {
    delay(50);
  }

  if (WiFi.status() == WL_CONNECTED) {
    AP_LOG("wifi recover ok ip=%s rssi=%d",
           WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    AP_LOG("wifi recover failed status=%d", WiFi.status());
  }
}

bool SignalKClient::fetchAutopilot(const SkProfile &profile,
                                   SkAutopilotSnapshot &out) {
  delay(50);

  char body[SK_GET_BODY_MAX];
  if (!httpGetPath(profile,
                   "/signalk/v1/api/vessels/self/steering/autopilot", body,
                   sizeof(body), SK_LINK_TIMEOUT_MS, false)) {
    return false;
  }
  delay(50);

  if (!extractSkObjectStringValue(body, "state", out.state, sizeof(out.state))) {
    AP_LOG("SK missing autopilot.state");
    return false;
  }

  double targetRad = 0.0;
  if (extractSkObjectNumberValue(body, "headingMagnetic", targetRad)) {
    out.targetHeadingDeg =
        apNormalizeHeadingDeg((float)(targetRad * 180.0 / M_PI));
    out.targetValid = true;
  } else {
    out.targetValid = false;
  }

  double windRad = 0.0;
  if (extractSkObjectNumberValue(body, "windAngleApparent", windRad)) {
    out.windAngleDeg = (float)(windRad * 180.0 / M_PI);
    out.windValid = true;
  } else {
    out.windValid = false;
  }

  out.profileLabel = profile.label;
  out.ok = true;
  AP_LOG("SK state=%s target=%03d wind=%d valid=%d windValid=%d", out.state,
         (int)roundf(out.targetHeadingDeg), (int)roundf(out.windAngleDeg),
         (int)out.targetValid, (int)out.windValid);
  return true;
}

bool SignalKClient::readAutopilot(SkAutopilotSnapshot &out) {
  out = SkAutopilotSnapshot{};
  const SkProfile *profile = activeProfile();
  if (profile == nullptr) {
    return false;
  }
  return fetchAutopilot(*profile, out);
}

bool SignalKClient::putAdjustHeading(int degrees) {
  if (degrees != 1 && degrees != -1 && degrees != 10 && degrees != -10) {
    AP_LOG("HTTP put: invalid adjustHeading %d", degrees);
    return false;
  }
  const SkProfile *profile = activeProfile();
  if (profile == nullptr) {
    AP_LOG("HTTP put: no profile");
    return false;
  }

  char body[32];
  snprintf(body, sizeof(body), "{\"value\":%d}", degrees);
  return httpPut(*profile, AP_API_ADJUST, body);
}

bool SignalKClient::putAutopilotState(const char *state) {
  const SkProfile *profile = activeProfile();
  if (profile == nullptr) {
    AP_LOG("HTTP put: no profile");
    return false;
  }

  char body[48];
  snprintf(body, sizeof(body), "{\"value\":\"%s\"}", state);
  return httpPut(*profile, AP_API_STATE, body);
}
