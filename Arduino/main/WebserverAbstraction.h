#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  WebserverAbstraction.h
//  HTTP server for Arduino Opta WiFi running as an Access Point.
//  Call begin() after WiFi.beginAP() succeeds, then call update() every loop.
//  All I/O data is exposed via getters/setters — no business logic here.
// ─────────────────────────────────────────────────────────────────────────────

#define HOME_PATH "/"
#define API_PATH "/api"
#define TIME_PATH "/api/time"
#define HTTP_GET "GET"
#define HTTP_POST "POST"

#define MAX_METHOD_LEN 16
#define MAX_PATH_LEN 128
#define STATUS_MSG_COUNT 10
#define STATUS_MSG_LEN 41    // 40 chars + null
#define STATUS_SHORT_LEN 21  // 20 chars + null

#define SENSOR_PATH "/api/sensor"
#define SENSOR_STRING_LEN 64

#define POWER_MAX 340
#define LEVEL_MAX 100

#define CLIENT_TIMEOUT_MS 2000UL
#define HTML_CHUNK_SIZE 256  // bytes sent per update() call when streaming HTML

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <ArduinoJson.h>
// NOTE: avr/pgmspace.h removed — Opta is ARM Cortex-M7, not AVR.
//       HTML strings are stored in regular RAM/flash via the linker (no PROGMEM needed).

// ─────────────────────────────────────────────────────────────────────────────
//  HTML pages
//  On Opta (ARM) there is no Harvard architecture — strings live in flash
//  automatically via the linker. No PROGMEM / pgm_read_byte needed.
//  The literal %BASE% token is substituted at stream time with "http://<ip>:<port>".
// ─────────────────────────────────────────────────────────────────────────────

