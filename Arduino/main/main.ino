#include <OptaBlue.h>
#include "opta_abs.h"
#include "WebserverAbstraction.h"
#include <WiFi.h>

//https://opta.findernet.com/en/tutorial/user-manual

//Wifi setup
WiFiServer server(80);

String NETWORK_SSID = "MicroHydro";
String NETWORK_PASSWORD = "Einstein123";
IPAddress serverIp = IPAddress(192, 168, 1, 1);

void setup(){
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {}

  Serial.println("*** Opta MicroHydroInit ***");
  OptaController.begin();

  while(OptaController.getExpansionNum() == 0) {
    Serial.println("Looking for expansions...");
    OptaController.update(); // call update to look for new expansion
    delay(1000);
  }

  /* analog expansion must be in position 0 */
  AnalogExpansion aexp = OptaController.getExpansion(0);

  if(aexp) {
     opta_abs cpu(aexp);

  }
  else {
    while(1) {
      Serial.println("NO Opta Analog found at position 0... looping forever...");
      delay(2000);
    }
  }

  // Wifi Setup


}

void loop(){
  //Eingabe
  cpu.read_inputs();
  //Verarbeitung
  blue_led_beat();


  
  //Ausgabe
  cpu.set_outputs();


}


unsigned long last_blink{0};
void blue_led_beat(){
  if (millis() - last_blink >= 2000){
    cpu.led_int_b = !cpu.led_int_b;
    last_blink = millis();
  }
}

void wifi_setup(){
  WiFi.config(serverIp);

    // Create Access Point.
    uint8_t isListening = WiFi.beginAP(NETWORK_SSID, NETWORK_PASSWORD) == WL_AP_LISTENING;
    if (!isListening)
    {
        digitalWrite(LEDR, HIGH);
        while (1);
    }
    // Start Web server.
    server.begin();
}




