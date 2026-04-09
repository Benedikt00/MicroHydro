#include <OptaBlue.h>
#include "opta_abs.h"

//https://opta.findernet.com/en/tutorial/user-manual

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

}

void loop(){



}