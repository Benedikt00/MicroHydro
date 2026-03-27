//#include "hw_lib.h"
#include "telegram_management.h"
//#include "SD_management.h"
#include "webserver.h"
//#include "lcd_management.h"
#include "value_monitoring.h"
#include "nozzle_control.h"
#include "lora.h"

telegram_management tel;

float test_mon = 0.5;

String incomeing = "123.456.7100000000001773778573";

//monitor_window test_value(0.0, 5.0, 0.05, 3000);
int valok;

nz_controller nz_con(2.4, 200.0);

float* nozzels;

const int LORA_SS = 5;
const int LORA_DIO1 = 0;
const int LORA_RESET = 17;
const int LORA_BUSY = 4;
const int LORA_POWER = 22;

LoRaCom LoRa_module(LORA_SS, LORA_DIO1, LORA_RESET, LORA_BUSY);


// ── Access Point config ───────────────────────────────────────────────────────
//  SSID must be ≤ 31 chars. Password must be ≥ 8 chars, or "" for open network.
const char*    AP_SSID    = "MicroHydro";
const char*    AP_PASS    = "Einstein123";     // set "" for an open (no password) AP
const uint8_t  AP_CHANNEL = 6;               // WiFi channel 1–13
const uint8_t  AP_HIDDEN  = 0;               // 0 = broadcast SSID, 1 = hidden
const uint8_t  AP_MAX_CON = 4;               // max simultaneous clients

// The ESP32 AP always uses 192.168.4.1 as its gateway/IP by default.
// You can override this with WiFi.softAPConfig() below if needed.
static const IPAddress AP_IP  (192, 168, 4, 1);
static const uint16_t  AP_PORT = 80;

WebserverAbstraction* ws = nullptr;

float measuredPower    = 0.0f;
float measuredPressure = 0.0f;
uint32_t deviceEpoch   = 0;
unsigned long lastRtcUpdate = 0;


void WIFI_init(){
    // ── Start Access Point ────────────────────────────────────────────────
    WiFi.mode(WIFI_AP);

    // Optional: fix the AP IP / gateway / subnet (defaults are fine for most use)
    // IPAddress gateway(192, 168, 4, 1);
    // IPAddress subnet (255, 255, 255, 0);
    // WiFi.softAPConfig(AP_IP, gateway, subnet);

    bool ok = WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, AP_HIDDEN, AP_MAX_CON);

    if (!ok) {
        Serial.println("[AP] softAP() failed — halting.");
        while (1) { delay(1000); }
    }

    IPAddress ip = WiFi.softAPIP();   // typically 192.168.4.1
    Serial.println("[AP] Network : " + String(AP_SSID));
    if (strlen(AP_PASS) > 0)
        Serial.println("[AP] Password: " + String(AP_PASS));
    else
        Serial.println("[AP] Password: (open network)");
    Serial.println("[AP] IP      : " + ip.toString());

    // ── Instantiate webserver ─────────────────────────────────────────────
    ws = new WebserverAbstraction(ip, AP_PORT);

    ws->setStatusMessage(0, "ESP32 AP started");
    ws->setStatusMessage(1, ("SSID: " + String(AP_SSID)).c_str());
    ws->setStatusMessage(2, ("IP:   " + ip.toString()).c_str());
    ws->setStatusMessage(3, "Waiting for commands");
    ws->setStatusMessage(4, "");
    ws->setStatusShort("IDLE");

    Serial.println("[WS] Dashboard: http://" + ip.toString() +
                   ":" + String(AP_PORT));
}

void WIFI_loop(){
      ws->update();

    // ── Time sync ─────────────────────────────────────────────────────────
    if (ws->hasNewClientTime()) {
        deviceEpoch   = ws->getClientEpoch();
        lastRtcUpdate = millis();
        Serial.println("[RTC] Synced to epoch: " + String(deviceEpoch));
        ws->setStatusMessage(3, "RTC synced from browser");
    }

    // ── Control logic ─────────────────────────────────────────────────────
    ControlMode mode  = ws->getMode();
    float         pwrSP = ws->getPowerSetpoint();
    float         presSP= ws->getPressureSetpoint();

    switch (mode) {
      case ControlMode::UNKNOWN:
            measuredPower    = 0.0;
            measuredPressure = 0.0;
            ws->setStatusShort("Unbekannt");
            break;
        case ControlMode::STOP:
            measuredPower    = 0.0;
            measuredPressure = 0.0;
            ws->setStatusShort("HALT");
            break;
        case ControlMode::CONSTANT_POWER:
            measuredPower    = pwrSP;   // replace with real sensor read
            measuredPressure = 2.0;      // replace with real sensor read
            ws->setStatusShort("Leistungskonstant");
            break;
        case ControlMode::CONSTANT_PRESSURE:
            measuredPower    = 45.0;      // replace with real sensor read
            measuredPressure = presSP;  // replace with real sensor read
            ws->setStatusShort("Druckkonstant");
            break;
        case ControlMode::CONSTANT_POWER_NIGHT:
            measuredPower    = 45.0;      // replace with real sensor read
            measuredPressure = presSP;  // replace with real sensor read
            ws->setStatusShort("Druckkonstant (Nacht)");
            break;
        case ControlMode::CONSTANT_PRESSURE_NIGHT:
            measuredPower    = 45.0;      // replace with real sensor read
            measuredPressure = presSP;  // replace with real sensor read
            ws->setStatusShort("Druckkonstant");
            break;
    }

    ws->setPower   ((int)measuredPower);
    ws->setPressure((int)measuredPressure);

    // ── Software clock (ticks from last browser sync) ─────────────────────
    if (lastRtcUpdate > 0) {
        deviceEpoch = ws->getClientEpoch() +
                      (uint32_t)((millis() - lastRtcUpdate) / 1000UL);
    }

    // ── Periodic log ──────────────────────────────────────────────────────
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 5000) {
        lastLog = millis();
        char buf[STATUS_MSG_LEN];

        ws->setStatusMessage(0, buf);

        snprintf(buf, sizeof(buf), "Mode:%d Pwr:%d Pres:%d",
                 (int)mode, (int)measuredPower, (int)measuredPressure);
        ws->setStatusMessage(2, buf);

        snprintf(buf, sizeof(buf), "Clients: %d", WiFi.softAPgetStationNum());
        ws->setStatusMessage(4, buf);
    }
}

