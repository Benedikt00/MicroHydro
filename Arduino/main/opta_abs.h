#pragma once
#include <OptaBlue.h>

class opta_abs {
private:
  int device_index;  // Store index instead of reference

  float adc_to_V(int reading) {
    return (float)reading * 11.0 / 4095.0;
  }

public:
  opta_abs(int dev_idx)
    : device_index(dev_idx) {
    setup_io();
  }

  //Analog in
  float water_temp_dC{ 0.0 };
  float surround_temp_dC{ 0.0 };
  float water_preassure_bar{ 0.0 };
  float level_meassured_p{ 0.0 };
  float voltage_V{ 0.0 };
  float current_A{ 0.0 };
  float valve1_fb_pc{ 0.0 };
  float valve2_fb_pc{ 0.0 };

  //Analog Out
  float valve1_cv_pc = 1.0;
  float valve2_cv_pc = 1.0;

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

    // ----------- Analog Expansion (static API) -----------

    // A1 (ch 0): Water temperature — 3-wire RTD, 1.2 mA
    AnalogExpansion::beginChannelAsRtd(OptaController,
                                       device_index,
                                       0,      // channel
                                       false,  // 3-wire
                                       0.8);   // 0.8 mA

    // A2 (ch 1): Pressure — 4–20 mA current input
    AnalogExpansion::beginChannelAsCurrentAdc(OptaController,
                                              device_index,
                                              1);

    // A3 (ch 2): Not used

    // A4 (ch 3): Voltage measurement
    AnalogExpansion::beginChannelAsVoltageAdc(OptaController,
                                              device_index,
                                              3);  // channel

    // O1 (ch 4): Valve 1 — voltage DAC, 0–10 V
    AnalogExpansion::beginChannelAsDac(OptaController,
                                       device_index,
                                       4,  // channel
                                       OA_VOLTAGE_DAC,
                                       false,  // no current limit
                                       false,  // no slew rate
                                       OA_SLEW_RATE_0);

    // A5 (ch 5): Valve 1 feedback — voltage ADC
    AnalogExpansion::beginChannelAsVoltageAdc(OptaController,
                                              device_index,
                                              5);

    // A6 (ch 6): Valve 2 feedback — voltage ADC
    AnalogExpansion::beginChannelAsVoltageAdc(OptaController,
                                              device_index,
                                              6);

    // O2 (ch 7): Valve 2 — voltage DAC, 0–10 V
    AnalogExpansion::beginChannelAsDac(OptaController,
                                       device_index,
                                       7,  // channel
                                       OA_VOLTAGE_DAC,
                                       false,  // no current limit
                                       false,  // no slew rate
                                       OA_SLEW_RATE_0);

    delay(500);

    // Verify DAC channels via a fresh getExpansion handle
    AnalogExpansion aexp = OptaController.getExpansion(device_index);
    if (!aexp.isChVoltageDac(4)) {
      Serial.println("Channel 4 (Valve 1) NOT set as voltage DAC");
    } else {
      Serial.println("Channel 4 (Valve 1) set OK");
    }
    if (!aexp.isChVoltageDac(7)) {
      Serial.println("Channel 7 (Valve 2) NOT set as voltage DAC");
    } else {
      Serial.println("Channel 7 (Valve 2) set OK");
    }

    Serial.println("---- IO Set ----");
  }

  void set_nozzle_state(bool ball, float n1, float n2) {
    ball_valve_cv = ball;
    valve1_cv_pc = n1;
    valve2_cv_pc = n2;
  }

  float max_pr = 6.0;
  float p_high = 2.4516;
  float p_low = 2.157;

  float i_to_level_p(float i_in) {
    float p_measured = (i_in - 4.0) / 16.0 * max_pr;
    float dp = p_high - p_low;
    float level = (p_measured - p_low) / dp * 100.0;
    return level;
  }

  float get_meassured_power_W() {
    return voltage_V * current_A;
  }

  // RTD constants
  float a = 0.0039083;
  float b = -0.0000005775;

  void read_inputs() {
    // ----------- CPU -----------
    ball_valve_closed = digitalRead(A0);
    ball_valve_open = digitalRead(A1);
    float_switch_triggered = digitalRead(A2);
    select_off = digitalRead(A3);
    select_level = digitalRead(A4);
    select_remote = true;  //digitalRead(A5); todo
    current_A = adc_to_V(analogRead(A6));

    // ----------- Analog Expansion (fresh handle each cycle) -----------
    AnalogExpansion aexp = OptaController.getExpansion(device_index);

    water_temp_dC = (-(1.0 / 100.0) * (50.0 * a - 10*sqrt(b * aexp.getRtd(0) + 25.0 * pow(a, 2.0) - 100.0 * b))) / b;

    water_preassure_bar = aexp.pinVoltage(1);  
    voltage_V = aexp.pinVoltage(3);            //todo umrechnen
    valve1_fb_pc = aexp.pinVoltage(5) * 10.0;
    valve2_fb_pc = aexp.pinVoltage(6) * 10.0;

    level_meassured_p = i_to_level_p(water_preassure_bar);
  }

  void write_outputs() {
    // ----------- CPU -----------
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
    AnalogExpansion aexp = OptaController.getExpansion(device_index);

    float v1_V = valve1_cv_pc / 10.0;
    float v2_V = valve2_cv_pc / 10.0;

    aexp.pinVoltage(4, v1_V);
    aexp.pinVoltage(7, v2_V);
  }
};