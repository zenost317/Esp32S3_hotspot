#include "SensorHandler.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>

static Adafruit_AHTX0 _aht;
static bool _ready = false;

bool sensorInit(TwoWire& wire) {
  for (uint8_t i = 0; i < 5; i++) {
    if (_aht.begin(&wire, 0, 0x38)) {
      _ready = true;
      return true;
    }
    delay(500);
    yield();
  }
  return false;
}

bool sensorRead(float& tempC, float& humiPct) {
  if (!_ready) return false;
  for (int retry = 0; retry < 3; retry++) {
    sensors_event_t h, t;
    if (_aht.getEvent(&h, &t)) {
      tempC   = t.temperature;
      humiPct = h.relative_humidity;
      return true;
    }
    yield();
    delay(50);
  }
  return false;
}