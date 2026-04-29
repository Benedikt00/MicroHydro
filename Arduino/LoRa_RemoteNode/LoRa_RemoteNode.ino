#include <SD_management.h>
#include <lcd_management.h>
#include <lora.h>
#include <nozzle_control.h>
#include <telegram_management.h>
#include <value_monitoring.h>
#include <webserver.h>
#include <http_client.h>
#define LOG_PLATFORM_ESP32
#include <my_log.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

#include <ArduinoJson.h>

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
 *
 *  SD CS 16
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
const int thisLORA_ID = LORA_REMOTE_ID;

const int SD_SS = 15;

const int LORA_MESSAGE_RETRYS = 3;
int retries_used = 0;

// ── Access Point config ───────────────────────────────────────────────────────
//  SSID must be ≤ 31 chars. Password must be ≥ 8 chars, or "" for open network.
const char* AP_SSID = "MicroHydro";
const char* AP_PASS = "Einstein123";  // set "" for an open (no password) AP

const int WIFI_MAX_CONNECTION_TIME = 3000;
const int WIFI_POLLING_RATE = 3000;  //time after which cpu is requested again
int last_wifi_req{ 0 };

static const IPAddress AP_IP(192, 168, 0, thisLORA_ID);
static const uint16_t AP_PORT = 80;

//INIT COMS
LoRaCom lora_module(LORA_SS, LORA_DIO1, LORA_RESET, LORA_BUSY);

telegram_management tel;

lcd_management display;
unsigned long lastUpdate = 0;
const unsigned long LCD_RATE = 3000UL;   // refresh every 1 s

float measuredPower = 0.0f;
int measuredLevel = 0;
uint32_t deviceEpoch = 0;
unsigned long lastRtcUpdate = 0;

bool lora_startup_error;
bool direct_send_cpu = false;

enum SenderState { SEND,
                   RECIEVING,
                   ERROR };

SenderState senderState = RECIEVING;

unsigned long lastSendTime = 0;
unsigned long lastRecieveTime = 0;
int packetCounter = 0;

unsigned long ackSendTime = 0;
unsigned long last_ack = 0;

static bool waitingToAck = false;
static bool waitingForAck = false;
String pendingMsg = "";

int message_sender_time = 20000;
long LORA_WAIT_TO_ACK_TIMEOUT = 400;
long LORA_WAIT_FOR_ACK = 5000;
long LORA_AFTER_SEND_TIMEOUT = 200;
long LORA_REPLY_TIME = 500;

//bool lora_set_recieving = true;
bool lora_send_reply = false;
bool wait_for_lora_round_trip = false;
int round_trip_time_max_ms = 10000;
unsigned long round_trip_start;

long last_msg = 0;

//temperature sensor
Adafruit_BMP280 bmp;
int BMP_ADDR = 0x76;

http_client cpu_api(AP_SSID, AP_PASS, "http://192.168.3.1/");
const char* ESP_API_PATH = "api/sensor";
const char* CPU_API_PATH = "api";

