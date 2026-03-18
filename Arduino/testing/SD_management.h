#include "FS.h"
#include "SD.h"
#include <time.h>


class SD_management {
public:
  File cFile;
  int chipSelect;
  bool SD_error{ false };
  SD_management(int chipselect) {
    chipSelect = chipselect;
    init_SD();
  };

  bool init_SD() {
    if (!SD.begin(chipSelect)) {
      Serial.println("SD Initialization failed!");
      SD_error = true;
    } else {
      Serial.println("SD Initialization done.");
      SD_error = false;
    }
    return SD_error;
  };

  int write_telegram(const String &msg) {
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo = localtime(&now);

    char path[16];
    strftime(path, sizeof(path), "/%Y/%m%d.txt", timeinfo);

    // Build time prefix: "14:35 - "
    char entry_time[9];
    strftime(entry_time, sizeof(entry_time), "%H:%M - ", timeinfo);

    String entry = String(entry_time) + msg + "\n";

    write_to_file(entry, path);

    return 1;
  };



  void write_to_file(const String &msg, const String &filename) {
    Serial.println("Filename: " + filename);
    if (!SD_error) {
      SD.begin(chipSelect);
      if (!SD.exists(filename)) {
        createDir(filename);
      }

      cFile = SD.open(filename, FILE_APPEND);

      if (!cFile) {
        Serial.println("Failed to open file for appending");
        SD_error = true;
        return;
      }
      if (cFile.print(msg)) {
        Serial.println("Message appended");
      } else {
        Serial.println("Append failed");
        SD_error = true;
      }
      cFile.close();
    } else {
      Serial.println("SD not ready mby reinit");
    }
  };

  void createDir(const String &filepath) {
    // Extract directory from full filepath
    int lastSlash = filepath.lastIndexOf('/');
    if (lastSlash <= 0) return;  // no directory to create

    String dir = filepath.substring(0, lastSlash);

    if (!SD.exists(dir.c_str())) {
      Serial.printf("Creating dir: %s\n", dir.c_str());
      if (!SD.mkdir(dir.c_str())) {
        Serial.println("mkdir failed");
        SD_error = true;
      } else {
        Serial.println("Dir created");
      }
    }
  };
};