void setup() {

  Serial.begin(115200);

  while (!Serial && millis() < 3000) {}
  
  Serial.println("Begin");
  LoRa_module.init();
  LoRa_module.beginReceive();

  WIFI_init();


  /*SD_management sd(sd_cs);
    lcd_management ld;

    ld.power = 254.2;
    ld.status = "Testing";
    ld.update();

    tel.dec_incoming_msg(incomeing);

    Serial.print(String(tel.operating_mode));
    Serial.print(String(tel.power));
    Serial.println(String(tel.preassure));
    tel.errors.cpu_preassure_error = true;
    tel.errors.cpu_voltage_error = true;

    sd.write_telegram(incomeing);

    Serial.println(tel.enc_outgoing_msg());*/

  //test_value.set_target(4.0);
  /*for (int i = -2; i < 12; i++) {
    nozzels = nz_con.setpoint_to_aq(i);
    Serial.println(String(i) + " " + String(nozzels[0]) + " " + String(nozzels[1]) + " " + String(nozzels[2]));
  };*/
  delay(5000);
};

//valok = test_value.monitor(test_mon);
//test_mon += 0.001;
//Serial.println(test_mon + String(valok));

// Setup already done — lora is your LoRaCom instance
// States
enum SenderState { SEND,
                   WAIT_ACK,
                   WAITING };
SenderState senderState = SEND;

unsigned long lastSendTime = 0;
int packetCounter = 0;

unsigned long ackSendTime = 0;
static bool waitingToAck = false;
String pendingMsg = "";

void lrsender() {
  switch (senderState) {
    case WAIT_ACK:
      {
        String msg = LoRa_module.receive();

        if (msg == "ACK") {
          Serial.println("ACK received! Waiting 20 s...");
          lastSendTime = millis();  // reuse lastSendTime as the 10 s wait timer
          senderState = WAITING;   // new state — add this to your enum

        } else if (millis() - lastSendTime > 5000) {
          Serial.println("ACK timeout, retrying...");
          senderState = SEND;
        }
        break;
      }

    case WAITING:
      {
        if (millis() - lastSendTime >= 20000) {
          senderState = SEND;
        }
        break;
      }

    case SEND:
      {
        String payload = "TEST #" + String(packetCounter++);
        Serial.println("Sending: " + payload);
        LoRa_module.transmit(payload);

        // Switch to receive mode to listen for ACK
        LoRa_module.beginReceive();
        senderState = WAIT_ACK;
        lastSendTime = millis();
        break;
      }
  }
}

void lrrec() {
  // Non-blocking 100 ms gap before sending ACK
  if (waitingToAck && millis() - ackSendTime >= 100) {
    waitingToAck = false;
    Serial.println("Sending ACK...");
    LoRa_module.transmit("ACK");
    LoRa_module.beginReceive();
    return;
  }

  if (waitingToAck) return;  // still waiting, don't call receive()

  String msg = LoRa_module.receive();
  if (msg != "") {
    Serial.println("Received: " + msg);
    Serial.print("RSSI: ");
    Serial.print(LoRa_module.radio.getRSSI());
    Serial.println(" dBm");
    Serial.print("SNR:  ");
    Serial.print(LoRa_module.radio.getSNR());
    Serial.println(" dB");

    // Schedule ACK in 100 ms (non-blocking)
    pendingMsg = msg;
    ackSendTime = millis();
    waitingToAck = true;
  }
}

void loop() {
  lrrec();
  //lrsender();
  WIFI_loop();
};
// Setup already done — lora is your LoRaCom instance
// Call lora.beginReceive() at the end of your setup()!
/*

void loop() {

}*/
