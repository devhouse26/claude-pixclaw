#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <time.h>
#include "board_hw.h"
#include "ble_bridge.h"
#include "xfer.h"

struct TamaState {
  uint8_t  sessionsTotal;
  uint8_t  sessionsRunning;
  uint8_t  sessionsWaiting;
  bool     recentlyCompleted;
  uint32_t tokensToday;
  uint32_t lastUpdated;
  char     msg[48];
  bool     connected;
  char     lines[8][92];
  uint8_t  nLines;
  uint16_t lineGen;          // bumps when lines change — lets UI reset scroll
  char     promptId[40];     // pending permission request ID; empty = no prompt
  char     promptTool[20];
  char     promptHint[44];
};

// ---------------------------------------------------------------------------
// Three modes, checked in priority order:
//   demo   → auto-cycle fake scenarios every 8s, ignore live data
//   live   → JSON arrived in the last 10s over USB or BT
//   asleep → no data, all zeros, "No Claude connected"
// ---------------------------------------------------------------------------

static uint32_t _lastLiveMs = 0;
static uint32_t _lastBtByteMs = 0;   // hasClient() lies; track actual BT traffic
// Desktop sometimes leaves `running` at 0 while output tokens / snapshots still move.
static uint32_t _lastOutputActivityMs = 0;
static uint32_t _bridgeTokensSeen = 0;
static bool     _haveBridgeTokens = false;
static uint32_t _tokensTodaySeen = 0;
static bool     _haveTokensTodaySeen = false;
static bool     _demoMode   = false;
static uint8_t  _demoIdx    = 0;
static uint32_t _demoNext   = 0;

struct _Fake { const char* n; uint8_t t,r,w; bool c; uint32_t tok; };
static const _Fake _FAKES[] = {
  {"asleep",0,0,0,false,0}, {"one idle",1,0,0,false,12000},
  {"busy",4,3,0,false,89000}, {"attention",2,1,1,false,45000},
  {"completed",1,0,0,true,142000},
};

inline void dataSetDemo(bool on) {
  _demoMode = on;
  if (on) { _demoIdx = 0; _demoNext = millis(); }
}
inline bool dataDemo() { return _demoMode; }

inline bool dataConnected() {
  return _lastLiveMs != 0 && (millis() - _lastLiveMs) <= 30000;
}

inline bool dataBtActive() {
  // Desktop's idle keepalive is ~10s; give it 1.5x headroom.
  return _lastBtByteMs != 0 && (millis() - _lastBtByteMs) <= 15000;
}

// True shortly after bridge tokens increased, a turn event arrived, or HUD snapshot text changed.
inline bool dataRecentOutput(uint32_t windowMs = 12000) {
  return _lastOutputActivityMs != 0
      && (uint32_t)(millis() - _lastOutputActivityMs) < windowMs;
}

inline const char* dataScenarioName() {
  if (_demoMode) return _FAKES[_demoIdx].n;
  if (dataConnected()) return dataBtActive() ? "bt" : "usb";
  return "none";
}

// Set true once the bridge sends a time sync — until then the RTC may
// hold whatever was on the coin cell (or 2000-01-01 if it lost power).
static bool _rtcValid = false;
inline bool dataRtcValid() { return _rtcValid; }

static uint8_t _clampU8(unsigned v) { return (uint8_t)(v > 255 ? 255 : v); }

// Desktop may send 1/0 instead of true/false for generating-style flags.
static bool _variantTruthy(JsonVariant v) {
  if (v.isNull()) return false;
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<int>()) return v.as<int>() != 0;
  if (v.is<unsigned>()) return v.as<unsigned>() != 0;
  return false;
}

