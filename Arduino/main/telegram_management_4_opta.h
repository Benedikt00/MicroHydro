

class telegram_management {

public:

  class Errors {
  public:
    bool alarms[32];


    //General Errors
    bool cpu_not_reachable;
    bool remoteNode_not_reachable;
    bool gateway_not_reachable;
    bool level_station_not_reachable;
    bool general_reserve4;
    bool general_reserve5;
    bool general_reserve6;

    //Remot Note Errors
    bool rn_sd_not_reachable;
    bool rn_lora_ini_fail;
    bool rn_wlan_ini_fail;
    bool rn_lcd_fail;
    bool rn_reserve4;
    bool rn_reserve5;

    //Gateway Errors
    bool gw_lcd_fail;
    bool gw_lora_fail;
    bool gw_wlan_ini_fail;
    bool gw_reserve3;
    bool gw_reserve4;
    bool gw_reserve5;

    //CPU Errors
    bool cpu_es_triggered;
    bool cpu_voltage_error;
    bool cpu_preassure_error;
    bool cpu_temp_error;
    bool cpu_floater_triggered;
    bool cpu_main_valve_error;
    bool cpu_valve1_error;
    bool cpu_valve2_error;
    bool cpu_com_to;
    bool cpu_temp_to_low;
    bool cpu_reserve10;
    bool cpu_reserve11;
    bool cpu_reserve12;

    String alarm_msg;



    void
    fromAlarmString() {
      // Convert 10-char string to long
      long alarmVal = alarm_msg.toInt();

      // Map bits to bool variables
      // General Errors (bits 0-6)
      cpu_not_reachable = (alarmVal >> 0) & 1;
      remoteNode_not_reachable = (alarmVal >> 1) & 1;
      gateway_not_reachable = (alarmVal >> 2) & 1;
      level_station_not_reachable = (alarmVal >> 3) & 1;
      general_reserve4 = (alarmVal >> 4) & 1;
      general_reserve5 = (alarmVal >> 5) & 1;
      general_reserve6 = (alarmVal >> 6) & 1;

      // Remote Node Errors (bits 7-12)
      rn_sd_not_reachable = (alarmVal >> 7) & 1;
      rn_lora_ini_fail = (alarmVal >> 8) & 1;
      rn_wlan_ini_fail = (alarmVal >> 9) & 1;
      rn_lcd_fail = (alarmVal >> 10) & 1;
      rn_reserve4 = (alarmVal >> 11) & 1;
      rn_reserve5 = (alarmVal >> 12) & 1;

      // Gateway Errors (bits 13-18)
      gw_lcd_fail = (alarmVal >> 13) & 1;
      gw_lora_fail = (alarmVal >> 14) & 1;
      gw_wlan_ini_fail = (alarmVal >> 15) & 1;
      gw_reserve3 = (alarmVal >> 16) & 1;
      gw_reserve4 = (alarmVal >> 17) & 1;
      gw_reserve5 = (alarmVal >> 18) & 1;

      // CPU Errors (bits 19-30)
      cpu_es_triggered = (alarmVal >> 19) & 1;
      cpu_voltage_error = (alarmVal >> 20) & 1;
      cpu_preassure_error = (alarmVal >> 21) & 1;
      cpu_temp_error = (alarmVal >> 22) & 1;
      cpu_floater_triggered = (alarmVal >> 23) & 1;
      cpu_main_valve_error = (alarmVal >> 24) & 1;
      cpu_valve1_error = (alarmVal >> 25) & 1;
      cpu_valve2_error = (alarmVal >> 26) & 1;
      cpu_com_to = (alarmVal >> 27) & 1;
      cpu_temp_to_low = (alarmVal >> 28) & 1;
      cpu_reserve10 = (alarmVal >> 29) & 1;
      cpu_reserve11 = (alarmVal >> 30) & 1;
    }

