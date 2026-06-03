#include "signalk_client.h"

#include "config.h"

#include <Arduino_JSON.h>
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

bool jsonStringValue(const JSONVar &node, char *buf, size_t bufLen) {
  if (JSON.typeof(node) == "undefined" || bufLen == 0) {
    return false;
  }

  String type = JSON.typeof(node);
  if (type == "string") {
    const String s = (const char *)node;
    if (s.length() == 0) {
      return false;
    }
    strncpy(buf, s.c_str(), bufLen - 1);
    buf[bufLen - 1] = '\0';
    return true;
  }

  const JSONVar wrapped = node["value"];
  if (JSON.typeof(wrapped) == "undefined") {
    return false;
  }
  const String s = (const char *)wrapped;
  if (s.length() == 0) {
    return false;
  }
  strncpy(buf, s.c_str(), bufLen - 1);
  buf[bufLen - 1] = '\0';
  return true;
}

bool jsonNumberValue(JSONVar node, double &out) {
  if (JSON.typeof(node) == "undefined") {
    return false;
  }

  String type = JSON.typeof(node);
  if (type == "number" || type == "integer") {
    out = (double)node;
    return true;
  }

  JSONVar wrapped = node["value"];
  if (JSON.typeof(wrapped) == "undefined") {
    return false;
  }
  out = (double)wrapped;
  return true;
}

bool tokenConfigured() { return SK_DEVICE_TOKEN[0] != '\0'; }

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

int jsonStatusCode(JSONVar doc) {
  JSONVar code = doc["statusCode"];
  if (JSON.typeof(code) == "undefined") {
    return -1;
  }
  return (int)code;
}

JSONVar requestDocRoot(JSONVar doc) {
  if (JSON.typeof(doc) == "undefined") {
    return JSONVar();
  }
  if (JSON.typeof(doc) == "array" && doc.length() > 0) {
    return doc[0];
  }
  return doc;
}

PutParseResult parseRequestStateDoc(const JSONVar &docIn, int &statusCode) {
  const JSONVar doc = requestDocRoot(docIn);
  if (JSON.typeof(doc) == "undefined") {
    return PutParseResult::Invalid;
  }

  const String state = (const char *)doc["state"];
  statusCode = jsonStatusCode(doc);
  if (state.length() == 0) {
    return PutParseResult::Invalid;
  }
  if (state == "COMPLETED") {
    return (statusCode == 200) ? PutParseResult::CompletedOk
                               : PutParseResult::CompletedFail;
  }
  if (state == "PENDING") {
    return PutParseResult::Pending;
  }
  if (state == "FAILED") {
    return PutParseResult::Failed;
  }
  return PutParseResult::Invalid;
}

bool fillPollPath(const JSONVar &docIn, char *pollPath, size_t pollPathLen) {
  const JSONVar doc = requestDocRoot(docIn);
  if (JSON.typeof(doc) == "undefined" || pollPathLen == 0) {
    return false;
  }

  const String href = (const char *)doc["href"];
  if (href.length() > 0 && href.charAt(0) == '/') {
    strncpy(pollPath, href.c_str(), pollPathLen - 1);
    pollPath[pollPathLen - 1] = '\0';
    return true;
  }

  char requestId[SK_REQUEST_ID_MAX];
  if (!jsonStringValue(doc["requestId"], requestId, sizeof(requestId))) {
    return false;
  }
  snprintf(pollPath, pollPathLen, "/signalk/v1/requests/%s", requestId);
  return true;
}

bool logRequestMessage(const JSONVar &docIn) {
  const JSONVar doc = requestDocRoot(docIn);
  const String message = (const char *)doc["message"];
  if (message.length() > 0) {
    AP_LOG("SK message: %s", message.c_str());
    return true;
  }
  return false;
}

bool httpGetPath(const SkProfile &profile, const char *apiPath, String &outBody,
                 int timeoutMs, bool useToken) {
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
  outBody = http.getString();
  http.end();
  client.stop();

  if (code != HTTP_CODE_OK) {
    AP_LOG("HTTP GET %s -> %d", apiPath, code);
    return false;
  }
  return true;
}

bool httpGet(const SkProfile &profile, const char *apiPath, String &outBody) {
  return httpGetPath(profile, apiPath, outBody, SK_LINK_TIMEOUT_MS, false);
}

