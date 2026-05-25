// ============================================================
//  MicroHydro Control Firmware
//  Arduino / Opta platform
// ============================================================
#define LOG_PLATFORM_OPTA

#include <OptaBlue.h>
#include "opta_abs.h"
#include "WebserverAbstraction.h"
#include "pi_controller_power.h"
#include "time_management.h"
#include "telegram_management_4_opta.h"
#include <WiFi.h>
#include <WiFiServer.h>
//#include "opta_wifi_ap.h"
#include <ArduinoJson.h>
#include <my_log.h>
#include <value_monitoring.h>

// Fill rate: 1 l/s 3600 l/h
// Drain rate 3 l/s 10800 l/h
// Resarvuar: 8000l
// Fill w/o drain: 2.2h, fill w/ normal drain (1l/s) -> inf,
// Max Drain w/ normal fill: 0.7h

// ────────────────────────────────────────────────────────────
//  Network / AP configuration
// ────────────────────────────────────────────────────────────
// ── AP credentials ────────────────────────────────────────────────────────────

static const char* AP_PASSWORD = "Einstein123";
static const char* AP_SSID = "MicroHydro";
static const uint8_t AP_CHANNEL = 6;

// ── Server instance ───────────────────────────────────────────────────────────
static const uint16_t WS_PORT = 80;
static IPAddress AP_IP(192, 168, 3, 1);
const int esp_send_time = 300000;
unsigned long last_esp_send{ 0 };

// ── Telegram management ───────────────────────────────────────────────────────────
telegram_management tel_inc(1);
telegram_management tel_out(1);

// ────────────────────────────────────────────────────────────
//  Time / schedule configuration
// ────────────────────────────────────────────────────────────
static const int DAY_START_HOUR = 8;
static const int DAY_END_HOUR = 20;
static const float DAY_FILL_SETPOINT = 90.0f;
static const float NIGHT_DRAIN_SETPOINT = 10.0f;

// ────────────────────────────────────────────────────────────
//  Control parameters
// ────────────────────────────────────────────────────────────
static const float LEVEL_PI_DEADBAND = 0.04f;  // ±4 % of full scale

static const int LEVEL_PI_PERIOD_S = 30;  // PI update interval [s]
static const int POWER_PI_PERIOD_S = 3;   // PI update interval [s]

static const float NORMAL_FILL_RATE_PPH = 40.0f;  // fraction per hour
static const float NORMAL_DRAIN_RATE_PPH = 130.0f;

// PI gains for the power controller
static float KP_POWER = 0.7f;   // proportional gain
static float KI_POWER = 0.03f;  // integral gain
static float IP_MAX = 300.0f;   // anti-windup clamp (W)
static float IP_MIN = -300.0f;

// PI gains for the level controller
static float KP_LEVEL = 0.05f;      // proportional gain
static float KI_LEVEL = 0.01f;      // integral gain
static const float IL_MAX = 80.0f;  // anti-windup clamp (%)
static const float IL_MIN = -80.0f;

// Nozzle / turbine parameters
//
//  Three nozzles total:
//    valve1_cv_pc   proportional nozzle 1  (0-100 %)
//    valve2_cv_pc   proportional nozzle 2  (0-100 %)
//    ball_valve_cv  ON/OFF nozzle; stays open except in STOP and FILLING.
//                   Equivalent to NOZZLE_EFFICIENT_PC (80 %) of one prop. nozzle.
//                   Kept open by default to prevent pipe freeze and clogging.
//
//  Power budget (adjust ONE_NOZZLE_POWER_W to match your turbine):
//    ONE_NOZZLE_POWER_W  = power of one prop. nozzle at 100 %
//    BALL_POWER_W        = ONE_NOZZLE_POWER_W * 0.80  (fixed when open)
//    MAX_POWER_W         = 2 * ONE_NOZZLE_POWER_W + BALL_POWER_W
//
static const float ONE_NOZZLE_POWER_W = 120.0f;                              // W — tune to your turbine
static const float NOZZLE_EFFICIENT_PC = 0.80f;                              // prop. nozzle sweet-spot
static const float BALL_POWER_W = ONE_NOZZLE_POWER_W * NOZZLE_EFFICIENT_PC;  // 160 W
static const float MAX_POWER_W = 2.0f * ONE_NOZZLE_POWER_W + BALL_POWER_W;   // 560 W

