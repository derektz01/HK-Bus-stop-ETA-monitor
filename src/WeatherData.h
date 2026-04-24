#pragma once
#include <Arduino.h>

// Weather variables (global - accessible everywhere)
extern float current_temperature;
extern uint8_t current_humidity;
extern char current_weather_emoji[8];
extern char current_weather_desc[32];

// Functions
void Weather_FetchOpenMeteo();