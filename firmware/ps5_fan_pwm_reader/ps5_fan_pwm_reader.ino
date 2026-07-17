#include <Arduino.h>
#include <Preferences.h>
#include <U8g2lib.h>

#include "src/config.h"
#include "src/pwm_sampler.h"

U8G2_SSD1306_72X40_ER_F_HW_I2C display(
    U8G2_R0,
    U8X8_PIN_NONE,
    OLED_SCL_PIN,
    OLED_SDA_PIN);

PwmSampler pwmSampler(PWM_INPUT_PIN);
Preferences preferences;

enum class DisplayPage : uint8_t {
  SimplePwm = 0,
  StatusFace,
  Graph,
  Details,
  Count
};

float dutyFiltered = 0.0f;
float frequencyFiltered = 0.0f;
float dutyMax = 0.0f;
float dutyHistory[GRAPH_POINTS];
DisplayPage currentPage = DisplayPage::SimplePwm;
bool pwmDetected = false;

static uint32_t lastDisplayMs = 0;
static uint32_t lastGraphMs = 0;
static uint32_t lastButtonMs = 0;
static uint32_t lastValidSignalMs = 0;
static bool lastButtonPressed = false;

enum LedMode
{
    LED_OFF,
    LED_ON,
    LED_BLINK_SLOW,
    LED_BLINK_FAST
};

LedMode ledMode = LED_OFF;

bool ledState = false;
unsigned long lastBlink = 0;

static void drawBootScreen() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  drawCenteredText("PS5 FAN PWM", 10);
  drawCenteredText("PAR", 24);
  drawCenteredText("CABRIDIY", 38);

  display.sendBuffer();
}

static bool isButtonPressed() {
  const bool level = digitalRead(BUTTON_PIN);
  return BUTTON_ACTIVE_LOW ? !level : level;
}

static void waitOnBootScreen() {
  const uint32_t startMs = millis();

  while (millis() - startMs < BOOT_SCREEN_MS) {
    if (isButtonPressed()) {
      break;
    }

    delay(10);
  }

  lastButtonPressed = isButtonPressed();
  lastButtonMs = millis();
}

static void addHistoryPoint(float value) {
  for (uint8_t i = 0; i < GRAPH_POINTS - 1; i++) {
    dutyHistory[i] = dutyHistory[i + 1];
  }

  dutyHistory[GRAPH_POINTS - 1] = value;

  if (value > dutyMax) {
    dutyMax = value;
  }
}

static void updateButton() {
  const bool pressed = isButtonPressed();
  const uint32_t nowMs = millis();

  if (pressed && !lastButtonPressed && nowMs - lastButtonMs >= BUTTON_DEBOUNCE_MS) {
    lastButtonMs = nowMs;
    const uint8_t nextPage = (static_cast<uint8_t>(currentPage) + 1) % static_cast<uint8_t>(DisplayPage::Count);
    currentPage = static_cast<DisplayPage>(nextPage);
    preferences.putUChar("page", nextPage);
  }

  lastButtonPressed = pressed;
}

static void drawCenteredText(const char *text, const uint8_t y) {
  const uint8_t width = display.getStrWidth(text);
  const uint8_t x = width >= SCREEN_WIDTH ? 0 : (SCREEN_WIDTH - width) / 2;
  display.drawStr(x, y, text);
}

static void drawCoolFace(const uint8_t centerX, const uint8_t centerY) {
  display.drawCircle(centerX, centerY, 11);
  display.drawDisc(centerX - 4, centerY - 3, 1);
  display.drawDisc(centerX + 4, centerY - 3, 1);
  display.drawLine(centerX - 5, centerY + 4, centerX - 2, centerY + 7);
  display.drawLine(centerX - 2, centerY + 7, centerX + 2, centerY + 7);
  display.drawLine(centerX + 2, centerY + 7, centerX + 5, centerY + 4);
}

static void drawHotFace(const uint8_t centerX, const uint8_t centerY) {
  display.drawCircle(centerX, centerY, 11);
  display.drawLine(centerX - 6, centerY - 5, centerX - 2, centerY - 2);
  display.drawLine(centerX + 6, centerY - 5, centerX + 2, centerY - 2);
  display.drawCircle(centerX, centerY + 5, 3);
  display.drawLine(centerX + 10, centerY - 10, centerX + 14, centerY - 14);
  display.drawLine(centerX + 14, centerY - 14, centerX + 17, centerY - 10);
}

static void drawSimplePwm() {
  display.clearBuffer();

  display.setFont(u8g2_font_5x7_tf);
  drawCenteredText("PWM", 7);

  char text[8];
  snprintf(text, sizeof(text), "%.0f%%", dutyFiltered);
  display.setFont(u8g2_font_logisoso24_tf);
  if (display.getStrWidth(text) > SCREEN_WIDTH) {
    display.setFont(u8g2_font_logisoso18_tf);
  }
  drawCenteredText(text, 36);

  display.sendBuffer();
}

