#include <SD_management.h>
#include <lcd_management.h>
#include <lora.h>
#include <nozzle_control.h>
#include <telegram_management.h>
#include <value_monitoring.h>
#include <webserver.h>
#include <wire.h>
#define LOG_PLATFORM_ESP32
#include <my_log.h>
/*
 *   LLCC68 Pin  →  Arduino Pin
 *   ─────────────────────────
 *   VCC         →  3.3V
 *   GND         →  GND
 *   SCK         →  18 (SPI CLK)
 *   MISO        →  19 (SPI MISO)
 *   MOSI        →  23 (SPI MOSI)
 *   NSS/CS      →  5
 *   RESET       →  17
 *   BUSY        →  4
 *   DIO1        →  0
 *   ANT_SW      →  6  (optional, some boards need this for TX/RX switching)
*/

int led_onboard = 2;
long led_blink_time;
bool led_State = LOW;

const int LORA_SS = 5;
const int LORA_DIO1 = 16;
const int LORA_RESET = 17;
const int LORA_BUSY = 4;

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

lcd_management display;
unsigned long lastUpdateLcd = 0;
const unsigned long LCD_RATE = 5000UL;   // refresh every 5 s

telegram_management tel_inc;
telegram_management tel_out;

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
//bool need_to_respond_to_rem_nod = false;
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

//bool lora_set_recieving = true;
bool lora_send_reply = false;

long last_msg = 0;

void ackErrors() {
  tel_out.errors.gw_lora_fail = false;
  tel_out.errors.remoteNode_not_reachable = false;
  tel_out.errors.gw_lcd_fail = false;
  tel_out.errors.gw_wlan_ini_fail = false;
  tel_out.ack_out = 1;
  //senderState = SEND;
  waitingForAck = false;
  my_log("ACK ERRORS");
}

String payload;
char buf[STATUS_MSG_LEN];


void errorManagement(){
  if (tel_out.errors.gw_lora_fail){
    display.status = "LoRa Fail...";
  }
}

