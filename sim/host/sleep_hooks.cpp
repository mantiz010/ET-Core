// Simulator stand-in for deep sleep. The memory that survives it is the file named
// by ETCORE_SIM_RTC; "sleeping" ends the process, and starting the device again
// with the same file is the wake. No file = power was lost.
#include <ETCore.h>

bool etcoreSleepLoad(ETCoreSleepState& state) {
  const char* path = getenv("ETCORE_SIM_RTC");
  if (!path) return false;
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  const bool ok = fread(&state, sizeof(state), 1, f) == 1;
  fclose(f);
  return ok;
}

void etcoreSleepSave(const ETCoreSleepState& state) {
  const char* path = getenv("ETCORE_SIM_RTC");
  if (!path) return;
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fwrite(&state, sizeof(state), 1, f);
  fclose(f);
}

void etcoreDeepSleep(uint32_t) {
  fflush(stdout);
  _exit(0);
}
