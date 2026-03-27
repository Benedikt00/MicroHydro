#include <RadioLib.h>

class LoRaCom {
public:
  bool LoRaError;
  LLCC68 radio;
  String incoming;
  volatile bool receivedFlag = false;

  static LoRaCom* _instance;  // for ISR callback
  static void onReceiveISR() {
    if (_instance) _instance->receivedFlag = true;
  }

  LoRaCom(int PIN_NSS, int PIN_DIO1, int PIN_RESET, int PIN_BUSY)
    : radio(new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY)) {
    LoRaError = true;
  }

  void init() {
    const float LORA_FREQUENCY = 868.0;
    const float LORA_BANDWIDTH = 125.0;
    const int LORA_SF = 9;
    const int LORA_CR = 7;
    const int LORA_SYNC_WORD = 0x12;
    const int LORA_TX_POWER = 22;
    const int LORA_PREAMBLE = 16;


    int state = radio.begin(
      LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SF, LORA_CR,
      LORA_SYNC_WORD, LORA_TX_POWER, LORA_PREAMBLE);

    if (state == RADIOLIB_ERR_NONE) {
      Serial.println(F("LoRa OK"));
      LoRaError = false;
    } else {
      Serial.print(F("LoRa FAILED, error: "));
      Serial.println(state);
    }

    _instance = this;
  }


  void beginReceive() {
    radio.setPacketReceivedAction(onReceiveISR);
    radio.startReceive();
  }

  void transmit(String payload) {
    int state = radio.transmit(payload);
    if (state == RADIOLIB_ERR_NONE) {
      Serial.println("TX OK: " + payload);
    } else if (state == RADIOLIB_ERR_PACKET_TOO_LONG) {
      Serial.println(F("TX FAILED — packet too long"));
    } else {
      Serial.print(F("TX FAILED — error: "));
      Serial.println(state);
    }
  }

  String old_msg;

  // Returns received string, or "" if nothing ready / error
  String receive() {
    if (!receivedFlag) return "";
    receivedFlag = false;

    int state = radio.readData(incoming);
    radio.startReceive();  // restart listen

    if (state == RADIOLIB_ERR_NONE) {
      if (old_msg != incoming) {
        old_msg = incoming;
        return incoming;

      } else {
        
        return "";
      }


    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.println(F("RX CRC mismatch"));
    } else {
      Serial.print(F("RX error: "));
      Serial.println(state);
    }
    return "";
  }
};

LoRaCom* LoRaCom::_instance = nullptr;