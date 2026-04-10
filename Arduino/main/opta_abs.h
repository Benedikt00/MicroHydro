#pragma once
#include <OptaBlue.h>

class opta_abs {
private:
  float adc_to_V(int reading) {
    return (float)reading * 10.0 / 4095.0;  //= reading / 12 bit res * Vmax
  }

public:
  AnalogExpansion& aexp;
  opta_abs(AnalogExpansion& analog_exp)
    : aexp(analog_exp) {
      setup_io();
  }

  //Analog in
  float water_temp_dC{ 0.0 };
  float surround_temp_dC{ 0.0 };
  float water_preassure_bar{ 0.0 };
  float voltage_V{ 0.0 };
  float current_A{ 0.0 };
  float valve1_fb_pc{ 0.0 };  //feedback
  float valve2_fb_pc{ 0.0 };  //feedback

  //Analog Out
  float valve1_cv_pc{ 0.0 };  //control_value in percent
  float valve2_cv_pc{ 0.0 };  //control_value in percent

  //Digital in
  bool ball_valve_open{ false };
  bool ball_valve_closed{ false };
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
  bool led_int_r{ false };
  bool led_int_b{ false };
  bool led_int_g{ false };

  //expansion leds
  bool led_exp_a_0{ false };
  bool led_exp_a_1{ false };
  bool led_exp_a_2{ false };
  bool led_exp_a_3{ false };
  bool led_exp_a_4{ false };
  bool led_exp_a_5{ false };
  bool led_exp_a_6{ false };
  bool led_exp_a_7{ false };


  void setup_io() {

    analogReadResolution(12);

    // ----------- CPU -----------

    pinMode(A0, INPUT);
    pinMode(A1, INPUT);
    pinMode(A2, INPUT);
    pinMode(A3, INPUT);
    pinMode(A4, INPUT);
    pinMode(A5, INPUT);
    pinMode(A6, INPUT);
    pinMode(A7, INPUT);

    pinMode(D0, OUTPUT);
    pinMode(D1, OUTPUT);
    pinMode(D2, OUTPUT);
    pinMode(D3, OUTPUT);

    pinMode(LEDG, OUTPUT);
    pinMode(LEDR, OUTPUT);
    pinMode(LEDB, OUTPUT);

    // ----------- Analog Expansion -----------

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

    //O1: Valve1
    aexp.beginChannelAsDac(4,                //channel index
                           OA_VOLTAGE_DAC,   //DAC type
                           false,            //limit current (set to false so it can power the sensor current loop)
                           false,            //No slew rate
                           OA_SLEW_RATE_0);  //Slew rate setting.

    //A5: Valve Feedback 1
    aexp.beginChannelAsAdc(5,               //channel index
                           OA_VOLTAGE_ADC,  //ADC type
                           true,            //No Pull Down
                           false,           //No rejection
                           false,           //No diagnosis
                           5);              //averageing 5 samples

    //A6: Valve Feedback 1
    aexp.beginChannelAsAdc(6,               //channel index
                           OA_VOLTAGE_ADC,  //ADC type
                           true,            //No Pull Down
                           false,           //No rejection
                           false,           //No diagnosis
                           5);              //averageing 5 samples

    //O1: Valve2
    aexp.beginChannelAsDac(7,                //channel index
                           OA_VOLTAGE_DAC,   //DAC type
                           false,            //limit current (set to false so it can power the sensor current loop)
                           false,            //No slew rate
                           OA_SLEW_RATE_0);  //Slew rate setting.
  }

  void read_inputs() {
    // ----------- CPU -----------
    ball_valve_closed = digitalRead(A0);
    ball_valve_open = digitalRead(A1);
    float_switch_triggered = digitalRead(A2);
    select_off = digitalRead(A3);
    select_level = digitalRead(A4);
    select_remote = digitalRead(A5);
    current_A = adc_to_V(analogRead(A6)); //umrechnung noch notwendig

    // ----------- Analog Expansion -----------

    water_temp_dC = aexp.getRtd(0);
    water_preassure_bar = aexp.pinVoltage(1); //umrechnung noch notwendig
    voltage_V = aexp.pinVoltage(3); //umrechnung noch notwendig
    valve1_fb_pc = aexp.pinVoltage(4)*10.0;
    valve2_fb_pc = aexp.pinVoltage(5)*10.0;
  }

  void write_outputs() {

    // ----------- CPU -----------
    //Leds
    digitalWrite(LED_D0, led_int_0);
    digitalWrite(LED_D1, led_int_1);
    digitalWrite(LED_D2, led_int_2);
    digitalWrite(LED_D3, led_int_3);
    digitalWrite(LEDR, led_int_r);
    digitalWrite(LEDG, led_int_g);
    digitalWrite(LEDB, led_int_b);

    digitalWrite(D0, ball_valve_cv);
    digitalWrite(D1, lamp_green);
    digitalWrite(D2, lamp_red);

    // ----------- Analog Expansion -----------
    aexp.pinVoltage(4, valve1_cv_pc / 10.0, true);
    aexp.pinVoltage(7, valve2_cv_pc / 10.0, true);
  }
};