bool pollPendingRequest(const SkProfile &profile, const char *pollPath) {
  const unsigned long deadline =
      millis() + (unsigned long)httpPutPollTimeoutMs();
  int pollNum = 0;

  while ((long)(deadline - millis()) > 0) {
    delay(SK_PUT_POLL_INTERVAL_MS);
    pollNum++;

    String body;
    if (!httpGetPath(profile, pollPath, body, SK_LINK_TIMEOUT_MS, true)) {
      AP_LOG("HTTP PUT poll #%d GET failed", pollNum);
      continue;
    }

    JSONVar doc = JSON.parse(body);
    int statusCode = -1;
    const PutParseResult result = parseRequestStateDoc(doc, statusCode);

    if (result == PutParseResult::CompletedOk) {
      AP_LOG("HTTP PUT poll #%d COMPLETED 200", pollNum);
      return true;
    }
    if (result == PutParseResult::CompletedFail) {
      AP_LOG("HTTP PUT poll #%d COMPLETED status=%d", pollNum, statusCode);
      logRequestMessage(doc);
      return false;
    }
    if (result == PutParseResult::Failed) {
      AP_LOG("HTTP PUT poll #%d FAILED status=%d", pollNum, statusCode);
      logRequestMessage(doc);
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
    const String body = http.getString();
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

    JSONVar doc = JSON.parse(body);
    int statusCode = -1;
    const PutParseResult result = parseRequestStateDoc(doc, statusCode);

    if (result == PutParseResult::CompletedOk) {
      AP_LOG("HTTP PUT COMPLETED 200");
      wifiStale = false;
      return true;
    }
    if (result == PutParseResult::CompletedFail) {
      AP_LOG("HTTP PUT COMPLETED status=%d", statusCode);
      logRequestMessage(doc);
      return false;
    }
    if (result == PutParseResult::Failed) {
      AP_LOG("HTTP PUT FAILED status=%d", statusCode);
      logRequestMessage(doc);
      return false;
    }
    if (result == PutParseResult::Pending) {
      const JSONVar root = requestDocRoot(doc);
      char pollPath[SK_POLL_PATH_MAX];
      char requestId[SK_REQUEST_ID_MAX];
      if (!fillPollPath(root, pollPath, sizeof(pollPath))) {
        AP_LOG("HTTP PUT PENDING missing requestId/href");
        return false;
      }
      if (jsonStringValue(root["requestId"], requestId, sizeof(requestId))) {
        AP_LOG("HTTP PUT PENDING requestId=%s", requestId);
      } else {
        AP_LOG("HTTP PUT PENDING poll=%s", pollPath);
      }
      if (pollPendingRequest(profile, pollPath)) {
        wifiStale = false;
        return true;
      }
      return false;
    }

    AP_LOG("HTTP PUT bad response");
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

  const unsigned long deadline = millis() + (unsigned long)WIFI_CONNECT_MS;
  while (WiFi.status() != WL_CONNECTED && (long)(deadline - millis()) > 0) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    AP_LOG("wifi failed status=%d", WiFi.status());
    return false;
  }

  AP_LOG("wifi ok ip=%s rssi=%d", WiFi.localIP().toString().c_str(),
         WiFi.RSSI());
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

  String body;
  if (!httpGet(profile,
               "/signalk/v1/api/vessels/self/steering/autopilot", body)) {
    return false;
  }
  delay(50);

  JSONVar ap = JSON.parse(body);
  if (JSON.typeof(ap) == "undefined") {
    AP_LOG("SK JSON parse failed");
    return false;
  }

  if (!jsonStringValue(ap["state"], out.state, sizeof(out.state))) {
    AP_LOG("SK missing autopilot.state");
    return false;
  }

  double targetRad = 0.0;
  JSONVar targetNode = ap["target"]["headingMagnetic"];
  if (jsonNumberValue(targetNode, targetRad)) {
    out.targetHeadingDeg = apNormalizeHeadingDeg((float)(targetRad * 180.0 / M_PI));
    out.targetValid = true;
  } else {
    out.targetValid = false;
  }

  out.profileLabel = profile.label;
  out.ok = true;
  AP_LOG("SK state=%s target=%03d valid=%d", out.state,
         (int)roundf(out.targetHeadingDeg), (int)out.targetValid);
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
