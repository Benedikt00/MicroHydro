#include "OptaBlue.h"
#include "hw_lib.h"
#include "webserver.h"

//https://opta.findernet.com/en/tutorial/user-manual

//Web Server Config
OptaBoardInfo *info;
OptaBoardInfo *boardInfo();
info = boardInfo();

IPAddress ip(192, 168, 0, 3);
int port = 80;

WebserverAbstraction Webserver(info, ip, port)


void setup(){
  Serial.begin(115200);
  OptaController.begin();
  OptaAbstraction cpu();
}

void loop(){





}