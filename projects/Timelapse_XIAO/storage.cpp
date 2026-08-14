#include "storage.h"

#include <SPI.h>
#include <Preferences.h>   // bundled nel core ESP32: contatori persistenti in NVS

// Chip select della microSD sulla scheda Sense (vedi nota in storage.h:
// e' anche il LED utente della XIAO). ATTENZIONE: lo schematico Seeed
// riporta GPIO3, ma e' un errore noto — il pin buono, confermato anche dal
// loro tutorial e dagli utenti, e' il 21.
#define SD_CS_PIN 21

// Frequenza del bus SPI verso la card: 4 MHz e' il valore degli esempi
// ufficiali Seeed, prudente e sempre funzionante. Si puo' alzare (10-20 MHz)
// per scritture piu' rapide, a rischio di mount falliti con card lente.
// I pin SCK/MISO/MOSI non si passano: SD.begin() chiama SPI.begin() e i
// default della variante XIAO_ESP32S3 sono gia' 7/8/9 (D8/D9/D10).
#define SD_SPI_HZ 4000000

#define LOG_HEADER "ts_iso,boot_id,fonte_ora,sorgente,file,byte,esito"

#define NVS_KEY_BOOTID "boot_id"
#define NVS_KEY_SHOTS  "scatti"

// Occupazione della card: SD.usedBytes() percorre la FAT, quindi non va
// chiamata ad ogni giro della web UI (che chiede lo stato ogni pochi
// secondi). Il valore cambia lentamente: si rilegge al massimo ogni 15 s.
#define USAGE_TTL_MS 15000UL

static bool     s_mounted        = false;
static char     s_lastError[64]  = "non ancora montata";
static uint32_t s_bootId         = 0;
static uint32_t s_shotTotal      = 0;

static char     s_today[SD_DAY_LEN + 1] = "";   // giorno della cartella in uso
static uint32_t s_todayCount     = 0;

static uint64_t s_usedMbCache    = 0;
static uint64_t s_totalMbCache   = 0;
static uint32_t s_usageMs        = 0;

// ---------------------------------------------------------------------
//  NVS: contatori che devono sopravvivere al riavvio
// ---------------------------------------------------------------------
static uint32_t nvs_bump(const char* key, uint32_t step) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return 0;
  uint32_t v = prefs.getUInt(key, 0) + step;
  prefs.putUInt(key, v);
  prefs.end();
  return v;
}

static void nvs_set(const char* key, uint32_t value) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return;
  prefs.putUInt(key, value);
  prefs.end();
}

static uint32_t nvs_get(const char* key) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return 0;
  uint32_t v = prefs.getUInt(key, 0);
  prefs.end();
  return v;
}

uint32_t sd_boot_id() {
  if (s_bootId == 0) s_bootId = nvs_bump(NVS_KEY_BOOTID, 1);
  return s_bootId;
}

uint32_t sd_shot_total() { return s_shotTotal; }

// ---------------------------------------------------------------------
//  Utility sui nomi
// ---------------------------------------------------------------------
// f.name() torna il nome nudo o il percorso completo a seconda della
// versione del core: tieni sempre solo la parte dopo l'ultimo '/'.
static const char* baseName(const char* p) {
  const char* slash = strrchr(p, '/');
  return slash ? slash + 1 : p;
}

bool sd_day_is_valid(const char* day) {
  if (day == nullptr || strlen(day) != SD_DAY_LEN) return false;
  for (int i = 0; i < SD_DAY_LEN; i++) {
    const bool wantDash = (i == 4 || i == 7);
    if (wantDash != (day[i] == '-')) return false;
    if (!wantDash && (day[i] < '0' || day[i] > '9')) return false;
  }
  return true;
}

bool sd_name_is_safe(const char* name) {
  if (name == nullptr || name[0] == '\0' || strlen(name) > 32) return false;
  if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) return false;
  return true;
}

static void dayPath(const char* day, char* out, size_t cap) {
  snprintf(out, cap, SD_ROOT_DIR "/%s", day);
}

static void logPath(const char* day, char* out, size_t cap) {
  snprintf(out, cap, SD_LOG_DIR "/%s.csv", day);
}

// ---------------------------------------------------------------------
//  Mount
// ---------------------------------------------------------------------
bool sd_begin() {
  sd_boot_id();                        // fissa il boot_id anche senza card
  s_shotTotal = nvs_get(NVS_KEY_SHOTS);

  if (s_mounted) return true;

  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) {
    snprintf(s_lastError, sizeof(s_lastError), "mount fallito (card assente?)");
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    snprintf(s_lastError, sizeof(s_lastError), "nessuna card nello slot");
    return false;
  }
  if ((!SD.exists(SD_ROOT_DIR) && !SD.mkdir(SD_ROOT_DIR)) ||
      (!SD.exists(SD_LOG_DIR)  && !SD.mkdir(SD_LOG_DIR))) {
    SD.end();
    snprintf(s_lastError, sizeof(s_lastError), "impossibile creare %s (FAT32?)", SD_ROOT_DIR);
    return false;
  }

  s_mounted   = true;
  s_usageMs   = 0;                     // forza la prima lettura dell'occupazione
  s_today[0]  = '\0';
  s_todayCount = 0;
  snprintf(s_lastError, sizeof(s_lastError), "ok");
  Serial.printf("[SD] montata: %llu MB totali, %llu MB liberi\n",
                sd_total_mb(), sd_free_mb());
  return true;
}

