#include "Printable.h"

class monitor_window {
public:
  float vmin;
  float vmax;
  float max_error;
  int monitoring_time;

  float target_value{ 0.0 };

  unsigned long before{ 0 };

  monitor_window(float min, float max, float error_ip, int monitoring_time_ms) {
    vmin = min;
    vmax = max;
    max_error = error_ip;
    monitoring_time = monitoring_time_ms;
  }

  int set_target(float target_val) {
    if ((vmin <= target_val) && (vmax >= target_val)) {
      //target value in range
      target_value = target_val;
      before = millis();
      return 1;
    } else {  //target not in range
      return 0;
    }
  }

  int monitor(float current_value) {
    if ((vmin <= current_value) && (vmax >= current_value)) {                                                        //value in bound
      if ((millis() - before) > monitoring_time) {                                                                   //val should be set
        if ((current_value > target_value * (1 - max_error)) && (current_value < target_value * (1 + max_error))) {  //val in error bar
          return 1;
        } else {
          Serial.println("Value not in Error bar");
          return 0;
        }
      } else {
        return 1;
      }

    } else {
      //value out of bounds
      Serial.println("Value out of bounds");
      return 0;
    }
  };
};


class monitor_minmax {
public:
  float vmin;
  float vmax;

  monitor_minmax(float min, float max) {
    vmin = min;
    vmax = max;
  }

  int monitor(float current_value) {
    if ((vmin <= current_value) && (vmax >= current_value)) {  //value in bound
      return 1;
    } else {
      //value out of bounds
      Serial.println("Value out of bounds");
      return 0;
    }
  };
};


class monitor_bool {
public:

  int monitoring_time;

  bool target_value{ false };

  unsigned long before{ 0 };

  monitor_bool(int monitoring_time_ms) {
    monitoring_time = monitoring_time_ms;
  }

  int set_target(bool target_val) {
    target_value = target_val;
  }

  int monitor(float current_value) {
    if ((millis() - before) > monitoring_time) {  //val should be set
      if (current_value != target_value) {        //val in error bar
        return 1;
      } else {
        Serial.println("Value not ok");
        return 0;
      }
    } else {
      return 1;
    }
  };
};
