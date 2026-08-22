/*
 * XIAO_S3_Camera — starter nodo camera (Seeed XIAO ESP32-S3 Sense)
 * ---------------------------------------------------------------------------
 * Sorveglianza a movimento con notifica all'hub:
 *
 *   PIR HC-SR501  ->  scatto  ->  JPEG su microSD  ->  DATA ESP-NOW all'hub
 *
 * piu' una web UI (http://<OTA_HOSTNAME>.local/) per guardare il video live,
 * scattare a mano, rivedere/scaricare le foto e cambiare le impostazioni, e
 * l'aggiornamento OTA come sullo starter C3 (un nodo montato dove non ci si
 * arriva col cavo si aggiorna solo via rete).
 *
 * COS'E' QUESTO FILE: la logica applicativa. Il resto e' diviso per compito e
 * di norma non si tocca:
 *   camera.*    sensore OV2640/OV3660 (pin cablati sulla scheda Sense)
 *   storage.*   microSD SPI della Sense: foto + CSV degli eventi
 *   net_ota.*   WiFi + ArduinoOTA + /update
 *   web_ui.*    pagina di controllo e API HTTP
 *   hub_link.*  ESP-NOW verso l'hub, sopra la libreria EspNowLink
 *
 * DIPENDE DA libraries/ (EspNowLink): se sposti questa cartella fuori dal
 * repo, portati dietro anche libraries/EspNowLink (o una junction). A
 * differenza di starters/C3_OLED_OTA/, questo starter NON e' self-contained:
 * usa il protocollo condiviso dell'hub apposta, per non duplicarlo.
 *
 * CABLAGGIO del PIR HC-SR501:
 *   VCC  -> pin 5V della XIAO   (il modulo vuole 5V; l'uscita e' 3,3V, sicura)
 *   GND  -> GND
 *   OUT  -> D0 (GPIO1)          <- PIN_PIR qui sotto
 * Sul modulo: trimmer SENS (portata) e TIME (durata dell'impulso, tienilo al
 * minimo) e il ponticello H/L su H (retriggerabile). Dopo l'accensione il
 * sensore mente per una decina di secondi: PIR_WARMUP_MS lo ignora.
 * Gli altri GPIO liberi della XIAO Sense sono D1..D5 (2,3,4,5,6): D8/D9/D10
 * (7/8/9) sono la SPI della microSD, GPIO21 il suo chip select.
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
#include "net_ota.h"
#include "web_ui.h"
#include "hub_link.h"
#include "secrets.h"

// ============================ Configurazione ============================

// Versione firmware: cambiala ad ogni build, cosi' dalla web UI riconosci
// quale firmware sta girando dopo un aggiornamento OTA.
// Da incrementare a ogni firmware caricato: la web UI lo mostra, ed e' l'unico
// modo per sapere da remoto quale versione sta davvero girando.
//   v2  2026-08-22  Serial.setTxTimeoutMs(0), vedi la nota in setup()
static const char* FW_VERSION = "v2";

// Nome con cui il nodo si presenta all'hub (max 15 caratteri: LINK_NAME_LEN).
#define NODE_NAME "CamGavone"

// Ingresso del PIR. Vedi il cablaggio in testa al file.
static constexpr int PIN_PIR = D0;   // GPIO1

// Il HC-SR501 spara falsi positivi finche' non si stabilizza: dopo
// l'accensione i rilevamenti si ignorano per questo tempo.
#define PIR_WARMUP_MS 60000UL

// Pausa minima tra due scatti da movimento: senza, una persona che si muove
// riempie la microSD in pochi minuti. Modificabile dalla web UI.
#define COOLDOWN_S_DEFAULT 30

#define NVS_KEY_ARMED    "armato"
#define NVS_KEY_COOLDOWN "cooldown"

// ============================ Stato ============================

static volatile bool s_pirEvent = false;   // scritto dalla ISR, letto da loop()

static bool     s_armed        = true;
static uint32_t s_cooldownS    = COOLDOWN_S_DEFAULT;
static uint32_t s_motionCount  = 0;
static uint32_t s_lastMotionMs = 0;        // 0 = nessun rilevamento da quando e' accesa
static bool     s_otaActive    = false;
static uint32_t s_otaLastMs    = 0;   // ultimo segno di vita di un update in corso
static uint32_t s_lastStatusMs = 0;

// Se un aggiornamento si interrompe a meta' (rete caduta, browser chiuso) non
// arriva nessun evento di errore utile: senza questa scadenza il nodo
// resterebbe "in aggiornamento" e smetterebbe di sorvegliare fino al reset.
#define OTA_STALL_MS 30000UL

// ---------------------------------------------------------------------
//  ISR del PIR: il piu' corta possibile. Il timestamp lo prende loop(),
//  cosi' qui dentro non serve nemmeno millis().
// ---------------------------------------------------------------------
static void IRAM_ATTR pir_isr() {
  s_pirEvent = true;
}

// ---------------------------------------------------------------------
//  Impostazioni persistenti (stesso namespace NVS dei contatori di storage)
// ---------------------------------------------------------------------
static void settings_load() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return;
  s_armed     = prefs.getBool(NVS_KEY_ARMED, true);
  s_cooldownS = prefs.getUInt(NVS_KEY_COOLDOWN, COOLDOWN_S_DEFAULT);
  prefs.end();
}

static void settings_save() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return;
  prefs.putBool(NVS_KEY_ARMED, s_armed);
  prefs.putUInt(NVS_KEY_COOLDOWN, s_cooldownS);
  prefs.end();
}

// ---------------------------------------------------------------------
//  Scatto: camera -> microSD -> (opzionale) notifica all'hub -> riga CSV
//
//  Il numero della foto ricavato dal nome e' quello che finisce nel
//  messaggio all'hub: con quello, dalla web UI si apre esattamente lo
//  scatto di quell'allarme.
// ---------------------------------------------------------------------
static bool do_capture(const char* sorgente, bool notify_hub, char* name_out, size_t name_cap) {
  if (name_out && name_cap) name_out[0] = '\0';

  camera_fb_t* fb = camera_grab_fresh();
  if (!fb) {
    Serial.println("[APP] cattura fallita (camera non pronta?)");
    sd_log_event(sorgente, "", 0, false);
    return false;
  }

  const size_t len = fb->len;
  char name[40] = {0};
  const bool saved = sd_save_photo(fb->buf, len, name, sizeof(name));
  camera_release(fb);   // restituire SEMPRE il frame, anche se il salvataggio fallisce

  if (!saved) {
    Serial.printf("[APP] foto non salvata: %s\n", sd_last_error());
  }

  bool notified = false;
  if (notify_hub) {
    // Bloccante (conferma + ritentativi): per questo si fa qui in loop() e
    // non dentro la ISR o una callback radio.
    const long idx = saved ? atol(name + 4) : -1;   // "IMG_00042.JPG" -> 42
    notified = hub_notify_motion(s_motionCount, idx, saved);
    if (!notified) Serial.println("[APP] hub non raggiunto (non associato o nessun ACK)");
  }

  sd_log_event(sorgente, name, len, notified);
  if (name_out && name_cap) snprintf(name_out, name_cap, "%s", name);

  Serial.printf("[APP] scatto %s: %u byte, file %s, hub %s\n",
                sorgente, (unsigned)len, saved ? name : "-", notified ? "avvisato" : "no");
  return saved;
}

// ---------------------------------------------------------------------
//  Movimento rilevato dal PIR
// ---------------------------------------------------------------------
static void handle_motion() {
  if (!s_pirEvent) return;
  s_pirEvent = false;

  if (!app_pir_warm()) return;   // riscaldamento: il sensore non e' credibile
  if (!s_armed) return;

  const uint32_t now = millis();
  if (s_lastMotionMs != 0 && (now - s_lastMotionMs) < s_cooldownS * 1000UL) {
    return;   // ancora nella pausa dallo scatto precedente
  }
  s_lastMotionMs = now;
  s_motionCount++;

  Serial.printf("[PIR] movimento #%lu\n", (unsigned long)s_motionCount);
  do_capture("PIR", /*notify_hub=*/true, nullptr, 0);
}

