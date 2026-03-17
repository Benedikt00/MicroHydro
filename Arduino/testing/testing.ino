//#include "hw_lib.h"
#include "telegram_management.h"
//#include "webserver.h"

telegram_management tel;


void setup(){
  Serial.begin(115200);
};

void loop(){
  Serial.println("Bgin");
  tel.dec_incoming_msg("123.456.7100000000001773778573");

  Serial.print(String(tel.operating_mode));
  Serial.print(String(tel.power));
  Serial.println(String(tel.preassure));
  tel.errors.cpu_preassure_error = true;
  tel.errors.cpu_voltage_error = true;

  Serial.println(tel.enc_outgoing_msg());

};