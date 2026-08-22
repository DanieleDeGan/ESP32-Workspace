/*
 * Timelapse_XIAO — camera timelapse con galleria web (Seeed XIAO ESP32-S3 Sense)
 * ---------------------------------------------------------------------------
 * La catena e':
 *
 *   timer -> scatto -> JPEG su microSD in /timelapse/<giorno>/<ora>.JPG
 *
 * piu' una web UI (http://<OTA_HOSTNAME>.local/) per inquadrare la camera
 * (video live), cambiare intervallo e finestra oraria, sfogliare l'archivio
 * giorno per giorno, riprodurre la sequenza come un filmato, scaricare e
 * cancellare, e l'OTA per aggiornare il firmware senza smontare niente.
 *
 * NATO DA starters/XIAO_S3_Camera/, di cui riusa camera.* e net_ota.*. Le
 * differenze che contano:
 *   - niente PIR e niente ESP-NOW: qui scatta un timer, non un sensore, e
 *     non c'e' nessun hub da avvisare. Quindi questo progetto NON dipende da
 *     libraries/ e si puo' spostare ovunque cosi' com'e';
 *   - le foto sono organizzate per giorno e il nome e' l'ora dello scatto
 *     (storage.*), non un progressivo: un timelapse si guarda a giornate;
 *   - serve l'orario vero, quindi c'e' rtc_time.* (NTP), preso da
 *     projects/EnvNode_C3/.
 *
 * COS'E' QUESTO FILE: la logica applicativa (timer, scatto, gestione dello
 * spazio, impostazioni). Il resto e' diviso per compito e di norma non si
 * tocca:
 *   camera.*    sensore OV2640/OV3660 (pin cablati sulla scheda Sense)
 *   storage.*   microSD SPI: cartelle per giorno + CSV giornaliero
 *   rtc_time.*  orario: stima da build-time, poi NTP quando c'e' rete
 *   net_ota.*   WiFi (con watchdog) + ArduinoOTA + /update
 *   web_ui.*    pagina di controllo, galleria e API HTTP
 *
 * COLLOCAZIONE: la scheda va alimentata via USB e sta accesa per giorni;
 * niente PIR, niente 5V, nessun cablaggio oltre l'alimentazione. Se ti
 * serve un pin per un'aggiunta, i liberi sono D1..D5 (GPIO 2,3,4,5,6):
 * D8/D9/D10 e GPIO21 sono la microSD.
 *
 * IMPOSTAZIONI Arduino IDE (Tools):
 *   Board:            XIAO_ESP32S3        (non "ESP32S3 Dev Module")
 *   PSRAM:            OPI PSRAM           <- OBBLIGATORIO per la camera
 *   Partition Scheme: Default 8MB with spiffs (3MB APP/1.5MB SPIFFS)
 *                     -> ha le partizioni OTA. Mai "Maximum APP (No OTA)".
 *   USB CDC On Boot:  Enabled  (e' gia' il default di questa board, al
 *                     contrario delle altre schede del repo)
 *
 * PRIMA DI COMPILARE: copia secrets.h.example in secrets.h e riempilo.
 */

#include <Preferences.h>

#include "camera.h"
#include "storage.h"
#include "rtc_time.h"
#include "net_ota.h"
#include "web_ui.h"
#include "secrets.h"

// ============================ Configurazione ============================

// Versione firmware: cambiala ad ogni build, cosi' dalla web UI riconosci
// quale firmware sta girando dopo un aggiornamento OTA.
// Da incrementare a ogni firmware caricato: la web UI lo mostra, ed e' l'unico
// modo per sapere da remoto quale versione sta davvero girando.
//   v2  2026-08-22  Serial.setTxTimeoutMs(0), vedi la nota in setup()
static const char* FW_VERSION = "v2";

// Nome mostrato nella web UI.
#define NODE_NAME "Timelapse"

// Fuso orario in formato POSIX TZ (Italia, con cambio ora legale). Da
// cambiare se la scheda va altrove: qui e' una costante di compilazione,
// non un'impostazione da web (a differenza di EnvNode_C3, dove il nodo puo'
// spostarsi).
#define TZ_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"

// Valori di partenza, poi modificabili dalla web UI e ricordati in NVS.
#define INTERVAL_S_DEFAULT   60      // uno scatto al minuto
#define WINDOW_START_DEFAULT  0      // inizio == fine -> scatta tutto il giorno
#define WINDOW_END_DEFAULT    0
#define MIN_FREE_MB_DEFAULT 200      // sotto questa soglia la card e' "piena"