// ---------------------------------------------------------------------
//  Comandi in arrivo dall'hub (gia' nel contesto di loop(), vedi hub_link)
// ---------------------------------------------------------------------
static void on_hub_command(int cmd) {
  switch (cmd) {
    case HUB_CMD_ARM:     app_set_armed(true);  Serial.println("[HUB] comando: arma");    break;
    case HUB_CMD_DISARM:  app_set_armed(false); Serial.println("[HUB] comando: disarma"); break;
    case HUB_CMD_CAPTURE: Serial.println("[HUB] comando: scatta");
                          do_capture("HUB", /*notify_hub=*/false, nullptr, 0);            break;
    default:              Serial.printf("[HUB] comando sconosciuto: %d\n", cmd);          break;
  }
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
// (dichiarati in web_ui.h: la UI non sa niente di PIR e NVS, chiede qui.)

const char* app_node_name()  { return NODE_NAME; }
const char* app_fw_version() { return FW_VERSION; }

bool app_armed() { return s_armed; }

void app_set_armed(bool armed) {
  if (armed == s_armed) return;
  s_armed = armed;
  settings_save();
  Serial.printf("[APP] sorveglianza %s\n", armed ? "armata" : "disarmata");
}

uint32_t app_cooldown_s() { return s_cooldownS; }

void app_set_cooldown_s(uint32_t seconds) {
  if (seconds < 1 || seconds > 3600 || seconds == s_cooldownS) return;
  s_cooldownS = seconds;
  settings_save();
}

uint32_t app_motion_count() { return s_motionCount; }

long app_seconds_since_motion() {
  if (s_lastMotionMs == 0) return -1;
  return (long)((millis() - s_lastMotionMs) / 1000);
}

bool app_pir_warm() { return millis() > PIR_WARMUP_MS; }

bool app_capture_and_store(const char* sorgente, char* name_out, size_t name_cap) {
  return do_capture(sorgente, /*notify_hub=*/false, name_out, name_cap);
}

// Chiamata dal ciclo dello stream MJPEG, che tiene fermo il web server:
// qui NON si chiama net_loop(), perche' rientrare in handleClient() mentre
// si sta gia' servendo una richiesta rompe il web server.
void app_pump() {
  hub_loop();
  handle_motion();
}

// ============================ setup / loop ============================

void setup() {
  Serial.begin(115200);

  // Scritture su Serial mai bloccanti. Su questa board la Serial dell'USB non
  // e' una UART ma la CDC del chip: se il PC ha riconosciuto la porta e nessuno
  // la sta leggendo, il buffer si riempie e ogni print() aspetta un timeout
  // interno. Finche' aspetta, loop() e' fermo - e con lui net_loop(), quindi
  // web server, OTA, PIR ed ESP-NOW. Questo sketch stampa una riga di stato
  // ogni 10 s piu' una per scatto e una per rilevamento, e sta acceso da solo
  // per giorni: senza questa riga, il giorno che lo si lascia collegato a un PC
  // con il monitor chiuso smette di rispondere e sembra un guasto della rete.
  //
  // Il guardia serve perche' con "USB CDC On Boot: Disabled" la Serial torna
  // a essere una UART, che questo metodo non ce l'ha e non ne ha bisogno.
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(300);   // il CDC USB impiega un attimo: senza, si perde il banner

  Serial.println();
  Serial.printf("=== XIAO_S3_Camera — nodo camera \"%s\" %s ===\n", NODE_NAME, FW_VERSION);
  Serial.printf("PSRAM: %s   flash: %lu MB\n",
                psramFound() ? "presente" : "ASSENTE (compila con PSRAM=opi!)",
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)));

  settings_load();

  pinMode(PIN_PIR, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_PIR), pir_isr, RISING);

  camera_begin();   // se fallisce si va avanti lo stesso: la UI lo segnala
  sd_begin();       // idem: senza card il nodo avvisa comunque l'hub

  net_setOtaProgressCb(on_ota_progress);
  net_begin();      // WiFi + ArduinoOTA + /update
  web_ui_begin();   // rotte della camera sullo stesso web server

  // DOPO net_begin(): il canale ESP-NOW dipende dall'AP a cui ci siamo
  // connessi (vedi il commentone in hub_link.h).
  hub_on_command(on_hub_command);
  hub_begin(NODE_NAME);

  Serial.printf("Sorveglianza %s, pausa %lu s, riscaldamento PIR %lu s\n",
                s_armed ? "armata" : "disarmata",
                (unsigned long)s_cooldownS, (unsigned long)(PIR_WARMUP_MS / 1000));
}

