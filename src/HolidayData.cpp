#include "HolidayData.h"
#include <FS.h>
#include <LittleFS.h> // ← Changed to LittleFS
#include <ArduinoJson.h>

JsonDocument holidayDoc;

void Holiday_Init()
{
  // Try to mount LittleFS
  if (!LittleFS.begin(true))
  { // true = format if failed
    printf("LittleFS Mount Failed! Formatting...\n");
    if (!LittleFS.begin(true))
    {
      printf("LittleFS Format Failed. Holiday feature disabled.\n");
      return;
    }
  }

  File file = LittleFS.open("/holiday.json", "r");
  if (!file)
  {
    printf("Failed to open /holiday.json\n");
    return;
  }

  DeserializationError error = deserializeJson(holidayDoc, file);
  file.close();

  if (error)
  {
    printf("Failed to parse holiday.json\n");
  }
  else
  {
    printf("Holiday list loaded successfully (%d holidays)\n",
           (int)holidayDoc["holidays"].size());
  }
}