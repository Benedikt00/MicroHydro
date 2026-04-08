#include <SD_management.h>
//#include <lcd_management.h>
#include <lora.h>
#include <nozzle_control.h>
#include <telegram_management.h>
#include <value_monitoring.h>
#include <webserver.h>


#include <Wire.h>
#include <Adafruit_BMP280.h>

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
 *
 *  SD CS 16
*/


float test_mon = 0.5;

//monitor_window test_value(0.0, 5.0, 0.05, 3000);
int valok;

nz_controller nz_con(2.4, 200.0);

float* nozzels;

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
const int thisLORA_ID = LORA_REMOTE_ID;

const int SD_SS = 32;

const int LORA_MESSAGE_RETRYS = 3;
int retries_used = 0;

// ── Access Point config ───────────────────────────────────────────────────────
//  SSID must be ≤ 31 chars. Password must be ≥ 8 chars, or "" for open network.
const char* AP_SSID = "blackbird_2.0";
const char* AP_PASS = "groomlake";  // set "" for an open (no password) AP

const int WIFI_MAX_CONNECTION_TIME = 3000;
const int WIFI_POLLING_RATE = 3000; //time after which cpu is requested again
int last_wifi_req{0};

static const IPAddress AP_IP(192, 168, 0, thisLORA_ID);
static const uint16_t AP_PORT = 80;

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

bool lora_set_recieving = true;
bool lora_send_reply = false;

long last_msg = 0;

//temperature sensor
Adafruit_BMP280 bmp;
int BMP_ADDR = 0x76;

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
   
  tel.out_reciever_id = LORA_REMOTE_ID;

  BMP_init();

  //SD_management sd(SD_SS);

  http_client cpu_api();

  if (cpu_api.connectWiFi(AP_SSID, AP_PASS, WIFI_MAX_CONNECTION_TIME)){
    Serial.println("WiIfi init ok");
  } else {
    Serial.println("WiIfi init failed");
  }

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
/*
  if (millis() - last_msg > message_sender_time) {
    last_msg = millis();
    senderState = SEND;
  }*/
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

int BMP_init(){
  Wire.begin();

  // Read the actual chip ID so we can report it
  Wire.beginTransmission(BMP_ADDR);
  Wire.write(0xD0);  // chip ID register
  Wire.endTransmission(false);
  Wire.requestFrom(BMP_ADDR, 1);
  uint8_t chipId = Wire.read();
  Serial.print(F("BMP Chip ID: 0x"));
  Serial.println(chipId, HEX);
  // 0x60 = genuine BMP280, 0x56/0x58 = sample/clone BMP280, 0x61 = BME680

  // bmp.begin() normally rejects non-0x60 IDs.
  // Passing the chip ID as the second argument tells the library to accept it.
  if (!bmp.begin(0x76, chipId)) {
    Serial.println(F("BMP ERROR: could not initialise BMP280."));
    Serial.println(F("Try power-cycling the sensor"));
    return false;
  }
  Serial.println("BMP init success");
  return true;
}


void WIFI_loop() {
  if(cpu_api.wifi_connected){
    if (millis() - last_wifi_req >= WIFI_POLLING_RATE){
      
    }
  }
}