// ────────────────────────────────────────────────────────────
//  Enumerations
// ────────────────────────────────────────────────────────────
/*enum class ControlMode : uint8_t {
  UNKNOWN = 0,
  STOP = 1,
  CONSTANT_POWER = 2,        // fixed turbine power, day
  CONSTANT_LEVEL = 3,        // maintain reservoir level, day
  CONSTANT_POWER_NIGHT = 4,  // fixed power at night (fills by day)
  CONSTANT_LEVEL_NIGHT = 5,  // maintain level at night (fills by day)
  FILLING = 6                // open ball-valve to fill reservoir
};*/

enum class LampState : uint8_t { OFF,
                                 BLINK_FAST,
                                 BLINK_SLOW,
                                 ON };

// ────────────────────────────────────────────────────────────
//  Global objects
// ────────────────────────────────────────────────────────────
TimeScheduler scheduler;
opta_abs* cpu = nullptr;
WebserverAbstraction ws(AP_IP, WS_PORT);

//Value monitoring
monitor_window monitor_valve1(0.0, 100.0, 5, 90000);
monitor_window monitor_valve2(0.0, 100.0, 5, 90000);
monitor_ball_valve monitor_bv(60000);

// ────────────────────────────────────────────────────────────
//  Runtime state
// ────────────────────────────────────────────────────────────
bool is_time_set = false;
bool is_day = true;

ControlMode cmi = ControlMode::UNKNOWN;      // primary mode
ControlMode cmi_sec = ControlMode::UNKNOWN;  // secondary (night sub-mode)
//ControlMode cmi_previous = ControlMode::UNKNOWN;  // check if changed

float setpoint_power_w = 0.0f;   // [W]
float setpoint_level_pc = 0.0f;  //  0–100

LampState lamp_red = LampState::OFF;
LampState lamp_green = LampState::OFF;

// ────────────────────────────────────────────────────────────
//  Level controller state
// ────────────────────────────────────────────────────────────
float lc_indirect_setpoint = 0.5f;  // internal ramp target
bool lc_indirect_active = false;
float lc_integrator{ 0.0f };  //integrator part for level
float pc_integrator{ 0.0f };  //power
unsigned long lc_last_update_ms = 0;
unsigned long pc_last_update_ms = 0;

// ────────────────────────────────────────────────────────────
//  Api things
// ────────────────────────────────────────────────────────────
bool send_data{ false };

// ────────────────────────────────────────────────────────────
//  Utility helpers
// ────────────────────────────────────────────────────────────
static float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// ============================================================
//  Schedule callbacks (called by TimeScheduler)
// ============================================================
void on_day_start() {
  is_day = true;
  // If we were in a night mode, begin filling the reservoir
  /*if (cmi == ControlMode::CONSTANT_POWER_NIGHT || cmi == ControlMode::CONSTANT_LEVEL_NIGHT) {
    cmi_sec = ControlMode::FILLING;
  }*/
}

void on_night_start() {
  is_day = false;
  /*if (cmi == ControlMode::CONSTANT_POWER_NIGHT) {
    cmi_sec = ControlMode::CONSTANT_POWER;
  }
  if (cmi == ControlMode::CONSTANT_LEVEL_NIGHT) {
    cmi_sec = ControlMode::CONSTANT_LEVEL;
  }*/
}

void handle_new_msg() {
  if (tel_inc.operating_mode == 0) {  //doudel tut reinitialisieren
    if (cmi != ControlMode::UNKNOWN) {
      ws.setMessage(tel_out.enc_outgoing_msg().c_str());
      last_esp_send = millis();
      my_log("WS send set after reiint of subcomponent");
    }
  }
  //todo, inc to out
}

// ============================================================
//  WIFI
// ============================================================
void wifi_setup() {
  my_log("[WiFi] Starting AP \"");
  my_log(AP_SSID);
  my_log("\" … ");

  int status = WiFi.beginAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
  if (status != WL_AP_LISTENING) {
    my_log("FAILED. Halting.");
    while (true)
      ;
  }
  my_log("OK");

  delay(1000);

  my_log("[WiFi] IP  : ");
  my_log(String(WiFi.localIP()));

  ws.begin();
}