static const char CPU_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='UTF-8'/>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
  "<title>Status</title>"
  "<style>"
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{font-family:monospace;font-size:14px;color:#111;background:#fff;padding:12px;max-width:480px}"
  "a{color:inherit;font-size:12px;display:inline-block;margin-bottom:14px;text-decoration:underline}"
  "h2{font-size:15px;font-weight:500;margin-bottom:10px;padding-bottom:6px;border-bottom:1px solid #ddd}"
  ".g{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:16px}"
  ".c{background:#f5f5f5;border-radius:6px;padding:10px 12px}"
  ".n{font-size:26px;font-weight:500}"
  ".a{font-size:14px}"
  ".u{font-size:11px;color:#888;margin-top:2px}"
  ".bw{height:3px;background:#ddd;border-radius:2px;margin-top:8px}"
  ".b{height:3px;border-radius:2px;background:#111;width:0%;transition:width .4s}"
  ".ms{margin-bottom:0}"
  ".m{padding:5px 0;border-bottom:1px solid #eee;font-size:13px;color:#555;"
  "white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
  ".m:last-child{border-bottom:none}"
  "#md{display:flex;flex-direction:row;flex-wrap:nowrap;justify-content:space-around}"
  "#md div{display:flex}"
  "@media(prefers-color-scheme:dark){"
  "body{background:#111;color:#eee}"
  "h2{border-color:#2a2a2a}"
  ".c{background:#1e1e1e}"
  ".b{background:#eee}.bw{background:#333}"
  ".m{border-color:#2a2a2a;color:#aaa}"
  "}"
  "</style></head><body>"
  "<a href='%BASE%/?hmi'>Zur Steuerung</a>"
  "<h2>Status</h2>"
  "<div id='md'>"
  "<div><p>CPU wert:</p><span id='s'>-</span></div>"
  "<div><p>Steuerwert:</p><span id='t'>-</span></div>"
  "</div>"
  "<div class='g'>"
  "<div class='c'>"
  "<div style='font-size:11px;color:#888'>Leistung Aktuell</div>"
  "<div class='n' id='pwr'>-</div>"
  "<div style='font-size:11px;color:#636363'>Leistung Sollwert</div>"
  "<div class='a' id='pwrSP'>-</div>"
  "<div class='u'>W</div>"
  "<div class='bw'><div class='b' id='pwrB'></div></div>"
  "</div>"
  "<div class='c'>"
  "<div style='font-size:11px;color:#888'>Pegel Aktuell</div>"
  "<div class='n' id='lvl'>-</div>"
  "<div style='font-size:11px;color:#888'>Pegel Sollwert</div>"
  "<div class='a' id='lvlSP'>-</div>"
  "<div class='u'>cm</div>"
  "<div class='bw'><div class='b' id='lvlB'></div></div>"
  "</div>"
  "</div>"
  "<h2>Log</h2>"
  "<div class='ms' id='msgs'></div>"
  "<script>"
  "const B='%BASE%';"
  "async function p(){"
  "try{"
  "const r=await fetch(B+'/api');if(!r.ok)return;const d=await r.json();"
  "document.getElementById('pwr').textContent=d.power??'-';"
  "document.getElementById('lvl').textContent=d.level??'-';"
  "document.getElementById('pwrSP').textContent=d.powerSetpoint??'-';"
  "document.getElementById('lvlSP').textContent=d.levelSetpoint??'-';"
  "document.getElementById('pwrB').style.width=(((d.power??0)/340)*100)+'%';"
  "document.getElementById('lvlB').style.width=(((d.level??0)/300)*100)+'%';"
  "document.getElementById('s').textContent=d.statusShort||'-';"
  "document.getElementById('t').textContent=d.statusShortSetpoint||'-';"
  "const el=document.getElementById('msgs');el.innerHTML='';"
  "(d.statusMessages||[]).forEach(m=>{"
  "const v=document.createElement('div');v.className='m';v.textContent=m||'-';el.appendChild(v);"
  "});"
  "}catch(e){}"
  "}"
  "setInterval(p,2000);p();"
  "</script></body></html>";

static const char HMI_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='UTF-8'/>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
  "<title>Steuerung</title>"
  "<style>"
  "*{box-sizing:border-box;margin:0;padding:0}"
  /*"body{font-family:monospace;font-size:14px;color:#111;background:#fff;padding:12px;max-width:480px}"
  "a{color:inherit;font-size:12px;display:inline-block;margin-bottom:14px;text-decoration:underline}"
  "h2{font-size:15px;font-weight:500;margin-bottom:10px;padding-bottom:6px;border-bottom:1px solid #ddd}"
  ".mo{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-bottom:16px}"
  "button{font-family:monospace;font-size:13px;padding:10px 6px;border:1px solid #ccc;"
  "background:transparent;color:inherit;border-radius:6px;cursor:pointer;width:100%;text-align:center}"*/
  "button.on{background:#111;color:#fff;border-color:#111}"
  ".sp{display:none;flex-direction:column;gap:6px;margin-bottom:16px}"
  ".sp.show{display:flex}"
  ".st{display:flex;justify-content:space-between;font-size:12px;color:#888}"
  ".st span{color:inherit;font-weight:500}"
  "input[type=range]{width:100%;accent-color:#111}"
  ".tb{width:100%;padding:10px;font-size:13px;font-family:monospace;"
  "border:1px solid #ccc;background:transparent;color:inherit;border-radius:6px;cursor:pointer;margin-top:6px}"
  ".ti{font-size:11px;color:#888;margin-top:6px;text-align:center}"
  "@media(prefers-color-scheme:dark){"
  "body{background:#111;color:#eee}"
  "h2{border-color:#2a2a2a}"
  "button{border-color:#444}"
  "button.on{background:#eee;color:#111;border-color:#eee}"
  "input[type=range]{accent-color:#eee}"
  ".tb{border-color:#444}"
  "}"
  "</style></head><body>"
  "<a href='%BASE%/'>Zur Anlagenübersicht</a>"
  "<h2>Modus</h2>"
  "<div class='mo'>"
  "<button id='b1' onclick='setMode(1)'>Stop</button>"
  "<button id='b6' onclick='setMode(6)'>Speicherbetrieb</button>"
  "<button id='b2' onclick='setMode(2)'>Leistungsregelung</button>"
  "<button id='b3' onclick='setMode(3)'>Pegelregelung</button>"
  "<button id='b4' onclick='setMode(4)'>Leistungsregelung (Nacht)</button>"
  "<button id='b5' onclick='setMode(5)'>Pegelregelung (Nacht)</button>"
  "</div>"
  "<div class='sp' id='sp'>"
  "<div class='st' id='sl'>&nbsp;<span id='sv'>0</span><span id='su'></span></div>"
  "<input type='range' id='sr' min='0' max='340' value='0' step='1'"
  " oninput=\"document.getElementById('sv').textContent=this.value\""
  " onchange='send()'/>"
  "</div>"
  "<h2>Zeit</h2>"
  "<button class='tb' onclick='syncTime()'>Zeit synchronisieren</button>"
  "<div class='ti' id='ti'>nicht synchronisiert</div>"
  "<h2>Fehler</h2>"
  "<button class='tb' onclick='errorAck()'>Fehler quittieren</button>"
  "<script>"
  "const PM=new Set([2,4]),LM=new Set([3,5]),SM=new Set([2,3,4,5]);"
  "let mode=0,ackErrors=0;"
  "function errorAck(){ackErrors=1;send();}"
  "function applySlider(m,d){"
  "const sp=document.getElementById('sp'),"
  "sl=document.getElementById('sl'),"
  "sr=document.getElementById('sr'),"
  "su=document.getElementById('su');"
  "if(!SM.has(m)){sp.classList.remove('show');return;}"
  "sp.classList.add('show');"
  "if(PM.has(m)){"
  "sl.childNodes[0].textContent='Leistung Sollwert ';"
  "su.textContent=' W';sr.max='340';"
  "sr.value=Math.min(d&&d.powerSetpoint!=null?d.powerSetpoint:0,340);"
  "}else{"
  "sl.childNodes[0].textContent='Pegel Sollwert ';"
  "su.textContent=' %';sr.max='100';"
  "sr.value=Math.min(d&&d.levelSetpoint!=null?d.levelSetpoint:0,300);"
  "}"
  "document.getElementById('sv').textContent=sr.value;"
  "}"
  "async function setMode(m){"
  "mode=m;"
  "for(let i=1;i<=6;i++)document.getElementById('b'+i).className=i===m?'on':'';"
  "if(SM.has(m)){try{const r=await fetch('%BASE%/api');"
  "applySlider(m,r.ok?await r.json():{});}catch(e){applySlider(m,{});}}"
  "else applySlider(m,{});"
  "send();}"
  "async function send(){"
  "const sp=parseInt(document.getElementById('sr').value);"
  "try{await fetch('%BASE%/api',{method:'POST',"
  "headers:{'Content-Type':'application/json'},"
  "body:JSON.stringify({mode,"
  "...(PM.has(mode)&&{powerSetpoint:sp}),"
  "...(LM.has(mode)&&{levelSetpoint:sp}),"
  "ackErrors})});}catch(e){}"
  "ackErrors=0;}"
  "async function syncTime(){"
  "const e=Math.floor(Date.now()/1000);"
  "try{await fetch('%BASE%/api/time',{method:'POST',"
  "headers:{'Content-Type':'application/json'},"
  "body:JSON.stringify({epoch:e})});"
  "document.getElementById('ti').textContent="
  "'synchronisiert: '+new Date(e*1000).toLocaleTimeString();"
  "}catch(e){}}"
  "async function poll(){"
  "try{const r=await fetch('%BASE%/api');if(!r.ok)return;"
  "const d=await r.json();"
  "if(document.activeElement.id!=='sr'){"
  "const m=d.mode??1;"
  "if(m!==mode)await setMode(m);else applySlider(m,d);"
  "}"
  "document.getElementById('ti').textContent= new Date(d.currentTime*1000).toLocaleString();"
  "}catch(e){}}"
  "setInterval(poll,4000);poll();"
  "</script></body></html>";

// ─────────────────────────────────────────────────────────────────────────────
//  Control mode enum
// ─────────────────────────────────────────────────────────────────────────────
enum class ControlMode : uint8_t {
  UNKNOWN = 0,
  STOP = 1,
  CONSTANT_POWER = 2,
  CONSTANT_LEVEL = 3,
  CONSTANT_POWER_NIGHT = 4,
  CONSTANT_LEVEL_NIGHT = 5,
  FILLING = 6
};

// ─────────────────────────────────────────────────────────────────────────────
//  DeviceState  — single source of truth, owned by the library
// ─────────────────────────────────────────────────────────────────────────────
struct DeviceState {
  char statusMessages[STATUS_MSG_COUNT][STATUS_MSG_LEN];
  float power;
  float level;
  char statusShort[STATUS_SHORT_LEN];
  char statusShortSetpoint[STATUS_SHORT_LEN];

  ControlMode mode;
  float powerSetpoint;
  float levelSetpoint;
  uint32_t clientEpoch;
  bool timeUpdated;
  int ackErrors;

  // Sensor endpoint
  float sensorTemperature;
  bool msgSendFlag;        // set externally → sent in next response, then auto-cleared
  char msgString[SENSOR_STRING_LEN];

  DeviceState()
    : power(0), level(0),
      mode(ControlMode::UNKNOWN),
      powerSetpoint(0), levelSetpoint(0),
      clientEpoch(0), timeUpdated(false), ackErrors(0),
      sensorTemperature(0.0f),
      msgSendFlag(false) {
    for (int i = 0; i < STATUS_MSG_COUNT; i++) statusMessages[i][0] = '\0';
    statusShort[0] = '\0';
    statusShortSetpoint[0] = '\0';
    msgString[0] = '\0';
  };
};

// ─────────────────────────────────────────────────────────────────────────────
//  WebserverAbstraction
// ─────────────────────────────────────────────────────────────────────────────
class WebserverAbstraction {
public:

  WebserverAbstraction(IPAddress ip, uint16_t port)
    : _ip(ip), _port(port), _server(port),
      _clientActive(false), _connState(ConnState::IDLE),
      _contentLength(0), _htmlProgmem(nullptr), _htmlLen(0), _htmlSent(0) {}

  void begin() {
    _server.begin();
    Serial.println("[WS] Server started on " + _ip.toString() + ":" + String(_port));
  }

  void update() {
    if (!_clientActive) {
      _client = _server.available();
      if (!_client) return;
      _clientActive = true;
      _connState = ConnState::READING_HEADER;
      _headerBuf = "";
      _body = "";
      _contentLength = 0;
      _deadline = millis() + CLIENT_TIMEOUT_MS;
      _htmlProgmem = nullptr;
      _htmlSent = 0;
    }

    if (millis() > _deadline) {
      Serial.println("[WS] Client timeout");
      _closeClient();
      return;
    }

    switch (_connState) {

      case ConnState::READING_HEADER:
        while (_client.available()) {
          _headerBuf += (char)_client.read();
          if (_headerBuf.endsWith("\r\n\r\n")) {
            _parseHeader();
            _connState = (_contentLength > 0) ? ConnState::READING_BODY
                                              : ConnState::DISPATCHING;
            break;
          }
        }
        break;

      case ConnState::READING_BODY:
        while (_client.available() && (int)_body.length() < _contentLength)
          _body += (char)_client.read();
        if ((int)_body.length() >= _contentLength)
          _connState = ConnState::DISPATCHING;
        break;

      case ConnState::DISPATCHING:
        _dispatch();
        break;

      case ConnState::STREAMING_HTML:
        _streamHtmlChunk();
        break;

      case ConnState::DONE:
        _closeClient();
        break;

      case ConnState::IDLE:
      default:
        break;
    }
  }

  // ── SETTERS ───────────────────────────────────────────────────────────────

  void setStatusMessage(uint8_t index, const char* msg) {
    if (index >= STATUS_MSG_COUNT) return;
    strncpy(_sd.statusMessages[index], msg, STATUS_MSG_LEN - 1);
    _sd.statusMessages[index][STATUS_MSG_LEN - 1] = '\0';
  }

  void pushStatusMessage(const char* msg) {
    for (int i = 0; i < STATUS_MSG_COUNT - 1; i++)
      memcpy(_sd.statusMessages[i], _sd.statusMessages[i + 1], STATUS_MSG_LEN);
    strncpy(_sd.statusMessages[STATUS_MSG_COUNT - 1], msg, STATUS_MSG_LEN - 1);
    _sd.statusMessages[STATUS_MSG_COUNT - 1][STATUS_MSG_LEN - 1] = '\0';
  }

  void setPower(float v) {
    _sd.power = constrain(v, 0, POWER_MAX);
  }
  void setLevel(float v) {
    _sd.level = constrain(v, 0, LEVEL_MAX);
  }

  void setStatusShort(const char* s) {
    strncpy(_sd.statusShort, s, STATUS_SHORT_LEN - 1);
    _sd.statusShort[STATUS_SHORT_LEN - 1] = '\0';
  }

  void setStatusShortSetpoint(const char* s) {
    strncpy(_sd.statusShortSetpoint, s, STATUS_SHORT_LEN - 1);
    _sd.statusShortSetpoint[STATUS_SHORT_LEN - 1] = '\0';
  }

  // ── GETTERS ───────────────────────────────────────────────────────────────

  bool getAckErrors() const {
    return _sd.ackErrors == 1;
  }
  void resetAck() {
    _sd.ackErrors = 0;
  }

  ControlMode getMode() const {
    return _sd.mode;
  }
  
  ControlMode setMode(ControlMode nextt) {
    _sd.mode = nextt;
    return _sd.mode;
  }

  float getPowerSetpoint() const {
    return _sd.powerSetpoint;
  }
  int getLevelSetpoint() const {
    return (int)_sd.levelSetpoint;
  }
  uint32_t getClientEpoch() const {
    return _sd.clientEpoch;
  }
  float getPower() const {
    return _sd.power;
  }
  float getLevel() const {
    return _sd.level;
  }
  const char* getStatusShort() const {
    return _sd.statusShort;
  }
  const char* getStatusShortSetpoint() const {
    return _sd.statusShortSetpoint;
  }
  const char* getStatusMessage(uint8_t i) const {
    return (i < STATUS_MSG_COUNT) ? _sd.statusMessages[i] : "";
  }

  bool hasNewClientTime() {
    if (_sd.timeUpdated) {
      _sd.timeUpdated = false;
      return true;
    }
    return false;
  }

  const DeviceState& getState() const {
    return _sd;
  }

  // Sensor temperature — written by the remote sensor via POST /api/sensor
  float getTemp() const {
    return _sd.sensorTemperature;
  }



  // Send flag + string — set externally, consumed on next sensor poll, then cleared
  void setMessage(const char* msg, bool activate = true) {
    strncpy(_sd.msgString, msg, SENSOR_STRING_LEN - 1);
    _sd.msgString[SENSOR_STRING_LEN - 1] = '\0';
    _sd.msgSendFlag = activate;
  }
  void clearMessage() {
    _sd.msgSendFlag = false;
    _sd.msgString[0] = '\0';
  }
  bool getmsgSendFlag() const {
    return _sd.msgSendFlag;
  }
  const char* getSensorString() const {
    return _sd.msgString;
  }

  // ─────────────────────────────────────────────────────────────────────────────
private:
  // ─────────────────────────────────────────────────────────────────────────────

  enum class ConnState : uint8_t {
    IDLE,
    READING_HEADER,
    READING_BODY,
    DISPATCHING,
    STREAMING_HTML,
    DONE
  };

  IPAddress _ip;
  uint16_t _port;
  WiFiServer _server;
  WiFiClient _client;
  bool _clientActive;

  ConnState _connState;
  String _headerBuf;
  String _body;
  String _methodStr;
  String _pathStr;
  int _contentLength;
  unsigned long _deadline;

  // HTML streaming (plain pointer — no PROGMEM on ARM)
  const char* _htmlProgmem;  // name kept for minimal diff; points to regular char[]
  size_t _htmlLen;
  size_t _htmlSent;
  String _base;  // "http://<ip>:<port>", built once per request

  DeviceState _sd;
  StaticJsonDocument<1024> _res;
  StaticJsonDocument<256> _req;

  // ── Parse method, path, Content-Length from buffered header ──────────────
  void _parseHeader() {
    int sp1 = _headerBuf.indexOf(' ');
    int sp2 = _headerBuf.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) {
      _connState = ConnState::DONE;
      return;
    }

    _methodStr = _headerBuf.substring(0, sp1);
    _pathStr = _headerBuf.substring(sp1 + 1, sp2);

    int clIdx = _headerBuf.indexOf("Content-Length: ");
    _contentLength = (clIdx >= 0)
                       ? _headerBuf.substring(clIdx + 16, _headerBuf.indexOf('\r', clIdx)).toInt()
                       : 0;

    Serial.println("[WS] " + _methodStr + " " + _pathStr);
  }

  void _dispatch() {
    _base = "http://" + _ip.toString() + ":" + String(_port);

    // ── POST /api/sensor ───────────────────────────────────────────────────────
    if (_pathStr == SENSOR_PATH) {
      if (_methodStr == HTTP_POST) _handleESPPost();
      else _badRequest();
      _connState = ConnState::DONE;

      // ── /api/time ──────────────────────────────────────────────────────────────
    } else if (_pathStr == TIME_PATH) {
      if (_methodStr == HTTP_POST) _handleTimePost();
      else _badRequest();
      _connState = ConnState::DONE;

      // ── /api ───────────────────────────────────────────────────────────────────
    } else if (_pathStr == API_PATH) {
      if (_methodStr == HTTP_GET) {
        _sendApiState();
      } else if (_methodStr == HTTP_POST) {
        if (_body.length()) _parseControlRequest();
        _sendApiState();
      } else {
        _badRequest();
      }
      _connState = ConnState::DONE;

      // ── / (HMI or status page) ─────────────────────────────────────────────────
    } else if (_pathStr == "/" || _pathStr.startsWith("/?")) {
      if (_methodStr == HTTP_GET) {
        _beginHtmlResponse(_pathStr.indexOf("hmi") >= 0);
        // _connState → STREAMING_HTML set inside _beginHtmlResponse
      } else {
        _badRequest();
        _connState = ConnState::DONE;
      }

      // ── 404 ────────────────────────────────────────────────────────────────────
    } else {
      _notFound();
      _connState = ConnState::DONE;
    }
  }

  // ── POST /api/sensor ──────────────────────────────────────────────────────────
  // Request  (JSON): { "temperature": 23.4 }
  // Response (JSON): { "power": 120.0, "sendFlag": true, "message": "hello" }
  //                  sendFlag + message are cleared after sending.
  void _handleESPPost() {
    // Parse incoming temperature
    _req.clear();
    Serial.println("API request");
    DeserializationError err = deserializeJson(_req, _body);
    if (err) {
      Serial.println("[WS] Sensor JSON err: " + String(err.f_str()));
      _badRequest();
      return;
    }

    if (_req.containsKey("temperature"))
      _sd.sensorTemperature = (float)_req["temperature"];

    // Build response
    _res.clear();
    _res["power"] = _sd.power;
    _res["level"] = _sd.level;
    _res["sendFlag"] = _sd.msgSendFlag;
    _res["message"] = _sd.msgSendFlag ? _sd.msgString : "";
    _res["status"] = _sd.statusShort;

    // Auto-clear flag + string after sending
    if (_sd.msgSendFlag) {
      _sd.msgSendFlag = false;
      _sd.msgString[0] = '\0';
    }

    String body;
    serializeJson(_res, body);

    _client.print("HTTP/1.1 200 OK\r\nConnection: close\r\n"
                  "Content-Type: application/json\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Content-Length: ");
    _client.print(body.length() + 1);
    _client.print("\r\n\r\n");
    _client.println(body);
  }

  unsigned long getTime() {
    time_t seconds = time(NULL);
    return (unsigned int)seconds;
  }

  // ── POST /api/time ────────────────────────────────────────────────────────
  void _handleTimePost() {
    _req.clear();
    if (!deserializeJson(_req, _body) && _req.containsKey("epoch")) {
      _sd.clientEpoch = (uint32_t)_req["epoch"];
      _sd.timeUpdated = true;
    }
    _client.print(
      "HTTP/1.1 200 OK\r\n"
      "Connection: close\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: 15\r\n"
      "\r\n"
      "{\"status\":\"ok\"}\n");
  }

  // ── Parse POST /api ───────────────────────────────────────────────────────
  void _parseControlRequest() {
    _req.clear();
    DeserializationError err = deserializeJson(_req, _body);
    if (err) {
      Serial.println("[WS] JSON err: " + String(err.f_str()));
      return;
    }

    if (_req.containsKey("mode"))
      _sd.mode = (ControlMode)constrain((uint8_t)_req["mode"], 0, 6);
    if (_req.containsKey("powerSetpoint"))
      _sd.powerSetpoint = constrain((float)_req["powerSetpoint"], 0.0f, (float)POWER_MAX);
    if (_req.containsKey("levelSetpoint"))
      _sd.levelSetpoint = constrain((int)_req["levelSetpoint"], 0, (int)LEVEL_MAX);
    if (_req.containsKey("ackErrors"))
      _sd.ackErrors = constrain((int)_req["ackErrors"], 0, 2);

    Serial.println("[WS] mode=" + String((int)_sd.mode)
                   + " pwr=" + String(_sd.powerSetpoint)
                   + " lvl=" + String(_sd.levelSetpoint)
                   + " ack=" + String(_sd.ackErrors));
  }

  // ── GET /api — send JSON state ────────────────────────────────────────────
  void _sendApiState() {
   
    _res.clear();
    JsonArray msgs = _res.createNestedArray("statusMessages");
    for (int i = 0; i < STATUS_MSG_COUNT; i++) msgs.add(_sd.statusMessages[i]);
    _res["power"] = _sd.power;
    _res["level"] = _sd.level;
    _res["statusShort"] = _sd.statusShort;
    _res["statusShortSetpoint"] = _sd.statusShortSetpoint;
    _res["mode"] = (uint8_t)_sd.mode;
    _res["powerSetpoint"] = _sd.powerSetpoint;
    _res["levelSetpoint"] = _sd.levelSetpoint;
    _res["currentTime"] = getTime();

    String body;
    serializeJson(_res, body);

    _client.print("HTTP/1.1 200 OK\r\nConnection: close\r\n"
                  "Content-Type: application/json\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Content-Length: ");
    _client.print(body.length() + 1);
    _client.print("\r\n\r\n");
    _client.println(body);
  }

  // ── Send HTTP header then switch to STREAMING_HTML state ─────────────────
  void _beginHtmlResponse(bool isHmi) {
    _htmlProgmem = isHmi ? HMI_HTML : CPU_HTML;
    _htmlLen = strlen(_htmlProgmem);  // plain strlen — no PROGMEM on ARM
    _htmlSent = 0;

    // Count %BASE% occurrences to calculate exact Content-Length
    const size_t TOKEN_LEN = 6;  // strlen("%BASE%")
    size_t occ = 0;
    for (size_t i = 0; i + TOKEN_LEN <= _htmlLen; i++) {
      if (_htmlProgmem[i] == '%' && _htmlProgmem[i + 1] == 'B' && _htmlProgmem[i + 2] == 'A' && _htmlProgmem[i + 3] == 'S' && _htmlProgmem[i + 4] == 'E' && _htmlProgmem[i + 5] == '%') {
        occ++;
      }
    }
    size_t bodyLen = _htmlLen + occ * (_base.length() - TOKEN_LEN) + 1;  // +1 trailing \n

    _client.print("HTTP/1.1 200 OK\r\nConnection: close\r\n"
                  "Content-Type: text/html; charset=utf-8\r\n"
                  "Content-Length: ");
    _client.print(bodyLen);
    _client.print("\r\n\r\n");

    _connState = ConnState::STREAMING_HTML;
  }

  // ── Stream next chunk, substituting %BASE% inline ─────────────────────────
  // Direct array indexing replaces pgm_read_byte() — identical logic otherwise.
  void _streamHtmlChunk() {
    if (!_client.connected()) {
      _closeClient();
      return;
    }

    const size_t TOKEN_LEN = 6;
    int sent = 0;

    while (_htmlSent < _htmlLen && sent < HTML_CHUNK_SIZE) {
      if (_htmlLen - _htmlSent >= TOKEN_LEN && _htmlProgmem[_htmlSent] == '%' && _htmlProgmem[_htmlSent + 1] == 'B' && _htmlProgmem[_htmlSent + 2] == 'A' && _htmlProgmem[_htmlSent + 3] == 'S' && _htmlProgmem[_htmlSent + 4] == 'E' && _htmlProgmem[_htmlSent + 5] == '%') {
        _client.print(_base);
        _htmlSent += TOKEN_LEN;
        sent += _base.length();
      } else {
        _client.write(_htmlProgmem[_htmlSent++]);
        sent++;
      }
    }

    if (_htmlSent >= _htmlLen) {
      _client.print('\n');
      _connState = ConnState::DONE;
    }
  }

  // ── Helpers ───────────────────────────────────────────────────────────────
  void _badRequest() {
    _client.print("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
  }
  void _notFound() {
    _client.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
  }

  void _closeClient() {
    delay(1);
    _client.stop();
    _clientActive = false;
    _connState = ConnState::IDLE;
    _headerBuf = "";
    _body = "";
  }
};