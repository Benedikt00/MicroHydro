

class OptaAbstraction{
  public:
    OptaAbstraction()
    {
      getExpansions()
    }

  private:
   void getExpansions(){

  }

  /* -------------------------------------------------------------------------- */
  void getExpansionInfo() {
  /* -------------------------------------------------------------------------- */
    static long int start = millis();
    
    if(millis() - start > 30000) {
      start = millis();
      Serial.print("\n*** Number of expansions: ");
      Serial.println(OptaController.getExpansionNum());

      for(int i = 0; i < OptaController.getExpansionNum(); i++) {
        Serial.print("    Expansion n. ");
        Serial.print(i);
        Serial.print(" type ");
        printExpansionType(OptaController.getExpansionType(i));
        Serial.print(" I2C address ");
        Serial.println(OptaController.getExpansionI2Caddress(i));
      }
    } 
  
    /* -------------------------------------------------------------------------- */
    void printExpansionType(ExpansionType_t t) {
      /* -------------------------------------------------------------------------- */
      if (t == EXPANSION_NOT_VALID) {
        Serial.print("Unknown!");
      } else if (t == EXPANSION_OPTA_DIGITAL_MEC) {
        Serial.print("Opta --- DIGITAL [Mechanical]  ---");
      } else if (t == EXPANSION_OPTA_DIGITAL_STS) {
        Serial.print("Opta --- DIGITAL [Solid State] ---");
      } else if (t == EXPANSION_DIGITAL_INVALID) {
        Serial.print("Opta --- DIGITAL [!!Invalid!!] ---");
      } else if (t == EXPANSION_OPTA_ANALOG) {
        Serial.print("~~~ Opta  ANALOG ~~~");
      } else {
        Serial.print("Unknown!");
      }
    }
    
    void setTime(const unsigned long epoch){
      const unsigned long epoch = timeClient.getEpochTime();
      set_time(epoch);

      // Show the synchronized time.
      Serial.println();
      Serial.println("- TIME INFORMATION:");
      Serial.print("- RTC time: ");
      Serial.println(getLocalTime());
    }

    String getLocalTime() {
      char buffer[32];
      tm t;
      _rtc_localtime(time(NULL), &t, RTC_FULL_LEAP_YEAR_SUPPORT);
      strftime(buffer, 32, "%k:%M:%S", &t);
      return String(buffer);
    }


}


class generalIO {
public:
	int address;
};


class DigitalOutput : generalIO {
public:
	bool state;

	int setState(bool set_to) {
		std::cout << "Setting State ADD" << address << " " << state << std::endl;
		state = set_to;
		if (state) {
			return 1;
		}
		return 0;
	}

	int updateIO() {
		//TODO
		return 0;
	};

};


class DigitalOutput : generalIO {
public:
	bool get_state() {
		bool state = 0;
		//Todo
		return state;
	};

};