static void drawNoPwmMessage() {
  display.clearBuffer();

  display.setFont(u8g2_font_6x10_tf);
  drawCenteredText("PWM", 10);
  drawCenteredText("ABSENT", 22);

  display.setFont(u8g2_font_4x6_tf);
  drawCenteredText("Verifier signal", 33);
  //drawCenteredText("et masse", 39);

  display.sendBuffer();
}

static void drawStatusFace() {
  display.clearBuffer();

  const bool idle = dutyFiltered < STATUS_IDLE_HOT_MAX;
  const bool hot = (dutyFiltered > STATUS_IDLE_COOL_MAX && dutyFiltered < STATUS_IDLE_HOT_MAX) ||
                   dutyFiltered > STATUS_GAME_COOL_MAX;

  display.setFont(u8g2_font_6x10_tf);
  drawCenteredText(idle ? "IDLE" : "GAME", 9);

  if (hot) {
    drawHotFace(36, 25);
  } else {
    drawCoolFace(36, 25);
  }

  display.sendBuffer();
}

static void drawGraph() {
  constexpr int graphTop = 13;
  constexpr int graphBottom = 39;
  constexpr int graphHeight = graphBottom - graphTop;
  constexpr int labelWidth = 12;
  constexpr int graphLeft = labelWidth;
  constexpr int graphWidth = SCREEN_WIDTH - graphLeft;

  float minValue = 100.0f;
  float maxValue = 0.0f;
  bool hasValue = false;

  for (uint8_t i = 0; i < GRAPH_VISIBLE_POINTS; i++) {
    const uint8_t historyIndex = GRAPH_POINTS - GRAPH_VISIBLE_POINTS + i;
    const float value = dutyHistory[historyIndex];

    if (value < 0.0f) {
      continue;
    }

    hasValue = true;

    if (value < minValue) {
      minValue = value;
    }

    if (value > maxValue) {
      maxValue = value;
    }
  }

  display.setFont(u8g2_font_4x6_tf);

  if (!hasValue) {
    return;
  }

  minValue -= GRAPH_MARGIN_PERCENT;
  maxValue += GRAPH_MARGIN_PERCENT;

  if (maxValue - minValue < GRAPH_MIN_SPAN_PERCENT) {
    const float center = (minValue + maxValue) / 2.0f;
    minValue = center - GRAPH_MIN_SPAN_PERCENT / 2.0f;
    maxValue = center + GRAPH_MIN_SPAN_PERCENT / 2.0f;
  }

  minValue = constrain(minValue, 0.0f, 100.0f);
  maxValue = constrain(maxValue, 0.0f, 100.0f);

  if (maxValue - minValue < 1.0f) {
    maxValue = min(100.0f, minValue + GRAPH_MIN_SPAN_PERCENT);
  }

  char label[8];

  snprintf(label, sizeof(label), "%.0f", maxValue);
  display.drawStr(0, graphTop + 5, label);

  snprintf(label, sizeof(label), "%.0f", minValue);
  display.drawStr(0, graphBottom, label);

  for (uint8_t x = graphLeft; x < SCREEN_WIDTH; x += 6) {
    display.drawPixel(x, graphTop + graphHeight / 2);
  }

  display.drawVLine(graphLeft - 1, graphTop, graphHeight + 1);
  display.drawHLine(graphLeft, graphBottom, graphWidth);

  int previousX = -1;
  int previousY = -1;

  for (uint8_t i = 0; i < GRAPH_VISIBLE_POINTS; i++) {
    const uint8_t historyIndex = GRAPH_POINTS - GRAPH_VISIBLE_POINTS + i;
    const float value = dutyHistory[historyIndex];

    if (value < 0.0f) {
      continue;
    }

    const float normalized = (constrain(value, minValue, maxValue) - minValue) / (maxValue - minValue);
    const int x = graphLeft + (i * (graphWidth - 1)) / (GRAPH_VISIBLE_POINTS - 1);
    const int y = graphBottom - static_cast<int>(normalized * graphHeight);

    display.drawPixel(x, y);
    display.drawPixel(x, y - 1);

    if (previousX >= 0) {
      display.drawLine(previousX, previousY, x, y);
    }

    previousX = x;
    previousY = y;
  }
}

static void drawGraphDashboard() {
  display.clearBuffer();
  display.setFont(u8g2_font_4x6_tf);

  char text[20];

  snprintf(text, sizeof(text), "PWM %2.0f%%", dutyFiltered);
  display.drawStr(0, 7, text);

  snprintf(text, sizeof(text), "Max:%.0f", dutyMax);
  display.drawStr(SCREEN_WIDTH - display.getStrWidth(text), 7, text);

  drawGraph();
  display.sendBuffer();
}

