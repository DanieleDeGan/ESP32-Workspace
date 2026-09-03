#include "sd_logger.h"
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <ctype.h>

// Pin dedicati per l'HW-125 (vedi tabella nel piano): SCK/MISO/MOSI/CS
// passati ESPLICITAMENTE a SPI.begin()+SD.begin(), per non dipendere dai
// pin di default della variante esp32c3 (che potrebbero ricadere su
// GPIO5/6, in conflitto con l'I2C dell'OLED, o su GPIO2, di strapping).
// Pin della microSD sulla scheda di espansione Sense della XIAO S3. Sono gli
// STESSI SCK/MISO/MOSI dell'e-ink (il pannello non usa MISO): cambia solo il
// CS. Il bus lo apre il .ino per il display, prima di tutto il resto — vedi
// sd_begin(), che per questo NON chiama SPI.begin().
// Nota: lo schematico Seeed indica il CS su GPIO3, ed e' un errore noto; il
// pin buono e' il 21, che e' anche il LED utente (lampeggia ad ogni accesso).
#define SD_CS_PIN   21
#define SD_SCK_PIN  7
#define SD_MISO_PIN 8
#define SD_MOSI_PIN 9
// 4 MHz: la card sta su PCB, non su dupont come quella di EnvNode_C3, quindi
// i 400 kHz prudenti di la' non servono. Si puo' alzare se il log dei nodi
// dovesse costare troppo tempo dentro loop(); se invece comparissero errori di
// lettura, il primo numero da abbassare e' questo.
#define SD_SPI_HZ   4000000UL
                                 // per l'inizializzazione. Alzabile (fino a 4-20MHz)
                                 // una volta confermato che la card monta in modo
                                 // affidabile, per velocizzare le scritture.

#define LOG_HEADER "ts_iso,ts_unix,fonte_ora,temp_c,hum_pct"

#define NVS_NS          "meteohub"
#define NVS_KEY_BOOTID  "boot_id"
#define NVS_KEY_RECTOT  "rec_total"

// Il contatore totale si scrive su NVS ogni tot campioni invece che a ogni
// singolo campione: con l'intervallo minimo di log (5s) sarebbero oltre
// 6 milioni di scritture/anno sulla stessa chiave, che nel tempo consuma
// i cicli di erase della flash sottostante la NVS. Un flush ogni 20
// campioni (+ a ogni rollover di giorno, sempre) porta lo stesso caso
// estremo a poche centinaia di migliaia di scritture/anno: sicuro per anni
// di funzionamento continuo. Il contatore e' solo un valore mostrato in
// UI: un crash tra due flush perde al piu' gli ultimi 19 incrementi dal
// display, mai dati nel CSV.
#define REC_TOTAL_FLUSH_EVERY 20

static bool     s_mounted          = false;
static char     s_lastError[48]    = "non ancora montata";
static uint32_t s_bootId           = 0;
static uint32_t s_recordsTotal     = 0;
static uint32_t s_recordsToday     = 0;
static uint8_t  s_writesSinceFlush = 0;
static char     s_currentDay[11]   = "";   // "YYYY-MM-DD" del file attualmente in uso

// ---------------------------------------------------------------------
//  NVS: apri / leggi-o-scrivi / chiudi, mai tenuta aperta a lungo.
// ---------------------------------------------------------------------
static uint32_t nvs_get_u32(const char* key) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, /*readOnly=*/true)) return 0;
  uint32_t v = prefs.getUInt(key, 0);
  prefs.end();
  return v;
}

static void nvs_put_u32(const char* key, uint32_t value) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, /*readOnly=*/false)) return;
  prefs.putUInt(key, value);
  prefs.end();
}

static uint32_t nvs_bump_u32(const char* key, uint32_t step) {
  uint32_t v = nvs_get_u32(key) + step;
  nvs_put_u32(key, v);
  return v;
}

uint32_t sd_boot_id() {
  if (s_bootId == 0) s_bootId = nvs_bump_u32(NVS_KEY_BOOTID, 1);
  return s_bootId;
}