bool        sd_mounted()    { return s_mounted; }
const char* sd_last_error() { return s_lastError; }

static void refreshUsage() {
  if (!s_mounted) { s_totalMbCache = s_usedMbCache = 0; return; }
  const uint32_t now = millis();
  if (s_usageMs != 0 && (now - s_usageMs) < USAGE_TTL_MS) return;
  s_usageMs      = now ? now : 1;
  s_totalMbCache = SD.cardSize()  / (1024ULL * 1024ULL);
  s_usedMbCache  = SD.usedBytes() / (1024ULL * 1024ULL);
}

uint64_t sd_total_mb() { refreshUsage(); return s_totalMbCache; }
uint64_t sd_used_mb()  { refreshUsage(); return s_usedMbCache; }

uint64_t sd_free_mb() {
  refreshUsage();
  return (s_totalMbCache > s_usedMbCache) ? (s_totalMbCache - s_usedMbCache) : 0;
}

// ---------------------------------------------------------------------
//  Scrittura
// ---------------------------------------------------------------------
static uint32_t countPhotos(const char* dirPath) {
  File dir = SD.open(dirPath);
  if (!dir) return 0;
  uint32_t n = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory()) n++;
    f.close();
  }
  dir.close();
  return n;
}

bool sd_save_photo(const uint8_t* data, size_t len, const struct tm* when,
                   char* day_out, size_t day_cap, char* name_out, size_t name_cap) {
  if (day_out  && day_cap)  day_out[0]  = '\0';
  if (name_out && name_cap) name_out[0] = '\0';
  if (!s_mounted || data == nullptr || len == 0 || when == nullptr) return false;

  char day[SD_DAY_LEN + 1];
  char hhmmss[8];
  strftime(day,    sizeof(day),    "%Y-%m-%d", when);
  strftime(hhmmss, sizeof(hhmmss), "%H%M%S",   when);

  char dir[32];
  dayPath(day, dir, sizeof(dir));
  if (!SD.exists(dir) && !SD.mkdir(dir)) {
    snprintf(s_lastError, sizeof(s_lastError), "impossibile creare %s", dir);
    return false;
  }

  // Cambio di giorno (o primo scatto dopo il boot): il contatore delle foto
  // del giorno si ricava scandendo la cartella una volta sola.
  if (strcmp(day, s_today) != 0) {
    strlcpy(s_today, day, sizeof(s_today));
    s_todayCount = countPhotos(dir);
  }

  // Uno scatto manuale puo' cadere nello stesso secondo di quello
  // automatico: in quel caso si aggiunge un suffisso invece di
  // sovrascrivere la foto gia' salvata.
  char path[48];
  char name[20];
  snprintf(name, sizeof(name), "%s.JPG", hhmmss);
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  for (int i = 1; i < 10 && SD.exists(path); i++) {
    snprintf(name, sizeof(name), "%s_%d.JPG", hhmmss, i);
    snprintf(path, sizeof(path), "%s/%s", dir, name);
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    snprintf(s_lastError, sizeof(s_lastError), "apertura %s fallita", path);
    return false;
  }
  const size_t written = f.write(data, len);
  f.close();

  if (written != len) {
    SD.remove(path);   // meglio nessun file che un JPEG troncato
    snprintf(s_lastError, sizeof(s_lastError), "scritti %u/%u byte (card piena?)",
             (unsigned)written, (unsigned)len);
    return false;
  }

  s_todayCount++;
  s_shotTotal++;
  nvs_set(NVS_KEY_SHOTS, s_shotTotal);
  s_usageMs = 0;   // l'occupazione e' cambiata: rileggila alla prossima richiesta

  if (day_out  && day_cap)  strlcpy(day_out,  day,  day_cap);
  if (name_out && name_cap) strlcpy(name_out, name, name_cap);
  return true;
}