void ackErrors() {
  tel.errors.gw_lora_fail = false;
  tel.errors.remoteNode_not_reachable = false;
  tel.errors.gw_lcd_fail = false;
  tel.errors.gw_wlan_ini_fail = false;
  tel.ack_out = 1;
  //senderState = SEND;
  waitingForAck = false;
  my_log("ACK ERRORS");
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
          if (msg != "") {
            my_log("^^^^ Trown away");
          }
          break;
        }

        //set send to reply after timeoute
        if (lora_send_reply && !waitingForAck && (millis() - lastSendTime >= LORA_REPLY_TIME)) {
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

          snprintf(buf, sizeof(buf), "SNR: %.1f", lora_module.radio.getSNR());

          waitingForAck = false;
          //lora_send_reply = true;
          break;
        }
        //ack running intop timeout
        else if (waitingForAck && (millis() - lastSendTime > LORA_WAIT_FOR_ACK)) {
          my_log("ACK timeout, retrying... " + String(retries_used));
          snprintf(buf, sizeof(buf), "ACK timeout, retrying... %1d", retries_used);
          //Telegram repetition when not acknowledged
          if (retries_used < LORA_MESSAGE_RETRYS) {
            senderState = SEND;
            retries_used +=1;

          } else {  //errors when retrys exceeded
            retries_used = 0;
            senderState = ERROR;

            my_log("Lora Max retries exceedet");
            tel.errors.remoteNode_not_reachable = true;
            snprintf(buf, sizeof(buf), "Lora Max retries exceedet, listening");
            waitingForAck = false;

            senderState = RECIEVING;
          }
        }
        //Handle NEw message recieve
        if (msg != "") {
          my_log("New msg " + String(msg.length()));

          if (msg.length() == tel.MSG_LENGTH) {  //right len
            my_log("Right len, for node " + msg.substring(tel.DEVICE_ID_SPOT, tel.DEVICE_ID_SPOT + 1));
            if (msg.substring(tel.DEVICE_ID_SPOT, tel.DEVICE_ID_SPOT + 1).toInt() == thisLORA_ID) {  //for this node
              tel.dec_incoming_msg(msg);
              lastRecieveTime = millis();
              my_log("MSG recieved " + msg);
              char buf[STATUS_MSG_LEN];

              snprintf(buf, sizeof(buf), "Received: %s  RSSI: %d dBm", msg, lora_module.radio.getRSSI());

              snprintf(buf, sizeof(buf), "SNR: %.1f", lora_module.radio.getSNR());
              waitingToAck = true;
              //lora_set_recieving = true;
              wait_for_lora_round_trip = false;
              direct_send_cpu = true; 
            }
          }
        }
        break;
      }

    case SEND:
      {
        //return if error
        if (tel.errors.gw_lora_fail) {
          senderState = ERROR;
          break;
        }
        //send ack if requested
        if (waitingToAck && (millis() - lastRecieveTime >= LORA_WAIT_TO_ACK_TIMEOUT)) {
          payload = "ACK";
          waitingToAck = false;
          my_log("Sending ack after: " + String(lastRecieveTime) + " " + String(millis() - lastRecieveTime) + " TO: " + String(LORA_WAIT_TO_ACK_TIMEOUT));

          //lora_send_reply = true;
        } else {
          //set message
          payload = tel.enc_outgoing_msg();
          waitingForAck = true;
          my_log("Waiting for ack");
        }

        //send
        lora_module.transmit(payload);

        if (lora_module.loraError) {
          tel.errors.gw_lora_fail = true;
          my_log("Set Gateway Error");
          senderState = ERROR;
          break;
        }
        tel.errors.gw_lora_fail = false;

        // Switch to receive mode to listen for ACK

        lastSendTime = millis();
        my_log("Set Recieve");

        lora_module.beginReceive();
        senderState = RECIEVING;
        break;
      }

    case ERROR:
      {
        if (!tel.errors.rn_lora_ini_fail && !tel.errors.gateway_not_reachable) {
          lora_module.init();
          if (!lora_module.loraError) {
            tel.errors.remoteNode_not_reachable = false;
            my_log("Set Send from Error");
            senderState = SEND;
          }
          break;
        }
        if (tel.errors.gateway_not_reachable){
            senderState = RECIEVING;
            my_log("Set Recieve from Error");
            break;
        }
      }
  }
}

int BMP_init() {
  Wire.begin();

  // Read the actual chip ID so we can report it
  Wire.beginTransmission(BMP_ADDR);
  Wire.write(0xD0);  // chip ID register
  Wire.endTransmission(false);
  Wire.requestFrom(BMP_ADDR, 1);
  uint8_t chipId = Wire.read();
  Serial.print(("BMP Chip ID: 0x"));
  my_log(String(chipId));
  // 0x60 = genuine BMP280, 0x56/0x58 = sample/clone BMP280, 0x61 = BME680

  // bmp.begin() normally rejects non-0x60 IDs.
  // Passing the chip ID as the second argument tells the library to accept it.
  if (!bmp.begin(0x76, chipId)) {
    my_log(F("BMP ERROR: could not initialise BMP280."));
    my_log(F("Try power-cycling the sensor"));
    return false;
  }
  my_log("BMP init success");
  return true;
}