// ---------------------------------------------------------------------
//  Validazione nomi data (query string web -> path SD)
// ---------------------------------------------------------------------
bool sd_name_is_safe(const char* isoDate) {
  if (!isoDate || strlen(isoDate) != 10) return false;
  for (int i = 0; i < 10; i++) {
    char c = isoDate[i];
    if (i == 4 || i == 7) {
      if (c != '-') return false;
    } else if (!isdigit((unsigned char)c)) {
      return false;
    }
  }
  return true;
}

static void logPath(const char* isoDate, char* out, size_t outCap) {
  snprintf(out, outCap, "%s/%s.csv", SD_LOG_DIR, isoDate);
}

// ---------------------------------------------------------------------
//  Mount
// ---------------------------------------------------------------------
bool sd_begin() {
  sd_boot_id();   // fissa il boot_id anche se la card non c'e' (come in storage.cpp)

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  // NIENTE SPI.begin() qui, al contrario di EnvNode_C3: su questa scheda il
  // bus e' gia' stato aperto dal .ino per il pannello e-ink, sugli stessi
  // pin. Richiamarlo rifarebbe l'attach dei segnali mentre il display ci sta
  // sopra, e non c'e' niente da guadagnarci: SCK/MISO/MOSI sono identici.
  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) {
    s_mounted = false;
    strlcpy(s_lastError, "card assente o non FAT32", sizeof(s_lastError));
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    s_mounted = false;
    strlcpy(s_lastError, "nessuna card rilevata", sizeof(s_lastError));
    return false;
  }
  if (!SD.exists(SD_LOG_DIR)) SD.mkdir(SD_LOG_DIR);

  s_mounted        = true;
  s_lastError[0]   = '\0';
  s_recordsTotal   = nvs_get_u32(NVS_KEY_RECTOT);
  s_currentDay[0]  = '\0';   // forza lo scan/rollover al primo sd_log_sample()
  return true;
}

bool sd_mounted() { return s_mounted; }
const char* sd_last_error() { return s_lastError; }

uint64_t sd_total_mb() { return s_mounted ? (SD.totalBytes() / (1024ULL * 1024ULL)) : 0; }
uint64_t sd_free_mb()  { return s_mounted ? ((SD.totalBytes() - SD.usedBytes()) / (1024ULL * 1024ULL)) : 0; }

uint32_t sd_record_count_total() { return s_recordsTotal; }
uint32_t sd_record_count_today() { return s_recordsToday; }

// ---------------------------------------------------------------------
//  Cambio di giorno: azzera il contatore di oggi e, se il file esiste gia'
//  (riavvio a meta' giornata), lo scansiona UNA volta per riprendere il
//  conteggio giusto — non a ogni richiesta successiva.
// ---------------------------------------------------------------------
static void enterDay(const char* isoDate) {
  strlcpy(s_currentDay, isoDate, sizeof(s_currentDay));
  s_recordsToday = 0;

  char path[32];
  logPath(isoDate, path, sizeof(path));
  if (!SD.exists(path)) return;   // file nuovo: verra' creato con header alla prima scrittura

  File f = SD.open(path, FILE_READ);
  if (!f) return;
  bool firstLine = true;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() == 0) continue;
    if (firstLine) { firstLine = false; continue; }   // intestazione
    s_recordsToday++;
  }
  f.close();
}

