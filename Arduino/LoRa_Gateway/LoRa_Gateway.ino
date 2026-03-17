/*
 * =====================================================
 *  LoRa GATEWAY
 *  Board  : ESP32
 *  LoRa   : SX1278 (LoRa lib by Sandeep Mistry)
 *  
 *
 *  SX1278 → ESP32:
 *    VCC  → 3.3V
 *    GND  → GND
 *    SCK  → GPIO18
 *    MISO → GPIO19
 *    MOSI → GPIO23
 *    NSS  → GPIO5
 *    RST  → GPIO14
 *    DIO0 → GPIO2
 *

 *
 *  Libraries needed (Library Manager):
 *    - LoRa  by Sandeep Mistry
 *    - Adafruit SSD1306
 *    - Adafruit GFX Library
 * =====================================================
 */

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <WiFi.h>
#include <WebServer.h>

// ── WiFi credentials ──────────────────────────────────
const char* SSID     = "MicroHydro";
const char* PASSWORD = "Einstein123";

// ── LoRa pins & frequency ─────────────────────────────
#define LORA_SS    5
#define LORA_RST   14
#define LORA_DIO0  2
// SX1278 supports 433 MHz. Use 868E6 (EU) or 915E6 (US) for SX1276.
#define LORA_FREQ  868E6

Adafruit_SSD1306 display(128, 64, &Wire, -1);
WebServer        server(80);

// ── State ─────────────────────────────────────────────
String  lastPayload = "";
int     lastRSSI    = 0;
float   lastSNR     = 0.0;
int     pktCount    = 0;
String  lastCmd     = "";
String  logLines    = "";   // newest on top, shown in webpage

void addLog(String line) {
  logLines = line + "\n" + logLines;
  if (logLines.length() > 3000) logLines = logLines.substring(0, 3000);
}



// ═════════════════════════════════════════════════════
//  Web handlers
// ═════════════════════════════════════════════════════
void handleRoot() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
             "<meta http-equiv='refresh' content='5'>"
             "<title>LoRa Gateway</title></head><body>"
             "<h2>LoRa Gateway</h2>"
             "<p>Packets received: " + String(pktCount) + "</p>"
             "<p>Last RSSI: "        + String(lastRSSI) + " dBm</p>"
             "<p>Last SNR: "         + String(lastSNR, 1) + " dB</p>"
             "<p>Last message: <code>" + lastPayload + "</code></p>"
             "<hr>"
             "<h3>Send command to node</h3>"
             "<form action='/send' method='get'>"
             "<input name='cmd' value='" + lastCmd + "' size='30'>"
             "<input type='submit' value='Send'>"
             "</form>"
             "<p>Quick: "
             "<a href='/send?cmd=LED:ON'>LED ON</a> | "
             "<a href='/send?cmd=LED:OFF'>LED OFF</a> | "
             "<a href='/send?cmd=PING'>PING</a></p>"
             "<hr><h3>Log</h3><pre>" + logLines + "</pre>"
             "</body></html>";
  server.send(200, "text/html", h);
}

void handleSend() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    cmd.trim();
    if (cmd.length() > 0 && cmd.length() <= 64) {
      LoRa.beginPacket();
      LoRa.print(cmd);
      LoRa.endPacket();
      lastCmd = cmd;
      addLog("TX: " + cmd);
      Serial.println("[TX] " + cmd);
    }
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

// ═════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  
  // LoRa init
  SPI.begin(18, 19, 23, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("[ERROR] LoRa init failed");
    while (true) delay(1000);
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setTxPower(14);
  Serial.println("[OK] LoRa ready");

  // WiFi
   WiFi.begin(SSID, PASSWORD);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t++ < 40) delay(500);

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    Serial.println("[WiFi] " + ip);
    addLog("IP: " + ip);
  } else {
    Serial.println("[WiFi] FAILED");
    addLog("WiFi FAILED");
  }

  server.on("/",     handleRoot);
  server.on("/send", handleSend);
  server.begin();
}

// ═════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════
void loop() {
  server.handleClient();

  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;

  String incoming = "";
  while (LoRa.available()) incoming += (char)LoRa.read();
  incoming.trim();

  lastPayload = incoming;
  lastRSSI    = LoRa.packetRssi();
  lastSNR     = LoRa.packetSnr();
  pktCount++;

  Serial.println("[RX] " + incoming +
                 "  RSSI=" + String(lastRSSI) +
                 "  SNR="  + String(lastSNR, 1));
  addLog("RX [" + String(lastRSSI) + "dBm SNR:" + String(lastSNR,1) + "]: " + incoming);

  // Send ACK back to node with gateway-side RSSI
  String ack = "ACK:GW_RSSI:" + String(lastRSSI);
  delay(100);
  LoRa.beginPacket();
  LoRa.print(ack);
  LoRa.endPacket();
  addLog("TX: " + ack);
  Serial.println("[TX] " + ack);

}
