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

constexpr char AP_API_ADJUST[] =
    "/signalk/v1/api/vessels/self/steering/autopilot/actions/adjustHeading";
constexpr char AP_API_STATE[] =
    "/signalk/v1/api/vessels/self/steering/autopilot/state";

bool jsonStringValue(JSONVar node, char *buf, size_t bufLen) {
  if (JSON.typeof(node) == "undefined" || bufLen == 0) {
    return false;
  }

  String type = JSON.typeof(node);
  if (type == "string") {
    const char *s = (const char *)node;
    if (s == nullptr || s[0] == '\0') {
      return false;
    }
    strncpy(buf, s, bufLen - 1);
    buf[bufLen - 1] = '\0';
    return true;
  }

  JSONVar wrapped = node["value"];
  if (JSON.typeof(wrapped) == "undefined") {
    return false;
  }
  const char *s = (const char *)wrapped;
  if (s == nullptr || s[0] == '\0') {
    return false;
  }
  strncpy(buf, s, bufLen - 1);
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

bool parseHttpPutResponse(const String &body) {
  JSONVar doc = JSON.parse(body);
  if (JSON.typeof(doc) == "undefined") {
    return false;
  }

  const char *state = (const char *)doc["state"];
  if (state == nullptr || strcmp(state, "COMPLETED") != 0) {
    return false;
  }

  JSONVar code = doc["statusCode"];
  if (JSON.typeof(code) != "undefined" && (int)code != 200) {
    return false;
  }
  return true;
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
    if (code != HTTP_CODE_OK) {
      AP_LOG("HTTP PUT -> %d", code);
      return false;
    }
    if (!parseHttpPutResponse(body)) {
      AP_LOG("HTTP PUT bad response");
      return false;
    }

    AP_LOG("HTTP PUT COMPLETED 200");
    wifiStale = false;
    return true;
  }

  return false;
}

bool httpGet(const SkProfile &profile, const char *apiPath, String &outBody) {
  const String url = String("http://") + profile.skHost + ":" +
                     String(profile.skPort) + apiPath;

  WiFiClient client;
  client.setTimeout(SK_LINK_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(SK_LINK_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    AP_LOG("HTTP GET begin failed");
    return false;
  }

  http.addHeader("Connection", "close");

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
