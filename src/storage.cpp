#include "storage.h"
#include "settings.h"
#include "config.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

namespace storage {

#define MNT "/sdcard"
#define SDDIR MNT "/incl"

static bool ok = false;
bool mounted() { return ok; }

static uint32_t fnv(const char* p, size_t n) {
  uint32_t h = 2166136261u;
  while (n--) { h ^= (uint8_t)*p++; h *= 16777619u; }
  return h;
}

static bool readFile(const char* path, char* buf, size_t size) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  size_t n = fread(buf, 1, size - 1, f);
  fclose(f);
  buf[n] = 0;
  return true;
}

static bool writeFile(const char* path, const char* text) {
  FILE* f = fopen(path, "w");
  if (!f) return false;
  size_t n = strlen(text);
  bool good = fwrite(text, 1, n, f) == n;
  good = fflush(f) == 0 && good;
  fsync(fileno(f));
  return fclose(f) == 0 && good;
}

// ------------------------------------------------------------------ A/B records
struct Record {
  const char* name;
  uint32_t seq = 0;
  int slot = 1;   // slot of the newest valid copy (the next write goes to the other)
};
// "rlevel": the name from when there was also a built-in IMU ("level", now unused),
// so a level reference set for the remote IMU then still applies.
static Record recLevel{"rlevel"}, recState{"state"};

static void slotPath(char* out, size_t n, const Record& r, int slot) {
  snprintf(out, n, SDDIR "/%s_%c.txt", r.name, slot ? 'b' : 'a');
}

// Valid copy: "seq=N" first line, "crc=XXXXXXXX" last line over everything before it.
static bool readSlot(const Record& r, int slot, char* buf, size_t size, uint32_t& seq) {
  char path[48];
  slotPath(path, sizeof path, r, slot);
  if (!readFile(path, buf, size)) return false;
  char* c = strstr(buf, "crc=");
  if (!c) return false;
  if ((uint32_t)strtoul(c + 4, nullptr, 16) != fnv(buf, c - buf)) return false;
  *c = 0;
  unsigned long s;
  if (sscanf(buf, "seq=%lu", &s) != 1) return false;
  seq = s;
  return true;
}

static bool loadRecord(Record& r, char* out, size_t size) {
  char tmp[512];
  uint32_t sa = 0, sb = 0;
  bool a = readSlot(r, 0, out, size, sa);
  bool b = readSlot(r, 1, tmp, sizeof tmp, sb);
  if (b && (!a || sb > sa)) {
    strncpy(out, tmp, size - 1);
    out[size - 1] = 0;
    r.seq = sb; r.slot = 1;
  } else if (a) {
    r.seq = sa; r.slot = 0;
  }
  return a || b;
}

static bool saveRecord(Record& r, const char* body) {
  char text[512];
  int n = snprintf(text, sizeof text, "seq=%lu\n%s", (unsigned long)(r.seq + 1), body);
  if (n < 0 || n > (int)sizeof text - 16) return false;
  snprintf(text + n, sizeof text - n, "crc=%08lx\n", (unsigned long)fnv(text, n));
  char path[48];
  slotPath(path, sizeof path, r, r.slot ^ 1);
  if (!writeFile(path, text)) return false;
  r.seq++;
  r.slot ^= 1;
  return true;
}

// ------------------------------------------------------------------ level / state
struct LevelCtx { float* m; int got; };

bool loadLevel(float m[9]) {
  if (!ok) return false;
  char buf[512];
  if (!loadRecord(recLevel, buf, sizeof buf)) return false;
  LevelCtx c{m, 0};
  settings::parseKv(buf, [](const char* k, const char* v, void* p) {
    auto* c = (LevelCtx*)p;
    if (strcmp(k, "mount")) return;
    c->got = sscanf(v, "%f %f %f %f %f %f %f %f %f", &c->m[0], &c->m[1], &c->m[2], &c->m[3], &c->m[4],
                    &c->m[5], &c->m[6], &c->m[7], &c->m[8]);
  }, &c);
  return c.got == 9;
}