// ============================================================
//  Control-mode setter
//  Call this from the hardware selector, the webserver, or
//  any future interface.  Validates and applies side-effects.
// ============================================================
void set_control_mode(ControlMode requested_mode,
                      float power_setpoint_w = 0.0f,
                      float level_setpoint_pc = -1.0f) {

  if (cmi == requested_mode) {
    return;
  }

  reset_pi_controllers();

  // Resolve UNKNOWN → STOP
  if (requested_mode == ControlMode::UNKNOWN) {
    requested_mode = ControlMode::STOP;
  }

  // Night modes require a working RTC
  if (!is_time_set && (requested_mode == ControlMode::CONSTANT_POWER_NIGHT || requested_mode == ControlMode::CONSTANT_LEVEL_NIGHT)) {
    // Fall back to the daytime equivalent
    requested_mode = (requested_mode == ControlMode::CONSTANT_POWER_NIGHT)
                       ? ControlMode::CONSTANT_POWER
                       : ControlMode::CONSTANT_LEVEL;
  }

  // Apply mode-specific defaults
  switch (requested_mode) {

    case ControlMode::STOP:
      cmi = ControlMode::STOP;
      cmi_sec = ControlMode::UNKNOWN;
      my_log("CMI set to STOP");
      break;

    case ControlMode::CONSTANT_POWER:
    case ControlMode::CONSTANT_POWER_NIGHT:
      cmi = requested_mode;
      //setpoint_power_w = (power_setpoint_w > 0.0f) ? power_setpoint_w : 100.0f;  //todo
      //cmi_sec = is_day ? ControlMode::FILLING : ControlMode::CONSTANT_POWER;     //todo check if needs to be filling
      my_log("CMI set to CONSTANT_POWER/NIGHT");
      break;

    case ControlMode::CONSTANT_LEVEL:
    case ControlMode::CONSTANT_LEVEL_NIGHT:
      cmi = requested_mode;
      setpoint_level_pc = (level_setpoint_pc >= 0.0f)
                            ? level_setpoint_pc
                            : cpu->level_meassured_p;
      lc_indirect_setpoint = setpoint_level_pc;
      //cmi_sec = is_day ? ControlMode::FILLING : ControlMode::CONSTANT_LEVEL;  //todo check if needs to be filling
      my_log("CMI set to CONSTANT_LEVEL/NIGHT");

      break;

    case ControlMode::FILLING:
      cmi = ControlMode::FILLING;
      cmi_sec = ControlMode::UNKNOWN;
      my_log("CMI set to FILLING");

      break;

    default:
      cmi = ControlMode::STOP;
      cmi_sec = ControlMode::UNKNOWN;
      my_log("CMI set to STOP");

      break;
  }
}

void wifi_loop() {
  ws.update();

  if (cpu->select_remote) {
    if ((ws.getMode() == ControlMode::UNKNOWN)) {
      set_control_mode(ControlMode::STOP);

    } else {
      set_control_mode(ws.getMode());
    }

    if (ws.getMode() == ControlMode::CONSTANT_LEVEL) {
      setpoint_level_pc = ws.getLevelSetpoint();
    }
    if (ws.getMode() == ControlMode::CONSTANT_POWER) {
      setpoint_power_w = ws.getPowerSetpoint();
    }
  }

  if (ws.getAckErrors()) {
    // handle error acknowledgement
    ws.resetAck();
  }

  if (ws.hasNewClientTime()) {
    uint32_t epoch = ws.getClientEpoch();
    scheduler.setFromEpoch(epoch);
    my_log("Time set to: " + String((unsigned int)time(NULL)));
    is_time_set = true;
  }

  if (millis() - last_esp_send > esp_send_time) {
    set_lora_send();
  }

  if (ws.is_new_msg_avail()) {
    tel_inc.dec_incoming_msg(ws.get_new_msg());
    handle_new_msg();
  }

  if (ws.is_ESP_reiint()){
    set_lora_send();
  }

  if (ws.hasNewPiData()) {
    if (ws.isForPowerControl()) {
      KP_POWER = ws.getPValue();
      KI_POWER = ws.getIValue();
      if (ws.resetIntegral()) {
        pc_integrator = 0;
      }
    } else {
      KP_LEVEL = ws.getPValue();
      KI_LEVEL = ws.getIValue();
      if (ws.resetIntegral()) {
        lc_integrator = 0;
      }
    }
  }
}

