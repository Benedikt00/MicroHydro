// TimeScheduler.h
#pragma once

// Maximum number of scheduled events
#define MAX_SCHEDULES 8

struct ScheduleEntry {
    uint8_t  hour;
    uint8_t  minute;
    void     (*callback)();
    bool     fired;
    bool     active;
};

class TimeScheduler {
public:
    TimeScheduler() : _scheduleCount(0) {}

    /** Set RTC from a Unix epoch (UTC). */
    bool setFromEpoch(uint32_t epoch) {
        set_time(epoch); 
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();
        Serial.println("Time set from epoche");
    }

    // ── Scheduling ────────────────────────────────────────────────────────────

    /**
     * Register a callback to fire daily at hh:mm.
     * Returns the schedule index (0-based), or -1 if the table is full.
     */
    int8_t addSchedule(uint8_t hour, uint8_t minute, void (*callback)()) {
        if (_scheduleCount >= MAX_SCHEDULES || callback == nullptr) return -1;
        if (hour > 23 || minute > 59)                                return -1;

        _schedules[_scheduleCount] = { hour, minute, callback, false, true };
        return (int8_t)(_scheduleCount++);
    }

    /** Remove a previously added schedule by index. */
    bool removeSchedule(int8_t index) {
        if (index < 0 || index >= _scheduleCount) return false;
        _schedules[index].active = false;
        return true;
    }

    /** Re-enable a schedule that was removed. */
    bool enableSchedule(int8_t index) {
        if (index < 0 || index >= _scheduleCount) return false;
        _schedules[index].active = true;
        return true;
    }


    // ── Tick ─────────────────────────────────────────────────────────────────

    /**
     * Call this every loop iteration (or at least once per second).
     * Fires any callbacks whose hour:minute matches the current RTC time.
     */
    struct tm * ptm;
    void tick() {
        time_t t = time(NULL);
        ptm = gmtime ( &t );
    }

    void run_tasks(){
        uint8_t h = (uint8_t)ptm->tm_hour;
        uint8_t m = (uint8_t)ptm->tm_min;

        for (uint8_t i = 0; i < _scheduleCount; i++) {
            ScheduleEntry &e = _schedules[i];
            if (!e.active) continue;

            if (h == e.hour && m == e.minute) {
                if (!e.fired) {
                    e.fired = true;
                    e.callback();
                }
            } else {
                e.fired = false; // reset when outside the window
            }
        }
    }

    float time_h_till_h(int dest_h){
        if (dest_h > 24 || dest_h < 0){
            return -1;
        }
        uint8_t h = (uint8_t)ptm->tm_hour;
        uint8_t m = (uint8_t)ptm->tm_min;

        float now = h + m/60;

        if (now > dest_h){
            return dest_h + 24 - now;
        }else{
            return dest_h - now;
        }
        

    }

    uint8_t scheduleCount() const { return _scheduleCount; }

private:
    ScheduleEntry _schedules[MAX_SCHEDULES];
    uint8_t       _scheduleCount;
};