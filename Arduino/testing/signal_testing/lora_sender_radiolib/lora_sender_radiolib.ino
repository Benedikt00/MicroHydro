/*
 * =====================================================
 *  LoRa SENDER  —  RadioLib + Bluetooth Serial
 *  Board  : ESP32
 *  LoRa   : SX1278
 *  Lib    : RadioLib
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
 *  Libraries (Library Manager):
 *    - RadioLib  by Jan Gromes
 *
 *  Bluetooth: pair with "LoRa_Sender"
 * =====================================================
 */

#include <RadioLib.h>
#include <BluetoothSerial.h>

// ── LoRa config ──────────────────────────────────────
const float LORA_FREQUENCY  = 868.0;
const float LORA_BANDWIDTH  = 125.0;
const int   LORA_SF         = 9;
const int   LORA_CR         = 7;
const int   LORA_SYNC_WORD  = 0x12;
const int   LORA_TX_POWER   = 22;
const int   LORA_PREAMBLE   = 16;

// ── LoRa pins ────────────────────────────────────────
#define LORA_NSS    5
#define PIN_RESET   17
#define LORA_BUSY  4
#define LORA_DIO1  0   

SX1278 radio = new Module(LORA_NSS, LORA_DIO1, PIN_RESET, LORA_BUSY);

// ── Bluetooth ────────────────────────────────────────
BluetoothSerial BT;

// ── Timing ───────────────────────────────────────────
#define SEND_INTERVAL_MS  30000UL
#define ACK_TIMEOUT_MS    5000UL

// ── State ────────────────────────────────────────────
unsigned long lastSendTime  = 0;
int           packetCount   = 0;
bool          waitingForAck = false;
unsigned long ackTimestamp  = 0;

// ── Helper ───────────────────────────────────────────
void log(String msg) {
  Serial.println(msg);
  BT.println(msg);
}

// ═════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  BT.begin("LoRa_Sender");
  log("[BT] Bluetooth started as 'LoRa_Sender'");

  delay(4000);

  int state = radio.begin(
    LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SF, LORA_CR,
    LORA_SYNC_WORD, LORA_TX_POWER, LORA_PREAMBLE);

  if (state == RADIOLIB_ERR_NONE) {
    log("[OK] LoRa Sender ready");
    log("[INFO] Will transmit every 30 seconds");
  } else {
    log("[ERROR] LoRa init failed, code: " + String(state));
    while (true) delay(1000);
  }

  // Send immediately on boot
  lastSendTime = millis() - SEND_INTERVAL_MS;
}

// ═════════════════════════════════════════════════════
//  SEND
// ═════════════════════════════════════════════════════
void sendPacket() {
  packetCount++;
  String msg = "PING:" + String(packetCount);

  int state = radio.transmit(msg);

  log("---------------------------");
  if (state == RADIOLIB_ERR_NONE) {
    log("[TX] Sent    : " + msg);
    log("[TX] Packet# : " + String(packetCount));
    // Switch to receive mode to wait for ACK
    radio.startReceive();
    waitingForAck = true;
    ackTimestamp  = millis();
  } else {
    log("[TX] Failed, code: " + String(state));
  }
}

// ═════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // ── Time to send? ──────────────────────────────────
  if (!waitingForAck && (now - lastSendTime >= SEND_INTERVAL_MS)) {
    lastSendTime = now;
    sendPacket();
  }

  // ── Waiting for ACK ────────────────────────────────
  if (waitingForAck) {
    // Check if a packet arrived
    if (radio.available()) {
      String incoming;
      int state = radio.readData(incoming);

      if (state == RADIOLIB_ERR_NONE) {
        float rssi = radio.getRSSI();
        float snr  = radio.getSNR();

        log("[RX] ACK    : " + incoming);
        log("[RX] RSSI   : " + String(rssi, 1) + " dBm");
        log("[RX] SNR    : " + String(snr,  1) + " dB");
        log("[INFO] Next TX in 30s...");
      } else {
        log("[RX] Read error, code: " + String(state));
      }
      waitingForAck = false;
    }

    // ACK timeout
    if (now - ackTimestamp > ACK_TIMEOUT_MS) {
      log("[WARN] No ACK received within 5s");
      waitingForAck = false;
    }
  }

  // ── BT commands ────────────────────────────────────
  if (BT.available()) {
    String cmd = BT.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("SEND")) {
      log("[BT] Manual send triggered");
      lastSendTime = millis() - SEND_INTERVAL_MS;
    } else if (cmd.equalsIgnoreCase("STATUS")) {
      unsigned long remaining = SEND_INTERVAL_MS - (millis() - lastSendTime);
      log("[STATUS] Packets sent : " + String(packetCount));
      log("[STATUS] Next TX in   : " + String(remaining / 1000) + "s");
    } else {
      log("[BT] Commands: SEND, STATUS");
    }
  }
}