bool sd_log_sample(time_t ts, const char* timeSource, float tempC, float humPct) {
  if (!s_mounted) return false;

  char isoDate[11];
  char isoDateTime[20];
  struct tm local;
  localtime_r(&ts, &local);
  strftime(isoDate, sizeof(isoDate), "%Y-%m-%d", &local);
  strftime(isoDateTime, sizeof(isoDateTime), "%Y-%m-%dT%H:%M:%S", &local);

  if (strcmp(isoDate, s_currentDay) != 0) {
    enterDay(isoDate);
    nvs_put_u32(NVS_KEY_RECTOT, s_recordsTotal);   // punto sicuro per un flush extra
    s_writesSinceFlush = 0;
  }

  char path[32];
  logPath(isoDate, path, sizeof(path));
  bool isNew = !SD.exists(path);

  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    strlcpy(s_lastError, "scrittura CSV fallita", sizeof(s_lastError));
    return false;
  }
  if (isNew) f.println(LOG_HEADER);
  const size_t scritti = f.printf("%s,%lu,%s,%.1f,%.1f\n", isoDateTime,
                                  (unsigned long)ts, timeSource, tempC, humPct);
  f.close();

  // Le print() non alzano il writeError del core (File::write si limita a
  // ritornare i byte scritti), quindi l'unico modo di sapere se la riga e'
  // finita davvero sulla card e' guardare quel ritorno. Senza, una card
  // piena o sfilata a caldo produce il guasto peggiore per un logger: la
  // funzione dice "fatto", il contatore sale, e il CSV non cresce.
  if (scritti == 0) {
    strlcpy(s_lastError, "riga non scritta: card piena o assente?", sizeof(s_lastError));
    return false;
  }

  s_recordsToday++;
  s_recordsTotal++;
  if (++s_writesSinceFlush >= REC_TOTAL_FLUSH_EVERY) {
    nvs_put_u32(NVS_KEY_RECTOT, s_recordsTotal);
    s_writesSinceFlush = 0;
  }
  return true;
}

// ---------------------------------------------------------------------
bool sd_delete_day(const char* isoDate) {
  if (!s_mounted || !sd_name_is_safe(isoDate)) return false;

  char path[32];
  logPath(isoDate, path, sizeof(path));
  if (!SD.exists(path)) {
    strlcpy(s_lastError, "file inesistente", sizeof(s_lastError));
    return false;
  }
  if (!SD.remove(path)) {
    strlcpy(s_lastError, "cancellazione file fallita", sizeof(s_lastError));
    return false;
  }

  if (strcmp(isoDate, s_currentDay) == 0) {
    s_recordsToday = 0;   // il file verra' ricreato da zero alla prossima scrittura
  }
  return true;
}

File sd_open_day(const char* isoDate) {
  if (!s_mounted || !sd_name_is_safe(isoDate)) return File();
  char path[32];
  logPath(isoDate, path, sizeof(path));
  return SD.open(path, FILE_READ);
}

// ---------------------------------------------------------------------
//  Dashboard personalizzata su SD
// ---------------------------------------------------------------------
bool sd_dashboard_exists() {
  return s_mounted && SD.exists(WWW_DASHBOARD_PATH);
}

File sd_open_dashboard() {
  if (!s_mounted) return File();
  return SD.open(WWW_DASHBOARD_PATH, FILE_READ);
}

// ---------------------------------------------------------------------
//  Pagine sostituibili in /www (vedi la nota in sd_logger.h)
// ---------------------------------------------------------------------
#define WWW_REGISTRO_PATH WWW_DIR "/caricate.csv"

