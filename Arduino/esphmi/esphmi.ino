// ─────────────────────────────────────────────────────────────────────────────
//  Example_ESP32.ino
//  ESP32 usage example for WebserverAbstraction — Access Point (hotspot) mode.
//
//  The ESP32 creates its own WiFi network. Connect any phone/PC to that
//  network, then open http://192.168.4.1 in a browser.
//
//  Required libraries (install via Library Manager):
//    - ArduinoJson  (Benoit Blanchon)
//    - Arduino_JSON (Arduino)
//    WiFi.h / WiFiClient.h / WiFiServer.h are built into the ESP32 core.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "webserver_mh.h"

// ── Access Point config ───────────────────────────────────────────────────────
//  SSID must be ≤ 31 chars. Password must be ≥ 8 chars, or "" for open network.
const char*    AP_SSID    = "MicroHydro";
const char*    AP_PASS    = "Einstein123";     // set "" for an open (no password) AP
const uint8_t  AP_CHANNEL = 6;               // WiFi channel 1–13
const uint8_t  AP_HIDDEN  = 0;               // 0 = broadcast SSID, 1 = hidden
const uint8_t  AP_MAX_CON = 4;               // max simultaneous clients

// The ESP32 AP always uses 192.168.4.1 as its gateway/IP by default.
// You can override this with WiFi.softAPConfig() below if needed.
static const IPAddress AP_IP  (192, 168, 4, 1);
static const uint16_t  AP_PORT = 80;

WebserverAbstraction* ws = nullptr;

float measuredPower    = 0.0f;
float measuredPressure = 0.0f;
uint32_t deviceEpoch   = 0;
unsigned long lastRtcUpdate = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    // ── Start Access Point ────────────────────────────────────────────────
    WiFi.mode(WIFI_AP);

    // Optional: fix the AP IP / gateway / subnet (defaults are fine for most use)
    // IPAddress gateway(192, 168, 4, 1);
    // IPAddress subnet (255, 255, 255, 0);
    // WiFi.softAPConfig(AP_IP, gateway, subnet);

    bool ok = WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, AP_HIDDEN, AP_MAX_CON);
    if (!ok) {
        Serial.println("[AP] softAP() failed — halting.");
        while (1) { delay(1000); }
    }

    IPAddress ip = WiFi.softAPIP();   // typically 192.168.4.1
    Serial.println("[AP] Network : " + String(AP_SSID));
    if (strlen(AP_PASS) > 0)
        Serial.println("[AP] Password: " + String(AP_PASS));
    else
        Serial.println("[AP] Password: (open network)");
    Serial.println("[AP] IP      : " + ip.toString());

    // ── Instantiate webserver ─────────────────────────────────────────────
    ws = new WebserverAbstraction(ip, AP_PORT);

    ws->setStatusMessage(0, "ESP32 AP started");
    ws->setStatusMessage(1, ("SSID: " + String(AP_SSID)).c_str());
    ws->setStatusMessage(2, ("IP:   " + ip.toString()).c_str());
    ws->setStatusMessage(3, "Waiting for commands");
    ws->setStatusMessage(4, "");
    ws->setStatusShort("IDLE");

    Serial.println("[WS] Dashboard: http://" + ip.toString() +
                   ":" + String(AP_PORT));
}

void loop() {
    ws->update();

    // ── Time sync ─────────────────────────────────────────────────────────
    if (ws->hasNewClientTime()) {
        deviceEpoch   = ws->getClientEpoch();
        lastRtcUpdate = millis();
        Serial.println("[RTC] Synced to epoch: " + String(deviceEpoch));
        ws->setStatusMessage(3, "RTC synced from browser");
    }

    // ── Control logic ─────────────────────────────────────────────────────
    ControlMode mode  = ws->getMode();
    int         pwrSP = ws->getPowerSetpoint();
    int         presSP= ws->getPressureSetpoint();

    switch (mode) {
        case ControlMode::STOP:
            measuredPower    = 0;
            measuredPressure = 0;
            ws->setStatusShort("STOP");
            break;
        case ControlMode::CONSTANT_POWER:
            measuredPower    = pwrSP;   // replace with real sensor read
            measuredPressure = 30;      // replace with real sensor read
            ws->setStatusShort("Konstantleistung");
            break;
        case ControlMode::CONSTANT_PRESSURE:
            measuredPower    = 45;      // replace with real sensor read
            measuredPressure = presSP;  // replace with real sensor read
            ws->setStatusShort("Konstantdruck");
            break;
    }

    ws->setPower   ((int)measuredPower);
    ws->setPressure((int)measuredPressure);

    // ── Software clock (ticks from last browser sync) ─────────────────────
    if (lastRtcUpdate > 0) {
        deviceEpoch = ws->getClientEpoch() +
                      (uint32_t)((millis() - lastRtcUpdate) / 1000UL);
    }

    // ── Periodic log ──────────────────────────────────────────────────────
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 5000) {
        lastLog = millis();
        char buf[STATUS_MSG_LEN];

        snprintf(buf, sizeof(buf), "Uptime: %lu s", millis() / 1000UL);
        ws->setStatusMessage(0, buf);

        snprintf(buf, sizeof(buf), "Modus:%d Leistung:%d W Druck:%d Bar",
                 (int)mode, (int)measuredPower, (int)measuredPressure);
        ws->setStatusMessage(2, buf);

    }

    delay(10);
}
