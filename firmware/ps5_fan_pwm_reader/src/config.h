#pragma once

#include <Arduino.h>

constexpr uint8_t PWM_INPUT_PIN = 2;
constexpr uint8_t OLED_SDA_PIN = 5;
constexpr uint8_t OLED_SCL_PIN = 6;
constexpr uint8_t BUTTON_PIN = 9;
constexpr uint8_t LED_PIN = 8;

constexpr bool BUTTON_ACTIVE_LOW = true;
constexpr bool LED_TEMP_ENABLED = true;

constexpr uint8_t SCREEN_WIDTH = 72;
constexpr uint8_t SCREEN_HEIGHT = 40;
constexpr uint8_t OLED_CONTRAST = 255;

constexpr bool SERIAL_DEBUG_ENABLED = false;

constexpr uint32_t SERIAL_BAUDRATE = 115200;
constexpr uint32_t DISPLAY_REFRESH_MS = 100;
constexpr uint32_t GRAPH_UPDATE_MS = 10000;
constexpr uint32_t BOOT_SCREEN_MS = 5000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 180;
constexpr uint32_t SIGNAL_LOST_MS = 800;

constexpr uint8_t GRAPH_POINTS = 72;
constexpr uint8_t GRAPH_VISIBLE_POINTS = 30;
constexpr float GRAPH_MIN_SPAN_PERCENT = 10.0f;
constexpr float GRAPH_MARGIN_PERCENT = 2.0f;
constexpr uint8_t MIN_PERIODS_PER_SAMPLE = 10;

constexpr float FILTER_KEEP_RATIO = 0.90f;
constexpr float FILTER_NEW_RATIO = 0.10f;

constexpr float STATUS_IDLE_COOL_MAX = 11.0f;
constexpr float STATUS_IDLE_HOT_MAX = 17.0f;
constexpr float STATUS_GAME_COOL_MAX = 19.0f;
