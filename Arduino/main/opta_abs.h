#pragma once
#include "OptaBlue.h"

class opta_abs {
  AnalogExpansion *aexp
  opta_abs(AnalogExpansion &analog_exp) {
    aexp = analog_exp
  }

  //Analog in
  float water_temp_°C{ 0.0 };
  float surround_temp_°C{ 0.0 };
  float water_preassure_bar{ 0.0 };
  float voltage_V{ 0.0 };
  float current_A{ 0.0 };
  float valve1_fb_ % { 0.0 };  //feedback
  float valve2_fb_ % { 0.0 };  //feedback

  //Analog Out
  float valve1_cv_ % { 0.0 };  //control_value
  float valve2_cv_ % { 0.0 };  //control_value

  //Digital in
  bool ball_valve_open{ false };
  bool float_switch_triggered{ false };
  bool select_off{ false };
  bool select_level{ false };
  bool select_remote{ false };

  //Digital Out
  bool ball_valve_cv{ false };
  bool lamp_green{ false };
  bool lamp_red{ false };

  //internal leds
  bool led_int_0{ false };
  bool led_int_1{ false };
  bool led_int_2{ false };
  bool led_int_3{ false };

  //expansion leds
  bool led_exp_a_0{ false };
  bool led_exp_a_1{ false };
  bool led_exp_a_2{ false };
  bool led_exp_a_3{ false };
  bool led_exp_a_4{ false };
  bool led_exp_a_5{ false };
  bool led_exp_a_6{ false };
  bool led_exp_a_7{ false };


  void setup_inputs() {

    // ----------- Expansion AI 1 -----------

    //A1: Water temperature
    aexp.beginChannelAsRtd(0,      //channel index
                           false,  //use 2 wires
                           1.0);   //1mA current (ignored in case of 2 wires)

    //A2: Preasure
    aexp.beginChannelAsAdc(0,               //channel index
                           OA_CURRENT_ADC,  //ADC type
                           false,           //Pull Down
                           false,           //No rejection
                           false,           //No diagnosis
                           5);              //averageing 5 sample

    //A3: Not used

    //A4: Voltage Meassurement
    aexp.beginChannelAsAdc(3,               //channel index
                           OA_VOLTAGE_ADC,  //ADC type
                           true,            //No Pull Down
                           false,           //No rejection
                           false,           //No diagnosis
                           5);              //averageing 5 samples

    //A5: Valve Feedback 1
    aexp.beginChannelAsAdc(4,               //channel index
                           OA_VOLTAGE_ADC,  //ADC type
                           true,            //No Pull Down
                           false,           //No rejection
                           false,           //No diagnosis
                           5);              //averageing 5 samples

    //A6: Valve Feedback 1
    aexp.beginChannelAsAdc(5,               //channel index
                           OA_VOLTAGE_ADC,  //ADC type
                           true,            //No Pull Down
                           false,           //No rejection
                           false,           //No diagnosis
                           5);              //averageing 5 samples
  }
}
