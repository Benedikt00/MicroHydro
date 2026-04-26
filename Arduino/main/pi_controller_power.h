// ============================================================
//  Turbine Nozzle Controller — with PI closed-loop
//  - N1: binary (0 or 1), contributes fixed power P1_WATTS
//  - N2: 0–100, cascades first up to 80% before N3 opens
//  - N3: 0–100, overflow nozzle
//  - PI controller closes the loop on measured power
// ============================================================

class pi_controller_power {

  float P1_WATTS = 90.0f;
  float N2_MAX_WATTS = 110.0f;
  float N3_MAX_WATTS = 110.0f;
  float CASCADE_THRESH = 80.0f;  // % threshold before N3 opens

  // PI gains — tune these for your turbine's response
  float KP = 0.05f;  // proportional gain [% output / W error]
  float KI = 0.01f;  // integral gain    [% output / (W·s)]

  // Anti-windup: clamp integrator contribution independently
  float I_MAX = 80.0f;  // max integrator term [% equivalent]
  float I_MIN = -80.0f;

  // Total PI output is in [W] demand — clamp to physical range
  float DEMAND_MAX = P1_WATTS + N2_MAX_WATTS + N3_MAX_WATTS;
  float DEMAND_MIN = 0.0f;

  // Setpoint slew-rate limiter (watts per second)
  float SLEW_RATE_WPS = 50.0f;

  // ---- State ----------------------------------------------------------
  float g_setpointTarget = 0.0f;
  float g_setpointSmooth = 0.0f;
  float g_measuredPower = 0.0f;

  // PI internal state
  float g_integrator = 0.0f;
  float g_lastError = 0.0f;

  // Outputs
  bool g_n1 = false;
  float g_n2 = 0.0f;
  float g_n3 = 0.0f;

  // ---- Helpers --------------------------------------------------------
  inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }

  float slewLimit(float current, float target, float dtSec) {
    float maxStep = SLEW_RATE_WPS * dtSec;
    float delta = target - current;
    delta = clampf(delta, -maxStep, maxStep);
    return current + delta;
  }

  // ---- PI controller --------------------------------------------------
  //  Input:  setpoint and measured power [W], timestep [s]
  //  Output: demanded power [W] to feed into the allocator
  //
  //  The PI output is a *demand in watts*, not a percentage.
  //  KP and KI are scaled so that a 100 W error produces a meaningful
  //  correction — adjust to taste once you know your plant's gain.
  float piUpdate(float setpoint, float measured, float dtSec) {
    float error = setpoint - measured;

    // Integral — with clamped anti-windup
    g_integrator += error * dtSec;
    g_integrator = clampf(g_integrator, I_MIN / KI, I_MAX / KI);
    // (dividing limits by KI keeps the raw integrator in watts·s,
    //  so the scaled term KI*integrator stays within [I_MIN, I_MAX])

    float p_term = KP * error;
    float i_term = KI * g_integrator;

    float output = setpoint + p_term + i_term;  // setpoint feed-forward + correction
    return clampf(output, DEMAND_MIN, DEMAND_MAX);
  }

  // ---- Nozzle allocator -----------------------------------------------
  //  Cascade priority:
  //   1. N1 on/off (binary, hysteresis)
  //   2. N2 fills to CASCADE_THRESH (80%) first
  //   3. N3 fills 0→100%
  //   4. If N3 is saturated, N2 continues from CASCADE_THRESH to 100%
  void allocate(float demand) {
    float remaining = demand;

    // --- N1: turns on first, as soon as there is any demand ---
    float N1_ON_THRESH = P1_WATTS * 0.9;  // small deadband to avoid chattering at zero
    float N1_OFF_THRESH = P1_WATTS * 0.3;
    if (!g_n1 && remaining >= N1_ON_THRESH) g_n1 = true;
    else if (g_n1 && remaining < N1_OFF_THRESH) g_n1 = false;

    if (!g_n1) {
      g_n2 = 0.0f;
      g_n3 = 0.0f;
      return;
    }

    // N1 is on — subtract its fixed contribution
    remaining -= P1_WATTS;

    if (remaining <= 0.0f) {
      g_n2 = 0.0f;
      g_n3 = 0.0f;
      return;
    }

    // --- N2 first pass: fill to CASCADE_THRESH (80%) ---
    float n2Phase1Max = (CASCADE_THRESH / 100.0f) * N2_MAX_WATTS;

    if (remaining <= n2Phase1Max) {
      g_n2 = clampf((remaining / N2_MAX_WATTS) * 100.0f, 0.0f, CASCADE_THRESH);
      g_n3 = 0.0f;
      return;
    }

    g_n2 = CASCADE_THRESH;
    remaining -= n2Phase1Max;

    // --- N3: fill 0→100% ---
    if (remaining <= N3_MAX_WATTS) {
      g_n3 = clampf((remaining / N3_MAX_WATTS) * 100.0f, 0.0f, 100.0f);
      return;
    }

    // --- N3 saturated: N2 second pass, 80%→100% ---
    g_n3 = 100.0f;
    remaining -= N3_MAX_WATTS;

    float n2Phase2Max = (1.0f - CASCADE_THRESH / 100.0f) * N2_MAX_WATTS;
    g_n2 = clampf(CASCADE_THRESH + (remaining / n2Phase2Max) * (100.0f - CASCADE_THRESH),
                  CASCADE_THRESH, 100.0f);
  }
public:
  // ---- Public API -----------------------------------------------------
  void setDemand(float watts) {
    g_setpointTarget = clampf(watts, DEMAND_MIN, DEMAND_MAX);
  }

  // Call this to reset integrator (e.g. on large setpoint jumps or startup)
  void resetPI() {
    g_integrator = 0.0f;
    g_lastError = 0.0f;
  }

  // ---- Main update ----------------------------------------------------
  void update(unsigned long nowMs, float watts) {

    g_measuredPower = watts;
    static unsigned long lastMs = 0;
    float dtSec = clampf((nowMs - lastMs) / 1000.0f, 0.001f, 0.5f);
    lastMs = nowMs;

    // 1. Slew-limit the user setpoint
    g_setpointSmooth = slewLimit(g_setpointSmooth, g_setpointTarget, dtSec);

    // 2. PI controller produces corrected demand
    float piDemand = piUpdate(g_setpointSmooth, g_measuredPower, dtSec);

    // 3. Allocate corrected demand across nozzles
    allocate(piDemand);
  }
};