void set_lora_send() {
  tel_out.operating_mode = static_cast<int>(cmi);
  tel_out.power = cpu->get_meassured_power_W();
  tel_out.level_pc = cpu->level_meassured_p;

  ws.setMessage(tel_out.enc_outgoing_msg().c_str()); //also sets send flag = true
  last_esp_send = millis();
  my_log("WS send set");
}

// Convenience overload used by the physical selector switch
void set_control_mode_from_switch() {
  if (cpu->select_off) set_control_mode(ControlMode::STOP);
  else if (cpu->select_level) set_control_mode(ControlMode::CONSTANT_LEVEL);
  else if (cpu->select_remote) { /* webserver will call set_control_mode() */ }
}

// ============================================================
//  Nozzle staging
//
//  The ball valve is the baseline: it opens first and stays open.
//  The two proportional nozzles fill in whatever additional power
//  is needed above that baseline, following the same 80%-first rule.
//
//  Staging order (demand_w increasing):
//
//  Stage 0  demand <= BALL_POWER_W
//           ball = ON,  n1 = 0,   n2 = 0
//           (ball alone covers it; prop. nozzles stay shut)
//
//  Stage 1  BALL_POWER_W < demand <= BALL_POWER_W + ONE_NOZZLE_POWER_W * 0.80
//           ball = ON
//           n1 ramps 0 -> 80 %
//           n2 = 0
//
//  Stage 2  above stage 1 up to BALL_POWER_W + 2 * ONE_NOZZLE_POWER_W * 0.80
//           ball = ON,  n1 = 80 % (held)
//           n2 ramps 0 -> 80 %
//
//  Stage 3  above stage 2 up to MAX_POWER_W
//           ball = ON
//           n1 ramps 80 % -> 100 %
//           n2 ramps 80 % -> 100 %
//
//  ball_valve_cv is NOT touched here in STOP or FILLING — those
//  modes manage it directly in run_control_mode().
// ============================================================
static float mapf(float x, float in_lo, float in_hi, float out_lo, float out_hi) {
  if (in_hi == in_lo) return out_lo;
  return out_lo + (x - in_lo) * (out_hi - out_lo) / (in_hi - in_lo);
}

// Precomputed stage boundaries [W]
static const float STAGE1_END_W = BALL_POWER_W + ONE_NOZZLE_POWER_W * NOZZLE_EFFICIENT_PC;
static const float STAGE2_END_W = BALL_POWER_W + 2.0f * ONE_NOZZLE_POWER_W * NOZZLE_EFFICIENT_PC;
// STAGE3 runs from STAGE2_END_W to MAX_POWER_W

void set_nozzle_positions(float demand_w) {
  demand_w = clampf(demand_w, 0.0f, MAX_POWER_W);

  float n1, n2;
  bool bv = true;

  if (demand_w <= BALL_POWER_W) {

    n1 = 0.0f;
    n2 = 0.0f;

    if (demand_w <= BALL_POWER_W / 2) {
      bv = false;
    }

  } else if (demand_w <= STAGE1_END_W) {
    // Stage 1: ramp nozzle 1 from 0 to 80 %
    n1 = mapf(demand_w, BALL_POWER_W, STAGE1_END_W, 0.0f, NOZZLE_EFFICIENT_PC);
    n2 = 0.0f;

  } else if (demand_w <= STAGE2_END_W) {
    // Stage 2: nozzle 1 held at 80 %, bring in nozzle 2 from 0 to 80 %
    n1 = NOZZLE_EFFICIENT_PC;
    n2 = mapf(demand_w, STAGE1_END_W, STAGE2_END_W, 0.0f, NOZZLE_EFFICIENT_PC);

  } else {
    // Stage 3: both prop. nozzles driven from 80 % to 100 %
    n1 = mapf(demand_w, STAGE2_END_W, MAX_POWER_W, NOZZLE_EFFICIENT_PC, 1.0f);
    n2 = mapf(demand_w, STAGE2_END_W, MAX_POWER_W, NOZZLE_EFFICIENT_PC, 1.0f);
  }

  cpu->valve1_cv_pc = n1;
  cpu->valve2_cv_pc = n2;
  cpu->ball_valve_cv = bv;
}