void loop() {
  net_loop();   // ArduinoOTA + richieste web (e' anche il gestore dello stream)

  if (s_otaActive) {
    if (millis() - s_otaLastMs < OTA_STALL_MS) return;   // update in corso: niente altro
    s_otaActive = false;
    Serial.println("[OTA] nessun avanzamento: aggiornamento abbandonato, riprendo");
  }

  hub_loop();       // HELLO/pairing + comandi dell'hub
  handle_motion();  // PIR -> foto -> notifica

  // Riga di stato periodica su Serial (utile col cavo attaccato in banco).
  if (millis() - s_lastStatusMs > 10000) {
    s_lastStatusMs = millis();
    Serial.printf("[stato] wifi %s  hub %s  sd %s  armato %d  rilevamenti %lu  heap %lu\n",
                  net_isConnected() ? net_ip().c_str() : "no",
                  hub_paired() ? "ok" : "in attesa",
                  sd_mounted() ? "ok" : sd_last_error(),
                  (int)s_armed, (unsigned long)s_motionCount,
                  (unsigned long)ESP.getFreeHeap());
  }

  // <-- Nuove periferiche/logica: qui, senza bloccare a lungo (net_loop()
  //     deve girare spesso, altrimenti OTA e web UI diventano lentissimi).
}
