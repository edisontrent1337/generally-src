// Drivers (drivers/*.drv) and the car models they drive.
#pragma once
#include "bridge/msvc.h"

// A joystick input bound to a car control.
struct JoyBinding {
    int32_t joy;    // joystick number
    int32_t index;  // axis, button or hat number
    uint8_t unknown8;
    uint8_t kind;      // 0 axis, 1 button, 2 hat
    uint8_t negative;  // axis direction
    uint8_t pad;
};
static_assert(sizeof(JoyBinding) == 12);

enum Control { CTRL_ACCELERATE, CTRL_BRAKE, CTRL_LEFT, CTRL_RIGHT, CTRL_COUNT };

// A driver record (array at g_drivers, 0x8c bytes each).
struct Driver {
    int32_t seasons;      // seasons completed
    int32_t places[6];    // how often the driver finished a season 1st..6th
    int32_t points;       // career points
    int32_t races;        // races finished
    int32_t race_places[6];  // how often the driver finished a race 1st..6th
    int32_t fastest_laps; // races with the fastest lap
    int32_t car;                     // index into g_cars
    uint32_t color[2];               // car colours, 0x00RRGGBB
    JoyBinding joy[CTRL_COUNT];      // used where key[i] is negative
    int32_t key[CTRL_COUNT];         // SDL key of each control, -1 = joystick binding; 0 for AI drivers,
                                     // which keep their skill level in key[CTRL_BRAKE] instead
    bool human() const { return key[CTRL_ACCELERATE] != 0; }
    int32_t &ai_skill() { return key[CTRL_BRAKE]; }
};
static_assert(offsetof(Driver, car) == 0x40 && offsetof(Driver, joy) == 0x4c && offsetof(Driver, key) == 0x7c);
static_assert(sizeof(Driver) == 0x8c);

// A car model (cars/*): 0x540 bytes, starting with its name.
struct CarModel {
    char name[0x540];
};
static_assert(sizeof(CarModel) == 0x540);

GLOBAL(ptr32<CarModel>, g_cars, 0x4a4d44);
GLOBAL(int32_t, g_car_count, 0x4a4c90);
GLOBAL(ptr32<Driver>, g_drivers, 0x4a4c98);

struct GameFile;
void driver_save(Driver *d, const MsString *name);
int driver_write(GameFile *f, const Driver *d);
int driver_read(GameFile *f, Driver *d);
void driver_load(Driver *d, const MsString *name);
void driver_delete(const MsString *name);