// ============================================================
//  Level controller
//  Implements a PI with indirect setpoint ramping so that:
//   - At night (drain mode): the level gently tracks toward
//     NIGHT_DRAIN_SETPOINT by day-start.
//   - Inside the deadband:   pass-through (direct setpoint).
//   - Outside the deadband:  ramp the internal setpoint and
//     run the PI.
// ============================================================
namespace LevelController {

// Returns the desired fill-rate [fraction/hour] to reach
// `goal` from `now` in `hours_remaining` hours.
float required_fill_rate(float level_now, float level_goal, float hours_remaining) {
  if (hours_remaining <= 0.0f) return 0.0f;
  return (level_goal - level_now) / hours_remaining;
}

// Core PI update; returns new power setpoint [W]
float pi_level_update(float setpoint, float measured, float dt_s) {
  float error = setpoint - measured;

  lc_integrator += error * dt_s;
  // Anti-windup
  lc_integrator = clampf(lc_integrator, IL_MIN, IL_MAX);

  float output = setpoint
                 + KP_LEVEL * error
                 + KI_LEVEL * lc_integrator;
  return output;
}

// Called every control cycle when the mode is CONSTANT_LEVEL
// or the CONSTANT_LEVEL sub-mode of a night mode.
void update_level() {
  float now_ms = (float)millis();
  float ac_level = cpu->level_meassured_p;
  float goal = setpoint_level_pc;

  // ── Deadband check ───────────────────────────────────
  float error = ac_level - goal;
  if (fabsf(error) <= LEVEL_PI_DEADBAND) {
    lc_indirect_active = false;
    // Within tolerance – hold current outputs, do nothing
    return;
  }
  lc_indirect_active = true;

  unsigned long dt_ms = (unsigned long)(now_ms)-lc_last_update_ms;
  dt_ms = (unsigned long)clampi((int)dt_ms, 0, LEVEL_PI_PERIOD_S * 1200);

  if (dt_ms < (unsigned long)(LEVEL_PI_PERIOD_S * 1000)) {
    return;  // not yet time to update
  }
  lc_last_update_ms = (unsigned long)now_ms;

  // ── Indirect setpoint ramp (night drain mode) ────────
  bool night_drain = (cmi == ControlMode::CONSTANT_LEVEL_NIGHT) && !is_day;
  bool day_fill = (cmi == ControlMode::CONSTANT_LEVEL_NIGHT) && is_day;

  if (night_drain) {
    // Ramp the internal setpoint toward NIGHT_DRAIN_SETPOINT
    float hours_left = scheduler.time_h_till_h(DAY_START_HOUR);
    float rate_ph = required_fill_rate(ac_level, NIGHT_DRAIN_SETPOINT, hours_left);
    float slope = (float)dt_ms / 1000.0f / 3600.0f * rate_ph;
    goal = clampf(NIGHT_DRAIN_SETPOINT + slope, 0.0f, 100.0f);
  } else if (day_fill) {
    float hours_left = scheduler.time_h_till_h(DAY_END_HOUR);
    float rate_ph = required_fill_rate(ac_level, DAY_FILL_SETPOINT, hours_left);
    float slope = (float)dt_ms / 1000.0f / 3600.0f * rate_ph;
    goal = clampf(DAY_FILL_SETPOINT + slope, 0.0f, 100.0f);
  }

  // ── Run PI ───────────────────────────────────────────
  float dt_s = (float)dt_ms / 1000.0f;
  float new_power = pi_level_update(goal, ac_level, dt_s);
  setpoint_power_w = clampf(new_power, 0.0f, 500.0f);  // clamp to turbine range
}

}  // namespace LevelController