static bool wwwPath(const char* nome, char* out, size_t cap) {
  // Il nome arriva da una whitelist di web_ui, non dalla rete: qui si
  // controlla comunque che sia fatto di soli caratteri innocui, perche' un
  // giorno potrebbe arrivare da altrove e questa e' l'ultima riga prima di
  // SD.open().
  if (nome == nullptr || *nome == '\0') return false;
  for (const char* p = nome; *p; p++) {
    const char c = *p;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  snprintf(out, cap, WWW_DIR "/%s.html", nome);
  return true;
}

bool sd_www_exists(const char* nome) {
  char path[48];
  if (!s_mounted || !wwwPath(nome, path, sizeof(path))) return false;
  return SD.exists(path);
}

File sd_open_www(const char* nome) {
  char path[48];
  if (!s_mounted || !wwwPath(nome, path, sizeof(path))) return File();
  return SD.open(path, FILE_READ);
}

File sd_open_www_for_write(const char* nome) {
  char path[48];
  if (!s_mounted || !wwwPath(nome, path, sizeof(path))) return File();
  if (!SD.exists(WWW_DIR)) SD.mkdir(WWW_DIR);
  return SD.open(path, FILE_WRITE);
}

bool sd_delete_www(const char* nome) {
  char path[48];
  if (!s_mounted || !wwwPath(nome, path, sizeof(path))) return false;
  if (!SD.exists(path)) return true;   // gia' assente: non e' un errore
  return SD.remove(path);
}

// Il registro e' riscritto per intero ad ogni upload: sono quattro righe, e
// riscriverle e' molto piu' semplice che modificarne una in mezzo a un file.
void sd_www_registra(const char* nome, const char* fw, time_t quando) {
  if (!s_mounted || nome == nullptr) return;

  struct Voce { char nome[20]; char fw[12]; long quando; };
  Voce voci[8];
  int n = 0;

  File in = SD.open(WWW_REGISTRO_PATH, FILE_READ);
  if (in) {
    while (in.available() && n < 8) {
      const String riga = in.readStringUntil('\n');
      const int c1 = riga.indexOf(','), c2 = riga.indexOf(',', c1 + 1);
      if (c1 <= 0 || c2 <= c1) continue;
      const String nm = riga.substring(0, c1);
      if (nm == nome) continue;              // la voce vecchia si sostituisce
      strlcpy(voci[n].nome, nm.c_str(), sizeof(voci[0].nome));
      strlcpy(voci[n].fw, riga.substring(c1 + 1, c2).c_str(), sizeof(voci[0].fw));
      voci[n].quando = riga.substring(c2 + 1).toInt();
      n++;
    }
    in.close();
  }

  if (!SD.exists(WWW_DIR)) SD.mkdir(WWW_DIR);
  File out = SD.open(WWW_REGISTRO_PATH, FILE_WRITE);
  if (!out) return;
  for (int i = 0; i < n; i++) {
    out.printf("%s,%s,%ld\n", voci[i].nome, voci[i].fw, voci[i].quando);
  }
  out.printf("%s,%s,%ld\n", nome, fw ? fw : "?", (long)quando);
  out.close();
}

bool sd_www_info(const char* nome, char* fwOut, size_t fwCap, time_t* quandoOut) {
  if (!s_mounted || nome == nullptr) return false;
  File in = SD.open(WWW_REGISTRO_PATH, FILE_READ);
  if (!in) return false;

  bool trovato = false;
  while (in.available()) {
    const String riga = in.readStringUntil('\n');
    const int c1 = riga.indexOf(','), c2 = riga.indexOf(',', c1 + 1);
    if (c1 <= 0 || c2 <= c1) continue;
    if (riga.substring(0, c1) != nome) continue;
    if (fwOut && fwCap) strlcpy(fwOut, riga.substring(c1 + 1, c2).c_str(), fwCap);
    if (quandoOut) *quandoOut = (time_t)riga.substring(c2 + 1).toInt();
    trovato = true;
    break;
  }
  in.close();
  return trovato;
}

File sd_open_dashboard_for_write() {
  if (!s_mounted) return File();
  if (!SD.exists(WWW_DIR)) SD.mkdir(WWW_DIR);
  return SD.open(WWW_DASHBOARD_PATH, FILE_WRITE);
}

bool sd_delete_dashboard() {
  if (!s_mounted) return false;
  if (!SD.exists(WWW_DASHBOARD_PATH)) return true;   // gia' assente: nessun errore
  return SD.remove(WWW_DASHBOARD_PATH);
}

// ---------------------------------------------------------------------
//  Log dei nodi remoti (vedi la nota in sd_logger.h)
// ---------------------------------------------------------------------
#define NODI_HEADER "ts_iso,ts_unix,fonte_ora,mac,seq,temp_c,hum_pct,press_hpa,batt_mv"

bool sd_node_dir_name(const char* nodeName, char* out, size_t outCap) {
  if (nodeName == nullptr || out == nullptr || outCap < 2) return false;

  size_t n = 0;
  const size_t maxLen = (outCap - 1 < 16) ? (outCap - 1) : 16;
  for (const char* p = nodeName; *p && n < maxLen; p++) {
    const char c = *p;
    // Lista bianca, non lista nera: qualunque altro carattere sparisce. Un
    // nome arriva dalla radio o da una query string, quindi ".." e "/" non
    // devono nemmeno poter esistere in un path composto qui sotto.
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      out[n++] = c;
    }
  }
  out[n] = '\0';
  return n > 0;
}

