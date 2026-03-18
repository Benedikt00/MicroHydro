//#include "hw_lib.h"
#include "telegram_management.h"
#include "SD_management.h"
//#include "webserver.h"
#include "lcd_management.h"

int sd_cs = 5;

telegram_management tel;


String incomeing = "123.456.7100000000001773778573";

void setup(){
  
    Serial.begin(115200);
    while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
    }
    Serial.println("Bgin");

    SD_management sd(sd_cs);
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

    Serial.println(tel.enc_outgoing_msg());
    
};

void loop(){


};