// REFERENCE.md uses flat total/running/waiting; newer desktop builds may nest them
// or use separate generating/streaming flags without bumping `running`.
static void _mergeSessionObject(JsonObject o, TamaState* out) {
  if (o.isNull()) return;
  if (!o["total"].isNull()) out->sessionsTotal = _clampU8(o["total"].as<unsigned>());
  if (!o["running"].isNull()) out->sessionsRunning = _clampU8(o["running"].as<unsigned>());
  if (!o["waiting"].isNull()) out->sessionsWaiting = _clampU8(o["waiting"].as<unsigned>());
  // Alternate numeric counters some builds use instead of `running`.
  static const char* const RUN_KEYS[] = {
      "active_generating", "generating_count", "generating_sessions",
      "active_sessions",   "busy_sessions",    "num_running",
      "running_count",     nullptr};
  for (const char* const* pk = RUN_KEYS; *pk; pk++) {
    JsonVariant v = o[*pk];
    if (v.isNull()) continue;
    unsigned long u = v.as<unsigned long>();
    if (u > 255u) u = 255u;
    uint8_t c = (uint8_t)u;
    if (c > out->sessionsRunning) out->sessionsRunning = c;
  }
  static const char* const GEN_KEYS[] = {
      "generating", "streaming", "is_generating", "isGenerating", nullptr};
  for (const char* const* pk = GEN_KEYS; *pk; pk++) {
    if (_variantTruthy(o[*pk])) {
      _lastOutputActivityMs = millis();
      break;
    }
  }
}

static bool _statusLooksBusy(const char* st) {
  if (!st || !st[0]) return false;
  // Substrings — desktop may send "assistant_generating", etc.
  return strstr(st, "generat") || strstr(st, "stream") ||
         strstr(st, "thinking") || strstr(st, "responding") ||
         strcmp(st, "busy") == 0 || strcmp(st, "running") == 0;
}

// Some builds send sessions as an array of per-chat objects instead of aggregate counters.
static void _mergeSessionsArray(JsonArray arr, TamaState* out) {
  if (arr.isNull()) return;
  size_t n = arr.size();
  if (n > 0 && out->sessionsTotal == 0)
    out->sessionsTotal = _clampU8((unsigned)n);
  unsigned run = 0, wait = 0;
  for (JsonVariant item : arr) {
    if (!item.is<JsonObject>()) continue;
    JsonObject so = item.as<JsonObject>();
    static const char* const GEN_KEYS[] = {
        "generating", "streaming", "is_generating", "isGenerating", nullptr};
    bool busy = false;
    for (const char* const* pk = GEN_KEYS; *pk; pk++) {
      if (_variantTruthy(so[*pk])) { busy = true; break; }
    }
    const char* st = so["status"];
    if (!st || !st[0]) st = so["state"];
    if (!busy && _statusLooksBusy(st)) busy = true;
    JsonVariant rv = so["running"];
    if (!rv.isNull()) {
      if ((rv.is<unsigned>() || rv.is<int>()) && rv.as<long>() > 0) busy = true;
      else if (_variantTruthy(rv)) busy = true;
    }
    if (_variantTruthy(so["waiting"]) || _variantTruthy(so["blocked_on_permission"]))
      wait++;
    if (busy) run++;
  }
  if (run > out->sessionsRunning) out->sessionsRunning = _clampU8(run);
  if (wait > out->sessionsWaiting) out->sessionsWaiting = _clampU8(wait);
  if (run > 0) _lastOutputActivityMs = millis();
}

static void _mergeSessionsVariant(JsonVariant v, TamaState* out) {
  if (v.isNull()) return;
  if (v.is<JsonObject>()) _mergeSessionObject(v.as<JsonObject>(), out);
  else if (v.is<JsonArray>()) _mergeSessionsArray(v.as<JsonArray>(), out);
}