// Intervallo minimo: sotto i 5 s lo scatto + la scrittura su SD non stanno
// dentro un ciclo e il timer accumulerebbe ritardo ad ogni giro.
#define INTERVAL_S_MIN 5
#define INTERVAL_S_MAX 86400

#define NVS_KEY_ENABLED  "attivo"
#define NVS_KEY_INTERVAL "intervallo"
#define NVS_KEY_WIN_FROM "ora_inizio"
#define NVS_KEY_WIN_TO   "ora_fine"
#define NVS_KEY_POLICY   "politica"
#define NVS_KEY_MINFREE  "min_liberi"

// Se un aggiornamento si interrompe a meta' (rete caduta, browser chiuso)
// non arriva nessun evento di errore utile: senza questa scadenza il nodo
// resterebbe "in aggiornamento" e smetterebbe di scattare fino al reset.
#define OTA_STALL_MS 30000UL

// Ogni quanto ritentare il mount se la card non c'e' (o e' stata sfilata).
#define SD_RETRY_MS 30000UL

// Un orario prima di questo (2020-01-01) vuol dire che l'orologio non e'
// mai stato seminato: meglio non creare cartelle datate 1970.
#define EPOCH_PLAUSIBLE 1577836800L

// ============================ Stato ============================

static bool     s_enabled    = true;
static uint32_t s_intervalS  = INTERVAL_S_DEFAULT;
static int      s_winStart   = WINDOW_START_DEFAULT;
static int      s_winEnd     = WINDOW_END_DEFAULT;
static int      s_policy     = APP_FULL_STOP;
static uint32_t s_minFreeMb  = MIN_FREE_MB_DEFAULT;

static time_t   s_nextEpoch  = 0;    // istante del prossimo scatto (0 = da calcolare)
static uint32_t s_shots      = 0;    // scatti riusciti da questa accensione
static uint32_t s_skipped    = 0;    // slot saltati (streaming, card piena, errori)
static char     s_lastShot[24] = ""; // ISO dell'ultimo scatto riuscito
static char     s_lastError[64] = "";

static bool     s_otaActive  = false;
static uint32_t s_otaLastMs  = 0;
static uint32_t s_lastSdTryMs   = 0;
static uint32_t s_lastStatusMs  = 0;

// ---------------------------------------------------------------------
//  Impostazioni persistenti (stesso namespace NVS dei contatori di storage)
// ---------------------------------------------------------------------
static void settings_load() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return;
  s_enabled   = prefs.getBool(NVS_KEY_ENABLED, true);
  s_intervalS = prefs.getUInt(NVS_KEY_INTERVAL, INTERVAL_S_DEFAULT);
  s_winStart  = prefs.getInt (NVS_KEY_WIN_FROM, WINDOW_START_DEFAULT);
  s_winEnd    = prefs.getInt (NVS_KEY_WIN_TO,   WINDOW_END_DEFAULT);
  s_policy    = prefs.getInt (NVS_KEY_POLICY,   APP_FULL_STOP);
  s_minFreeMb = prefs.getUInt(NVS_KEY_MINFREE,  MIN_FREE_MB_DEFAULT);
  prefs.end();
}

static void settings_save() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return;
  prefs.putBool(NVS_KEY_ENABLED,  s_enabled);
  prefs.putUInt(NVS_KEY_INTERVAL, s_intervalS);
  prefs.putInt (NVS_KEY_WIN_FROM, s_winStart);
  prefs.putInt (NVS_KEY_WIN_TO,   s_winEnd);
  prefs.putInt (NVS_KEY_POLICY,   s_policy);
  prefs.putUInt(NVS_KEY_MINFREE,  s_minFreeMb);
  prefs.end();
}

// ---------------------------------------------------------------------
//  Programmazione degli scatti
//
//  Il prossimo istante e' un multiplo dell'intervallo contato sull'epoch,
//  non "adesso + intervallo": cosi' con intervallo 60 gli scatti cadono al
//  secondo :00 di ogni minuto e i nomi dei file restano leggibili. Il conto
//  si rifa' ad ogni scatto, quindi il primo sync NTP (che sposta l'orologio
//  anche di ore) o uno stream lungo non lasciano indietro una coda di
//  scatti arretrati da recuperare tutti insieme.
// ---------------------------------------------------------------------
static time_t nextSlot(time_t now) {
  const time_t step = (time_t)s_intervalS;
  return ((now / step) + 1) * step;
}

