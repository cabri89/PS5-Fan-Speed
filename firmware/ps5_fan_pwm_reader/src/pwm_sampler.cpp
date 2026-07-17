#include "pwm_sampler.h"

#include <driver/gpio.h>
#include <esp_timer.h>

#include "config.h"

namespace {
volatile uint64_t totalHighUs = 0;
volatile uint64_t totalLowUs = 0;
volatile uint32_t periods = 0;
volatile uint64_t lastEdgeUs = 0;
volatile bool lastLevel = false;

uint8_t interruptPin = 0;

void IRAM_ATTR handlePwmChange() {
  const uint64_t nowUs = esp_timer_get_time();
  const bool level = gpio_get_level(static_cast<gpio_num_t>(interruptPin));
  const uint32_t deltaUs = static_cast<uint32_t>(nowUs - lastEdgeUs);

  lastEdgeUs = nowUs;

  if (lastLevel) {
    totalHighUs += deltaUs;
  } else {
    totalLowUs += deltaUs;
  }

  lastLevel = level;

  if (level) {
    periods++;
  }
}
}

PwmSampler::PwmSampler(uint8_t inputPin) : pin(inputPin) {}

void PwmSampler::begin() {
  interruptPin = pin;
  pinMode(pin, INPUT);

  lastLevel = digitalRead(pin);
  lastEdgeUs = esp_timer_get_time();

  attachInterrupt(digitalPinToInterrupt(pin), handlePwmChange, CHANGE);
}

PwmSample PwmSampler::readAndReset() {
  noInterrupts();

  const uint64_t highUs = totalHighUs;
  const uint64_t lowUs = totalLowUs;
  const uint32_t periodCount = periods;

  totalHighUs = 0;
  totalLowUs = 0;
  periods = 0;

  interrupts();

  PwmSample sample;
  const uint64_t totalUs = highUs + lowUs;

  if (periodCount <= MIN_PERIODS_PER_SAMPLE || totalUs == 0) {
    return sample;
  }

  sample.valid = true;
  sample.dutyPercent = (100.0f * static_cast<float>(highUs)) / static_cast<float>(totalUs);
  sample.frequencyHz = static_cast<float>(periodCount) / (static_cast<float>(totalUs) / 1000000.0f);
  return sample;
}
