/*
 * =====================================================
 *  LoRa REMOTE NODE
 *  Board  : ESP32
 *  LoRa   : SX1278 (LoRa lib by Sandeep Mistry)
 *  Display: SSD1306 128x64 I2C (Adafruit SSD1306)
 *
 *  Same wiring as gateway:
 *    SX1278: SCK→18, MISO→19, MOSI→23, NSS→5, RST→14, DIO0→2
 *    SSD1306: SDA→21, SCL→22
 *
 *  Sends a packet every 30 seconds.
 *  Waits up to 3 s for an ACK from the gateway.
 *  Both RSSI values (node-side RX and gateway-side RX) shown on OLED.
 *
 *  Libraries needed:
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
const char* SSID     = "MicroHydroRemoteNode";
const char* PASSWORD = "Einstein123";

WebServer        server(80);

// ── Node identity ─────────────────────────────────────
#define NODE_ID  "NODE1"

// ── LoRa pins & frequency ─────────────────────────────
#define LORA_SS    5
#define LORA_RST   14
#define LORA_DIO0  2
#define LORA_FREQ  868E6    // Must match gateway


// ── IO ────────────────────────────────────────────────
#define LED_PIN    2        // Built-in LED on most ESP32 boards

// ── Timing ────────────────────────────────────────────
#define TX_INTERVAL_MS  30000UL   // 30 seconds
#define ACK_TIMEOUT_MS   3000UL   // Wait 3 s for ACK

// ── State ─────────────────────────────────────────────
long   packetNum      = 0;
int    txRSSI         = 0;    // RSSI of last received packet (ACK) at this node
float  txSNR          = 0.0;
int    gwRSSI         = 0;    // RSSI reported by gateway in ACK
bool   lastAckOk      = false;
bool   ledState       = false;
String lastCmd        = "";
unsigned long lastTx  = 0;


// ═════════════════════════════════════════════════════
//  Send packet + wait for ACK
// ═════════════════════════════════════════════════════
void sendPacket() {
  packetNum++;

  // Simple payload: ID, packet number, LED state
  String payload = String(NODE_ID) + "," +
                   String(packetNum) + "," +
                   "LED:" + (ledState ? "1" : "0");

  Serial.println("[TX] " + payload);
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  
  unsigned long t = millis();
  lastAckOk = false;
  while (millis() - t < ACK_TIMEOUT_MS) {
    int sz = LoRa.parsePacket();
    if (sz > 0) {
      String reply = "";
      while (LoRa.available()) reply += (char)LoRa.read();
      reply.trim();
      txRSSI = LoRa.packetRssi();
      txSNR  = LoRa.packetSnr();
      Serial.println("[RX] " + reply + "  RSSI=" + String(txRSSI));

      // Parse gateway RSSI from "ACK:GW_RSSI:-75"
      if (reply.startsWith("ACK:GW_RSSI:")) {
        gwRSSI    = reply.substring(12).toInt();
        lastAckOk = true;
      }
      // Also handle command piggyback "ACK:GW_RSSI:-75:CMD:LED:ON"
      // (optional extension — gateway can append :CMD:xxx to its ACK)
      int cmdIdx = reply.indexOf(":CMD:");
      if (cmdIdx >= 0) {
        lastCmd = reply.substring(cmdIdx + 5);
        handleCommand(lastCmd);
      }
      break;
    }
  }

  if (!lastAckOk) Serial.println("[WARN] No ACK received");
  
}

// ═════════════════════════════════════════════════════
//  Handle command from gateway (received inside ACK)
// ═════════════════════════════════════════════════════
void handleCommand(String cmd) {
  cmd.trim();
  Serial.println("[CMD] " + cmd);
  lastCmd = cmd
  if (cmd == "LED:ON") {
    ledState = true;
    digitalWrite(LED_PIN, HIGH);
  } else if (cmd == "LED:OFF") {
    ledState = false;
    digitalWrite(LED_PIN, LOW);
  } else if (cmd == "PING") {
    // Just acknowledge — next packet will include response
  }
}

// ═════════════════════════════════════════════════════
//  Web handlers
// ═════════════════════════════════════════════════════
void handleRoot() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
             "<meta http-equiv='refresh' content='5'>"
             "<title>LoRa Gateway</title></head><body>"
             "<h2>LoRa Gateway</h2>"
             "<p>Packets received: " + String(packetNum) + "</p>"
             "<p>Last RSSI: "        + String(txRSSI) + " dBm</p>"
             "<p>Last SNR: "         + String(txSNR, 1) + " dB</p>"
             "<p>Last message: <code>" + String(lastCmd) + "</code></p>"
             "<hr>"
             "<h3>Send command to node</h3>"
             "<form action='/send' method='get'>"
             "<input name='cmd' value='" + String(lastCmd) + "' size='30'>"
             "<input type='submit' value='Send'>"
             "</form>"
            
             "<hr><h3>Last Tyg</h3><pre>" + String(lastTx) + "</pre>"
             "</body></html>";
  server.send(200, "text/html", h);
}

// ═════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);


  // LoRa
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
  delay(1000);

  // Send first packet immediately
  sendPacket();
  lastTx = millis();

  WiFi.begin(SSID, PASSWORD);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t++ < 40) delay(500);

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    Serial.println("[WiFi] " + ip);
  } else {
    Serial.println("[WiFi] FAILED");
  }

  server.on("/",     handleRoot);
  server.begin();
}

// ═════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════
void loop() {
  // Send every 30 seconds
  if (millis() - lastTx >= TX_INTERVAL_MS) {
    sendPacket();
    lastTx = millis();
  }

  server.handleClient();

  // Also listen for unsolicited commands between transmissions
  int sz = LoRa.parsePacket();
  if (sz > 0) {
    String msg = "";
    while (LoRa.available()) msg += (char)LoRa.read();
    msg.trim();
    Serial.println("[RX mid-cycle] " + msg);
    // If it's a plain command (not an ACK), handle it
    if (!msg.startsWith("ACK:")) {
      handleCommand(msg);
      // Send a quick ACK reply
      delay(100);
      LoRa.beginPacket();
      LoRa.print(String(NODE_ID) + ":ACK:" + msg);
      LoRa.endPacket();
    }
    
  }
}
