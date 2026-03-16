#include <array>
#include <algorithm>
#include <cmath>

class PeltonPIController {
public:
    struct PIParams {
        double Kp{ 0.0 };
        double Ki{ 0.0 };
        double minOutput{ 0.0 };
        double maxOutput{ 100.0 };
    };

    PeltonPIController(double dtSeconds)
        : dt(dtSeconds)
    {
        integral.fill(0.0);
        output.fill(0.0);
    }

    void setParams(int nozzle, const PIParams& params) {
        if (nozzle < 0 || nozzle >= 3) return;
        gains[nozzle] = params;
    }

    void setSetpoint(double sp) {
        setpoint = sp;
    }

    void reset() {
        integral.fill(0.0);
    }

    std::array<double, 3> update(double measurement) {

        double error = setpoint - measurement;

        for (int i = 0; i < 3; ++i) {

            integral[i] += error * dt * gains[i].Ki;

            double u = gains[i].Kp * error + integral[i];

            // anti windup
            if (u > gains[i].maxOutput) {
                u = gains[i].maxOutput;
                integral[i] = u - gains[i].Kp * error;
            }
            else if (u < gains[i].minOutput) {
                u = gains[i].minOutput;
                integral[i] = u - gains[i].Kp * error;
            }

            output[i] = u;
        }

        return output;
    }

    // =====================================================
    //  AUTO TUNE
    // =====================================================

    struct TuneResult {
        double processGain;     // K
        double timeConstant;    // T
        double deadTime;        // L
        PIParams pi;            // suggested parameters
    };

    /*
        measurements[] : response of speed/power
        inputs[]       : applied nozzle step [%]
        N              : number of samples
    */
    TuneResult auto_tune(const double* measurements,
        const double* inputs,
        int N)
    {
        // --- 1. Estimate static process gain K ---
        double du = inputs[N - 1] - inputs[0];
        double dy = measurements[N - 1] - measurements[0];

        double K = (std::abs(du) < 1e-6) ? 0.0 : dy / du;

        // --- 2. Estimate time constant by 63% method ---
        double y0 = measurements[0];
        double yInf = measurements[N - 1];
        double y63 = y0 + 0.63 * (yInf - y0);

        int t63_index = 0;
        for (int i = 0; i < N; ++i) {
            if ((yInf >= y0 && measurements[i] >= y63) ||
                (yInf < y0 && measurements[i] <= y63)) {
                t63_index = i;
                break;
            }
        }

        double T = t63_index * dt;

        // --- 3. Rough dead time estimation ---
        int t10_index = 0;
        double y10 = y0 + 0.1 * (yInf - y0);

        for (int i = 0; i < N; ++i) {
            if ((yInf >= y0 && measurements[i] >= y10) ||
                (yInf < y0 && measurements[i] <= y10)) {
                t10_index = i;
                break;
            }
        }

        double L = t10_index * dt;

        // --- 4. Compute PI parameters (hydro-friendly) ---
        PIParams p;

        if (std::abs(K) > 1e-6 && T > 1e-6) {

            // Lambda tuning – robust for turbines
            double lambda = std::max(T, 5.0 * dt);

            p.Kp = (T) / (K * (lambda + L));
            p.Ki = p.Kp / (T);

        }
        else {
            // fallback safe values
            p.Kp = 0.5;
            p.Ki = 0.1;
        }

        p.minOutput = 0.0;
        p.maxOutput = 100.0;

        TuneResult r;
        r.processGain = K;
        r.timeConstant = T;
        r.deadTime = L;
        r.pi = p;

        return r;
    }

private:
    double dt;
    double setpoint{ 0.0 };

    std::array<PIParams, 3> gains;
    std::array<double, 3> integral;
    std::array<double, 3> output;
};
