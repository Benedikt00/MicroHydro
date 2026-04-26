#include <SD_management.h>
//#include <lcd_management.h>
#include <lora.h>
#include <nozzle_control.h>
#include <telegram_management.h>
#include <value_monitoring.h>
#include <webserver.h>

/*
 *   LLCC68 Pin  →  Arduino Pin
 *   ─────────────────────────
 *   VCC         →  3.3V
 *   GND         →  GND
 *   SCK         →  18 (SPI CLK)
 *   MISO        →  19 (SPI MISO)
 *   MOSI        →  23 (SPI MOSI)
 *   NSS/CS      →  10
 *   RESET       →  17
 *   BUSY        →  4
 *   DIO1        →  0
 *   ANT_SW      →  6  (optional, some boards need this for TX/RX switching)
*/

int led_onboard = 2;
long led_blink_time;
bool led_State = LOW;

const int LORA_SS = 5;
const int LORA_DIO1 = 0;
const int LORA_RESET = 17;
const int LORA_BUSY = 4;
const int LORA_POWER = 22;

const int LORA_GATEWAY_ID = 3;
const int LORA_REMOTE_ID = 2;
const int thisLORA_ID = LORA_GATEWAY_ID;

const int SD_SS = 32;

const int LORA_MESSAGE_RETRYS = 3;
int retries_used = 0;

// ── Access Point config ───────────────────────────────────────────────────────
//  SSID must be ≤ 31 chars. Password must be ≥ 8 chars, or "" for open network.
const char* AP_SSID = "MicroHydro";
const char* AP_PASS = "Einstein123";  // set "" for an open (no password) AP
const uint8_t AP_CHANNEL = 6;         // WiFi channel 1–13
const uint8_t AP_HIDDEN = 0;          // 0 = broadcast SSID, 1 = hidden
const uint8_t AP_MAX_CON = 4;         // max simultaneous clients

// The ESP32 AP always uses 192.168.4.1 as its gateway/IP by default.
// You can override this with WiFi.softAPConfig() below if needed.
static const IPAddress AP_IP(192, 168, 0, thisLORA_ID);
static const uint16_t AP_PORT = 80;

WebserverAbstraction* ws = nullptr;

//INIT COMS
LoRaCom lora_module(LORA_SS, LORA_DIO1, LORA_RESET, LORA_BUSY);

telegram_management tel;

float measuredPower = 0.0f;
int measuredLevel = 0;
uint32_t deviceEpoch = 0;
unsigned long lastRtcUpdate = 0;

bool lora_startup_error;

enum SenderState { SEND,
                   RECIEVING,
                   ERROR };

SenderState senderState = SEND;

unsigned long lastSendTime = 0;
unsigned long lastRecieveTime = 0;
int packetCounter = 0;

unsigned long ackSendTime = 0;
unsigned long last_ack = 0;

static bool waitingToAck = false;
static bool waitingForAck = false;
String pendingMsg = "";

int message_sender_time = 20000;
int LORA_WAIT_TO_ACK_TIMEOUT = 400;
int LORA_WAIT_FOR_ACK = 5000;
int LORA_AFTER_SEND_TIMEOUT = 200;
int LORA_REPLY_TIME = 500;

bool lora_set_recieving = true;
bool lora_send_reply = false;

long last_msg = 0;