// Siamo dentro la finestra oraria? inizio == fine -> sempre.
static bool inWindow(time_t when) {
  if (s_winStart == s_winEnd) return true;
  struct tm lt;
  localtime_r(&when, &lt);
  const int h = lt.tm_hour;
  if (s_winStart < s_winEnd) return (h >= s_winStart && h < s_winEnd);
  return (h >= s_winStart || h < s_winEnd);   // finestra a cavallo della mezzanotte
}

// ---------------------------------------------------------------------
//  Spazio sulla card: true se si puo' scattare.
//
//  Con la politica "buffer circolare" si elimina il giorno piu' vecchio —
//  mai quello in corso, o un timelapse lasciato acceso con una card piccola
//  finirebbe a cancellare le foto di un'ora fa.
// ---------------------------------------------------------------------
static bool ensureSpace() {
  if (!sd_mounted()) return false;
  if (sd_free_mb() >= s_minFreeMb) return true;

  if (s_policy != APP_FULL_RING) {
    snprintf(s_lastError, sizeof(s_lastError), "card quasi piena (%llu MB liberi)", sd_free_mb());
    return false;
  }

  // Il giorno "in corso" si ricava dall'orologio, NON da sd_today_dir(): quello
  // e' vuoto finche' non si e' salvato qualcosa dopo il boot, e al primo scatto
  // dopo un riavvio con la card piena si finirebbe a cancellare proprio le foto
  // di oggi.
  char today[SD_DAY_LEN + 1];
  struct tm now_tm;
  rtctime_nowLocal(&now_tm);
  strftime(today, sizeof(today), "%Y-%m-%d", &now_tm);

  char oldest[SD_DAY_LEN + 1];
  if (!sd_oldest_day(oldest, sizeof(oldest)) || strcmp(oldest, today) >= 0) {
    snprintf(s_lastError, sizeof(s_lastError), "card piena: solo il giorno in corso da eliminare");
    return false;
  }

  Serial.printf("[SD] spazio finito: elimino il giorno %s\n", oldest);
  if (!sd_delete_day(oldest)) {
    snprintf(s_lastError, sizeof(s_lastError), "eliminazione di %s fallita", oldest);
    return false;
  }
  return sd_free_mb() >= s_minFreeMb;
}

// ---------------------------------------------------------------------
//  Scatto: camera -> microSD -> riga nel CSV del giorno
// ---------------------------------------------------------------------
static bool do_capture(const char* sorgente, char* day_out, size_t day_cap,
                       char* name_out, size_t name_cap) {
  if (day_out  && day_cap)  day_out[0]  = '\0';
  if (name_out && name_cap) name_out[0] = '\0';

  struct tm when;
  rtctime_nowLocal(&when);

  camera_fb_t* fb = camera_grab_fresh();
  if (!fb) {
    snprintf(s_lastError, sizeof(s_lastError), "cattura fallita (camera non pronta?)");
    sd_log_shot(&when, rtctime_source(), sorgente, "", 0, s_lastError);
    s_skipped++;
    return false;
  }

  const size_t len = fb->len;
  char day[SD_DAY_LEN + 1] = "";
  char name[24] = "";
  const bool saved = sd_save_photo(fb->buf, len, &when, day, sizeof(day), name, sizeof(name));
  camera_release(fb);   // restituire SEMPRE il frame, anche se il salvataggio fallisce

  if (!saved) {
    snprintf(s_lastError, sizeof(s_lastError), "%s", sd_last_error());
    sd_log_shot(&when, rtctime_source(), sorgente, "", len, s_lastError);
    s_skipped++;
    Serial.printf("[APP] foto non salvata: %s\n", s_lastError);
    return false;
  }

  s_shots++;
  s_lastError[0] = '\0';
  strftime(s_lastShot, sizeof(s_lastShot), "%Y-%m-%d %H:%M:%S", &when);
  sd_log_shot(&when, rtctime_source(), sorgente, name, len, "ok");

  if (day_out  && day_cap)  strlcpy(day_out,  day,  day_cap);
  if (name_out && name_cap) strlcpy(name_out, name, name_cap);

  Serial.printf("[APP] scatto %s: %s/%s, %u byte (%lu oggi)\n",
                sorgente, day, name, (unsigned)len, (unsigned long)sd_photos_today());
  return true;
}