// Un campo vuoto invece di uno zero quando il valore non c'e': nel grafico
// dev'essere un buco, non una misura che nessuno ha fatto.
static void appendCsvFloat(File& f, float v, int decimali) {
  if (isnan(v) || isinf(v)) return;
  f.print(v, decimali);
}

bool sd_log_remote(const char* nodeName, const char* mac, time_t ts,
                   const char* timeSource, uint32_t seq,
                   const float value[3], uint16_t batteryMv) {
  if (!s_mounted || value == nullptr) return false;

  char dir[20];
  if (!sd_node_dir_name(nodeName, dir, sizeof(dir))) return false;

  char isoDate[11], isoDateTime[20];
  struct tm local;
  localtime_r(&ts, &local);
  strftime(isoDate, sizeof(isoDate), "%Y-%m-%d", &local);
  strftime(isoDateTime, sizeof(isoDateTime), "%Y-%m-%dT%H:%M:%S", &local);

  char path[64];
  snprintf(path, sizeof(path), "%s/%s", SD_NODI_DIR, dir);
  if (!SD.exists(SD_NODI_DIR)) SD.mkdir(SD_NODI_DIR);
  if (!SD.exists(path)) SD.mkdir(path);
  snprintf(path, sizeof(path), "%s/%s/%s.csv", SD_NODI_DIR, dir, isoDate);

  const bool isNew = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    strlcpy(s_lastError, "scrittura CSV nodo fallita", sizeof(s_lastError));
    return false;
  }
  const size_t intestazione = isNew ? f.println(NODI_HEADER) : 1;

  const size_t riga = f.printf("%s,%lu,%s,%s,%lu,", isoDateTime, (unsigned long)ts,
                               timeSource ? timeSource : "", mac ? mac : "", (unsigned long)seq);
  appendCsvFloat(f, value[0], 2); f.print(',');
  appendCsvFloat(f, value[1], 2); f.print(',');
  appendCsvFloat(f, value[2], 2); f.print(',');
  if (batteryMv) f.print(batteryMv);   // 0 = non misurata: campo vuoto
  const size_t chiusa = f.println();
  f.close();

  // Le print() non alzano il writeError del core (File::write si limita a
  // ritornare i byte scritti), quindi l'unico modo di sapere se la riga e'
  // finita davvero sulla card e' guardare quel ritorno. Senza, una card
  // piena o sfilata a caldo produce il guasto peggiore per un logger: la
  // funzione dice "fatto", il contatore sale, e il CSV non cresce.
  //
  // Qui pesa piu' che sul log locale: il CSV dell'hub e' l'UNICO posto dove
  // la lettura di un nodo a batteria esiste, e il nodo ha gia' ridormito.
  if (intestazione == 0 || riga == 0 || chiusa == 0) {
    strlcpy(s_lastError, "riga nodo non scritta: card piena o assente?", sizeof(s_lastError));
    return false;
  }
  return true;
}

int sd_list_remote_days(const char* nodeName, sd_date_cb_t cb, void* arg, int maxItems) {
  if (!s_mounted || !cb) return 0;

  char dir[20];
  if (!sd_node_dir_name(nodeName, dir, sizeof(dir))) return 0;

  char path[48];
  snprintf(path, sizeof(path), "%s/%s", SD_NODI_DIR, dir);

  File d = SD.open(path);
  if (!d) return 0;

  int count = 0;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory()) {
      String name = f.name();
      const int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (name.endsWith(".csv")) {
        cb(name.substring(0, name.length() - 4).c_str(), f.size(), arg);
        count++;
        if (maxItems > 0 && count >= maxItems) { f.close(); break; }
      }
    }
    f.close();
  }
  d.close();
  return count;
}

File sd_open_remote_day(const char* nodeName, const char* isoDate) {
  char dir[20];
  if (!s_mounted || !sd_name_is_safe(isoDate) || !sd_node_dir_name(nodeName, dir, sizeof(dir))) {
    return File();
  }
  char path[64];
  snprintf(path, sizeof(path), "%s/%s/%s.csv", SD_NODI_DIR, dir, isoDate);
  if (!SD.exists(path)) return File();
  return SD.open(path, FILE_READ);
}