static void _mergeSessionFieldsFromDoc(JsonDocument& doc, TamaState* out) {
  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) return;
  _mergeSessionObject(root, out);
  static const char* const WRAP[] = {
      "data", "payload", "state", "metrics", "hud", "buddy_state", nullptr};
  for (const char* const* w = WRAP; *w; w++) {
    JsonObject sub = root[*w].as<JsonObject>();
    if (!sub.isNull()) _mergeSessionObject(sub, out);
  }
  _mergeSessionsVariant(root["sessions"], out);
  JsonObject s2 = root["snapshot"].as<JsonObject>();
  JsonObject s3 = root["buddy"].as<JsonObject>();
  JsonObject s4 = root["hardware_buddy"].as<JsonObject>();
  if (!s2.isNull()) {
    _mergeSessionObject(s2, out);
    for (const char* const* w = WRAP; *w; w++) {
      JsonObject sub = s2[*w].as<JsonObject>();
      if (!sub.isNull()) _mergeSessionObject(sub, out);
    }
    _mergeSessionsVariant(s2["sessions"], out);
  }
  if (!s3.isNull()) _mergeSessionObject(s3, out);
  if (!s4.isNull()) _mergeSessionObject(s4, out);
}

static uint32_t _tokensTripleFromObject(JsonObject o) {
  if (o.isNull()) return 0;
  JsonVariant tv = o["tokens"];
  if (tv.isNull()) tv = o["output_tokens"];
  if (tv.isNull()) tv = o["outputTokens"];
  if (tv.isNull()) return 0;
  unsigned long t64 = tv.as<unsigned long>();
  return (t64 > 4294967295UL) ? 4294967295U : (uint32_t)t64;
}

static void _considerTokensMax(JsonObject o, uint32_t* best) {
  if (o.isNull()) return;
  uint32_t t = _tokensTripleFromObject(o);
  if (t > *best) *best = t;
  static const char* const WRAP[] = {
      "data", "payload", "state", "metrics", "hud", "buddy_state", nullptr};
  for (const char* const* w = WRAP; *w; w++) {
    JsonObject sub = o[*w].as<JsonObject>();
    if (!sub.isNull()) _considerTokensMax(sub, best);
  }
}

static void _mergeTokensFromDoc(JsonDocument& doc) {
  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) return;
  uint32_t best = 0;
  _considerTokensMax(root, &best);
  JsonVariant sess = root["sessions"];
  if (sess.is<JsonObject>()) _considerTokensMax(sess.as<JsonObject>(), &best);
  JsonObject snap = root["snapshot"].as<JsonObject>();
  _considerTokensMax(snap, &best);
  _considerTokensMax(root["buddy"].as<JsonObject>(), &best);
  _considerTokensMax(root["hardware_buddy"].as<JsonObject>(), &best);
  if (best == 0) return;
  statsOnBridgeTokens(best);
  if (_haveBridgeTokens && best > _bridgeTokensSeen) _lastOutputActivityMs = millis();
  _haveBridgeTokens = true;
  _bridgeTokensSeen = best;
}

static void _noteTokensTodayDelta(uint32_t v) {
  if (!_haveTokensTodaySeen) {
    _haveTokensTodaySeen = true;
    _tokensTodaySeen = v;
    return;
  }
  if (v > _tokensTodaySeen) _lastOutputActivityMs = millis();
  _tokensTodaySeen = v;
}

#if defined(BOARD_DISPLAY_ONLY)
static void _logSnapshotKeysThrottled(JsonObject o) {
  if (o.isNull()) return;
  static uint32_t last = 0;
  uint32_t m = millis();
  if (m < 20000 || m - last < 60000) return;
  if (o["total"].isNull() && o["running"].isNull() && o["msg"].isNull() &&
      o["entries"].isNull() && o["snapshot"].isNull())
    return;
  last = m;
  Serial.print("[data] snapshot keys:");
  for (JsonPair kv : o) {
    Serial.printf(" %s", kv.key().c_str());
  }
  Serial.println();
}
#endif