namespace PowerController {
// Core PI update; returns new power setpoint [W]
float pi_power_update(float setpoint, float measured, float dt_s) {
  float error = setpoint - measured;

  pc_integrator += error * dt_s;
  // Anti-windup: clamp raw integrator so KI*integrator ∈ [I_MIN, I_MAX]
  pc_integrator = clampf(pc_integrator, IP_MIN, IP_MAX);

  float output = setpoint
                 + KP_POWER * error
                 + KI_POWER * pc_integrator;
  my_log("PI CONTROLLER: Error: " + String(error) + " I: " + String(pc_integrator));
  return output;
}

void update_power() {
  float now_ms = (float)millis();

  unsigned long dt_ms = (unsigned long)(now_ms)-pc_last_update_ms;
  dt_ms = (unsigned long)clampi((int)dt_ms, 0, POWER_PI_PERIOD_S * 1200);
  if (dt_ms < (unsigned long)(POWER_PI_PERIOD_S * 1000)) {
    return;  // not yet time to update
  }

  float ac_power = cpu->get_meassured_power_W();
  float goal = setpoint_power_w;

  pc_last_update_ms = (unsigned long)now_ms;
  // ── Run PI ───────────────────────────────────────────
  float dt_s = (float)dt_ms / 1000.0f;
  float new_power = pi_power_update(goal, ac_power, dt_s);
  //my_log("Running Pi Power update, Meassured: " + String(ac_power) + "W, Setpoint: " + String(goal) + "W, PI power: " + String(new_power) + "W");
  set_nozzle_positions(new_power);
}

}  // namespace Powercontroleer

void reset_pi_controllers() {
  pc_integrator = 0.0;
  lc_integrator = 0.0;
}

unsigned long last_power_control_update;
unsigned long last_level_control_update;

// ============================================================
//  Control-mode actions  (called every loop cycle)
// ============================================================
void run_control_mode() {

  int level_update_interval_s = 30;
  int power_update_interval_s = 5;

  // Helper lambdas
  auto stop_all = [&]() {
    cpu->valve1_cv_pc = 0.0f;
    cpu->valve2_cv_pc = 0.0f;
    cpu->ball_valve_cv = false;
  };
  auto open_fill = [&]() {
    cpu->valve1_cv_pc = 0.0f;
    cpu->valve2_cv_pc = 0.0f;
    cpu->ball_valve_cv = true;
  };

  // Resolve the effective mode when a secondary is active
  ControlMode effective = cmi;
  /*if (cmi == ControlMode::CONSTANT_POWER_NIGHT || cmi == ControlMode::CONSTANT_LEVEL_NIGHT) {
    effective = cmi_sec;
  }*/

  switch (effective) {

    // ── STOP ─────────────────────────────────────────────
    case ControlMode::STOP:
    case ControlMode::UNKNOWN:
      stop_all();
      lamp_green = LampState::BLINK_SLOW;
      lamp_red = LampState::OFF;
      break;

    // ── FILLING ──────────────────────────────────────────
    case ControlMode::FILLING:
      if (cpu->level_meassured_p >= 95.0f) {
        set_control_mode(
          (cmi == ControlMode::CONSTANT_POWER_NIGHT)
            ? ControlMode::CONSTANT_POWER_NIGHT
            : ControlMode::CONSTANT_LEVEL_NIGHT,
          setpoint_power_w,
          setpoint_level_pc);
      } else {
        open_fill();
        lamp_green = LampState::BLINK_FAST;
        lamp_red = LampState::OFF;
      }
      break;

    // ── CONSTANT POWER ───────────────────────────────────
    case ControlMode::CONSTANT_POWER_NIGHT:
    case ControlMode::CONSTANT_POWER:
      PowerController::update_power();
      break;

    // ── CONSTANT LEVEL ───────────────────────────────────
    case ControlMode::CONSTANT_LEVEL_NIGHT:
    case ControlMode::CONSTANT_LEVEL:
      LevelController::update_level();
      PowerController::update_power();
      break;

    default:
      stop_all();
      break;
  }
}

void stageing_test(float power) {
  set_nozzle_positions(power);
  my_log("setting positions for " + String(power) + "W, BV0: " + String(cpu->ball_valve_cv) + "V1:" + String(cpu->valve1_cv_pc) + " ,V2: " + String(cpu->valve1_cv_pc));
}