// ---------------------------------------------------------------------
//  Elenco giorni disponibili
// ---------------------------------------------------------------------
int sd_list_days(sd_date_cb_t cb, void* arg, int maxItems) {
  if (!s_mounted || !cb) return 0;

  File dir = SD.open(SD_LOG_DIR);
  if (!dir) return 0;

  int count = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory()) {
      String name = f.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (name.endsWith(".csv")) {
        String iso = name.substring(0, name.length() - 4);
        cb(iso.c_str(), f.size(), arg);
        count++;
        if (maxItems > 0 && count >= maxItems) { f.close(); break; }
      }
    }
    f.close();
  }
  dir.close();
  return count;
}

// ---------------------------------------------------------------------
//  Lettura in streaming di un giorno
// ---------------------------------------------------------------------
int sd_read_day(const char* isoDate, sd_row_cb_t cb, void* arg) {
  if (!s_mounted || !cb || !sd_name_is_safe(isoDate)) return 0;

  char path[32];
  logPath(isoDate, path, sizeof(path));
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;

  int  count     = 0;
  bool firstLine = true;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (firstLine) { firstLine = false; continue; }   // intestazione

    // ts_iso,ts_unix,fonte_ora,temp_c,hum_pct
    int c1 = line.indexOf(',');
    int c2 = (c1 >= 0) ? line.indexOf(',', c1 + 1) : -1;
    int c3 = (c2 >= 0) ? line.indexOf(',', c2 + 1) : -1;
    int c4 = (c3 >= 0) ? line.indexOf(',', c3 + 1) : -1;
    if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) continue;

    time_t ts = (time_t)strtoul(line.c_str() + c1 + 1, nullptr, 10);
    float  t  = atof(line.c_str() + c3 + 1);
    float  h  = atof(line.c_str() + c4 + 1);
    cb(ts, t, h, arg);
    count++;
  }
  f.close();
  return count;
}

// ---------------------------------------------------------------------
//  Pagine immagine — /images/<nome>.bin (vedi il contratto in sd_logger.h)
// ---------------------------------------------------------------------

bool sd_img_name_safe(const char* nome, char* out, size_t outCap) {
  if (nome == nullptr || out == nullptr || outCap < 2) return false;

  size_t n = 0;
  const size_t maxLen = (outCap - 1 < IMG_NOME_MAX) ? (outCap - 1) : IMG_NOME_MAX;
  for (const char* p = nome; *p && n < maxLen; p++) {
    const char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      out[n++] = c;
    }
  }
  out[n] = '\0';
  return n > 0;
}

// Compone /images/<nome>.bin da un nome gia' sanificato.
static bool imgPath(const char* nome, char* out, size_t outCap) {
  char safe[IMG_NOME_MAX + 1];
  if (!sd_img_name_safe(nome, safe, sizeof(safe))) return false;
  snprintf(out, outCap, "%s/%s.bin", IMG_DIR, safe);
  return true;
}

// Confronto senza distinguere maiuscole e minuscole: chi cerca "gig" deve
// trovare "GigiFeligi", o la ricerca sembra rotta.
static bool nomeContiene(const char* nome, const char* cerca) {
  if (cerca == nullptr || *cerca == 0) return true;
  const size_t ln = strlen(nome), lc = strlen(cerca);
  if (lc > ln) return false;
  for (size_t i = 0; i + lc <= ln; i++) {
    size_t k = 0;
    while (k < lc && tolower((unsigned char)nome[i + k]) == tolower((unsigned char)cerca[k])) k++;
    if (k == lc) return true;
  }
  return false;
}