bool saveLevel(const float m[9]) {
  if (!ok) return false;
  char body[200];
  snprintf(body, sizeof body, "mount=%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
           m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
  return saveRecord(recLevel, body);
}

bool loadState(PersistState& s) {
  if (!ok) return false;
  char buf[512];
  if (!loadRecord(recState, buf, sizeof buf)) return false;
  settings::parseKv(buf, [](const char* k, const char* v, void* p) {
    auto* s = (PersistState*)p;
    double d = atof(v);
    float f = (float)d;
    if (!strcmp(k, "peak_roll_l")) s->peakRollL = f;
    else if (!strcmp(k, "peak_roll_r")) s->peakRollR = f;
    else if (!strcmp(k, "peak_pitch_up")) s->peakPitchUp = f;
    else if (!strcmp(k, "peak_pitch_dn")) s->peakPitchDn = f;
    else if (!strcmp(k, "max_kmh")) s->maxKmh = f;
    else if (!strcmp(k, "trip_km")) s->tripKm = d;   // trip A (key from before trip B)
    else if (!strcmp(k, "trip_b_km")) s->tripBKm = d;
    else if (!strcmp(k, "fuel_l")) s->fuelL = d;
    else if (!strcmp(k, "fuel_km")) s->fuelKm = d;
    else if (!strcmp(k, "log_no")) s->logNo = strtoul(v, nullptr, 10);
    else if (!strcmp(k, "screen")) s->screen = (uint8_t)atoi(v);
  }, &s);
  return true;
}

bool saveState(const PersistState& s) {
  if (!ok) return false;
  char body[360];
  snprintf(body, sizeof body,
           "peak_roll_l=%.2f\npeak_roll_r=%.2f\npeak_pitch_up=%.2f\npeak_pitch_dn=%.2f\n"
           "max_kmh=%.1f\ntrip_km=%.3f\ntrip_b_km=%.3f\nfuel_l=%.3f\nfuel_km=%.3f\nlog_no=%lu\nscreen=%u\n",
           s.peakRollL, s.peakRollR, s.peakPitchUp, s.peakPitchDn, s.maxKmh, s.tripKm, s.tripBKm, s.fuelL, s.fuelKm,
           (unsigned long)s.logNo, (unsigned)s.screen);
  return saveRecord(recState, body);
}

// ------------------------------------------------------------------ drive log
static FILE* logF = nullptr;

bool logOpen(uint32_t no, const char* header) {
  if (!ok) return false;
  mkdir(SDDIR "/logs", 0777);
  char path[48];
  snprintf(path, sizeof path, SDDIR "/logs/%05lu.csv", (unsigned long)no);
  logF = fopen(path, "w");
  if (!logF) { Serial.printf("[sd] can't create %s\n", path); return false; }
  Serial.printf("[sd] drive log %s\n", path);
  fputs(header, logF);
  logSync();
  return true;
}

bool logWrite(const char* line) { return logF && fputs(line, logF) >= 0; }

void logSync() {
  if (!logF) return;
  fflush(logF);
  fsync(fileno(logF));   // updates the file size in the directory: rows survive a power cut
}

void logClose() {
  if (!logF) return;
  fclose(logF);
  logF = nullptr;
}

bool appendText(const char* name, const char* text, size_t len, size_t maxBytes) {
  if (!ok) return false;
  char path[48], old[56];
  snprintf(path, sizeof path, SDDIR "/%s", name);
  struct stat st;
  if (stat(path, &st) == 0 && (size_t)st.st_size > maxBytes) {
    snprintf(old, sizeof old, "%s.old", path);
    remove(old);
    rename(path, old);
  }
  FILE* f = fopen(path, "a");
  if (!f) return false;
  bool good = fwrite(text, 1, len, f) == len;
  return fclose(f) == 0 && good;
}

static void listDir(const char* path, const char* prefix) {
  DIR* d = opendir(path);
  if (!d) return;
  while (struct dirent* e = readdir(d)) {
    char full[80];
    snprintf(full, sizeof full, "%s/%s", path, e->d_name);
    struct stat st;
    if (stat(full, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      char pre[40];
      snprintf(pre, sizeof pre, "%s%s/", prefix, e->d_name);
      listDir(full, pre);
    } else {
      Serial.printf("  %s%-16s %8lu\n", prefix, e->d_name, (unsigned long)st.st_size);
    }
  }
  closedir(d);
}

void listFiles() {
  if (!ok) { Serial.println("[sd] no card"); return; }
  Serial.println("[sd] /incl:");
  listDir(SDDIR, "");
}

void printFile(const char* name, size_t tail) {
  if (!ok) { Serial.println("[sd] no card"); return; }
  char path[64];
  snprintf(path, sizeof path, SDDIR "/%s", name);
  FILE* f = fopen(path, "r");
  if (!f) { Serial.printf("[sd] no %s\n", path); return; }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  long from = size > (long)tail ? size - (long)tail : 0;
  fseek(f, from, SEEK_SET);
  Serial.printf("----- %s (%ld bytes, from %ld) -----\n", path, size, from);
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) Serial.write((const uint8_t*)buf, n);
  fclose(f);
  Serial.println("\n----- end -----");
}

// ------------------------------------------------------------------ mount + config
bool begin() {
  // D3 must be high for SD (not SPI) mode; board::begin() already set EXIO4 high.
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  if (!SD_MMC.begin(MNT, true, false, SDMMC_FREQ_DEFAULT, 3)) {
    Serial.println("[sd] no card (or not FAT32)");
    return false;
  }
  ok = true;
  Serial.printf("[sd] card %llu MB, %llu MB used\n", SD_MMC.cardSize() >> 20, SD_MMC.usedBytes() >> 20);
  mkdir(SDDIR, 0777);

  const size_t bufSize = 2048;
  char* buf = (char*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return true;
  const char* path = SDDIR "/config.txt";
  if (readFile(path, buf, bufSize)) {
    settings::apply(buf, [](const char* k) { Serial.printf("[sd] config.txt: unknown setting \"%s\"\n", k); });
    Serial.println("[sd] config.txt loaded");
  } else {
    settings::format(buf, bufSize);
    Serial.printf("[sd] config.txt %s\n", writeFile(path, buf) ? "written with defaults" : "write FAILED");
  }
  heap_caps_free(buf);
  return true;
}

bool saveConfig() {
  if (!ok) return false;
  const size_t bufSize = 2048;
  char* buf = (char*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return false;
  settings::format(buf, bufSize);
  bool good = writeFile(SDDIR "/config.txt", buf);
  heap_caps_free(buf);
  return good;
}

}  // namespace storage