void loracom() {
  switch (senderState) {
    case RECIEVING:
      {
        String msg = lora_module.receive();

        if (tel_out.errors.gw_lora_fail || tel_out.errors.remoteNode_not_reachable) {
          senderState = ERROR;
          break;
        }

        //break recieve after send to avoid echo
        if (millis() - lastSendTime < LORA_AFTER_SEND_TIMEOUT) {
          if (msg != ""){
            my_log("^^^^^ Trown away");
          }
          break;
        }

        //set send to reply after ack and timeoute
        if (lora_send_reply && !waitingForAck && (millis() - lastSendTime >= LORA_REPLY_TIME)){
          my_log("Sending reply msg");
          senderState = SEND;
          lora_send_reply = false;
          break;
        }

        //Send ack
        if (waitingToAck && (millis() - lastRecieveTime >= LORA_WAIT_TO_ACK_TIMEOUT)) {
          my_log("Acknowledging, set state send");
          senderState = SEND;
          break;
        }

        //recieve ack
        if (msg == "ACK") {
          my_log("ACK received! ");
          snprintf(buf, sizeof(buf), "Received: %s  RSSI: %.1f dBm", msg, lora_module.radio.getRSSI());
          ws->pushStatusMessage(buf);

          snprintf(buf, sizeof(buf), "SNR: %.1f", lora_module.radio.getSNR());
          ws->pushStatusMessage(buf);

          waitingForAck = false;
          break;
        }

        //ack running intop timeout
        else if (waitingForAck && (millis() - lastSendTime > LORA_WAIT_FOR_ACK)) {
          my_log("ACK timeout, retrying... " + String(retries_used));
          snprintf(buf, sizeof(buf), "ACK timeout, retrying... %1d", retries_used);
          ws->pushStatusMessage(buf);
          //Telegram repetition when not acknowledged
          if (retries_used < LORA_MESSAGE_RETRYS) {
            senderState = SEND;
            retries_used += 1;
          } else {  //errors when retrys exceeded
            retries_used = 0;
            senderState = ERROR;

            my_log("Lora Max retries exceedet");
            tel_out.errors.remoteNode_not_reachable = true;
            snprintf(buf, sizeof(buf), "Lora Max retries exceedet");
            ws->pushStatusMessage(buf);
            waitingForAck = false;
          }
        }
        //Handle NEw message recieve
        if (msg != "") {  
          my_log("New msg " + String(msg.length()));

          if (msg.length() == tel_inc.MSG_LENGTH) {  //right len
            my_log("Right len, for node " + msg.substring(tel_inc.DEVICE_ID_SPOT, tel_inc.DEVICE_ID_SPOT + 1));
            if (msg.substring(tel_inc.DEVICE_ID_SPOT, tel_inc.DEVICE_ID_SPOT + 1).toInt() == thisLORA_ID) {  //for this node
              tel_inc.dec_incoming_msg(msg);
              lastRecieveTime = millis();
              my_log("MSG recieved " + msg);
              char buf[STATUS_MSG_LEN];

              snprintf(buf, sizeof(buf), "Received: %s  RSSI: %d dBm", msg, lora_module.radio.getRSSI());
              ws->pushStatusMessage(buf);

              snprintf(buf, sizeof(buf), "SNR: %.1f", lora_module.radio.getSNR());
              ws->pushStatusMessage(buf);
              waitingToAck = true;
              lora_send_reply = true;
              tel_out.errors.remoteNode_not_reachable = false;
            }
          }
        }
        break;
      }

    case SEND:
      {
        //return if error
        if (tel_out.errors.gw_lora_fail || tel_out.errors.remoteNode_not_reachable) {
          senderState = ERROR;
          break;
        }
        //send ack if requested
        if (waitingToAck && (millis() - lastRecieveTime >= LORA_WAIT_TO_ACK_TIMEOUT)) {
          payload = "ACK";
          waitingToAck = false;
          my_log("Sending ack after: " +  String(lastRecieveTime) + " " +  String(millis() - lastRecieveTime) + " TO: " + String(LORA_WAIT_TO_ACK_TIMEOUT));

        } else {
          //set message
          payload = tel_out.enc_outgoing_msg();
          waitingForAck = true;
          my_log("Waiting for ack");
        }

        //send
        lora_module.transmit(payload);

        if (lora_module.loraError) {
          tel_out.errors.gw_lora_fail = true;
          my_log("Setting Gateway Error");
          senderState = ERROR;
          break;
        }
        tel_out.errors.gw_lora_fail = false;

        // Switch to receive mode to listen for ACK

        lastSendTime = millis();
        my_log("Set Recieve");

        lora_module.beginReceive();
        senderState = RECIEVING;
        break;
      }

    case ERROR:
      {
        if (!tel_out.errors.gw_lora_fail && !tel_out.errors.remoteNode_not_reachable) {
          lora_module.init();
          if (!lora_module.loraError) {
            tel_out.errors.remoteNode_not_reachable = false;
            my_log("Set send from Error state");
            senderState = SEND;
          }
        }

        break;
        if (tel_out.errors.remoteNode_not_reachable && !(senderState == RECIEVING)){
            senderState = RECIEVING;
            my_log("Set Recieve from Error");
            break;
        }
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
    my_log("[AP] softAP() failed — halting.");
    while (1) { delay(1000); }
  }

  IPAddress ip = WiFi.softAPIP();  // typically 192.168.4.1
  my_log("[AP] Network : " + String(AP_SSID));
  if (strlen(AP_PASS) > 0)
    my_log("[AP] Password: " + String(AP_PASS));
  else
    my_log("[AP] Password: (open network)");
  my_log("[AP] IP      : " + ip.toString());

  // ── Instantiate webserver ─────────────────────────────────────────────
  ws = new WebserverAbstraction(ip, AP_PORT);

  ws->setStatusMessage(0, "ESP32 AP started");
  ws->setStatusMessage(1, ("SSID: " + String(AP_SSID)).c_str());
  ws->setStatusMessage(2, ("IP:   " + ip.toString()).c_str());
  ws->setStatusMessage(3, "Waiting for commands");
  ws->setStatusMessage(4, "");
  ws->setStatusShort("IDLE");

  my_log("[WS] Dashboard: http://" + ip.toString() + ":" + String(AP_PORT));
}

void WIFI_loop() {
  ws->update();

  // ── Time sync ─────────────────────────────────────────────────────────
  if (ws->hasNewClientTime()) {
    tel_inc.time_management(ws->getClientEpoch());
  }

  // ── Control logic ─────────────────────────────────────────────────────
  ControlMode mode = ws->getMode();
  float pwrSP = ws->getPowerSetpoint();
  int lvlSP = ws->getLevelSetpoint();

  

  // ── Periodic my_log ──────────────────────────────────────────────────────
  if (ws->getAckErrors()) {
    ws->resetAck();
    ackErrors();
  }
}


void setup() {
  my_log_begin();
  delay(1000);

  pinMode(led_onboard, OUTPUT);

  digitalWrite(led_onboard, HIGH);

  while (millis() < 3000) {}

  my_log("Begin");

  Wire.begin();

  display.init();
  display.status = "Starte...";
  display.update();

  while (millis() < 6000) {}

  lora_module.init();
  tel_out.errors.gw_lora_fail = lora_module.loraError;
  if (!lora_module.loraError) {
    lora_module.beginReceive();
  }
  display.status = "WIFI init...";
  display.update();
  WIFI_init();

  tel_out.out_reciever_id = LORA_REMOTE_ID;
  tel_inc.out_reciever_id = LORA_REMOTE_ID;

  if (lora_module.loraError){
    display.status = "LoRa error..";
    tel_out.errors.gw_lora_fail = true;
  }else{
    display.status = "LoRa Connecting...";
  }
  display.update();
  /*
    ld.power = 254.2;
    ld.status = "Testing";
    ld.update();

    tel.dec_incoming_msg(incomeing);

    my_log(String(tel.operating_mode));
    my_log(String(tel.power));
    my_log(String(tel.preassure));
    tel.errors.cpu_preassure_error = true;
    tel.errors.cpu_voltage_error = true;

    sd.write_telegram(incomeing);

    my_log(tel.enc_outgoing_msg());*/

  //test_value.set_target(4.0);
  /*for (int i = -2; i < 12; i++) {
    nozzels = nz_con.setpoint_to_aq(i);
    my_log(String(i) + " " + String(nozzels[0]) + " " + String(nozzels[1]) + " " + String(nozzels[2]));
  };*/
};

void loop() {

  loracom();
  WIFI_loop();

  /*if (!waitingToAck && !waitingForAck && need_to_respond_to_rem_nod){
    senderState = SEND;
  }*/

  if (millis() - lastUpdateLcd > LCD_RATE) {
    lastUpdateLcd = millis();
    display.update();
  }

  if (millis() - led_blink_time > 2000) {
    led_blink_time = millis();
    led_State = !led_State;
  }
  digitalWrite(led_onboard, led_State);
};