void WIFI_loop() {
  if (cpu_api.wifiConnected) {
    
    StaticJsonDocument<64> reqDoc;
    StaticJsonDocument<128> resDoc;

    if ((millis() - last_wifi_req >= WIFI_POLLING_RATE)) {
      last_wifi_req = millis();

      // ── Build payload ───────────────────────────────────────────────────
      reqDoc["temperature"] = bmp.readTemperature();  // or whichever temp field
      reqDoc["message"] = tel.enc_outgoing_msg();

      String payload;
      serializeJson(reqDoc, payload);

      // ── POST and parse response ─────────────────────────────────────────
      String response = cpu_api.httpPost(ESP_API_PATH, payload.c_str());
      if (response.length() == 0) return;

      DeserializationError err = deserializeJson(resDoc, response);
      if (err) {
        my_log("[WIFI] Response parse error: " + String(err.f_str()));
        return;
      }

      float power = resDoc["power"] | 0.0f;
      bool sendFlag = resDoc["sendFlag"] | false;
      const char* message = resDoc["message"] | "";
      const char* status = resDoc["status"] | "";

      Serial.printf("[WIFI] power=%.1f  sendFlag=%d  msg=%s\n", power, sendFlag, message);

      display.power = power;
      display.status = String("Regelart: ") + String(status);
      display.update();

      //olta jaaa, message zu versenden
      if (sendFlag && strlen(message) > 0) {
        my_log("[WIFI] Message from Opta, ready to send: " + String(message));
        // handle message 
        tel.dec_incoming_msg(message);
        senderState = SEND;
        wait_for_lora_round_trip = true;
        round_trip_start = millis();
      }
    }
    if (direct_send_cpu){
      direct_send_cpu = false;

      // ── Build payload ───────────────────────────────────────────────────
      reqDoc["message"] = tel.enc_outgoing_msg();

      String payload;
      serializeJson(reqDoc, payload);

      // ── POST and parse response ─────────────────────────────────────────
      String response = cpu_api.httpPost(CPU_API_PATH, payload.c_str());
      if (response.length() == 0) return;

      DeserializationError err = deserializeJson(resDoc, response);
      if (err) {
        my_log("[WIFI] Response parse error: " + String(err.f_str()));
        return;
      }
    }
  }else{
    //todo reconnect handling
  }
};

void setup() {
  my_log_begin();
  Serial.available();
  pinMode(led_onboard, OUTPUT);

  digitalWrite(led_onboard, HIGH);

  while (millis() < 2000) {}

  my_log("Begin");

  Wire.begin();

  display.init();
  display.status = "Starte...";
  display.update();

  lora_module.init();

  tel.errors.gw_lora_fail = lora_module.loraError;
  if (!lora_module.loraError) {
    lora_module.beginReceive();
  }

  tel.out_reciever_id = LORA_GATEWAY_ID;

  BMP_init();

  //SD_management sd(SD_SS);


  if (cpu_api.connectWiFi(5000)) {
    my_log("WiIfi init ok");
  } else {
    my_log("!!!! WiIfi init failed !!!!!");
    display.error = "Wifi failed";
    delay(2000);
  }

   
  
    
/*
    tel.dec_incoming_msg(incomeing);

    Serial.print(String(tel.operating_mode));
    Serial.print(String(tel.power));
    my_log(String(tel.preassure));
    tel.errors.cpu_preassure_error = true;
    tel.errors.cpu_voltage_error = true;

    sd.write_telegram(incomeing);

    my_log(tel.enc_outgoing_msg());*/

  //test_value.set_target(4.0);
};


void loop() {

  loracom();

  if (!wait_for_lora_round_trip){
    WIFI_loop();
  }

  if (millis() - round_trip_start > round_trip_time_max_ms){
    wait_for_lora_round_trip = false;
    waitingToAck = false;
    
  }

  digitalWrite(led_onboard, led_State);

  if (millis() - led_blink_time > 2000) {
    led_blink_time = millis();
    led_State = !led_State;
  }

};