void sd_log_shot(const struct tm* when, const char* fonte_ora, const char* sorgente,
                 const char* file, size_t bytes, const char* esito) {
  if (!s_mounted || when == nullptr) return;

  char day[SD_DAY_LEN + 1];
  char iso[24];
  strftime(day, sizeof(day), "%Y-%m-%d",          when);
  strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", when);

  char path[40];
  logPath(day, path, sizeof(path));

  // Intestazione solo alla creazione: le accensioni successive si accodano
  // allo stesso file del giorno (la colonna boot_id le distingue).
  const bool isNew = !SD.exists(path);
  File f = SD.open(path, isNew ? FILE_WRITE : FILE_APPEND);
  if (!f) return;
  if (isNew) f.println(LOG_HEADER);
  f.printf("%s,%lu,%s,%s,%s,%u,%s\n",
           iso, (unsigned long)s_bootId,
           fonte_ora ? fonte_ora : "?",
           sorgente  ? sorgente  : "?",
           (file && file[0]) ? file : "-",
           (unsigned)bytes,
           esito ? esito : "?");
  f.close();
}

uint32_t    sd_photos_today() { return s_todayCount; }
const char* sd_today_dir()    { return s_today; }

// ---------------------------------------------------------------------
//  Lettura / gestione file (usata dalla web UI)
// ---------------------------------------------------------------------
int sd_list_days(sd_day_cb_t cb, void* arg, int max_items) {
  if (!s_mounted || cb == nullptr) return 0;
  File root = SD.open(SD_ROOT_DIR);
  if (!root) return 0;

  int n = 0;
  for (File f = root.openNextFile(); f && n < max_items; f = root.openNextFile()) {
    const char* nm = baseName(f.name());
    // Solo le cartelle-giorno: "log" e qualunque file finito li' dentro a
    // mano restano fuori dall'elenco.
    if (f.isDirectory() && sd_day_is_valid(nm)) {
      cb(nm, arg);
      n++;
    }
    f.close();
  }
  root.close();
  return n;
}

int sd_list_photos(const char* day, sd_photo_cb_t cb, void* arg, int max_items) {
  if (!s_mounted || cb == nullptr || !sd_day_is_valid(day)) return 0;
  char dir[32];
  dayPath(day, dir, sizeof(dir));
  File d = SD.open(dir);
  if (!d) return 0;

  int n = 0;
  for (File f = d.openNextFile(); f && n < max_items; f = d.openNextFile()) {
    if (!f.isDirectory()) {
      cb(baseName(f.name()), f.size(), arg);
      n++;
    }
    f.close();
  }
  d.close();
  return n;
}

// I nomi delle cartelle sono ordinabili come stringhe, quindi il giorno
// piu' vecchio e' semplicemente il minimo: nessuna conversione a data.
static void oldestCb(const char* day, void* arg) {
  char* best = (char*)arg;
  if (best[0] == '\0' || strcmp(day, best) < 0) strlcpy(best, day, SD_DAY_LEN + 1);
}

bool sd_oldest_day(char* out, size_t cap) {
  if (!out || cap <= SD_DAY_LEN) return false;
  char best[SD_DAY_LEN + 1] = "";
  sd_list_days(oldestCb, best, 400);
  if (best[0] == '\0') return false;
  strlcpy(out, best, cap);
  return true;
}

File sd_open_photo(const char* day, const char* name) {
  if (!s_mounted || !sd_day_is_valid(day) || !sd_name_is_safe(name)) return File();
  char path[48];
  snprintf(path, sizeof(path), SD_ROOT_DIR "/%s/%s", day, name);
  return SD.open(path, FILE_READ);
}

bool sd_delete_photo(const char* day, const char* name) {
  if (!s_mounted || !sd_day_is_valid(day) || !sd_name_is_safe(name)) return false;
  char path[48];
  snprintf(path, sizeof(path), SD_ROOT_DIR "/%s/%s", day, name);
  if (!SD.remove(path)) return false;
  if (strcmp(day, s_today) == 0 && s_todayCount > 0) s_todayCount--;
  s_usageMs = 0;
  return true;
}

bool sd_delete_day(const char* day) {
  if (!s_mounted || !sd_day_is_valid(day)) return false;
  char dir[32];
  dayPath(day, dir, sizeof(dir));

  // SD.rmdir() vuole la cartella vuota: prima i file, poi la cartella.
  // I nomi vanno letti e chiusi PRIMA di rimuoverli, senza cancellare
  // mentre la directory e' aperta in iterazione.
  while (true) {
    File d = SD.open(dir);
    if (!d) break;
    char name[36] = "";
    File f = d.openNextFile();
    if (f) {
      strlcpy(name, baseName(f.name()), sizeof(name));
      f.close();
    }
    d.close();
    if (name[0] == '\0') break;

    char path[48];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (!SD.remove(path)) {
      snprintf(s_lastError, sizeof(s_lastError), "impossibile eliminare %s", path);
      return false;
    }
  }

  SD.rmdir(dir);
  if (strcmp(day, s_today) == 0) { s_today[0] = '\0'; s_todayCount = 0; }
  s_usageMs = 0;
  return true;
}

File sd_open_log(const char* day) {
  if (!s_mounted || !sd_day_is_valid(day)) return File();
  char path[40];
  logPath(day, path, sizeof(path));
  return SD.open(path, FILE_READ);
}
