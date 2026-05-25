#include <my_log.h>
#include <mh_errors.h>

class telegram_management {

public:


  const int MSG_LENGTH = 35;
  const int MAX_TIME_DRIFT = 40;

  const int DEVICE_ID_SPOT = 10;

  int operating_mode;
  int inc_reciever_id;
  int out_reciever_id;
  float power;
  float level_pc;
  int level_cm;
  long unix_time{ 0 };
  Errors errors;

  int ack_out = 0;
  int ack_in = 0;

  telegram_management(int reciever_id){
    out_reciever_id = reciever_id;
  }

  struct timeval tv;

  unsigned long getTime() {
    time_t seconds = time(NULL);
    return (unsigned int)seconds;
  }

  void time_management(long unix_incomeing) {
    if ((unix_incomeing != 0) && (unix_time == 0)) {
      tv.tv_sec = unix_incomeing;
      settimeofday(&tv, NULL);
      my_log("Set time from 0 ");

    } else if (unix_incomeing == 0) {
      my_log("Time incoming 0, returning");
      return;
    }
    //time drift
    else if (labs(unix_incomeing - getTime()) > MAX_TIME_DRIFT) {
      tv.tv_sec = unix_incomeing;
      settimeofday(&tv, NULL);
      my_log("Set time from telegram");
    }

    unix_time = unix_incomeing;
  };

  void dec_incoming_msg(const String &msg) {

    if (msg.length() == MSG_LENGTH) {
      time_management(msg.substring(0, 10).toInt());
      inc_reciever_id = msg.substring(DEVICE_ID_SPOT, DEVICE_ID_SPOT + 1).toInt();
      power = msg.substring(11, 16).toFloat();
      level_pc = msg.substring(16, 20).toFloat();
      level_cm = msg.substring(20, 23).toFloat();
      operating_mode = msg.substring(23, 24).toInt();
      errors.alarm_msg = msg.substring(24, 34);
      errors.fromAlarmString();
      ack_in = msg.substring(34, 35).toInt();
    } else {
      my_log("Decoding Error, String length not ok");
    }
    my_log("=== Decoded Message ===");
    my_log("Raw msg:         " + msg);
    my_log("MSG_LENGTH:      " + String(MSG_LENGTH));
    my_log("msg.length():    " + String(msg.length()));
    my_log("time (0,10):     " + msg.substring(0, 10));
    my_log("device_id:       " + msg.substring(DEVICE_ID_SPOT, DEVICE_ID_SPOT + 1));
    my_log("power (11,16):   " + msg.substring(11, 16));
    my_log("pressure (16,20):" + msg.substring(16, 20));
    my_log("level_cm (20,23):   " + msg.substring(20, 23));
    my_log("op_mode (23,24): " + msg.substring(23, 24));
    my_log("alarm (24,34):   " + msg.substring(24, 34));
    my_log("ack_in (34,35):  " + msg.substring(34, 35));
    my_log("=======================");
  }

  String enc_outgoing_msg() {
    char buf[MSG_LENGTH + 1];  //string terminator

    if ((power > 999.9) || (power < 0.0)) {
      my_log("Power " + String(power));
      power = 0;
    }

    if ((level_pc > 99.9) || (level_pc < 0.0)) {
      my_log("level_pc " + String(level_pc));
      level_pc = 0;
    }

    if ((operating_mode > 9) || (operating_mode < 0)) {
      my_log("operating Mode " + String(operating_mode));
      operating_mode = 0;
    }

    if ((level_cm > 999) || (level_cm < 0)) {
      my_log("level_cm out of bounds " + String(level_cm));
      level_cm = 0;
    }

    if ((out_reciever_id > 9) || (out_reciever_id < 0)) {
      my_log("Reviever ID to high " + String(out_reciever_id));
      out_reciever_id = 0;
    }

    snprintf(buf, sizeof(buf), "%010d%1d%05.1f%04.1f%03d%01d%010s%01d",
             getTime(),
             out_reciever_id,
             power,
             level_pc,
             level_cm,
             operating_mode,
             errors.toAlarmString().c_str(),
             ack_out);

    return String(buf);
  }
};