static void _applyJson(const char* line, TamaState* out) {
  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, line);
  if (jerr) {
    static uint32_t lastJsonErrMs = 0;
    uint32_t m = millis();
    if (m - lastJsonErrMs > 4000) {
      lastJsonErrMs = m;
      Serial.printf("[data] json err %s len=%u\n", jerr.c_str(),
                    (unsigned)strlen(line));
    }
    return;
  }
  if (xferCommand(doc)) { _lastLiveMs = millis(); return; }

  // Bridge sends {"time":[epoch_sec, tz_offset_sec]}; gmtime_r on the
  // adjusted epoch yields local components including weekday.
  JsonArray t = doc["time"];
  if (!t.isNull() && t.size() == 2) {
    uint32_t local =
        t[0].as<uint32_t>() + (uint32_t)(int32_t)t[1].as<int32_t>();
    boardRtcSyncFromBridge(local);
    extern uint32_t _clkLastRead;
    _clkLastRead = 0;
    _rtcValid = true;
    _lastLiveMs = millis();
    return;
  }

  const char* evt = doc["evt"];
  if (evt && strcmp(evt, "turn") == 0) {
    _lastOutputActivityMs = millis();
    _lastLiveMs = millis();
    return;
  }

  _mergeSessionFieldsFromDoc(doc, out);
  out->recentlyCompleted = doc["completed"] | false;
  _mergeTokensFromDoc(doc);

  JsonObject rootObj = doc.as<JsonObject>();
  JsonObject snap = rootObj["snapshot"];
  uint32_t tt = out->tokensToday;
  auto foldTokensToday = [&](JsonObject o) {
    if (o.isNull()) return;
    auto bump = [&](uint32_t x) {
      if (x > tt) tt = x;
    };
    if (!o["tokens_today"].isNull()) bump(o["tokens_today"].as<uint32_t>());
    static const char* const WRAP[] = {
        "data", "payload", "state", "metrics", "hud", "buddy_state", nullptr};
    for (const char* const* w = WRAP; *w; w++) {
      JsonObject sub = o[*w].as<JsonObject>();
      if (!sub.isNull() && !sub["tokens_today"].isNull())
        bump(sub["tokens_today"].as<uint32_t>());
    }
  };
  foldTokensToday(rootObj);
  if (!snap.isNull()) {
    foldTokensToday(snap);
    out->recentlyCompleted = snap["completed"] | out->recentlyCompleted;
  }
  {
    JsonVariant sess = rootObj["sessions"];
    if (sess.is<JsonObject>()) foldTokensToday(sess.as<JsonObject>());
    else if (sess.is<JsonArray>()) {
      for (JsonVariant item : sess.as<JsonArray>()) {
        if (item.is<JsonObject>()) foldTokensToday(item.as<JsonObject>());
      }
    }
  }
  foldTokensToday(rootObj["buddy"].as<JsonObject>());
  foldTokensToday(rootObj["hardware_buddy"].as<JsonObject>());
  out->tokensToday = tt;
  _noteTokensTodayDelta(tt);
  const char* m = doc["msg"];
  if ((!m || !m[0]) && !snap.isNull()) m = snap["msg"];
  if (m) {
    if (strstr(m, "generat") || strstr(m, "thinking") || strstr(m, "stream") ||
        strstr(m, "Respond") || strstr(m, "respond"))
      _lastOutputActivityMs = millis();
    if (strncmp(out->msg, m, sizeof(out->msg)) != 0) _lastOutputActivityMs = millis();
    strncpy(out->msg, m, sizeof(out->msg)-1);
    out->msg[sizeof(out->msg)-1] = 0;
  }
  JsonArray la = doc["entries"];
  if (la.isNull() && !snap.isNull()) la = snap["entries"];
  if (!la.isNull()) {
    uint8_t n = 0;
    for (JsonVariant v : la) {
      if (n >= 8) break;
      const char* s = v.as<const char*>();
      strncpy(out->lines[n], s ? s : "", 91); out->lines[n][91]=0;
      n++;
    }
    if (n != out->nLines || (n > 0 && strcmp(out->lines[n-1], out->msg) != 0)) {
      out->lineGen++;
      _lastOutputActivityMs = millis();
    }
    out->nLines = n;
  }
  JsonObject pr = doc["prompt"];
  if (pr.isNull() && !snap.isNull()) pr = snap["prompt"];
  if (!pr.isNull()) {
    const char* pid = pr["id"]; const char* pt = pr["tool"]; const char* ph = pr["hint"];
    strncpy(out->promptId,   pid ? pid : "", sizeof(out->promptId)-1);   out->promptId[sizeof(out->promptId)-1]=0;
    strncpy(out->promptTool, pt  ? pt  : "", sizeof(out->promptTool)-1); out->promptTool[sizeof(out->promptTool)-1]=0;
    strncpy(out->promptHint, ph  ? ph  : "", sizeof(out->promptHint)-1); out->promptHint[sizeof(out->promptHint)-1]=0;
  } else {
    out->promptId[0] = 0; out->promptTool[0] = 0; out->promptHint[0] = 0;
  }
  out->lastUpdated = millis();
  _lastLiveMs = millis();