static void drawDetails() {
  display.clearBuffer();

  char text[20];

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 7, "PWM");
  display.drawStr(42, 7, "MAX");

  display.setFont(u8g2_font_7x13B_tf);
  snprintf(text, sizeof(text), "%2.0f", dutyFiltered);
  display.drawStr(0, 22, text);
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(21, 22, "%");

  display.setFont(u8g2_font_7x13B_tf);
  snprintf(text, sizeof(text), "%2.0f", dutyMax);
  display.drawStr(42, 22, text);
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(63, 22, "%");

  display.setFont(u8g2_font_6x10_tf);
  snprintf(text, sizeof(text), "%4.0f Hz", frequencyFiltered);
  drawCenteredText(text, 39);

  display.sendBuffer();
}

static void drawCurrentPage() {
  if (!pwmDetected) {
    drawNoPwmMessage();
    return;
  }

  switch (currentPage) {
    case DisplayPage::SimplePwm:
      drawSimplePwm();
      break;
    case DisplayPage::StatusFace:
      drawStatusFace();
      break;
    case DisplayPage::Graph:
      drawGraphDashboard();
      break;
    case DisplayPage::Details:
      drawDetails();
      break;
    case DisplayPage::Count:
      break;
  }
}


void updateLed()
{
    switch(ledMode)
    {
        case LED_OFF:
            digitalWrite(LED_PIN, HIGH);
            break;

        case LED_ON:
            digitalWrite(LED_PIN, LOW);
            break;

        case LED_BLINK_SLOW:
            if (millis() - lastBlink > 500)
            {
                lastBlink = millis();
                ledState = !ledState;
                digitalWrite(LED_PIN, ledState);
            }
            break;

        case LED_BLINK_FAST:
            if (millis() - lastBlink > 100)
            {
                lastBlink = millis();
                ledState = !ledState;
                digitalWrite(LED_PIN, ledState);
            }
            break;
    }
}

void setup() {
  if (SERIAL_DEBUG_ENABLED) {
    Serial.begin(SERIAL_BAUDRATE);
  }

  pinMode(BUTTON_PIN, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  //setup la led
  pinMode(LED_PIN, OUTPUT);

  preferences.begin("ps5fan", false);

  const uint8_t savedPage = preferences.getUChar("page", static_cast<uint8_t>(DisplayPage::SimplePwm));
  if (savedPage < static_cast<uint8_t>(DisplayPage::Count)) {
    currentPage = static_cast<DisplayPage>(savedPage);
  }

  display.begin();
  display.setContrast(OLED_CONTRAST);
  drawBootScreen();

  waitOnBootScreen();

  for (uint8_t i = 0; i < GRAPH_POINTS; i++) {
    dutyHistory[i] = -1.0f;
  }

  pwmSampler.begin();
}

void loop() {
  updateButton();

  const uint32_t nowMs = millis();

  if (nowMs - lastDisplayMs < DISPLAY_REFRESH_MS) {
    return;
  }

  lastDisplayMs = nowMs;

  const PwmSample sample = pwmSampler.readAndReset();

  if (sample.valid) {
    pwmDetected = true;
    lastValidSignalMs = nowMs;

    if (dutyFiltered == 0.0f) {
      dutyFiltered = sample.dutyPercent;
      frequencyFiltered = sample.frequencyHz;
    } else {
      dutyFiltered = dutyFiltered * FILTER_KEEP_RATIO + sample.dutyPercent * FILTER_NEW_RATIO;
      frequencyFiltered = frequencyFiltered * FILTER_KEEP_RATIO + sample.frequencyHz * FILTER_NEW_RATIO;
    }

    if (SERIAL_DEBUG_ENABLED) {
      Serial.printf("Duty : %.2f %%   Freq : %.1f Hz\n", dutyFiltered, frequencyFiltered);
    }
  } else if (nowMs - lastValidSignalMs >= SIGNAL_LOST_MS) {
    pwmDetected = false;
    dutyFiltered = 0.0f;
    frequencyFiltered = 0.0f;
  }

  if (pwmDetected && nowMs - lastGraphMs >= GRAPH_UPDATE_MS) {
    lastGraphMs = nowMs;
    addHistoryPoint(dutyFiltered);
  }
  
  //Si la ventilation max est atteinte la led s'allume et clignotte suivant comment la console ventille
  if(LED_TEMP_ENABLED){
    if (dutyFiltered > STATUS_GAME_COOL_MAX + 10)
        ledMode = LED_BLINK_FAST;
    else if (dutyFiltered > STATUS_GAME_COOL_MAX)
        ledMode = LED_BLINK_SLOW;
    else
        ledMode = LED_OFF;

    updateLed();
  }

  drawCurrentPage();
}