void setup() {
  delay(1000);

  pinMode(led_onboard, OUTPUT);
  Serial.begin(115200);

  digitalWrite(led_onboard, HIGH);

  while (!Serial && millis() < 3000) {}

  Serial.println("Begin");
  lora_module.init();
  tel.errors.gw_lora_fail = lora_module.loraError;
  if (!lora_module.loraError) {
    lora_module.beginReceive();
  }
  WIFI_init();
  tel.out_reciever_id = LORA_REMOTE_ID;

  //SD_management sd(SD_SS);
  //  lcd_management ld;
  /*
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
};


void loop() {
  loracom();
  //lrsender();
  WIFI_loop();

  digitalWrite(led_onboard, led_State);

  if (millis() - led_blink_time > 3000) {
    led_blink_time = millis();
    led_State = !led_State;
  }

  if (millis() - last_msg > message_sender_time) {
    last_msg = millis();
    senderState = SEND;
  }
};

void ackErrors() {
  tel.errors.gw_lora_fail = false;
  tel.errors.remoteNode_not_reachable = false;
  tel.errors.gw_lcd_fail = false;
  tel.errors.gw_wlan_ini_fail = false;
  tel.ack_out = 1;
  //senderState = SEND;
  waitingForAck = false;
  Serial.println("ACK ERRORS");
}

String payload;
char buf[STATUS_MSG_LEN];


void loracom() {
  switch (senderState) {
    case RECIEVING:
      {
        String msg = lora_module.receive();

        if (tel.errors.gw_lora_fail || tel.errors.remoteNode_not_reachable) {
          senderState = ERROR;
          break;
        }

        //break recieve after send to avoid echo
        if (millis() - lastSendTime < LORA_AFTER_SEND_TIMEOUT) {
          if (msg != ""){
            Serial.println("^^Trown away");
          }
          break;
        }

        //set send to reply after timeoute
        if (lora_send_reply && !waitingForAck && (millis() - lastSendTime >= LORA_REPLY_TIME)){
          Serial.println("Sending reply msg");
          senderState = SEND;
          lora_send_reply = false;
          break;
        }

        //Send ack
        if (waitingToAck && (millis() - lastRecieveTime >= LORA_WAIT_TO_ACK_TIMEOUT)) {
          Serial.println("Acknowledging, set state send");
          senderState = SEND;
          break;
        }

        //recieve ack
        if (msg == "ACK") {
          Serial.println("ACK received! ");
          snprintf(buf, sizeof(buf), "Received: %s  RSSI: %.1f dBm", msg, lora_module.radio.getRSSI());
          ws->pushStatusMessage(buf);

          snprintf(buf, sizeof(buf), "SNR: %.1f", lora_module.radio.getSNR());
          ws->pushStatusMessage(buf);

          waitingForAck = false;
          //lora_send_reply = true;
          break;
        } 
        //ack running intop timeout
        else if (waitingForAck && (millis() - lastSendTime > LORA_WAIT_FOR_ACK)) {
          Serial.println("ACK timeout, retrying... " + String(retries_used));
          snprintf(buf, sizeof(buf), "ACK timeout, retrying... %1d", retries_used);
          ws->pushStatusMessage(buf);
          //Telegram repetition when not acknowledged
          if (retries_used < LORA_MESSAGE_RETRYS) {
            senderState = SEND;
            retries_used += 1;

          } else {  //errors when retrys exceeded
            retries_used = 0;
            senderState = ERROR;

            Serial.println("Lora Max retries exceedet");
            tel.errors.remoteNode_not_reachable = true;
            snprintf(buf, sizeof(buf), "Lora Max retries exceedet");
            ws->pushStatusMessage(buf);
          }
        }
        //Handle NEw message recieve
        if (msg != "") {  
          Serial.println("New msg " + String(msg.length()));

          if (msg.length() == tel.MSG_LENGTH) {  //right len
            Serial.println("Right len, for node " + msg.substring(tel.DEVICE_ID_SPOT, tel.DEVICE_ID_SPOT + 1));
            if (msg.substring(tel.DEVICE_ID_SPOT, tel.DEVICE_ID_SPOT + 1).toInt() == thisLORA_ID) {  //for this node
              tel.dec_incoming_msg(msg);
              lastRecieveTime = millis();
              Serial.println("MSG recieved " + msg);
              char buf[STATUS_MSG_LEN];

              snprintf(buf, sizeof(buf), "Received: %s  RSSI: %d dBm", msg, lora_module.radio.getRSSI());
              ws->pushStatusMessage(buf);

              snprintf(buf, sizeof(buf), "SNR: %.1f", lora_module.radio.getSNR());
              ws->pushStatusMessage(buf);
              waitingToAck = true;
              lora_set_recieving = true;
            }
          }
        }
        break;
      }

    case SEND:
      {
        //return if error
        if (tel.errors.gw_lora_fail || tel.errors.remoteNode_not_reachable) {
          senderState = ERROR;
          break;
        }
        //send ack if requested
        if (waitingToAck && (millis() - lastRecieveTime >= LORA_WAIT_TO_ACK_TIMEOUT)) {
          payload = "ACK";
          waitingToAck = false;
          Serial.println("Sending ack after: " +  String(lastRecieveTime) + " " +  String(millis() - lastRecieveTime) + " TO: " + String(LORA_WAIT_TO_ACK_TIMEOUT));

          //lora_send_reply = true;
        } else {
          //set message
          payload = tel.enc_outgoing_msg();
          waitingForAck = true;
          Serial.println("Waiting for ack");
        }

        //send
        lora_module.transmit(payload);

        if (lora_module.loraError) {
          tel.errors.gw_lora_fail = true;
          Serial.println("Set Gateway Error");
          senderState = ERROR;
          break;
        }
        tel.errors.gw_lora_fail = false;

        // Switch to receive mode to listen for ACK

        lastSendTime = millis();
        Serial.println("Set Recieve");

        lora_module.beginReceive();
        senderState = RECIEVING;
        break;
      }

    case ERROR:
      {
        if (!tel.errors.gw_lora_fail && !tel.errors.remoteNode_not_reachable) {
          lora_module.init();
          if (!lora_module.loraError) {
            tel.errors.remoteNode_not_reachable = false;
            Serial.println("Set Send from Error");

            senderState = SEND;
          }
        }
        break;
      }
  }
}

void WIFI_init() {
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

  IPAddress ip = WiFi.softAPIP();  // typically 192.168.4.1
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

  Serial.println("[WS] Dashboard: http://" + ip.toString() + ":" + String(AP_PORT));
}

void WIFI_loop() {
  ws->update();

  // ── Time sync ─────────────────────────────────────────────────────────
  if (ws->hasNewClientTime()) {
    tel.time_management(ws->getClientEpoch());
  }

  // ── Control logic ─────────────────────────────────────────────────────
  ControlMode mode = ws->getMode();
  float pwrSP = ws->getPowerSetpoint();
  int lvlSP = ws->getLevelSetpoint();

  switch (mode) {
    case ControlMode::UNKNOWN:
      measuredPower = 0.0;
      measuredLevel = 0;
      ws->setStatusShort("Unbekannt");
      ws->setStatusShortSetpoint("Unbekannt");
      break;
    case ControlMode::STOP:
      measuredPower = 0.0;
      measuredLevel = 0;
      ws->setStatusShort("HALT");
      ws->setStatusShortSetpoint("HALT");
      break;
    case ControlMode::CONSTANT_POWER:
      measuredPower = pwrSP;  // replace with real sensor read
      measuredLevel = 2;      // replace with real sensor read
      ws->setStatusShort("Leistung");
      ws->setStatusShortSetpoint("Leistung");
      break;
    case ControlMode::CONSTANT_LEVEL:
      measuredPower = 45.0;   // replace with real sensor read
      measuredLevel = lvlSP;  // replace with real sensor read
      ws->setStatusShort("Pegel");
      ws->setStatusShortSetpoint("Pegel");
      break;
    case ControlMode::CONSTANT_POWER_NIGHT:
      measuredPower = pwrSP;  // replace with real sensor read
      measuredLevel = 2;      // replace with real sensor read
      ws->setStatusShort("Leistung N");
      ws->setStatusShortSetpoint("Leistung N");
      break;
    case ControlMode::CONSTANT_LEVEL_NIGHT:
      measuredPower = 45.0;   // replace with real sensor read
      measuredLevel = lvlSP;  // replace with real sensor read
      ws->setStatusShort("Pegel N");
      ws->setStatusShortSetpoint("Pegel N");
      break;
    case ControlMode::FILLING:
      measuredPower = 0.0;  // replace with real sensor read
      measuredLevel = 0;    // replace with real sensor read
      ws->setStatusShort("Speicher");
      ws->setStatusShortSetpoint("Speicher");
      break;
  }

  ws->setPower(measuredPower);
  ws->setLevel(measuredLevel);

  // ── Periodic log ──────────────────────────────────────────────────────
  if (ws->getAckErrors()) {
    ws->resetAck();
    ackErrors();
  }
}