// ---------------------------------------------------------------------
//  Il timer del timelapse. Da chiamare spesso da loop().
// ---------------------------------------------------------------------
static void timelapse_tick() {
  if (!s_enabled) return;

  const time_t now = rtctime_now();
  if (now < EPOCH_PLAUSIBLE) return;   // orologio non ancora seminato

  // Primo giro, oppure l'orologio e' saltato (sync NTP) / l'intervallo e'
  // cambiato: si riprogramma sul prossimo slot buono.
  if (s_nextEpoch == 0 || s_nextEpoch > now + (time_t)s_intervalS) {
    s_nextEpoch = nextSlot(now);
    return;
  }
  if (now < s_nextEpoch) return;

  s_nextEpoch = nextSlot(now);   // sempre PRIMA dello scatto: se qualcosa va
                                 // storto si riparte comunque dallo slot giusto

  if (!inWindow(now)) return;    // fuori dalla finestra oraria: niente scatto
  if (!ensureSpace()) { s_skipped++; return; }

  do_capture("AUTO", nullptr, 0, nullptr, 0);
}

// ---------------------------------------------------------------------
//  Feedback OTA (nessun display su questa scheda: va tutto su Serial)
// ---------------------------------------------------------------------
static void on_ota_progress(int percent, const char* what) {
  s_otaActive = true;
  s_otaLastMs = millis();
  if (percent >= 0) Serial.printf("[OTA] %s %d%%\n", what, percent);
  else              Serial.printf("[OTA] %s\n", what);
}

// ============================ Ganci per la web UI ============================
// (dichiarati in web_ui.h: la UI non sa niente di NVS ne' del timer.)

const char* app_node_name()  { return NODE_NAME; }
const char* app_fw_version() { return FW_VERSION; }

bool app_enabled() { return s_enabled; }

void app_set_enabled(bool on) {
  if (on == s_enabled) return;
  s_enabled   = on;
  s_nextEpoch = 0;            // riparte dal prossimo slot, non da quello vecchio
  settings_save();
  Serial.printf("[APP] scatti automatici %s\n", on ? "attivi" : "fermi");
}

uint32_t app_interval_s() { return s_intervalS; }

void app_set_interval_s(uint32_t seconds) {
  if (seconds < INTERVAL_S_MIN || seconds > INTERVAL_S_MAX || seconds == s_intervalS) return;
  s_intervalS = seconds;
  s_nextEpoch = 0;            // il vecchio slot non e' piu' un multiplo valido
  settings_save();
}

int app_window_start() { return s_winStart; }
int app_window_end()   { return s_winEnd; }

void app_set_window(int startHour, int endHour) {
  if (startHour < 0 || startHour > 23 || endHour < 0 || endHour > 23) return;
  if (startHour == s_winStart && endHour == s_winEnd) return;
  s_winStart = startHour;
  s_winEnd   = endHour;
  settings_save();
}

bool app_in_window() { return inWindow(rtctime_now()); }

int  app_full_policy() { return s_policy; }

void app_set_full_policy(int policy) {
  if ((policy != APP_FULL_STOP && policy != APP_FULL_RING) || policy == s_policy) return;
  s_policy = policy;
  settings_save();
}

uint32_t app_min_free_mb() { return s_minFreeMb; }

void app_set_min_free_mb(uint32_t mb) {
  if (mb < 10 || mb > 60000 || mb == s_minFreeMb) return;
  s_minFreeMb = mb;
  settings_save();
}

long app_next_shot_s() {
  if (!s_enabled || s_nextEpoch == 0) return -1;
  const time_t now = rtctime_now();
  if (!inWindow(s_nextEpoch)) return -1;      // cadrebbe fuori orario: non e' uno scatto
  return (s_nextEpoch > now) ? (long)(s_nextEpoch - now) : 0;
}

uint32_t    app_shots_session() { return s_shots; }
const char* app_last_shot_iso() { return s_lastShot; }
const char* app_last_error()    { return s_lastError; }

bool app_capture_now(char* day_out, size_t day_cap, char* name_out, size_t name_cap) {
  if (!sd_mounted()) sd_begin();
  return do_capture("WEB", day_out, day_cap, name_out, name_cap);
}

// Chiamata dal ciclo dello stream MJPEG, che tiene fermo il web server.
// Qui NON si chiama net_loop() (rientrare in handleClient() mentre si serve
// una richiesta rompe il web server) e NON si scatta: la camera sta gia'
// dando frame allo stream e una scrittura su SD lo bloccherebbe. Gli slot
// che cadono durante lo stream si saltano, e si riparte dal successivo.
void app_pump() {
  if (!s_enabled || s_nextEpoch == 0) return;
  const time_t now = rtctime_now();
  if (now >= s_nextEpoch) {
    s_nextEpoch = nextSlot(now);
    s_skipped++;
  }
}