#if defined(BOARD_DISPLAY_ONLY)
  _logSnapshotKeysThrottled(doc.as<JsonObject>());
#endif
}

// Desktop may send {"evt":"turn",...} lines up to ~4KB (REFERENCE.md). A 2K buffer
// truncates before '\n' → garbage parse or stale line glue — `live` never flips.
template<size_t N>
struct _LineBuf {
  char buf[N];
  uint16_t len = 0;
  bool overflow = false;

  void resetLine() {
    len = 0;
    overflow = false;
  }

  void consumeChar(char c, TamaState* out, const char* tag) {
    if (c == '\n' || c == '\r') {
      if (overflow) {
        static uint32_t lastOvMs = 0;
        uint32_t m = millis();
        if (m - lastOvMs > 4000) {
          lastOvMs = m;
          Serial.printf("[data] %s json line overflow (max %u bytes)\n", tag,
                        (unsigned)(N - 1));
        }
      } else if (len > 0) {
        buf[len] = 0;
        if (buf[0] == '{') _applyJson(buf, out);
      }
      resetLine();
      return;
    }
    if (overflow) return;
    if (len < N - 1) {
      buf[len++] = c;
    } else {
      overflow = true;
    }
  }

  void feed(Stream& s, TamaState* out, const char* tag = "usb") {
    while (s.available()) {
      char c = s.read();
      consumeChar(c, out, tag);
    }
  }
};

static _LineBuf<5120> _usbLine, _btLine;

inline void dataPoll(TamaState* out) {
  uint32_t now = millis();

  if (_demoMode) {
    if (now >= _demoNext) { _demoIdx = (_demoIdx + 1) % 5; _demoNext = now + 8000; }
    const _Fake& s = _FAKES[_demoIdx];
    out->sessionsTotal=s.t; out->sessionsRunning=s.r; out->sessionsWaiting=s.w;
    out->recentlyCompleted=s.c; out->tokensToday=s.tok; out->lastUpdated=now;
    out->connected = true;
    snprintf(out->msg, sizeof(out->msg), "demo: %s", s.n);
    return;
  }

  _usbLine.feed(Serial, out, "usb");
  // BLE ring buffer is drained manually since it's not a Stream.
  while (bleAvailable()) {
    int c = bleRead();
    if (c < 0) break;
    _lastBtByteMs = millis();
    _btLine.consumeChar((char)c, out, "bt");
  }

  out->connected = dataConnected();
  if (!out->connected) {
    out->sessionsTotal=0; out->sessionsRunning=0; out->sessionsWaiting=0;
    out->recentlyCompleted=false; out->lastUpdated=now;
    strncpy(out->msg, "No Claude connected", sizeof(out->msg)-1);
    out->msg[sizeof(out->msg)-1]=0;
    _haveBridgeTokens = false;
    _bridgeTokensSeen = 0;
    _haveTokensTodaySeen = false;
    _tokensTodaySeen = 0;
    _lastOutputActivityMs = 0;
  }
}
