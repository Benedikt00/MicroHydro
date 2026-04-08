	#include <WiFi.h>
#include "arduino_secrets.h"
#include <NTPClient.h>
#include <mbed_mktime.h>
 
// Wi-Fi network credentials.
int status  = WL_IDLE_STATUS;
 
// NTP client configuration and RTC update interval.
WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -6*3600, 0);
// Display time every 5 seconds.
unsigned long interval = 5000UL;
unsigned long lastTime = 0;
 
void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ;
  }
  delay(5000);
 
  // Attempt Wi-Fi connection.
  while (status != WL_CONNECTED) {
    Serial.print("- Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, password);
    delay(500);
  }
 
  // NTP client object initialization and time update, display updated time on the Serial Monitor.
  timeClient.begin();
  timeClient.update();
  const unsigned long epoch = timeClient.getEpochTime();
  set_time(epoch);
 
  // Show the synchronized time.
  Serial.println();
  Serial.println("- TIME INFORMATION:");
  Serial.print("- RTC time: ");
  Serial.println(getLocalTime());
}
 
void loop() {
  // Display RTC time periodically.
  unsigned long currentTime = millis();
  if (currentTime - lastTime >= interval) {
    displayRTC();
    lastTime = currentTime;
  }
}
 
/**
  Display Opta's internal RTC time 
 
  @param none
  @return none
*/
void displayRTC() {
  Serial.println();
  Serial.println("- TIME INFORMATION:");
  Serial.print("- RTC time: ");
  Serial.println(getLocalTime());
}
 
/**
  Retrieves Opta's RTC time
 
  @param none
  @return Opta's RTC time in hh:mm:ss format
*/
String getLocalTime() {
  char buffer[32];
  tm t;
  _rtc_localtime(time(NULL), &t, RTC_FULL_LEAP_YEAR_SUPPORT);
  strftime(buffer, 32, "%k:%M:%S", &t);
  return String(buffer);
}