from flask import Flask, request, jsonify, render_template
import time, random, threading

app = Flask(__name__)

# ── Shared device state (mirrors DeviceState struct) ─────────────────────────

state = {
    # Inputs (device → UI)
    "statusMessages": [
        "System gestartet",
        "Verbindung hergestellt",
        "Warte auf Befehle",
        "",
        "",
    ],
    "power":       42,
    "pressure":    67,
    "statusShort": "LEISTUNG",

    # Outputs (UI → device)
    "mode":              2,   # 1=Stop 2=Leistung 3=Druck 4=Leistung(N) 5=Druck(N) 6=Speicher
    "powerSetpoint":     60,
    "pressureSetpoint":  0,
    "clientEpoch":       0,
}

lock = threading.Lock()

# ── Background thread: simulate live sensor data ──────────────────────────────

def simulate():
    uptime = 0
    while True:
        time.sleep(2)
        uptime += 2
        with lock:
            m   = state["mode"]
            psp = state["powerSetpoint"]
            rsp = state["levelSetpoint"]

            if m == 1:    # Stop
                state["power"]       = random.randint(40, 340)
                state["level"]    =     random.randint(3, 340)

                state["statusShort"] = "STOP"
            elif m == 2:  # Leistungsregelung
                state["power"]       = random.randint(40, 340)
                state["level"]    =     random.randint(3, 340)
                state["statusShort"] = "LEISTUNG"
            elif m == 3:  # Druckregelung
                state["power"]       = random.randint(40, 340)
                state["level"]    =     random.randint(3, 340)
                state["statusShort"] = "DRUCK"
            elif m == 4:  # Leistungsregelung Nacht
                state["power"]       = random.randint(40, 340)
                state["level"]    =     random.randint(3, 340)
                state["statusShort"] = "LEISTUNG-N"
            elif m == 5:  # Druckregelung Nacht
                state["power"]       = random.randint(40, 340)
                state["level"]    =     random.randint(3, 340)
                state["statusShort"] = "DRUCK-N"
            elif m == 6:  # Speicherbetrieb
                state["power"]       = random.randint(40, 340)
                state["level"]    =     random.randint(3, 340)
                state["statusShort"] = "SPEICHER"

            # Shift a new log line in every 10 s
            if uptime % 10 == 0:
                msg  = (f"Uptime: {uptime}s  Modus: {m}  "
                        f"P: {state['power']}%  D: {state['level']}%")
                msgs = state["statusMessages"]
                for i in range(len(msgs) - 1):
                    msgs[i] = msgs[i + 1]
                msgs[-1] = msg

threading.Thread(target=simulate, daemon=True).start()

# ── Routes ────────────────────────────────────────────────────────────────────

@app.route("/")
def cpu_page():
    return render_template("cpu.html")

@app.route("/hmi")
def hmi_page():
    return render_template("hmi.html")

@app.route("/api", methods=["GET"])
def api_get():
    with lock:
        return jsonify(dict(state))

@app.route("/api", methods=["POST"])
def api_post():
    body = request.get_json(silent=True) or {}
    with lock:
        if "mode" in body:
            state["mode"] = int(body["mode"])
        if "powerSetpoint" in body:
            state["powerSetpoint"] = max(0, min(340, int(body["powerSetpoint"])))
        if "levelSetpoint" in body:
            state["levelSetpoint"] = max(0, min(300, int(body["levelSetpoint"])))
        return jsonify(dict(state))

@app.route("/api/time", methods=["POST"])
def api_time():
    body = request.get_json(silent=True) or {}
    with lock:
        if "epoch" in body:
            state["clientEpoch"] = int(body["epoch"])
            ts   = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(state["clientEpoch"]))
            msgs = state["statusMessages"]
            for i in range(len(msgs) - 1):
                msgs[i] = msgs[i + 1]
            msgs[-1] = f"Zeit sync: {ts}"
    return jsonify({"status": "ok"})

# ── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
