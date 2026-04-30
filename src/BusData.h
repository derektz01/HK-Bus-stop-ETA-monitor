#pragma once
#include <Arduino.h>
#include <vector>

struct BusInfo
{
  char route[8];
  char destination[40];
  char etaDisplay1[8]; // eta_seq = 1 (next bus)
  char etaDisplay2[8]; // eta_seq = 2 (next next bus)
  char dir[4];         // "I" for inbound, "O" for outbound, etc.
};

extern uint8_t currentPage;
extern std::vector<BusInfo> displayRoutes;

void BusData_Init();
void Switch_To_Next_Page();

// Guards displayRoutes and currentPage. The fetch task (core 0) writes them;
// the slideshow tick (core 1) reads them via Update_Bus_List. Recursive lock
// so nesting from inside the BusData translation unit is safe.
void BusData_Lock();
void BusData_Unlock();

void Fetch_KMB_StopETA(const char *stop_id);
void Fetch_Citybus_StopETA(const char *stop_id);
void AutoRefreshBusETA(const std::vector<String> &kmb_stop_ids,
                       const std::vector<String> &ctb_stop_ids);