    // Variables → long → String (len 10)
    String toAlarmString() {
      long alarmVal = 0;

      // General Errors (bits 0-6)
      alarmVal |= ((long)cpu_not_reachable << 0);
      alarmVal |= ((long)remoteNode_not_reachable << 1);
      alarmVal |= ((long)gateway_not_reachable << 2);
      alarmVal |= ((long)level_station_not_reachable << 3);
      alarmVal |= ((long)general_reserve4 << 4);
      alarmVal |= ((long)general_reserve5 << 5);
      alarmVal |= ((long)general_reserve6 << 6);

      // Remote Node Errors (bits 7-12)
      alarmVal |= ((long)rn_sd_not_reachable << 7);
      alarmVal |= ((long)rn_lora_ini_fail << 8);
      alarmVal |= ((long)rn_wlan_ini_fail << 9);
      alarmVal |= ((long)rn_lcd_fail << 10);
      alarmVal |= ((long)rn_reserve4 << 11);
      alarmVal |= ((long)rn_reserve5 << 12);

      // Gateway Errors (bits 13-18)
      alarmVal |= ((long)gw_lcd_fail << 13);
      alarmVal |= ((long)gw_lora_fail << 14);
      alarmVal |= ((long)gw_wlan_ini_fail << 15);
      alarmVal |= ((long)gw_reserve3 << 16);
      alarmVal |= ((long)gw_reserve4 << 17);
      alarmVal |= ((long)gw_reserve5 << 18);

      // CPU Errors (bits 19-30)
      alarmVal |= ((long)cpu_es_triggered << 19);
      alarmVal |= ((long)cpu_voltage_error << 20);
      alarmVal |= ((long)cpu_preassure_error << 21);
      alarmVal |= ((long)cpu_temp_error << 22);
      alarmVal |= ((long)cpu_floater_triggered << 23);
      alarmVal |= ((long)cpu_main_valve_error << 24);
      alarmVal |= ((long)cpu_valve1_error << 25);
      alarmVal |= ((long)cpu_valve2_error << 26);
      alarmVal |= ((long)cpu_com_to << 27);
      alarmVal |= ((long)cpu_temp_to_low << 28);
      alarmVal |= ((long)cpu_reserve10 << 29);
      alarmVal |= ((long)cpu_reserve11 << 30);

      // Pad to exactly 10 characters with leading zeros
      String result = String(alarmVal);
      while (result.length() < 10) result = "0" + result;

      return result;
    };
  };

  const int MSG_LENGTH = 35;
  const int MAX_TIME_DRIFT = 40;

  const int DEVICE_ID_SPOT = 10;

  int operating_mode;
  int inc_reciever_id;
  int out_reciever_id;
  float power;
  float preassure;
  int level;
  long unix_time{ 0 };
  Errors errors;

  int ack_out = 0;
  int ack_in = 0;

  struct timeval tv;

  unsigned long getTime() {
    time_t seconds = time(NULL);
    return (unsigned int)seconds;
  }


  void time_management(long unix_incomeing) {
      if ((unix_incomeing != 0) && (unix_time == 0)) {
        tv.tv_sec = unix_incomeing;
        settimeofday(&tv, NULL);

        Serial.println("Set time from 0 ");
      } else if (unix_incomeing == 0)
      {
        Serial.println("Time incoming 0, returning");
        return;
      }
          //time drift
        else if (labs(unix_incomeing - getTime()) > MAX_TIME_DRIFT) {
        tv.tv_sec = unix_incomeing;
        settimeofday(&tv, NULL);
        Serial.println("Set time from telegram");
      }
      
      unix_time = unix_incomeing;
    };

  void dec_incoming_msg(const String &msg) {

    if (msg.length() == MSG_LENGTH) {
      time_management(msg.substring(0, 10).toInt());
      inc_reciever_id = msg.substring(DEVICE_ID_SPOT, DEVICE_ID_SPOT + 1).toInt();
      power = msg.substring(11, 16).toFloat();
      preassure = msg.substring(16, 20).toFloat();
      level = msg.substring(20, 23).toFloat();
      operating_mode = msg.substring(23, 24).toInt();
      errors.alarm_msg = msg.substring(24, 34);
      errors.fromAlarmString();
      ack_in = msg.substring(34, 35).toInt();
    } else {
      Serial.println("Decoding Error, String length not ok");
    }
    Serial.println("=== Decoded Message ===");
    Serial.println("Raw msg:         " + msg);
    Serial.println("MSG_LENGTH:      " + String(MSG_LENGTH));
    Serial.println("msg.length():    " + String(msg.length()));
    Serial.println("time (0,10):     " + msg.substring(0, 10));
    Serial.println("device_id:       " + msg.substring(DEVICE_ID_SPOT, DEVICE_ID_SPOT + 1));
    Serial.println("power (11,16):   " + msg.substring(11, 16));
    Serial.println("pressure (16,20):" + msg.substring(16, 20));
    Serial.println("level (20,23):   " + msg.substring(20, 23));
    Serial.println("op_mode (23,24): " + msg.substring(23, 24));
    Serial.println("alarm (24,34):   " + msg.substring(24, 34));
    Serial.println("ack_in (34,35):  " + msg.substring(34, 35));
    Serial.println("=======================");
  }

  String enc_outgoing_msg() {
    char buf[MSG_LENGTH + 1];  //string terminator

    if (power > 999.9) {
      Serial.println("Power " + String(power));
      return "0";
    }

    if (preassure > 99.9) {
      Serial.println("Preassure " + String(preassure));
      return "0";
    }

    if (operating_mode > 9) {
      Serial.println("operating Mode " + String(operating_mode));
      return "0";
    }

    if (level > 999){
      Serial.println("level to high " + String(level));
      return "0";
    }
    if (out_reciever_id > 9){
      Serial.println("Reviever ID to high " + String(level));
      return "0";
    }

    Serial.println(unix_time);

    snprintf(buf, sizeof(buf), "%010d%1d%05.1f%04.1f%03d%01d%-010s%01d",
             getTime(),
             out_reciever_id, 
             power,
             preassure,
             level,
             operating_mode,
             errors.toAlarmString().c_str(),
             ack_out);
             
    return String(buf);
  }
};
