#define LOG_PLATFORM_ESP32

#include <SD_management.h>
#include <lcd_management.h>
#include <lora.h>
#include <telegram_management.h>
#include <webserver.h>
#include <http_client.h>
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
 *   
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
const int WIFI_RECONNECTION_TIME = 5000;  //try to reconnect every 5 seconds
const int WIFI_POLLING_RATE = 3000;       //time after which cpu is requested again
unsigned long last_con_time = 0;
unsigned long last_suc_req_time = 0;

int last_wifi_req{ 0 };

static const IPAddress AP_IP(192, 168, 0, thisLORA_ID);
static const uint16_t AP_PORT = 80;

//INIT COMS
LoRaCom lora_module(LORA_SS, LORA_DIO1, LORA_RESET, LORA_BUSY);
SD_management sd(SD_SS);


telegram_management tel_to_gw;
telegram_management tel_from_gw;
telegram_management tel_to_cpu;
telegram_management tel_from_cpu;

lcd_management display;
unsigned long lastUpdate = 0;
const unsigned long LCD_RATE = 3000UL;  // refresh every 3 s

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

void ackFromCpu() {
  tel_to_gw.errors.gw_lora_fail = false;
  tel_to_gw.errors.remoteNode_not_reachable = false;
  tel_to_gw.errors.gw_lcd_fail = false;
  tel_to_gw.errors.gw_wlan_ini_fail = false;

  tel_to_gw.ack_out = 1;

  //senderState = SEND;
  waitingForAck = false;
  my_log("ACK ERRORS");
}

void ackFromESP() {
  tel_to_cpu.errors.gw_lora_fail = false;
  tel_to_cpu.errors.gateway_not_reachable = false;
  tel_to_cpu.errors.gw_lcd_fail = false;
  tel_to_cpu.errors.gw_wlan_ini_fail = false;

  tel_to_cpu.ack_out = 1;

  //senderState = SEND;
  waitingForAck = false;
  my_log("ESP ACK ERRORS");
}

String payload;
char buf[STATUS_MSG_LEN];