int sd_img_page(sd_img_cb_t cb, void* arg, int da, int quante,
                const char* cerca, int* totale) {
  if (totale) *totale = 0;
  if (cb == nullptr || !sd_mounted()) return 0;

  File dir = SD.open(IMG_DIR);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }

  int visti = 0, dati = 0;
  for (;;) {
    File f = dir.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      const char* base = strrchr(f.name(), '/');
      String nome = base ? String(base + 1) : String(f.name());
      if (nome.endsWith(".bin")) {
        nome.remove(nome.length() - 4);
        if (nomeContiene(nome.c_str(), cerca)) {
          // Si continua a scorrere anche dopo aver riempito la pagina: il
          // totale serve tutto, ed e' l'unico modo di dire "12 di 87" invece
          // di fermarsi a un numero che non vuol dire niente.
          if (visti >= da && dati < quante) {
            cb(nome.c_str(), (size_t)f.size(), arg);
            dati++;
          }
          visti++;
        }
      }
    }
    f.close();
  }
  dir.close();
  if (totale) *totale = visti;
  return dati;
}

bool sd_log_refresh(const char* motivo, bool completo, uint32_t ms) {
  if (!sd_mounted()) return false;

  // Un file al mese: cosi' resta leggibile e si puo' cancellare un mese
  // vecchio senza toccare il resto.
  char nomeFile[40];
  time_t ora = time(nullptr);
  struct tm tmv;
  localtime_r(&ora, &tmv);
  snprintf(nomeFile, sizeof(nomeFile), "/epd/%04d-%02d.csv",
           tmv.tm_year + 1900, tmv.tm_mon + 1);

  if (!SD.exists("/epd")) SD.mkdir("/epd");

  const bool nuovo = !SD.exists(nomeFile);
  File f = SD.open(nomeFile, FILE_APPEND);
  if (!f) return false;

  size_t scritti = 0;
  if (nuovo) scritti += f.println("ts_iso,motivo,tipo,ms");

  char ts[24] = "";
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tmv);
  scritti += f.print(ts);
  scritti += f.print(',');
  scritti += f.print(motivo);
  scritti += f.print(completo ? ",completo," : ",parziale,");
  scritti += f.println(ms);
  f.close();

  // File::write() non alza il writeError: su una card piena o sfilata la
  // print() torna 0 e basta. Senza questo controllo il contatore salirebbe
  // mentre il file non cresce -- e' la stessa trappola gia' corretta in
  // sd_log_sample().
  return scritti > 0;
}

bool sd_img_mini(const char* nome, uint8_t* out) {
  if (out == nullptr) return false;
  File f = sd_img_open(nome);
  if (!f) return false;
  if (f.size() != (size_t)((IMG_W_PX / 8) * IMG_H_PX)) { f.close(); return false; }

  memset(out, 0, MINI_BYTES);

  // Si legge una BANDA di 5 righe per volta (250 byte) invece di tutta
  // l'immagine: 15 kB in RAM ci starebbero, ma non c'e' motivo di prenderli
  // per un lavoro che scorre in avanti e non torna mai indietro.
  uint8_t banda[5 * (IMG_W_PX / 8)];
  for (int my = 0; my < MINI_H; my++) {
    if (f.read(banda, sizeof(banda)) != (int)sizeof(banda)) { f.close(); return false; }
    for (int mx = 0; mx < MINI_W; mx++) {
      int neri = 0;
      for (int dy = 0; dy < 5; dy++) {
        const uint8_t* riga = banda + dy * (IMG_W_PX / 8);
        for (int dx = 0; dx < 5; dx++) {
          const int px = mx * 5 + dx;
          // bit 1 = bianco, come in tutta la catena: il nero e' lo zero.
          if (((riga[px >> 3] >> (7 - (px & 7))) & 1) == 0) neri++;
        }
      }
      // Maggioranza dei 25: sotto soglia il blocco resta bianco. Con il
      // minimo (>=1 nero) una foto ditherata diventerebbe una macchia nera,
      // perche' in un dithering il nero e' sparso ovunque.
      if (neri < 13) out[my * MINI_STRIDE + (mx >> 3)] |= (uint8_t)(0x80 >> (mx & 7));
    }
  }
  f.close();
  return true;
}

