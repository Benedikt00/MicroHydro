#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  WebserverAbstraction.h
//  Unified HTTP server for Arduino Opta (Ethernet) and ESP32 (WiFi).
//  All I/O data is exposed via getters/setters only — no business logic here.
// ─────────────────────────────────────────────────────────────────────────────

#define HOME_PATH    "/"
#define API_PATH     "/api"
#define TIME_PATH    "/api/time"

#define HTTP_GET     "GET"
#define HTTP_POST    "POST"

#define MAX_METHOD_LEN  16
#define MAX_PATH_LEN   2048

#define STATUS_MSG_COUNT   5
#define STATUS_MSG_LEN    41   // 40 chars + null
#define STATUS_SHORT_LEN  21   // 20 chars + null

// ── Platform detection ────────────────────────────────────────────────────────
#if defined(ARDUINO_OPTA) || defined(ARDUINO_PORTENTA_H7_M7)
  #define WS_PLATFORM_OPTA
  #include <Ethernet.h>
  #include <ArduinoHttpClient.h>
#elif defined(ESP32) || defined(ESP8266)
  #define WS_PLATFORM_ESP
  #include <WiFi.h>
  #include <WiFiClient.h>
  #include <WiFiServer.h>
#else
  #error "Unsupported platform. Add Ethernet or WiFi includes manually."
#endif

#include <Arduino_JSON.h>   // or ArduinoJson — swap as needed
#include <ArduinoJson.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Control mode enum
// ─────────────────────────────────────────────────────────────────────────────
enum class ControlMode : uint8_t {
    STOP            = 0,
    CONSTANT_POWER    = 1,
    CONSTANT_PRESSURE = 2
};

// ─────────────────────────────────────────────────────────────────────────────
//  Shared state struct — owned by the library, accessed via get/set API
// ─────────────────────────────────────────────────────────────────────────────
struct DeviceState {
    // ── Inputs (set by application, read by web UI) ───────────────────────
    char     statusMessages[STATUS_MSG_COUNT][STATUS_MSG_LEN];  // 5 × 40‑char lines
    int      power;                // measured power   (0–100)
    int      pressure;             // measured pressure (0–100)
    char     statusShort[STATUS_SHORT_LEN];                     // 20‑char status tag

    // ── Outputs (set by web UI, consumed by application) ─────────────────
    ControlMode mode;              // STOP / CONSTANT_POWER / CONSTANT_PRESSURE
    int      powerSetpoint;        // 0–100
    int      pressureSetpoint;     // 0–100
    uint32_t clientEpoch;          // Unix timestamp sent by the browser
    bool     timeUpdated;          // true once a new timestamp has arrived