void loracom() {
  switch (senderState) {
    case RECIEVING:
      {
        String msg = lora_module.receive();

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
            retries_used += 1;

          } else {  //errors when retrys exceeded
            retries_used = 0;

            my_log("Lora Max retries exceedet");
            tel_to_cpu.errors.gateway_not_reachable = true;
            waitingForAck = false;

            senderState = RECIEVING;
          }
        }
        //Handle NEw message recieve
        if (msg != "") {
          my_log("New msg " + String(msg.length()));

          //check if any foreign characters in msg
          bool valid = true;
          for (int i = 0; i < msg.length(); i++) {
            if (!isDigit(msg[i]) && msg[i] != '.') {
              valid = false;
              break;
            }
          }
          if (!valid) {
            my_log("Invalid chars in msg: " + msg);
            if (msg.length() > 45){
              //wierd lora rec, reeint
              my_log("Lora Reint ");
              lora_module.init();
            }
            return;
          }
          if (msg.length() == tel_from_gw.MSG_LENGTH) {                                                              //right len
            if (msg.substring(tel_from_gw.DEVICE_ID_SPOT, tel_from_gw.DEVICE_ID_SPOT + 1).toInt() == thisLORA_ID) {  //for this node
              tel_from_gw.dec_incoming_msg(msg);

              sd.write_telegram(msg);

              tel_to_cpu.errors.gw_lora_fail = false;
              tel_to_cpu.errors.rn_lora_ini_fail = false;

              handle_gw_tel_in();
              lastRecieveTime = millis();
              my_log("MSG recieved " + msg);
              char buf[STATUS_MSG_LEN];

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

        //send ack if requested
        if (waitingToAck) {
          payload = "ACK";
          waitingToAck = false;
          //lora_send_reply = true;
        } else if (millis() - lastRecieveTime >= LORA_WAIT_TO_ACK_TIMEOUT){
          //set message
          payload = tel_to_gw.enc_outgoing_msg();
          waitingForAck = true;
          my_log("Waiting for ack");
        }
        //send
        lora_module.transmit(payload);
        lastSendTime = millis();

        if (lora_module.loraError && !(lora_module.loraStatus == -5)) {  // -5 = timeout, probably sent it anyways
          tel_to_cpu.errors.gw_lora_fail = true;
          my_log("Set Gateway Error");
          senderState = ERROR;
          break;
        }
        tel_to_cpu.errors.gw_lora_fail = false;

        // Switch to receive mode to listen for ACK

        my_log("Set Recieve");

        lora_module.beginReceive();
        senderState = RECIEVING;
        break;
      }

    case ERROR:
      {
        if (lora_module.loraError && !tel_to_cpu.errors.rn_lora_ini_fail && !tel_to_cpu.errors.gateway_not_reachable) {
          lora_module.init();
          if (!lora_module.loraError) {
            tel_to_cpu.errors.rn_lora_ini_fail = false;
            my_log("Set Send from Error");
            senderState = SEND;
          }
          break;
        }

        if (tel_to_cpu.errors.gateway_not_reachable) {
          senderState = RECIEVING;
          my_log("Set Recieve from Error");
          break;
        }
      }
  }
}

void write_error_to_display() {
  if (tel_to_gw.errors.rn_wlan_ini_fail) {
    display.set_error("Wlan fehler");
    return;
  }
  if (tel_to_gw.errors.cpu_not_reachable) {
    display.set_error("CPU nicht erreichbar");
    return;
  }
  if (tel_to_cpu.errors.rn_lora_ini_fail) {
    display.set_error("LoRa init. fehler");
    return;
  }
  if (tel_to_gw.errors.gateway_not_reachable) {
    display.set_error("LoRa nicht erreichbar");
    return;
  }

  if (tel_to_cpu.errors.rn_bmp_fail){
    display.set_error("BMP sensor failed");
    return;
  }

  if (tel_from_cpu.errors.cpu_es_triggered) {
    display.set_error("CPU NOTHALT");
    return;
  }
  if (tel_from_cpu.errors.level_station_not_reachable) {
    display.set_error("Messung nicht errei");
    return;
  }
  if (tel_from_cpu.errors.cpu_floater_triggered) {
    display.set_error("Schwimmer ausgelöst");
    return;
  }
  if (tel_from_cpu.errors.cpu_temp_to_low) {
    display.set_error("Wasser zu kalt");
    return;
  }
  if (tel_from_cpu.errors.cpu_temp_error_general) {
    display.set_error("Temperatur fehler");
    return;
  }
  if (tel_from_cpu.errors.cpu_power_error) {
    display.set_error("Pegelfehler");
    return;
  }
  if (tel_from_cpu.errors.cpu_power_error) {
    display.set_error("Leistungsfehler");
    return;
  }
  if (tel_from_cpu.errors.cpu_main_valve_error) {
    display.set_error("Kugelhahn NIO");
    return;
  }
  if (tel_from_cpu.errors.cpu_valve1_error) {
    display.set_error("Ventil 1 NIO");
    return;
  }
  if (tel_from_cpu.errors.cpu_valve2_error) {
    display.set_error("Ventil 2 NIO");
    return;
  }
  if (tel_from_cpu.errors.cpu_time_not_set) {
    display.set_error("bitte Zeit setzten");
    return;
  }
  if (tel_from_cpu.errors.rn_lcd_fail) {
    display.set_error("RemN LCD fehler");
    return;
  }
  if (tel_to_gw.errors.rn_sd_not_reachable) {
    display.set_error("RemN SD fehler");
    return;
  }
  if (tel_to_gw.errors.gw_wlan_ini_fail) {
    display.set_error("GW Wlan fehler");
    return;
  }
}

void error_management() {
  if (millis() - last_suc_req_time >= 60000) {
    tel_to_gw.errors.cpu_not_reachable;
  }
  if (millis() - lastRecieveTime >= 300000) {
    tel_to_gw.errors.remoteNode_not_reachable = true;
  }


  tel_to_gw.errors.rn_sd_not_reachable = sd.SD_error;

  write_error_to_display();
}

void handle_gw_tel_in() {

  tel_to_cpu.level_pc = tel_from_gw.level_pc;
  tel_to_cpu.power = tel_from_gw.power;
  tel_to_cpu.operating_mode = tel_from_gw.operating_mode;
  tel_to_cpu.ack_out = tel_from_gw.ack_in;
  tel_to_cpu.errors.gw_wlan_ini_fail = tel_from_gw.errors.gw_wlan_ini_fail;
}

void handle_cpu_tel_in() {

  tel_to_gw.level_pc = tel_from_cpu.level_pc;
  tel_to_gw.power = tel_from_cpu.power;
  tel_to_gw.ack_in = tel_from_cpu.ack_out;
  tel_to_gw.operating_mode = tel_from_cpu.operating_mode;

  tel_to_gw.errors.cpu_es_triggered = tel_from_cpu.errors.cpu_es_triggered;
  tel_to_gw.errors.cpu_power_error = tel_from_cpu.errors.cpu_power_error;
  tel_to_gw.errors.cpu_preassure_error = tel_from_cpu.errors.cpu_preassure_error;
  tel_to_gw.errors.cpu_temp_error_general = tel_from_cpu.errors.cpu_temp_error_general;
  tel_to_gw.errors.cpu_main_valve_error = tel_from_cpu.errors.cpu_main_valve_error;
  tel_to_gw.errors.cpu_valve1_error = tel_from_cpu.errors.cpu_valve1_error;
  tel_to_gw.errors.cpu_valve2_error = tel_from_cpu.errors.cpu_valve2_error;
  tel_to_gw.errors.cpu_com_to = tel_from_cpu.errors.cpu_com_to;
  tel_to_gw.errors.cpu_temp_to_low = tel_from_cpu.errors.cpu_temp_to_low;
  tel_to_gw.errors.cpu_reserve10 = tel_from_cpu.errors.cpu_reserve10;
  tel_to_gw.errors.cpu_reserve11 = tel_from_cpu.errors.cpu_reserve11;
  tel_to_gw.errors.cpu_reserve12 = tel_from_cpu.errors.cpu_reserve12;
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
    tel_to_cpu.errors.rn_bmp_fail = true;
    return false;
  }
  my_log("BMP init success");
  return true;
}

