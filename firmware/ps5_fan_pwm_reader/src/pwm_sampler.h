#pragma once

#include <Arduino.h>

struct PwmSample {
  bool valid = false;
  float dutyPercent = 0.0f;
  float frequencyHz = 0.0f;
};

class PwmSampler {
public:
  explicit PwmSampler(uint8_t inputPin);

  void begin();
  PwmSample readAndReset();

private:
  uint8_t pin;
};

