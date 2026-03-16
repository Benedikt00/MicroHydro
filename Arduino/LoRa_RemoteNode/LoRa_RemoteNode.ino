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
#include <Adafruit_SSD1306.h>

// ── Node identity ─────────────────────────────────────
#define NODE_ID  "NODE1"

// ── LoRa pins & frequency ─────────────────────────────
#define LORA_SS    5
#define LORA_RST   14
#define LORA_DIO0  2
#define LORA_FREQ  868E6    // Must match gateway

// ── OLED ──────────────────────────────────────────────
#define OLED_SDA  21
#define OLED_SCL  22
#define OLED_ADDR 0x3C

// ── IO ────────────────────────────────────────────────
#define LED_PIN    2        // Built-in LED on most ESP32 boards

// ── Timing ────────────────────────────────────────────
#define TX_INTERVAL_MS  30000UL   // 30 seconds
#define ACK_TIMEOUT_MS   3000UL   // Wait 3 s for ACK

Adafruit_SSD1306 display(128, 64, &Wire, -1);

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
//  OLED helpers
// ═════════════════════════════════════════════════════
void oledShow(String l1, String l2, String l3, String l4) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,  0); display.println(l1);
  display.setCursor(0, 16); display.println(l2);
  display.setCursor(0, 32); display.println(l3);
  display.setCursor(0, 48); display.println(l4);
  display.display();
}

void updateOled() {
  String ackStr  = lastAckOk ? "ACK OK" : "NO ACK";
  String rssiStr = "Tx RSSI(ack):" + String(txRSSI);
  String gwStr   = "GW RSSI:" + String(gwRSSI) + " dBm";
  oledShow("=NODE= #" + String(packetNum),
           ackStr + " LED:" + (ledState ? "ON" : "OFF"),
           rssiStr,
           gwStr);
}

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

  // Wait for ACK
  oledShow("=NODE= #" + String(packetNum), "Waiting ACK...", "", "");

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
    oledShow("LoRa FAILED", "Check wiring", "", "");
    while (true) delay(1000);
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setTxPower(14);
  Serial.println("[OK] LoRa ready");
  oledShow("LoRa OK", NODE_ID, "Ready", "");
  delay(1000);

  // Send first packet immediately
  sendPacket();
  lastTx = millis();
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