    DeviceState() :
        power(0), pressure(0),
        mode(ControlMode::STOP),
        powerSetpoint(0), pressureSetpoint(0),
        clientEpoch(0), timeUpdated(false)
    {
        for (int i = 0; i < STATUS_MSG_COUNT; i++) statusMessages[i][0] = '\0';
        statusShort[0] = '\0';
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  WebserverAbstraction
// ─────────────────────────────────────────────────────────────────────────────
class WebserverAbstraction {
public:

    // ── Constructor (Opta) ────────────────────────────────────────────────
#ifdef WS_PLATFORM_OPTA
    WebserverAbstraction(OptaBoardInfo* info, IPAddress ip, uint16_t port)
        : _ip(ip), _port(port), _server(port)
    {
        if (info && info->magic == 0xB5) {
            Ethernet.begin(info->mac_address, ip);
        } else {
            // invalid board info — halt
            while (1) { delay(1000); }
        }
        _server.begin();
    }
#endif

    // ── Constructor (ESP32) ───────────────────────────────────────────────
#ifdef WS_PLATFORM_ESP
    WebserverAbstraction(IPAddress ip, uint16_t port)
        : _ip(ip), _port(port), _server(port)
    {
        // WiFi must already be connected before calling this constructor.
        _server.begin();
    }
#endif

    // ─────────────────────────────────────────────────────────────────────
    //  Call this every loop() iteration — non-blocking
    // ─────────────────────────────────────────────────────────────────────
    void update()
    {
#ifdef WS_PLATFORM_OPTA
        EthernetClient client = _server.available();
        if (!client) return;
        _handleClient(client);
#endif
#ifdef WS_PLATFORM_ESP
        WiFiClient client = _server.available();
        if (!client) return;
        _handleClient(client);
#endif
    }

    // ─────────────────────────────────────────────────────────────────────
    //  SETTERS — call from your application to push data to the UI
    // ─────────────────────────────────────────────────────────────────────

    /** Set one of the 5 status message lines (index 0–4, max 40 chars). */
    void setStatusMessage(uint8_t index, const char* msg) {
        if (index >= STATUS_MSG_COUNT) return;
        strncpy(_state.statusMessages[index], msg, STATUS_MSG_LEN - 1);
        _state.statusMessages[index][STATUS_MSG_LEN - 1] = '\0';
    }

    /** Set measured power reading (0–100). */
    void setPower(int value)    { _state.power    = constrain(value, 0, 100); }

    /** Set measured pressure reading (0–100). */
    void setPressure(int value) { _state.pressure = constrain(value, 0, 100); }

    /** Set the short status tag (max 20 chars). */
    void setStatusShort(const char* status) {
        strncpy(_state.statusShort, status, STATUS_SHORT_LEN - 1);
        _state.statusShort[STATUS_SHORT_LEN - 1] = '\0';
    }

    // ─────────────────────────────────────────────────────────────────────
    //  GETTERS — call from your application to consume UI commands
    // ─────────────────────────────────────────────────────────────────────

    ControlMode getMode()             const { return _state.mode; }
    int         getPowerSetpoint()    const { return _state.powerSetpoint; }
    int         getPressureSetpoint() const { return _state.pressureSetpoint; }

    /** Returns the Unix timestamp sent by the last browser time-sync, or 0. */
    uint32_t    getClientEpoch()      const { return _state.clientEpoch; }

    /**
     * Returns true once after a new client timestamp has been received.
     * Automatically resets the flag on read.
     */
    bool        hasNewClientTime() {
        if (_state.timeUpdated) { _state.timeUpdated = false; return true; }
        return false;
    }

    // Expose the full state struct for advanced use
    const DeviceState& getState() const { return _state; }

// ─────────────────────────────────────────────────────────────────────────────
private:
// ─────────────────────────────────────────────────────────────────────────────

    IPAddress  _ip;
    uint16_t   _port;
    DeviceState _state;

    // Reuse JSON documents
    StaticJsonDocument<512> _res;
    StaticJsonDocument<256> _req;

#ifdef WS_PLATFORM_OPTA
    EthernetServer _server;
    void _handleClient(EthernetClient& client)
#endif
#ifdef WS_PLATFORM_ESP
    WiFiServer _server;
    void _handleClient(WiFiClient& client)
#endif
    {
        String requestLine = "";
        char method[MAX_METHOD_LEN] = {0};
        char path[MAX_PATH_LEN]     = {0};

        unsigned long timeout = millis() + 2000;
        String headerBuf = "";

        // Read the full HTTP request headers into a buffer
        while (client.connected() && millis() < timeout) {
            if (client.available()) {
                char c = client.read();
                headerBuf += c;
                // End of headers
                if (headerBuf.endsWith("\r\n\r\n")) break;
            }
        }

        if (headerBuf.length() == 0) { client.stop(); return; }

        // Parse method and path from first line
        int sp1 = headerBuf.indexOf(' ');
        int sp2 = headerBuf.indexOf(' ', sp1 + 1);
        if (sp1 < 0 || sp2 < 0) { client.stop(); return; }

        String methodStr = headerBuf.substring(0, sp1);
        String pathStr   = headerBuf.substring(sp1 + 1, sp2);
        methodStr.toCharArray(method, MAX_METHOD_LEN);
        pathStr.toCharArray(path, MAX_PATH_LEN);

        // Determine Content-Length for POST body
        int contentLength = 0;
        int clIdx = headerBuf.indexOf("Content-Length: ");
        if (clIdx >= 0) {
            int clEnd = headerBuf.indexOf('\r', clIdx);
            contentLength = headerBuf.substring(clIdx + 16, clEnd).toInt();
        }

        // Read body if present
        String body = "";
        if (contentLength > 0) {
            timeout = millis() + 2000;
            while ((int)body.length() < contentLength && millis() < timeout) {
                if (client.available()) body += (char)client.read();
            }
        }

        IPAddress clientIP = client.remoteIP();
        Serial.println("[WS] " + methodStr + " " + pathStr +
                       " from " + clientIP.toString());

        // ── Route ────────────────────────────────────────────────────────
        if (strncmp(path, TIME_PATH, MAX_PATH_LEN) == 0) {
            if (strncmp(method, HTTP_POST, MAX_METHOD_LEN) == 0) {
                _handleTimePost(body, &client);
            } else {
                _badRequest(&client);
            }
        }
        else if (strncmp(path, API_PATH, MAX_PATH_LEN) == 0) {
            if (strncmp(method, HTTP_GET, MAX_METHOD_LEN) == 0) {
                _sendApiState(&client);
            } else if (strncmp(method, HTTP_POST, MAX_METHOD_LEN) == 0) {
                if (body.length() > 0) _parseControlRequest(body);
                _sendApiState(&client);
            } else {
                _badRequest(&client);
            }
        }
        else if (pathStr == "/" || pathStr.startsWith("/?")) {
            if (strncmp(method, HTTP_GET, MAX_METHOD_LEN) == 0) {
                _sendHomepage(&client, pathStr);
            } else {
                _badRequest(&client);
            }
        }
        else {
            _notFound(&client);
        }

        delay(1);
        client.stop();
    }

    // ── Handle POST /api/time ─────────────────────────────────────────────
    void _handleTimePost(const String& body, 
#ifdef WS_PLATFORM_OPTA
        EthernetClient* client
#else
        WiFiClient* client
#endif
    ) {
        _req.clear();
        DeserializationError err = deserializeJson(_req, body);
        if (!err && _req.containsKey("epoch")) {
            _state.clientEpoch  = (uint32_t)_req["epoch"];
            _state.timeUpdated  = true;
            Serial.println("[WS] Time synced: " + String(_state.clientEpoch));
        }
        client->println("HTTP/1.1 200 OK");
        client->println("Connection: close");
        client->println("Content-Type: application/json");
        client->println("Content-Length: 15");
        client->println();
        client->println("{\"status\":\"ok\"}");
        Serial.println("[WS] POST /api/time -> 200");
    }

    // ── Parse /api POST body for control commands ─────────────────────────
    void _parseControlRequest(const String& body) {
        _req.clear();
        DeserializationError err = deserializeJson(_req, body);
        if (err) {
            Serial.print("[WS] JSON error: "); Serial.println(err.f_str());
            return;
        }
        if (_req.containsKey("mode")) {
            uint8_t m = (uint8_t)_req["mode"];
            _state.mode = (ControlMode)constrain(m, 0, 2);
        }
        if (_req.containsKey("powerSetpoint")) {
            _state.powerSetpoint = constrain((int)_req["powerSetpoint"], 0, 100);
        }
        if (_req.containsKey("pressureSetpoint")) {
            _state.pressureSetpoint = constrain((int)_req["pressureSetpoint"], 0, 100);
        }
        Serial.println("[WS] Control update -> mode=" + String((int)_state.mode) +
                       " pwr=" + String(_state.powerSetpoint) +
                       " pres=" + String(_state.pressureSetpoint));
    }

    // ── Serialise state to JSON and send ─────────────────────────────────
    void _sendApiState(
#ifdef WS_PLATFORM_OPTA
        EthernetClient* client
#else
        WiFiClient* client
#endif
    ) {
        _res.clear();
        // Inputs
        JsonArray msgs = _res.createNestedArray("statusMessages");
        for (int i = 0; i < STATUS_MSG_COUNT; i++) msgs.add(_state.statusMessages[i]);
        _res["power"]       = _state.power;
        _res["pressure"]    = _state.pressure;
        _res["statusShort"] = _state.statusShort;
        // Outputs
        _res["mode"]              = (uint8_t)_state.mode;
        _res["powerSetpoint"]     = _state.powerSetpoint;
        _res["pressureSetpoint"]  = _state.pressureSetpoint;

        String resBody;
        serializeJsonPretty(_res, resBody);

        client->println("HTTP/1.1 200 OK");
        client->println("Connection: close");
        client->println("Content-Type: application/json");
        client->println("Access-Control-Allow-Origin: *");
        client->println("Content-Length: " + String(resBody.length() + 1));
        client->println();
        client->println(resBody);
        Serial.println("[WS] GET/POST /api -> 200");
    }

    // ── Serve the HTML dashboard ──────────────────────────────────────────
    void _sendHomepage(
#ifdef WS_PLATFORM_OPTA
        EthernetClient* client,
#else
        WiFiClient* client,
#endif
        const String& pathStr
    ) {
        String baseUrl = "http://" + _ip.toString() + ":" + String(_port);
        bool isHmi = (pathStr.indexOf("hmi") >= 0);
        String html = _buildHtml(baseUrl, isHmi);

        client->println("HTTP/1.1 200 OK");
        client->println("Connection: close");
        client->println("Content-Type: text/html; charset=utf-8");
        client->println("Content-Length: " + String(html.length() + 1));
        client->println();
        client->println(html);
        Serial.println(isHmi ? "[WS] GET /?hmi -> 200" : "[WS] GET / -> 200");
    }

    // ── 400 Bad Request ───────────────────────────────────────────────────
    void _badRequest(
#ifdef WS_PLATFORM_OPTA
        EthernetClient* client
#else
        WiFiClient* client
#endif
    ) {
        client->println("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
        Serial.println("[WS] -> 400");
    }

    // ── 404 Not Found ─────────────────────────────────────────────────────
    void _notFound(
#ifdef WS_PLATFORM_OPTA
        EthernetClient* client
#else
        WiFiClient* client
#endif
    ) {
        client->println("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
        Serial.println("[WS] -> 404");
    }

    // ── HTML Builder ──────────────────────────────────────────────────────
    // ── Route: "/" shows CPU page, "/?hmi" shows HMI page ────────────────
    String _buildHtml(const String& baseUrl, bool isHmi = false) {
        return isHmi ? _buildHmiHtml(baseUrl) : _buildCpuHtml(baseUrl);
    }

    String _buildCpuHtml(const String& baseUrl) {
        String h = "<!DOCTYPE html><html><head>"
          "<meta charset='UTF-8'/>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
          "<title>Status</title>"
          "<style>"
          "*{box-sizing:border-box;margin:0;padding:0}"
          "body{font-family:monospace;font-size:14px;color:#111;background:#fff;padding:12px;max-width:480px}"
          "@media(prefers-color-scheme:dark){body{background:#111;color:#eee}.card{background:#1e1e1e}.badge{border-color:#444;color:#aaa}.msg,.row{border-color:#2a2a2a}}"
          "h2{font-size:15px;font-weight:500;margin-bottom:10px;padding-bottom:6px;border-bottom:1px solid #ddd}"
          "@media(prefers-color-scheme:dark){h2{border-color:#2a2a2a}}"
          ".readings{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:16px}"
          ".card{background:#f5f5f5;border-radius:6px;padding:10px 12px}"
          ".card .n{font-size:26px;font-weight:500}"
          ".card .u{font-size:11px;color:#888;margin-top:2px}"
          ".bar-wrap{height:3px;background:#ddd;border-radius:2px;margin-top:8px}"
          ".bar{height:3px;border-radius:2px;background:#111;width:0%;transition:width .4s}"
          "@media(prefers-color-scheme:dark){.bar{background:#eee}.bar-wrap{background:#333}}"
          ".badge{display:inline-block;font-size:11px;padding:2px 8px;border-radius:4px;border:1px solid #ccc;color:#666;margin-bottom:10px}"
          ".msgs{margin-bottom:0}"
          ".msg{padding:5px 0;border-bottom:1px solid #eee;font-size:13px;color:#555;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
          ".msg:last-child{border-bottom:none}"
          "@media(prefers-color-scheme:dark){.msg{color:#aaa;border-color:#2a2a2a}}"
          "a{color:inherit;font-size:12px;display:inline-block;margin-bottom:14px;text-decoration:underline}"
          "</style></head><body>";

        h += "<a href='" + baseUrl + "/?hmi'>Go to HMI</a>";
        h += "<h2 style='margin-bottom:8px'>Status</h2>";
        h += "<span class='badge' id='s'>—</span>";
        h += "<div class='readings'>"
               "<div class='card'><div class='lbl' style='font-size:11px;color:#888'>Power</div>"
               "<div class='n' id='pwr'>—</div><div class='u'>%</div>"
               "<div class='bar-wrap'><div class='bar' id='pwrB'></div></div></div>"
               "<div class='card'><div class='lbl' style='font-size:11px;color:#888'>Pressure</div>"
               "<div class='n' id='pres'>—</div><div class='u'>%</div>"
               "<div class='bar-wrap'><div class='bar' id='presB'></div></div></div>"
             "</div>";
        h += "<h2 style='margin-bottom:8px'>Log</h2>";
        h += "<div class='msgs' id='msgs'>";
        for (int i = 0; i < STATUS_MSG_COUNT; i++) h += "<div class='msg'>—</div>";
        h += "</div>";
        h += "<script>const B='" + baseUrl + "';"
             "async function p(){"
               "try{const r=await fetch(B+'/api');if(!r.ok)return;const d=await r.json();"
               "document.getElementById('pwr').textContent=d.power??'—';"
               "document.getElementById('pres').textContent=d.pressure??'—';"
               "document.getElementById('pwrB').style.width=(d.power??0)+'%';"
               "document.getElementById('presB').style.width=(d.pressure??0)+'%';"
               "document.getElementById('s').textContent=d.statusShort||'—';"
               "const el=document.getElementById('msgs');el.innerHTML='';"
               "(d.statusMessages||[]).forEach(m=>{const v=document.createElement('div');"
               "v.className='msg';v.textContent=m||'—';el.appendChild(v);});"
             "}catch(e){}}"
             "setInterval(p,2000);p();"
             "</script></body></html>";
        return h;
    }

    String _buildHmiHtml(const String& baseUrl) {
        String h = "<!DOCTYPE html><html><head>"
          "<meta charset='UTF-8'/>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
          "<title>Control</title>"
          "<style>"
          "*{box-sizing:border-box;margin:0;padding:0}"
          "body{font-family:monospace;font-size:14px;color:#111;background:#fff;padding:12px;max-width:480px}"
          "@media(prefers-color-scheme:dark){body{background:#111;color:#eee}input[type=range]{accent-color:#eee}}"
          "h2{font-size:15px;font-weight:500;margin-bottom:10px;padding-bottom:6px;border-bottom:1px solid #ddd}"
          "@media(prefers-color-scheme:dark){h2{border-color:#2a2a2a}}"
          ".modes{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-bottom:16px}"
          "button{font-family:monospace;font-size:13px;padding:10px 4px;border:1px solid #ccc;background:transparent;color:inherit;border-radius:6px;cursor:pointer;width:100%}"
          "@media(prefers-color-scheme:dark){button{border-color:#444}}"
          "button.on{background:#111;color:#fff;border-color:#111}"
          "@media(prefers-color-scheme:dark){button.on{background:#eee;color:#111;border-color:#eee}}"
          ".sp{display:none;flex-direction:column;gap:6px;margin-bottom:16px}"
          ".sp.show{display:flex}"
          ".sp-top{display:flex;justify-content:space-between;font-size:12px;color:#888}"
          ".sp-top span{color:inherit;font-weight:500}"
          "input[type=range]{width:100%;accent-color:#111}"
          ".tbtn{width:100%;padding:10px;font-size:13px;font-family:monospace;border:1px solid #ccc;background:transparent;color:inherit;border-radius:6px;cursor:pointer}"
          "@media(prefers-color-scheme:dark){.tbtn{border-color:#444}}"
          ".tinfo{font-size:11px;color:#888;margin-top:6px;text-align:center}"
          "a{color:inherit;font-size:12px;display:inline-block;margin-bottom:14px;text-decoration:underline}"
          "</style></head><body>";

        h += "<a href='" + baseUrl + "/'>Go to status</a>";
        h += "<h2>Mode</h2>";
        h += "<div class='modes'>"
               "<button id='b0' onclick='setMode(0)'>Stop</button>"
               "<button id='b1' onclick='setMode(1)'>Const power</button>"
               "<button id='b2' onclick='setMode(2)'>Const pres.</button>"
             "</div>";
        h += "<div class='sp' id='sp'>"
               "<div class='sp-top' id='spLbl'>Setpoint <span id='spV'>0</span>%</div>"
               "<input type='range' id='sl' min='0' max='100' value='0' step='1'"
               " oninput=\"document.getElementById('spV').textContent=this.value\""
               " onchange='send()'/>"
             "</div>";
        h += "<h2>Time</h2>";
        h += "<button class='tbtn' onclick='syncTime()'>Set device time</button>";
        h += "<div class='tinfo' id='ti'>not synced</div>";
        h += "<script>const B='" + baseUrl + "';"
             "let mode=0;"
             "function setMode(m){mode=m;"
               "[0,1,2].forEach(i=>document.getElementById('b'+i).className=i===m?'on':'');"
               "const sp=document.getElementById('sp');"
               "const lb=document.getElementById('spLbl');"
               "if(m===0){sp.classList.remove('show');}else{"
               "sp.classList.add('show');"
               "lb.childNodes[0].textContent=m===1?'Power setpoint ':'Pressure setpoint ';}"
               "send();}"
             "async function send(){"
               "const sp=parseInt(document.getElementById('sl').value);"
               "try{await fetch(B+'/api',{method:'POST',"
               "headers:{'Content-Type':'application/json'},"
               "body:JSON.stringify({mode,powerSetpoint:mode===1?sp:0,pressureSetpoint:mode===2?sp:0})});"
               "}catch(e){}}"
             "async function syncTime(){"
               "const e=Math.floor(Date.now()/1000);"
               "try{await fetch(B+'/api/time',{method:'POST',"
               "headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch:e})});"
               "document.getElementById('ti').textContent='synced: '+new Date(e*1000).toLocaleTimeString();"
               "}catch(e){}}"
             "async function poll(){"
               "try{const r=await fetch(B+'/api');if(!r.ok)return;const d=await r.json();"
               "if(document.activeElement.id!=='sl'){"
               "const m=d.mode??0;if(m!==mode)setMode(m);"
               "const sp=m===1?(d.powerSetpoint??0):m===2?(d.pressureSetpoint??0):0;"
               "document.getElementById('sl').value=sp;"
               "document.getElementById('spV').textContent=sp;}}"
               "catch(e){}}"
             "setInterval(poll,2000);poll();"
             "</script></body></html>";
        return h;
    }
};

