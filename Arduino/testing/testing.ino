//#include "hw_lib.h"
//#include "telegram_management.h"
//#include "SD_management.h"
//#include "webserver.h"
//#include "lcd_management.h"
#include "value_monitoring.h"
#include "nozzle_control.h"

int sd_cs = 5;

//telegram_management tel;

float test_mon = 0.5;

String incomeing = "123.456.7100000000001773778573";

//monitor_window test_value(0.0, 5.0, 0.05, 3000);
int valok;

nz_controller nz_con(2.4, 200.0);

float* nozzels;

void setup(){
  
    Serial.begin(115200);
    while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
    }
    Serial.println("Begin");

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
    for (int i = -2; i < 12; i++){
        nozzels = nz_con.setpoint_to_aq(i);
        Serial.println(String(i) + " " + String(nozzels[0]) + " " + String(nozzels[1]) + " " + String(nozzels[2]));
    };
    
};

void loop(){
    //valok = test_value.monitor(test_mon);
    //test_mon += 0.001;
    //Serial.println(test_mon + String(valok));
    sleep(0.2);
    

};