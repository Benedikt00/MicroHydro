
class nz_controller {
public:
  float bar_normal{ 0.0 };
  float watt_normal{ 0.0 };


  float setpoint_matrix[2][3] = { { 0.3, 0.5, 0.8 },
                                  { 0.3, 0.5, 0.8 } };


  int setpoints_per_nozzle = sizeof(setpoint_matrix[0])/sizeof(*setpoint_matrix[0]);

  int num_setpoints = 2 * (setpoints_per_nozzle + 1) + 2;  //pro nozzle noch 100, plus kugelhahn auf und zu

  int current_setpoint{ 0 };

  nz_controller(float preassure_preset, float power_normal) {
    bar_normal = preassure_preset;
    watt_normal = power_normal;
  };


  static float params[3];

  float* setpoint_to_aq(int setpoint) {
    //{main 0/1, n1, n2}
    
    params[0] = 0.0;
    params[1] = 0.0;
    params[2] = 0.0;
    current_setpoint = setpoint;

    if (setpoint <= 0) {
      return params;  //Aus
    }

    params[0] = 1.0;

    if (setpoint >= num_setpoints - 1) {
      params[1] = 1.0;
      params[2] = 1.0;
      return params;  //Volle gas Leiberaks
    }
    /*if (setpoint == num_setpoints - 2) {
      params[1] = 1.0;
      params[2] = setpoint_matrix[1][-1];
      return params;
    };*/


    int corsets[2] = { -1, -1 };

    for (int i = 0; i <= setpoint - 1 ; i++) {
      //Serial.print("I:" + String(i) );
      if (corsets[0] < setpoints_per_nozzle - 1) {
        corsets[0] += 1;
        //Serial.println(String(corsets[0]) + " c " + corsets[1]);
        continue;
      }
      if (corsets[1] < setpoints_per_nozzle - 1) {
        corsets[1] += 1;
        //Serial.println(String(corsets[0]) + " c " + corsets[1]);
        continue;
      }
      corsets[0] += 1;
      break;
    }
    //Serial.println(String(corsets[0]) + " c " + corsets[1]);

    params[1] = setpoint_matrix[0][corsets[0]];
    params[2] = setpoint_matrix[1][corsets[1]];

    if (corsets[0] == -1) {params[1] = 0.0;};
    if (corsets[1] == -1) {params[2] = 0.0;};

    return params;
  }



};
