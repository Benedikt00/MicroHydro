#include <LiquidCrystal_I2C.h>

#include <Wire.h>

class lcd_management {
public:
  float power{ 0.0 };
  float preassure{ 0.0 };
  String error{""};
  String status{ "" };



  LiquidCrystal_I2C lcd;  // ✅ declared at class level


  lcd_management()
    : lcd(0x27, 20, 4) {  // ✅ initialized in initializer list
    lcd.init();
    lcd.backlight();
  }

  void update() {
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print("Leistung: " + String(power) + " W");
    lcd.setCursor(1, 1);
    lcd.print("Druck: " + String(power) + " Bar");
    lcd.setCursor(1, 2);
    lcd.print(status);
    lcd.setCursor(1, 3);
    lcd.print(error)

  };
};