int sd_img_list(sd_img_cb_t cb, void* arg, int maxItems) {
  if (cb == nullptr || !sd_mounted()) return 0;

  File dir = SD.open(IMG_DIR);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }

  int n = 0;
  while (n < maxItems) {
    File f = dir.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      // Il nome sulla card e' "<nome>.bin": si consegna senza estensione,
      // che e' la forma con cui il resto del firmware lo maneggia (parametro
      // della pagina, query string).
      const char* base = strrchr(f.name(), '/');
      String nome = base ? String(base + 1) : String(f.name());
      if (nome.endsWith(".bin")) {
        nome.remove(nome.length() - 4);
        cb(nome.c_str(), (size_t)f.size(), arg);
        n++;
      }
    }
    f.close();
  }
  dir.close();
  return n;
}

bool sd_log_evento(const char* tipo, const char* dettaglio) {
  if (!sd_mounted() || tipo == nullptr) return false;

  char nomeFile[40];
  time_t ora = time(nullptr);
  struct tm tmv;
  localtime_r(&ora, &tmv);
  snprintf(nomeFile, sizeof(nomeFile), "/eventi/%04d-%02d.csv",
           tmv.tm_year + 1900, tmv.tm_mon + 1);

  if (!SD.exists("/eventi")) SD.mkdir("/eventi");

  const bool nuovo = !SD.exists(nomeFile);
  File f = SD.open(nomeFile, FILE_APPEND);
  if (!f) return false;

  size_t scritti = 0;
  if (nuovo) scritti += f.println("ts_iso,tipo,dettaglio");

  char ts[24] = "";
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tmv);
  scritti += f.print(ts);
  scritti += f.print(',');
  scritti += f.print(tipo);
  scritti += f.print(',');
  // Il dettaglio e' testo libero e finisce in un CSV: le virgole e gli a capo
  // spezzerebbero la riga. Si sostituiscono invece di virgolettare, perche'
  // questo file lo legge anche un occhio umano da `type` o `cat`.
  if (dettaglio) {
    for (const char* c = dettaglio; *c; c++) {
      const char ch = (*c == ',' || *c == '\n' || *c == '\r') ? ' ' : *c;
      scritti += f.print(ch);
    }
  }
  scritti += f.println();
  f.close();

  // Come in sd_log_refresh(): File::write() non alza il writeError, quindi su
  // una card piena la print() torna 0 e basta. Senza questo controllo il
  // diario direbbe di aver scritto righe che non esistono.
  return scritti > 0;
}

File sd_open_eventi(const char* mese) {
  if (!sd_mounted() || mese == nullptr) return File();

  // "AAAA-MM" e nient'altro: il nome arriva dalla rete e diventa un pezzo di
  // path. Si valida invece di comporre a fiducia.
  if (strlen(mese) != 7 || mese[4] != '-') return File();
  for (int i = 0; i < 7; i++) {
    if (i == 4) continue;
    if (mese[i] < '0' || mese[i] > '9') return File();
  }

  char nomeFile[40];
  snprintf(nomeFile, sizeof(nomeFile), "/eventi/%s.csv", mese);
  if (!SD.exists(nomeFile)) return File();
  return SD.open(nomeFile, FILE_READ);
}

bool sd_img_exists(const char* nome) {
  if (!sd_mounted()) return false;
  char path[64];
  if (!imgPath(nome, path, sizeof(path))) return false;
  return SD.exists(path);
}

File sd_img_open(const char* nome) {
  if (!sd_mounted()) return File();
  char path[64];
  if (!imgPath(nome, path, sizeof(path))) return File();
  if (!SD.exists(path)) return File();
  return SD.open(path, FILE_READ);
}

File sd_img_open_for_write(const char* nome) {
  if (!sd_mounted()) return File();
  char path[64];
  if (!imgPath(nome, path, sizeof(path))) return File();
  if (!SD.exists(IMG_DIR)) SD.mkdir(IMG_DIR);
  return SD.open(path, FILE_WRITE);   // tronca un eventuale file preesistente
}

bool sd_img_delete(const char* nome) {
  if (!sd_mounted()) return false;
  char path[64];
  if (!imgPath(nome, path, sizeof(path))) return false;
  if (!SD.exists(path)) return false;
  return SD.remove(path);
}
