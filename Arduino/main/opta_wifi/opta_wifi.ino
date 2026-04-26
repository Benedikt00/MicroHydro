/*
 * Arduino Opta WiFi – Access Point Web Server Test
 * Fixed for Mbed Opta WiFi BSP 4.x (arduino::WiFiClass)
 */

#include <WiFi.h>
#include <WiFiServer.h>

// ── AP credentials ────────────────────────────────────────────────────────────
const char* AP_SSID     = "OptaAP";
const char* AP_PASSWORD = "opta1234";   // min 8 chars; use "" for open
const uint8_t AP_CHANNEL = 6;           // 1–13; stored manually

// ── Server on port 80 ─────────────────────────────────────────────────────────
WiFiServer server(80);

// ── Forward declarations ───────────────────────────────────────────────────────
void printWiFiStatus();
String buildHtmlPage();

// =============================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000);

  Serial.println("\n=== Arduino Opta WiFi – AP Web Server Test ===");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);   // active-low on Opta → off

  // ── Start Access Point ──────────────────────────────────────────────────────
  Serial.print("Starting Access Point \"");
  Serial.print(AP_SSID);
  Serial.print("\" … ");

  // WiFi.beginAP(ssid, passphrase, channel)
  int status = WiFi.beginAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
  if (status != WL_AP_LISTENING) {
    Serial.println("FAILED. Halting.");
    while (true);
  }
  Serial.println("OK");

  delay(1000);
  printWiFiStatus();

  server.begin();
  Serial.println("HTTP server started on port 80");
  Serial.print("Connect to http://");
  Serial.println(WiFi.localIP());
}

// =============================================================================
void loop() {
  WiFiClient client = server.available();
  if (!client) return;

  Serial.println("\nClient connected");
  digitalWrite(LED_BUILTIN, LOW);   // LED on

  String requestLine = "";
  String currentLine = "";

  unsigned long timeout = millis() + 2000;

  while (client.connected() && millis() < timeout) {
    if (!client.available()) continue;

    char c = client.read();

    if (c == '\n') {
      if (currentLine.length() == 0) {
        break;   // blank line = end of headers
      }
      if (requestLine.length() == 0) {
        requestLine = currentLine;
      }
      currentLine = "";
    } else if (c != '\r') {
      currentLine += c;
    }
  }

  Serial.print("Request : ");
  Serial.println(requestLine);

  // ── Routing ─────────────────────────────────────────────────────────────────
  String path = "/";
  if (requestLine.startsWith("GET ")) {
    int start = 4;
    int end   = requestLine.indexOf(' ', start);
    if (end > start) path = requestLine.substring(start, end);
  }

  String body;
  int    statusCode  = 200;
  String contentType = "text/html";

  if (path == "/" || path == "/index.html") {
    body = buildHtmlPage();
  } else if (path == "/api/status") {
    body  = "{";
    body += "\"uptime_ms\":"  + String(millis())          + ",";
    body += "\"ssid\":\""     + String(AP_SSID)           + "\",";
    body += "\"ip\":\""       + WiFi.localIP().toString() + "\",";
    body += "\"channel\":"    + String(AP_CHANNEL);
    body += "}";
    contentType = "application/json";
  } else {
    statusCode = 404;
    body = "<h1>404 Not Found</h1><p>Path: " + path + "</p>";
  }

  // ── HTTP response ────────────────────────────────────────────────────────────
  client.print("HTTP/1.1 ");
  client.print(statusCode);
  client.println(statusCode == 200 ? " OK" : " Not Found");
  client.println("Content-Type: " + contentType);
  client.println("Connection: close");
  client.println("Cache-Control: no-cache");
  client.println();
  client.print(body);

  client.stop();
  Serial.println("Client disconnected");
  digitalWrite(LED_BUILTIN, HIGH);   // LED off
}

// =============================================================================
String buildHtmlPage() {
  String html;
  html.reserve(1024);

  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<title>Opta AP Status</title>";
  html += "<style>";
  html += "body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:2rem;}";
  html += "h1{color:#e94560;}";
  html += "table{border-collapse:collapse;width:100%;max-width:500px;}";
  html += "td,th{border:1px solid #444;padding:.5rem 1rem;text-align:left;}";
  html += "th{background:#16213e;}";
  html += ".ok{color:#4caf50;font-weight:bold;}";
  html += "</style></head><body>";

  html += "<h1>Arduino Opta – AP Web Server</h1>";
  html += "<table>";
  html += "<tr><th>Parameter</th><th>Value</th></tr>";

  html += "<tr><td>SSID</td><td>";         html += AP_SSID;                       html += "</td></tr>";
  html += "<tr><td>Channel</td><td>";      html += AP_CHANNEL;                    html += "</td></tr>";
  html += "<tr><td>AP IP Address</td><td class='ok'>"; html += WiFi.localIP().toString(); html += "</td></tr>";
  html += "<tr><td>MAC Address</td><td>"; html += WiFi.macAddress();              html += "</td></tr>";
  html += "<tr><td>Uptime (ms)</td><td>"; html += millis();                       html += "</td></tr>";

  html += "</table>";
  html += "<p><small>Page auto-refreshes every 5 seconds.</small></p>";
  html += "<p><a href='/api/status' style='color:#e94560'>JSON endpoint</a></p>";
  html += "</body></html>";

  return html;
}

// =============================================================================
void printWiFiStatus() {
  Serial.print("AP SSID    : "); Serial.println(WiFi.SSID());
  Serial.print("AP IP Addr : "); Serial.println(WiFi.localIP());
  Serial.print("MAC Addr   : "); Serial.println(WiFi.macAddress());
  Serial.print("Channel    : "); Serial.println(AP_CHANNEL);
  Serial.print("Encryption : "); Serial.println(strlen(AP_PASSWORD) > 0 ? "WPA2-PSK" : "Open");
}