// ============================ setup / loop ============================

void setup() {
  Serial.begin(115200);

  // Scritture su Serial mai bloccanti. Su questa board la Serial dell'USB non
  // e' una UART ma la CDC del chip: se il PC ha riconosciuto la porta e nessuno
  // la sta leggendo, il buffer si riempie e ogni print() aspetta un timeout
  // interno. Finche' aspetta, loop() e' fermo - e con lui net_loop() e il timer
  // degli scatti. Qui pesa piu' che altrove: c'e' una riga di stato ogni 30 s
  // PIU' una riga per ogni foto, e un timelapse e' fatto apposta per restare
  // acceso settimane. Perdere qualche riga di log quando nessuno la legge non
  // costa niente; perdere gli scatti si'.
  //
  // Il guardia serve perche' con "USB CDC On Boot: Disabled" la Serial torna
  // a essere una UART, che questo metodo non ce l'ha e non ne ha bisogno.
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(300);   // il CDC USB impiega un attimo: senza, si perde il banner

  Serial.println();
  Serial.printf("=== Timelapse_XIAO — \"%s\" %s ===\n", NODE_NAME, FW_VERSION);
  Serial.printf("PSRAM: %s   flash: %lu MB\n",
                psramFound() ? "presente" : "ASSENTE (compila con PSRAM=opi!)",
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)));

  settings_load();

  // Orario PRIMA del WiFi: senza la stima da build-time il primo scatto
  // finirebbe in una cartella datata 1970 (vedi rtc_time.h).
  rtctime_begin(TZ_POSIX);
  rtctime_seedFromBuild();

  camera_begin();   // se fallisce si va avanti: la web UI lo segnala
  sd_begin();       // idem: senza card il nodo resta raggiungibile e aggiornabile

  net_setOtaProgressCb(on_ota_progress);
  net_begin();      // WiFi + ArduinoOTA + /update
  web_ui_begin();   // rotte della camera e dell'archivio sullo stesso server

  Serial.printf("Scatti %s ogni %lu s, finestra %02d:00-%02d:00%s, card piena -> %s\n",
                s_enabled ? "attivi" : "fermi", (unsigned long)s_intervalS,
                s_winStart, s_winEnd, (s_winStart == s_winEnd) ? " (tutto il giorno)" : "",
                (s_policy == APP_FULL_RING) ? "elimina il giorno piu' vecchio" : "ferma");
}

void loop() {
  net_loop();   // watchdog WiFi + ArduinoOTA + richieste web (anche lo stream)

  if (s_otaActive) {
    if (millis() - s_otaLastMs < OTA_STALL_MS) return;   // update in corso: niente altro
    s_otaActive = false;
    Serial.println("[OTA] nessun avanzamento: aggiornamento abbandonato, riprendo");
  }

  // Il sync NTP va rilanciato ad ogni riconnessione, non solo alla prima.
  if (net_takeReconnectedFlag()) rtctime_onWifiConnected();

  // Card sfilata/rimessa o inserita dopo l'accensione: si ritenta il mount
  // ogni tanto, invece di restare senza archivio fino al riavvio.
  if (!sd_mounted() && millis() - s_lastSdTryMs > SD_RETRY_MS) {
    s_lastSdTryMs = millis();
    sd_begin();
  }

  timelapse_tick();

  // Riga di stato periodica su Serial (utile col cavo attaccato in banco).
  if (millis() - s_lastStatusMs > 30000) {
    s_lastStatusMs = millis();
    char ora[24];
    rtctime_format(rtctime_now(), "%H:%M:%S", ora, sizeof(ora));
    Serial.printf("[stato] %s (%s)  wifi %s  sd %s  scatti %lu (saltati %lu)  prossimo %ld s  heap %lu\n",
                  ora, rtctime_source(),
                  net_isConnected() ? net_ip().c_str() : "no",
                  sd_mounted() ? "ok" : sd_last_error(),
                  (unsigned long)s_shots, (unsigned long)s_skipped,
                  app_next_shot_s(), (unsigned long)ESP.getFreeHeap());
  }

  // <-- Nuove periferiche/logica: qui, senza bloccare a lungo (net_loop()
  //     deve girare spesso, altrimenti OTA e web UI diventano lentissimi).
}