// ============================================================
//  Error / safety management
// ============================================================
void error_management() {
  // RTC timeout warning
  if (!is_time_set && millis() > 300000UL) {
    lamp_red = LampState::BLINK_SLOW;  // signal time-not-set
    tel_out.errors.cpu_time_not_set = true;
  }

  float level = cpu->level_meassured_p;

  // Overfill guard: if level ever exceeds 101 %, drain back to 90 %
  /*if (level > 1.01f) {
    set_control_mode(ControlMode::CONSTANT_LEVEL, 0.0f, 0.90f);
    lamp_red = LampState::BLINK_FAST; 
  }

  // Low-level guard: if below 5 %, start filling (or switch sub-mode)
  if (level < 0.05f) {
    if (cmi == ControlMode::CONSTANT_LEVEL_NIGHT || cmi == ControlMode::CONSTANT_POWER_NIGHT) {
      cmi_sec = ControlMode::FILLING;
    } else {
      set_control_mode(ControlMode::CONSTANT_LEVEL, 0.0f, NIGHT_DRAIN_SETPOINT);
    }
    lamp_red = LampState::BLINK_FAST;
  }*/

  //Value monitoring
  if (!monitor_valve1.set_target(cpu->valve1_cv_pc)) {
    cpu->valve1_cv_pc = 0.0;
    my_log("Value setting error Valve 1");
  }

  int v_mon_v1_status = monitor_valve1.monitor(cpu->valve1_fb_pc);
  if (v_mon_v1_status != 0) {
    my_log("Value monitoor error Valve 1, status: " + String(v_mon_v1_status));
  }

  monitor_bv.set_target(cpu->ball_valve_cv);
  int v_mon_bv_status = monitor_bv.monitor(cpu->ball_valve_open, cpu->ball_valve_closed);
  if (v_mon_bv_status != 0) {
    my_log("Value monitoor error Ball Valve, status: " + String(v_mon_bv_status));
  }
  
}

// ============================================================
//  LED helpers
// ============================================================
namespace LEDs {
unsigned long last_red_ms = 0;
unsigned long last_green_ms = 0;
unsigned long last_blue_ms = 0;

void update_channel(LampState state, bool& output,
                    unsigned long& last_ms,
                    unsigned long fast_ms = 500,
                    unsigned long slow_ms = 1000) {
  switch (state) {
    case LampState::OFF: output = false; break;
    case LampState::ON: output = true; break;
    case LampState::BLINK_FAST:
      if (millis() - last_ms >= fast_ms) {
        output = !output;
        last_ms = millis();
      }
      break;
    case LampState::BLINK_SLOW:
      if (millis() - last_ms >= slow_ms) {
        output = !output;
        last_ms = millis();
      }
      break;
  }
}

void update() {
  update_channel(lamp_red, cpu->lamp_red, last_red_ms);
  update_channel(lamp_green, cpu->lamp_green, last_green_ms);
  // Heartbeat: blue LED toggles every 2 s
  if (millis() - last_blue_ms >= 2000UL) {
    last_blue_ms = millis();
    cpu->led_int_b = !cpu->led_int_b;
  }
}
}  // namespace LEDs

const String controlModeStr(ControlMode m) {
  switch (m) {
    case ControlMode::UNKNOWN: return "UNBEKANNT";
    case ControlMode::STOP: return "STOP";
    case ControlMode::CONSTANT_POWER: return "LEISTUNG";
    case ControlMode::CONSTANT_LEVEL: return "PEGEL";
    case ControlMode::CONSTANT_POWER_NIGHT: return "LEISTUNG N";
    case ControlMode::CONSTANT_LEVEL_NIGHT: return "PEGEL N";
    case ControlMode::FILLING: return "FUELLEN";
    default: return "UNBEKANNT";
  }
}