const String controlModeStr(int m) {
  switch (m) {
    case 0: return "UNBEKANNT";
    case 1: return "STOP";
    case 2: return "LEISTUNG";
    case 3: return "PEGEL";
    case 4: return "LEISTUNG N";
    case 5: return "PEGEL N";
    case 6: return "FUELLEN";
    default: return "UNBEKANNT";
  }
}

void WIFI_loop() {
  if (cpu_api.wifiConnected) {

    StaticJsonDocument<64> reqDoc;
    StaticJsonDocument<128> resDoc;

    if ((millis() - last_wifi_req >= WIFI_POLLING_RATE)) {
      last_wifi_req = millis();

      // ── Build payload ───────────────────────────────────────────────────
      reqDoc["temperature"] = bmp.readTemperature();  // or whichever temp field
      reqDoc["message"] = tel_to_cpu.enc_outgoing_msg();

      String payload;
      serializeJson(reqDoc, payload);

      // ── POST and parse response ─────────────────────────────────────────
      String response = cpu_api.httpPost(ESP_API_PATH, payload.c_str());
      if (response.length() == 0) {
        if (cpu_api.cpu_request_failed) {
          cpu_api.wifiConnected = false;
        }
        return;
      }

      DeserializationError err = deserializeJson(resDoc, response);
      if (err) {
        my_log("[WIFI] Response parse error: " + String(err.f_str()));
        return;
      }

      float power = resDoc["power"] | 0.0f;
      float level = resDoc["level"] | 0.0f;
      bool sendFlag = resDoc["sendFlag"] | false;
      String message = String(resDoc["message"] | "");
      const char* status = resDoc["status"] | "";
      last_con_time = millis();

      display.power = power;
      display.level = level;
      display.status = String("Regelart: ") + String(status);

      display.update();

      //olta jaaa, message zu versenden
      if (sendFlag && message.length() > 0 && (millis() - round_trip_start > 10000)) {
        bool valid = true;
        for (int i = 0; i < message.length(); i++) {
          if (!isDigit(message[i]) && message[i] != '.') {
            valid = false;
            break;
          }
        }
        if (!valid) {
          my_log("Invalid chars in msg: " + message);
          return;
        }
        my_log("[WIFI] Message from Opta, ready to send: " + String(message));
        // handle message
        tel_from_cpu.dec_incoming_msg(message);
        sd.write_telegram(message);
        handle_cpu_tel_in();
        senderState = SEND;
        wait_for_lora_round_trip = true;
        round_trip_start = millis();
        last_suc_req_time = millis();
      }
    }
    if (direct_send_cpu) {
      direct_send_cpu = false;

      // ── Build payload ───────────────────────────────────────────────────
      reqDoc["message"] = tel_to_cpu.enc_outgoing_msg();

      String payload;
      serializeJson(reqDoc, payload);

      // ── POST and parse response ─────────────────────────────────────────
      String response = cpu_api.httpPost(CPU_API_PATH, payload.c_str());
      if (response.length() == 0) {
        if (cpu_api.cpu_request_failed) {
          cpu_api.wifiConnected = false;
        }
        return;
      }

      DeserializationError err = deserializeJson(resDoc, response);
      if (err) {
        my_log("[WIFI] Response parse error: " + String(err.f_str()));
        return;
      }
    }
  } else {
    if (millis() - last_con_time > WIFI_RECONNECTION_TIME) {
      last_con_time = millis();
      if (cpu_api.connectWiFi(5000)) {
        my_log("WiIfi reinit ok");
      } else {
        my_log("!!!! WiIfi reinit failed !!!!!");
        display.error = "Wifi failed";
        tel_to_gw.errors.cpu_not_reachable = true;
      }
    }
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

  tel_to_cpu.errors.gw_lora_fail = lora_module.loraError;
  if (!lora_module.loraError) {
    lora_module.beginReceive();
  }

  tel_to_gw.out_reciever_id = LORA_GATEWAY_ID;
  tel_to_gw.out_reciever_id = LORA_GATEWAY_ID;

  if (!BMP_init()){
    tel_to_cpu.errors.rn_bmp_fail = true;
  }

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

  if (!wait_for_lora_round_trip) {
    WIFI_loop();
  }

  if (millis() - round_trip_start > round_trip_time_max_ms) {
    wait_for_lora_round_trip = false;
    waitingToAck = false;
    tel_to_cpu.errors.gateway_not_reachable = true;
  }

  error_management();


  digitalWrite(led_onboard, led_State);
  if (millis() - led_blink_time > 2000) {
    led_blink_time = millis();
    led_State = !led_State;
  }
};
