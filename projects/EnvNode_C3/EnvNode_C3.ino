/*
 * EnvNode_C3 — nodo sensore ambientale ESP32-C3 Supermini
 * --------------------------------------------------------------------------
 * DHT11 + log su microSD (HW-125, SPI) + OLED 0.96" I2C (SSD1306 128x64) +
 * dashboard web con grafici + OTA.
 *
 * Il boilerplate di rete/OTA sta in net_ota.cpp (variante che espone
 * net_server(), cosi' web_ui.cpp puo' registrarci sopra le proprie rotte).
 * Compila secrets.h con la tua rete WiFi (copia secrets.h.example).
 *
 * Impostazioni Arduino IDE (Tools):
 *   Board:            ESP32C3 Dev Module
 *   USB CDC On Boot:  Enabled            (Serial via USB nativo)
 *   Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA)   <-- obbligatorio
 *                     (serve la partizione OTA; NON "Huge APP (No OTA)")
 *   Flash Size:       4MB
 *
 * CABLAGGIO — vedi il piano per i dettagli (alimentazione HW-125, ecc.):
 *   OLED SSD1306 I2C:  SDA=GPIO5  SCL=GPIO6
 *   DHT11 DATA:        GPIO0
 *   HW-125 (SD SPI):   CS=GPIO1  SCK=GPIO4  MISO=GPIO3  MOSI=GPIO7
 *   Tasto BOOT (cambio pagina OLED): GPIO9 (gia' presente sulla scheda)
 *   LED blu onboard (heartbeat, attivo LOW): GPIO8
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

#include "net_ota.h"
#include "secrets.h"
#include "comfort.h"
#include "settings.h"
#include "rtc_time.h"
#include "sd_logger.h"
#include "web_ui.h"
#include "remote_nodes.h"

// ============================ Hardware ============================
static constexpr int     PIN_SDA   = 5;      // -> SDA dell'OLED
static constexpr int     PIN_SCL   = 6;      // -> SCL dell'OLED
static constexpr int     PIN_LED   = 8;      // LED blu onboard (attivo LOW)
static constexpr int     PIN_BOOT  = 9;      // tasto BOOT: avanza pagina OLED
static constexpr int     PIN_DHT   = 0;      // DATA del DHT11

static constexpr uint8_t OLED_ADDR = 0x3C;   // indirizzo I2C tipico (0x3C, a volte 0x3D)
static constexpr int     OLED_W    = 128;
static constexpr int     OLED_H    = 64;
static constexpr int     OLED_RST  = -1;     // moduli a 4 pin: nessun reset

static constexpr uint32_t FIRST_SAMPLE_MS = 2500;   // il DHT11 vuole ~1s dopo l'accensione
static constexpr uint32_t BOOT_DEBOUNCE_MS = 250;
static constexpr uint32_t DRAW_PERIOD_MS   = 500;    // ~2Hz: e' testo, non serve di piu'

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, OLED_RST);
DHT              dht(PIN_DHT, DHT11);

// Da incrementare a ogni firmware caricato sul nodo: la dashboard lo mostra,
// ed e' l'unico modo per sapere da remoto quale versione sta davvero girando.
//   v8  2026-08-23  EspNowLink: nome e tipo del peer aggiornati ad ogni
//                   messaggio, non solo al primo. Un nodo riprogrammato
//                   con un nome nuovo restava in elenco con quello vecchio
//   v7  2026-08-23  registro dei nodi persistito in NVS (solo MAC/tipo/nome,
//                   mai i valori) + pulsante "dimentica nodo": un nodo
//                   sopravvive al riavvio dell'hub anche a pairing chiuso
//   v6  2026-08-23  EspNowLink: l'hub adotta anche un DATA unicast da un
//                   nodo sconosciuto mentre il pairing e' aperto. Senza,
//                   dopo un riavvio dell'hub il nodo restava invisibile
//                   per sempre, e in silenzio da entrambe le parti
//   v5  2026-08-23  Update.abort() sull'upload interrotto: senza, il primo
//                   upload caduto a meta' rendeva la scheda non piu'
//                   aggiornabile via rete (vedi CLAUDE.md)
//   v4  2026-08-23  hub ESP-NOW: riceve i DATA dei nodi a batteria, li
//                   mostra su /nodi e /api/nodi e segnala i nodi muti
//                   (vedi remote_nodes.*). Da qui in poi lo sketch usa
//                   libraries/EspNowLink: compilare con --libraries libraries
//   v3  2026-08-22  Serial.setTxTimeoutMs(0): senza, con la porta USB
//                   riconosciuta dal PC ma nessuno che legge, le print()
//                   bloccavano loop() e con lui web server, OTA e campionamento
//   v2  ...
static const char* FW_VERSION = "v8";

static bool     otaActive = false;   // true mentre un update e' in corso
static uint32_t lastDraw  = 0;

// ---------------------------------------------------------------------
//  Stato applicativo: lettura corrente (media mobile) + estremi dal boot
// ---------------------------------------------------------------------
static bool     s_haveData  = false;
static uint32_t s_dhtErrors = 0;

// Media mobile delle ultime letture valide (fino a 3): smoothing contro il
// rumore del DHT11, usata per "il valore corrente" su OLED e dashboard. Il
// CSV logga sempre il dato grezzo (vedi take_sample()).
static constexpr int HIST_N = 3;
static float   s_histT[HIST_N] = {0};
static float   s_histH[HIST_N] = {0};
static uint8_t s_histCount = 0;
static uint8_t s_histIdx   = 0;

static float  s_tempMin, s_tempMax, s_humMin, s_humMax;
static time_t s_tempMinTs = 0, s_tempMaxTs = 0, s_humMinTs = 0, s_humMaxTs = 0;

static void pushHistory(float t, float h) {
  s_histT[s_histIdx] = t;
  s_histH[s_histIdx] = h;
  s_histIdx = (uint8_t)((s_histIdx + 1) % HIST_N);
  if (s_histCount < HIST_N) s_histCount++;
}

static float historyAvg(const float* arr) {
  if (s_histCount == 0) return 0.0f;
  float sum = 0;
  for (uint8_t i = 0; i < s_histCount; i++) sum += arr[i];
  return sum / s_histCount;
}

// ---------------------------------------------------------------------
//  Ganci per web_ui.cpp (vedi web_ui.h)
// ---------------------------------------------------------------------
bool  app_has_reading()      { return s_haveData; }
float app_temp_now()         { return historyAvg(s_histT); }
float app_hum_now()          { return historyAvg(s_histH); }
float  app_temp_min()        { return s_tempMin; }
time_t app_temp_min_ts()     { return s_tempMinTs; }
float  app_temp_max()        { return s_tempMax; }
time_t app_temp_max_ts()     { return s_tempMaxTs; }
float  app_hum_min()         { return s_humMin; }
time_t app_hum_min_ts()      { return s_humMinTs; }
float  app_hum_max()         { return s_humMax; }
time_t app_hum_max_ts()      { return s_humMaxTs; }
uint32_t    app_dht_errors() { return s_dhtErrors; }
const char* app_fw_version() { return FW_VERSION; }

// ---------------------------------------------------------------------
//  Campionamento: legge il DHT11, aggiorna min/max, logga su SD.
//
//  Ne' la lettura del DHT11 ne' la scrittura su SD sono particolarmente
//  veloci (il protocollo DHT si misura al microsecondo, la SD puo' volerci
//  decine di ms): nessun problema qui, a differenza degli sketch LVGL sulla
//  board AMOLED non c'e' un lock di rendering da tenere breve, il loop()
//  semplicemente aspetta.
// ---------------------------------------------------------------------
static void take_sample() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    s_dhtErrors++;
    Serial.println("[DHT11] lettura fallita (cablaggio/pin/alimentazione?)");
    return;
  }

  time_t now = rtctime_now();

  if (!s_haveData) {
    s_tempMin = s_tempMax = t; s_tempMinTs = s_tempMaxTs = now;
    s_humMin  = s_humMax  = h; s_humMinTs  = s_humMaxTs  = now;
  } else {
    if (t < s_tempMin) { s_tempMin = t; s_tempMinTs = now; }
    if (t > s_tempMax) { s_tempMax = t; s_tempMaxTs = now; }
    if (h < s_humMin)  { s_humMin  = h; s_humMinTs  = now; }
    if (h > s_humMax)  { s_humMax  = h; s_humMaxTs  = now; }
  }
  s_haveData = true;
  pushHistory(t, h);

  bool logged = sd_log_sample(now, rtctime_source(), t, h);

  char tbuf[24];
  rtctime_format(now, "%Y-%m-%d %H:%M:%S", tbuf, sizeof(tbuf));
  Serial.printf("[%s %s] %.1fC  %.1f%%RH  SD:%s\n",
                tbuf, rtctime_source(), t, h,
                logged ? "ok" : (sd_mounted() ? sd_last_error() : "non montata"));
}

// ---------------------------------------------------------------------
//  OLED — 3 pagine, rotazione automatica + tasto BOOT
// ---------------------------------------------------------------------
enum Page : uint8_t { PAGE_NOW = 0, PAGE_MINMAX = 1, PAGE_SYSTEM = 2, PAGE_COUNT = 3 };

static Page     s_page          = PAGE_NOW;
static uint32_t s_pageStartMs   = 0;
static bool     s_bootLastLevel = HIGH;   // INPUT_PULLUP: HIGH = non premuto
static uint32_t s_bootLastMs    = 0;

static void handleBootButton() {
  bool level = digitalRead(PIN_BOOT);
  if (level != s_bootLastLevel && (millis() - s_bootLastMs) > BOOT_DEBOUNCE_MS) {
    s_bootLastMs    = millis();
    s_bootLastLevel = level;
    if (level == LOW) {   // fronte di discesa: pulsante premuto
      s_page        = (Page)((s_page + 1) % PAGE_COUNT);
      s_pageStartMs = millis();
    }
  }
}

static void updatePageRotation() {
  uint32_t periodMs = settings_get().pageSeconds * 1000UL;
  if (millis() - s_pageStartMs >= periodMs) {
    s_page        = (Page)((s_page + 1) % PAGE_COUNT);
    s_pageStartMs = millis();
  }
}

// Puntini in basso: indicano la pagina corrente fra le PAGE_COUNT.
static void drawPageDots() {
  int totalW = PAGE_COUNT * 8 - 4;
  int x0     = (OLED_W - totalW) / 2;
  for (int i = 0; i < PAGE_COUNT; i++) {
    int cx = x0 + i * 8;
    if (i == s_page) oled.fillCircle(cx, OLED_H - 4, 2, SSD1306_WHITE);
    else              oled.drawCircle(cx, OLED_H - 4, 2, SSD1306_WHITE);
  }
}

// ---------------------------------------------------------------------
//  Geometria condivisa dalle 3 pagine. Tutte le pagine rispettano lo
//  stesso budget verticale per evitare che testo/puntini si sovrappongano
//  (bug reale osservato su hardware: titolo lungo che si scontrava con
//  l'indicatore in alto a destra, riga di comfort che si scontrava con
//  l'orario sulla stessa riga, ultima riga di contenuto che si scontrava
//  con i puntini di pagina):
//    y 0-8    header (titolo + orario), poi una riga orizzontale a y=9
//    y 12-56  area contenuto: OGNI pagina deve finire per y<=56
//    y 58-62  puntini di pagina — nessun contenuto oltre y=56
//  In orizzontale, testTextSize1 e' 6px/carattere: 128px / 6 = 21
//  caratteri per riga, budget da rispettare per ogni stringa stampata.
// ---------------------------------------------------------------------
static constexpr int CONTENT_Y0 = 12;

static void drawHeaderBar(const char* title) {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(title);   // titoli tenuti <=7 char apposta, vedi sotto

  // Orologio a larghezza FISSA ("N 14:03:07" / "~ 14:03:07", 10 char),
  // ancorato a destra: cosi' non collide mai coi titoli (tutti <=7 char)
  // indipendentemente da come cambia il contenuto.
  char tbuf[12];
  oled.setCursor(OLED_W - 10 * 6, 0);
  if (rtctime_format(rtctime_now(), "%H:%M:%S", tbuf, sizeof(tbuf))) {
    oled.print(rtctime_isSynced() ? "N " : "~ ");
    oled.print(tbuf);
  }

  oled.drawFastHLine(0, 9, OLED_W, SSD1306_WHITE);
}

static void drawPageNow() {
  drawHeaderBar("Attuale");

  oled.setTextSize(2);
  oled.setCursor(0, CONTENT_Y0);
  if (app_has_reading()) {
    oled.print(app_temp_now(), 1);
    oled.write(248);   // '°' (richiede oled.cp437(true), gia' impostato in setup)
    oled.print("C");
  } else {
    oled.print("--.-C");
  }

  oled.setCursor(0, CONTENT_Y0 + 18);
  if (app_has_reading()) {
    oled.print(app_hum_now(), 0);
    oled.print("%");
  } else {
    oled.print("--%");
  }

  oled.setTextSize(1);
  if (app_has_reading()) {
    // "Confortevole (100)" e' la piu' lunga possibile: 19 char = 114px,
    // dentro il budget di 128px anche nel caso peggiore.
    ComfortResult c = comfort_eval(app_temp_now(), app_hum_now(), settings_get().comfort);
    oled.setCursor(0, CONTENT_Y0 + 36);
    oled.printf("%s (%d)", c.label, c.score);
  }

  drawPageDots();
}

static void drawPageMinMax() {
  drawHeaderBar("Min/Max");   // titolo corto apposta: vedi nota in drawHeaderBar

  oled.setTextSize(1);
  if (!app_has_reading()) {
    oled.setCursor(0, CONTENT_Y0 + 8);
    oled.print("in attesa di dati...");
  } else {
    char tbuf[9];
    oled.setCursor(0, CONTENT_Y0);
    rtctime_format(app_temp_min_ts(), "%H:%M", tbuf, sizeof(tbuf));
    oled.printf("T min %.1fC %s", app_temp_min(), tbuf);

    oled.setCursor(0, CONTENT_Y0 + 9);
    rtctime_format(app_temp_max_ts(), "%H:%M", tbuf, sizeof(tbuf));
    oled.printf("T max %.1fC %s", app_temp_max(), tbuf);

    oled.setCursor(0, CONTENT_Y0 + 22);
    rtctime_format(app_hum_min_ts(), "%H:%M", tbuf, sizeof(tbuf));
    oled.printf("H min %.0f%% %s", app_hum_min(), tbuf);

    oled.setCursor(0, CONTENT_Y0 + 31);
    rtctime_format(app_hum_max_ts(), "%H:%M", tbuf, sizeof(tbuf));
    oled.printf("H max %.0f%% %s", app_hum_max(), tbuf);
  }

  drawPageDots();
}

static void drawPageSystem() {
  drawHeaderBar("Sistema");

  oled.setTextSize(1);
  oled.setCursor(0, CONTENT_Y0);
  if (sd_mounted()) {
    oled.printf("SD %llu/%llu MB", (unsigned long long)sd_free_mb(), (unsigned long long)sd_total_mb());
  } else {
    oled.printf("SD: %s", sd_last_error());
  }

  oled.setCursor(0, CONTENT_Y0 + 9);
  oled.printf("Log oggi %lu tot %lu", (unsigned long)sd_record_count_today(),
              (unsigned long)sd_record_count_total());

  oled.setCursor(0, CONTENT_Y0 + 18);
  oled.printf("Errori DHT11: %lu", (unsigned long)app_dht_errors());

  oled.setCursor(0, CONTENT_Y0 + 27);
  oled.printf("FW %s up %lus", app_fw_version(), (unsigned long)(millis() / 1000));

  oled.setCursor(0, CONTENT_Y0 + 36);
  if (net_isConnected()) {
    oled.print(net_ip());
  } else {
    oled.print("WiFi: non connesso");
  }

  drawPageDots();
}

static void drawCurrentPage() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  switch (s_page) {
    case PAGE_NOW:     drawPageNow();     break;
    case PAGE_MINMAX:  drawPageMinMax();  break;
    case PAGE_SYSTEM:  drawPageSystem();  break;
    default: break;
  }
  oled.display();
}

// ---------------------------------------------------------------------
//  Feedback a schermo durante un update OTA (chiamata da net_ota).
//  percent = 0..100, oppure -1 = sconosciuto (upload web).
// ---------------------------------------------------------------------
static void onOtaProgress(int percent, const char* what) {
  otaActive = true;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("Aggiornamento OTA");
  oled.setCursor(0, 14);
  oled.println(what);
  if (percent >= 0) {
    oled.drawRect(0, 34, OLED_W, 12, SSD1306_WHITE);
    oled.fillRect(2, 36, (OLED_W - 4) * percent / 100, 8, SSD1306_WHITE);
    oled.setCursor(0, 52);
    oled.printf("%d%%", percent);
  } else {
    oled.setCursor(0, 40);
    oled.println("in corso...");
  }
  oled.display();
}

// ============================ setup / loop ============================
void setup() {
  Serial.begin(115200);

  // Scritture su Serial mai bloccanti. Su questa board Serial e' la USB CDC
  // nativa del C3, non una UART: se il PC ha riconosciuto la porta ma nessuno
  // la sta leggendo, il buffer si riempie e ogni printf aspetta il timeout,
  // fermando loop() - quindi web server, OTA e campionamento. Questo sketch
  // stampa una riga a OGNI campione, quindi e' particolarmente esposto.
  // Finora non e' successo niente probabilmente solo perche' il nodo sta su
  // un alimentatore e non su una porta USB di un PC: senza i pin dati la
  // porta non viene mai riconosciuta e le scritture si buttano via da sole.
  // E' una salvezza per fortuna, non per costruzione. Stessa riga, e stesso
  // motivo, di MeteoHub_S3 e MeteoNode_C3.
  Serial.setTxTimeoutMs(0);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);        // LED spento (attivo LOW)
  pinMode(PIN_BOOT, INPUT_PULLUP);    // tasto BOOT: solo lettura, mai output

  // Configurazione (NVS) e orario, PRIMA di tutto il resto: entrambi non
  // dipendono da hardware esterno e servono a chi viene dopo (OLED mostra
  // l'ora, sd_logger vuole gia' un settings_get() valido).
  settings_begin();
  rtctime_begin(settings_get().tz);
  rtctime_seedFromBuild();

  // I2C + OLED. periphBegin=false: teniamo i pin passati a Wire.begin(),
  // altrimenti Adafruit reinizializza Wire sui pin di default.
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, /*reset=*/true, /*periphBegin=*/false)) {
    Serial.println("[OLED] init fallita: controlla cablaggio SDA/SCL e indirizzo (0x3C/0x3D).");
    // Si continua lo stesso: OTA/SD/DHT devono funzionare anche senza display.
  }
  oled.cp437(true);   // mappa CP437: serve per il simbolo di grado, vedi drawPageNow()

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("EnvNode-C3");
  oled.println("avvio...");
  oled.display();

  dht.begin();

  if (!sd_begin()) {
    Serial.printf("[SD] non disponibile: %s (si continua senza logging)\n", sd_last_error());
  } else {
    Serial.printf("[SD] montata, %llu MB liberi su %llu MB\n",
                  (unsigned long long)sd_free_mb(), (unsigned long long)sd_total_mb());
  }

  net_setOtaProgressCb(onOtaProgress);
  net_begin();   // WiFi + ArduinoOTA + web /update
  if (net_isConnected()) rtctime_onWifiConnected();

  web_ui_begin();

  // ESP-NOW dopo net_begin(): la radio e' una sola e il canale lo detta
  // l'AP, quindi si lascia decidere allo stack WiFi gia' configurato (vedi
  // la nota sul canale in remote_nodes.h). Se il WiFi non fosse ancora
  // connesso qui non e' un problema: i peer sono registrati sul "canale
  // corrente" e seguono l'AP da soli quando la connessione arriva.
  remote_begin(settings_get().nodeName);

  s_pageStartMs = millis();
}

void loop() {
  net_loop();                 // gestisce ArduinoOTA + web server
  if (otaActive) return;      // durante un update non fare altro

  static bool wasConnected = false;
  bool nowConnected = net_isConnected();
  if (nowConnected && !wasConnected) rtctime_onWifiConnected();   // riconnesso: rilancia il sync NTP
  wasConnected = nowConnected;

  remote_loop();              // nodi ESP-NOW: WELCOME, nuovi DATA, stato muto

  handleBootButton();
  updatePageRotation();

  static uint32_t winStartMs = 0;
  static uint32_t winSpanMs  = FIRST_SAMPLE_MS;
  uint32_t now = millis();
  if ((int32_t)(now - (winStartMs + winSpanMs)) >= 0) {
    take_sample();
    winStartMs = millis();
    winSpanMs  = settings_get().logIntervalS * 1000UL;
  }

  if (millis() - lastDraw > DRAW_PERIOD_MS) {
    lastDraw = millis();
    drawCurrentPage();
  }

  // Heartbeat: LED acceso mezzo secondo ogni secondo (attivo LOW).
  digitalWrite(PIN_LED, (millis() % 1000 < 500) ? LOW : HIGH);
}