void logIOState(unsigned long now) {
  static unsigned long lastLog = 0;
  if (now - lastLog < 10000UL) return;
  lastLog = now;

  my_log("──────────────── IO State ────────────────");

  // Analog In
  my_log_pair("  water_temp        : ", cpu->water_temp_dC, "°C");
  my_log_pair("  surround_temp     : ", cpu->surround_temp_dC, "°C");
  my_log_pair("  water_pressure    : ", cpu->water_preassure_bar, "bar");
  my_log_pair("  level_measured    : ", cpu->level_meassured_p, "%");
  my_log_pair("  voltage           : ", cpu->voltage_V, "V");
  my_log_pair("  current           : ", cpu->current_A, "A");
  my_log_pair("  power             : ", cpu->get_meassured_power_W(), "W");
  my_log_pair("  valve1_fb         : ", cpu->valve1_fb_pc, "%");
  my_log_pair("  valve2_fb         : ", cpu->valve2_fb_pc, "%");

  // Analog Out
  my_log_pair("  valve1_cv         : ", cpu->valve1_cv_pc, "%");
  my_log_pair("  valve2_cv         : ", cpu->valve2_cv_pc, "%");

  // Digital In
  my_log_pair("  ball_valve_open   : ", cpu->ball_valve_open ? "1" : "0");
  my_log_pair("  ball_valve_closed : ", cpu->ball_valve_closed ? "1" : "0");
  my_log_pair("  float_switch      : ", cpu->float_switch_triggered ? "1" : "0");
  my_log_pair("  select_off        : ", cpu->select_off ? "1" : "0");
  my_log_pair("  select_level      : ", cpu->select_level ? "1" : "0");
  my_log_pair("  select_remote     : ", cpu->select_remote ? "1" : "0");

  // Digital Out
  my_log_pair("  ball_valve_cv     : ", cpu->ball_valve_cv ? "1" : "0");
  my_log_pair("  lamp_green        : ", cpu->lamp_green ? "1" : "0");
  my_log_pair("  lamp_red          : ", cpu->lamp_red ? "1" : "0");

  //Important variables
  my_log("  Power Setpoint  " + String(setpoint_power_w));
  my_log("  Level Setpoint  " + String(setpoint_level_pc));

  my_log("  CPU cmi:    " + controlModeStr(cmi));
  my_log("  WEB cm:     " + controlModeStr(ws.getMode()));

  my_log("──────────────────────────────────────────");
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  my_log_begin();
  while (millis() < 2000) {}
  my_log("*** Opta MicroHydro Init ***");

  // ── Opta controller ──────────────────────────────────────
  OptaController.begin();
  my_log("*** Opta MicroHydro begun ***");

  while (OptaController.getExpansionNum() == 0) {
    my_log("Looking for expansions...");
    OptaController.update();
    delay(1000);
  }

  AnalogExpansion aexp = OptaController.getExpansion(0);
  if (aexp) {
    cpu = new opta_abs(0);  // pass index, not the object
  } else {
    while (true) {
      my_log("NO Opta Analog found at position 0 — halted.");
      delay(2000);
    }
  }

  scheduler.addSchedule(DAY_START_HOUR, 0, on_day_start);
  scheduler.addSchedule(DAY_END_HOUR, 0, on_night_start);

  if ((unsigned int)time(NULL) > 1777127104) {  //probably still set
    is_time_set = true;
    my_log("Time still set");
  }

  // ── Initial control mode ─────────────────────────────────
  set_control_mode(ControlMode::STOP);

  // ── WiFi / Webserver (enable when ready) ─────────────────
  wifi_setup();

  delay(2000);
  my_log("*** Init complete ***");
  set_lora_send();
}

// ============================================================
//  Main loop
// ============================================================
void loop() {
  cpu->read_inputs();

  if (is_time_set) {
    scheduler.tick();
    scheduler.run_tasks();
  }

  set_control_mode_from_switch();

  //webserver stuff
  wifi_loop();

  //ws.setStatusShort("RUN");
  //ws.setStatusShortSetpoint("OK");
  // ws.pushStatusMessage("some my_log line");


  run_control_mode();
  //stageing_test(setpoint_power_w);

  LEDs::update();

  //error_management();

  // 7. Write all outputs
  // Push live values into the webserver state
  ws.setPower(cpu->get_meassured_power_W());
  ws.setLevel(cpu->level_meassured_p);
  ws.setMode(cmi);

  cpu->surround_temp_dC = ws.getTemp();
  ws.setStatusShort(controlModeStr(cmi).c_str());

  //logIOState(millis());
  cpu->write_outputs();
}
