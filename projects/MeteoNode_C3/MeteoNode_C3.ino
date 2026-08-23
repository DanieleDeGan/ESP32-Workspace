/*
 * MeteoNode_C3 - nodo sensore della stazione meteo e-ink (Seeed XIAO ESP32-C3)
 * ---------------------------------------------------------------------------
 * STATO: bring-up del sensore, con WiFi + pagina web + OTA per poterlo provare
 * SENZA cavo USB, anche a batteria. Della Fase 1 vera manca ancora l'ESP-NOW
 * verso l'hub; deep sleep e partitore della batteria sono di Fase 4 e qui non
 * ci sono. Questo sketch serve a dimostrare quattro cose prima di costruirci
 * sopra, esattamente come MeteoHub_S3 ha fatto col pannello:
 *
 *   1. che le saldature tengono (scansione I2C, non "il sensore non va");
 *   2. che i due chip sul modulo sono quelli che crediamo (legge il chip ID
 *      del BMP280: i cloni a volte sono BME280, che ha anche l'umidita' e un
 *      ID diverso, e la libreria Adafruit rifiuterebbe di partire);
 *   3. che le misure sono plausibili (le DUE temperature, una per chip, si
 *      devono somigliare: se divergono c'e' autoriscaldamento o un guasto);
 *   4. che l'alimentazione commutata da GPIO funziona davvero, con il
 *      power-cycle gia' scritto come lo vorra' il deep sleep di Fase 4.
 *
 * Il piano completo del progetto sta in docs/Stazione-Meteo.md (Fase 1).
 *
 * CABLAGGIO (modulo combinato AHT20 + BMP280, 4 fili):
 *
 *   modulo      XIAO C3      GPIO   nota
 *   ---------   ----------   ----   --------------------------------------
 *   SCL         D2             4    RTC-capable, non e' pin di strapping
 *   VCC/VIN     D3             5    alimentazione COMMUTATA, non il 3V3
 *   SDA         D4             6    il default della variante e' D4/D5: qui
 *   GND         GND            -    SCL e' spostato su D2, quindi Wire.begin()
 *                                   vuole i pin espliciti
 *
 * NON collegare VCC al pad 5V: e' alimentato solo dalla USB, a batteria e'
 * morto, e molti di questi moduli combo non hanno regolatore a bordo.
 * D1/GPIO3 resta libero apposta: e' riservato al partitore della batteria
 * (vedi BATTERY_ADC_ENABLED sotto, gia' scritto ma spento finche' non c'e').
 *
 * ATTENZIONE, la trappola di questo cablaggio: il sensore e' SPENTO finche'
 * il firmware non alza D3/GPIO5. Uno scanner I2C generico, o qualunque altro
 * sketch che non lo sa, non trova niente anche a saldature perfette.
 *
 * COME SI PROVA
 *   - via rete:   http://<OTA_HOSTNAME>.local/  (default meteonode-c3.local)
 *                 pagina di stato che si aggiorna da sola ogni 2 s, con gli
 *                 stessi comandi del monitor seriale. E' il modo previsto per
 *                 il test a batteria, dove la Serial non c'e' piu'.
 *                 Aggiornamento firmware: /update  oppure ArduinoOTA.
 *   - via USB:    monitor seriale a 115200, comandi:
 *                   s   ri-scansiona il bus I2C
 *                   r   power-cycle del sensore + re-init (il risveglio)
 *                   p   accende/spegne il sensore, per vedere che il GPIO
 *                       comanda davvero
 *                   h   questo elenco
 *
 * CREDENZIALI: copiare secrets.h.example in secrets.h e riempirlo. secrets.h
 * e' gitignorato (il repo e' pubblico). Senza WiFi lo sketch funziona lo
 * stesso: net_begin() rinuncia dopo 15 s e ritenta in background, il sensore
 * e la Serial non aspettano la rete.
 *
 * IMPOSTAZIONI Arduino IDE (Tools):
 *   Board:            XIAO_ESP32C3   (non "ESP32C3 Dev Module")
 *   USB CDC On Boot:  Enabled        (e' gia' il default di questa board)
 *   Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA)   <- serve l'OTA
 *
 * Da riga di comando, dalla radice del repo (niente --libraries: finche' non
 * c'e' l'ESP-NOW questo sketch non usa nulla di libraries/):
 *   arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32C3:PartitionScheme=min_spiffs" projects/MeteoNode_C3
 *
 * ATTENZIONE al CDC, come sulla XIAO S3 e al contrario delle altre schede del
 * repo: qui CDCOnBoot e' gia' Enabled di default e nel FQBN "CDCOnBoot=cdc"
 * significa DISABLED. Non va messo nulla, o la Serial finisce sui pin UART0 e
 * sulla USB si vede solo il log di boot della ROM.
 *
 * Dipendenze (Library Manager): Adafruit AHTX0, Adafruit BMP280 Library
 * (entrambe tirano dentro Adafruit Unified Sensor e Adafruit BusIO).
 * Il resto - WiFi, WebServer, ArduinoOTA, ESPmDNS - e' core ESP32.
 */

#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

#include "net_ota.h"
#include "web_ui.h"
#include "rtc_time.h"
#include "forecast.h"
#include "hub_link.h"

// Da incrementare a ogni firmware caricato: la pagina lo mostra, ed e' l'unico
// modo per sapere da remoto quale versione sta davvero girando.
//   v4  2026-08-23  stesso sketch anche su ESP32 "classico" (DOIT DevKit
//                   v1): pin, nome nodo e guardia della Serial scelti a
//                   compile-time dal tipo di chip
//   v3  2026-08-23  invio ESP-NOW all'hub ad ogni ciclo di misura
//                   (hub_link.*). Da qui in poi lo sketch usa
//                   libraries/EspNowLink: compilare con --libraries libraries
//   v2  2026-08-22  storico 24 h in RAM + grafici, previsione dal trend
//                   barometrico a 3 ore, intervallo e altitudine da pagina web
//   v1  2026-08-22  bring-up del sensore, web UI, OTA
static const char FW_VERSION[] = "v4";

// Nome con cui il nodo si presenta all'hub. Massimo 16 caratteri (troncato
// da EspNowLink): due nodi con lo stesso nome sarebbero indistinguibili
// nella lista dell'hub, ed e' il motivo per cui dipende dal chip.
#if defined(CONFIG_IDF_TARGET_ESP32)
static const char NODE_NAME[] = "MeteoEsp32";
#else
static const char NODE_NAME[] = "MeteoNode";
#endif

// Trieste. Costante di compilazione e non impostazione, come in
// Timelapse_XIAO: questa scheda non viaggia.
static const char TZ_POSIX[] = "CET-1CEST,M3.5.0,M10.5.0/3";

// ---------------------------------------------------------------------------
// Pin e costanti
// ---------------------------------------------------------------------------
// Numeri GPIO, non le etichette D2/D3/D4: cosi' lo sketch compila anche con la
// board generica "ESP32C3 Dev Module", che non definisce i D*.
//
// I pin dipendono dal chip, non dalla scheda: le due varianti supportate hanno
// pinout incompatibili, e sceglierli a compile-time e' l'unico modo di tenere
// UN solo firmware invece di due copie destinate a divergere al primo bugfix.
//
// Vincoli comuni a entrambe: il pin che alimenta il sensore deve essere
// RTC-capable (serve a gpio_hold_en() quando arrivera' il deep sleep, o
// durante il sonno tornerebbe flottante) e non deve essere di strapping.
#if defined(CONFIG_IDF_TARGET_ESP32)
// ESP32 "classico" (DOIT ESP32 DevKit v1 e simili).
//
// ATTENZIONE: i GPIO6-11 sono la FLASH SPI e non si toccano - cioe' proprio il
// GPIO6 che sulla XIAO C3 fa da SDA. E' la ragione per cui questo blocco
// esiste: gli stessi numeri, sull'altro chip, non sono liberi ma fatali.
// Fuori uso anche GPIO0/2/12/15 (strapping) e GPIO34-39 (solo ingresso).
static const uint8_t PIN_SCL        = 22;  // I2C di default sull'ESP32 classico
static const uint8_t PIN_SENSOR_PWR = 26;  // RTC-capable, non strapping, libero
static const uint8_t PIN_SDA        = 21;  // I2C di default
#else
// XIAO ESP32-C3 - cablaggio saldato il 2026-08-22.
static const uint8_t PIN_SCL        = 4;   // D2
static const uint8_t PIN_SENSOR_PWR = 5;   // D3
static const uint8_t PIN_SDA        = 6;   // D4
#endif

// Partitore della batteria: 2x1 MOhm fra + cella e GND, presa centrale su
// D1/GPIO3, piu' 100 nF verso massa. Non e' ancora cablato, quindi la lettura
// resta spenta: leggere un pin flottante darebbe una tensione inventata, molto
// peggio di un "non disponibile" onesto. Quando il partitore ci sara', basta
// mettere questo a 1.
#define BATTERY_ADC_ENABLED 0
#if defined(CONFIG_IDF_TARGET_ESP32)
// Sull'ESP32 classico l'ADC1 e' sui GPIO32-39; il 35 e' solo ingresso, che per
// un partitore va benissimo. NON usare il GPIO3, che li' e' la RX della UART0.
static const uint8_t PIN_BATTERY = 35;
#else
static const uint8_t PIN_BATTERY = 3;      // D1, ADC1: usabile col WiFi acceso
#endif

static const uint8_t ADDR_AHT20    = 0x38;
static const uint8_t ADDR_BMP_LOW  = 0x76;  // il piu' comune sui moduli combo
static const uint8_t ADDR_BMP_HIGH = 0x77;  // BMP280_ADDRESS di Adafruit

// 100 kHz invece dei 400: in bring-up si lavora su fili volanti, e un bus
// lento perdona lunghezze e pull-up che a 400 kHz darebbero errori casuali,
// scambiabili per una saldatura fredda.
static const uint32_t I2C_HZ = 100000;

static const uint32_t SENSOR_BOOT_MS  = 100;   // l'AHT20 lo chiede da datasheet
static const uint32_t SENSOR_OFF_MS   = 300;   // scarica dei condensatori
static const uint8_t  MAX_FAIL_STREAK = 3;     // poi power-cycle e re-init

// ---------------------------------------------------------------------------
// Stato
// ---------------------------------------------------------------------------
static Adafruit_AHTX0  aht;
static Adafruit_BMP280 bmp;

static bool     s_powered     = false;
static bool     s_ahtOk       = false;
static bool     s_bmpOk       = false;
static uint8_t  s_bmpAddr     = 0;
static uint8_t  s_bmpChipId   = 0;
static uint8_t  s_failStreak  = 0;
static uint32_t s_reads       = 0;
static uint32_t s_readErrors  = 0;
static uint32_t s_powerCycles = 0;
static uint32_t s_lastReadMs  = 0;

// Ultima lettura valida, quella che la pagina web mostra fra un campione e il
// successivo. Tenuta a parte dalle variabili locali di readAndPrint() apposta:
// la web UI puo' chiedere lo stato in qualunque istante, anche mentre il
// sensore e' spento o non risponde, e deve poter dire "l'ultima buona era
// questa" invece di lampeggiare a vuoto.
static bool     s_hasReading = false;
static uint32_t s_lastGoodMs = 0;   // millis() dell'ultima lettura riuscita
static float s_tempAht = NAN, s_hum = NAN, s_tempBmp = NAN;
static float s_press = NAN, s_dewpoint = NAN;

// Estremi dall'ultimo avvio (RAM, azzerati a ogni boot).
static float s_tMin = NAN, s_tMax = NAN;
static float s_hMin = NAN, s_hMax = NAN;
static float s_pMin = NAN, s_pMax = NAN;

static void trackMinMax(float v, float &mn, float &mx) {
  if (isnan(v)) return;
  if (isnan(mn) || v < mn) mn = v;
  if (isnan(mx) || v > mx) mx = v;
}

// ---------------------------------------------------------------------------
// Impostazioni persistite in NVS
// ---------------------------------------------------------------------------
// Stesso pattern di settings.cpp di EnvNode_C3: si apre la NVS, si legge o si
// scrive, si chiude subito. Mai tenuta aperta, e soprattutto mai scritta a
// ogni giro - la flash ha un numero finito di cicli di cancellazione, e qui si
// scrive solo quando l'utente cambia davvero un valore dalla pagina.
static const uint32_t INTERVALLO_MIN_S     = 2;
static const uint32_t INTERVALLO_MAX_S     = 3600;
static const uint32_t INTERVALLO_DEFAULT_S = 60;

// Trieste. E' una STIMA da calibrare, non un dato: conta circa 1 hPa ogni 8 m,
// quindi sbagliarla di 50 m sposta la pressione di 6 hPa e rende il confronto
// con i bollettini senza senso. Dalla pagina si calibra in un minuto inserendo
// la pressione al livello del mare letta da un bollettino locale.
static const float ALTITUDINE_DEFAULT_M = 40.0f;
static const float ALTITUDINE_MIN_M     = -400.0f;   // Mar Morto, per dire
static const float ALTITUDINE_MAX_M     = 4000.0f;

static uint32_t s_intervalloS = INTERVALLO_DEFAULT_S;
static float    s_altitudineM = ALTITUDINE_DEFAULT_M;

static void settingsLoad() {
  Preferences p;
  // In sola lettura begin() torna false se il namespace non esiste ancora:
  // e' il primo avvio, e i default sono gia' a posto.
  if (!p.begin("meteonode", true)) return;
  s_intervalloS = p.getULong("intervallo", INTERVALLO_DEFAULT_S);
  s_altitudineM = p.getFloat("altitudine", ALTITUDINE_DEFAULT_M);
  p.end();

  if (s_intervalloS < INTERVALLO_MIN_S || s_intervalloS > INTERVALLO_MAX_S) {
    s_intervalloS = INTERVALLO_DEFAULT_S;
  }
  if (isnan(s_altitudineM) ||
      s_altitudineM < ALTITUDINE_MIN_M || s_altitudineM > ALTITUDINE_MAX_M) {
    s_altitudineM = ALTITUDINE_DEFAULT_M;
  }
}

// ---------------------------------------------------------------------------
// Storico in RAM per i grafici
// ---------------------------------------------------------------------------
// A differenza di EnvNode_C3, qui NON c'e' una microSD: lo storico vive solo
// in RAM e si azzera a ogni riavvio o stacco della batteria. E' un limite
// accettato, non una dimenticanza - il nodo serve a leggere l'ambiente adesso,
// e i dati storici veri li terra' l'hub quando ci sara' l'ESP-NOW.
//
// Griglia FISSA da 2 minuti, indipendente dall'intervallo di misura che
// l'utente puo' cambiare: 720 slot = 24 ore in circa 4,3 kB. Ogni slot tiene
// la MEDIA delle misure cadute in quella finestra, il che smorza il rumore e
// rende il grafico leggibile. Se l'intervallo di misura e' piu' lungo di due
// minuti alcuni slot restano vuoti, ed e' giusto che si veda: un buco nella
// linea e' un'informazione, una linea che tira dritto sopra un buco e' una
// bugia.
static const uint16_t HIST_SLOT      = 720;
static const uint32_t HIST_PERIODO_S = 120;
static const uint16_t HIST_SLOT_3H   = (uint16_t)(10800UL / HIST_PERIODO_S);  // 90

// Valori a interi scalati invece che float: dimezza la RAM e vale la pena,
// perche' la risoluzione che serve a un grafico e' molto minore di quella di
// un float. INT16_MIN fa da sentinella di "slot senza dati".
static const int16_t HIST_VUOTO = INT16_MIN;

static int16_t  s_hT[HIST_SLOT];   // gradi C  x100
static int16_t  s_hH[HIST_SLOT];   // %RH      x100
static int16_t  s_hP[HIST_SLOT];   // (hPa x10) - 10000, cioe' 672..1327 hPa
static uint16_t s_hCount = 0;      // slot occupati
static uint16_t s_hIdx   = 0;      // prossima posizione da scrivere

static uint32_t s_slotAcc     = 0;      // indice di slot in accumulo
static bool     s_slotAvviato = false;
static uint32_t s_slotUltimo  = 0;      // indice dell'ultimo slot memorizzato
// Un accumulatore per canale, con il suo contatore: i due chip possono
// fallire in modo indipendente, e se cade il BMP280 non c'e' motivo di
// buttare via anche la temperatura dell'AHT20 di quella finestra.
static double   s_accT = 0, s_accH = 0, s_accP = 0;
static uint16_t s_accNT = 0, s_accNH = 0, s_accNP = 0;

static int16_t histScalaT(float v) { return (int16_t)lroundf(v * 100.0f); }
static int16_t histScalaH(float v) { return (int16_t)lroundf(v * 100.0f); }
static int16_t histScalaP(float v) { return (int16_t)lroundf(v * 10.0f - 10000.0f); }

static void histPush(int16_t t, int16_t h, int16_t p) {
  s_hT[s_hIdx] = t;
  s_hH[s_hIdx] = h;
  s_hP[s_hIdx] = p;
  s_hIdx = (uint16_t)((s_hIdx + 1) % HIST_SLOT);
  if (s_hCount < HIST_SLOT) s_hCount++;
}

static void histPushVuoto() { histPush(HIST_VUOTO, HIST_VUOTO, HIST_VUOTO); }

// Legge lo slot "back" posizioni indietro rispetto al piu' recente
// (back = 0 e' il piu' recente). Torna false se non c'e' o se e' vuoto.
static bool histAt(uint16_t back, float* t, float* h, float* p) {
  if (back >= s_hCount) return false;
  const uint16_t pos = (uint16_t)((s_hIdx + HIST_SLOT - 1 - back) % HIST_SLOT);
  if (s_hT[pos] == HIST_VUOTO && s_hP[pos] == HIST_VUOTO) return false;
  if (t) *t = (s_hT[pos] == HIST_VUOTO) ? NAN : s_hT[pos] / 100.0f;
  if (h) *h = (s_hH[pos] == HIST_VUOTO) ? NAN : s_hH[pos] / 100.0f;
  if (p) *p = (s_hP[pos] == HIST_VUOTO) ? NAN : (s_hP[pos] + 10000.0f) / 10.0f;
  return true;
}

// Chiude lo slot in accumulo e apre quello nuovo, riempiendo di vuoti gli
// slot eventualmente saltati in mezzo (nodo fermo, sensore spento, ora
// spostata dal primo sync NTP).
static void histChiudiSlot(uint32_t slotNuovo) {
  histPush(s_accNT ? histScalaT((float)(s_accT / s_accNT)) : HIST_VUOTO,
           s_accNH ? histScalaH((float)(s_accH / s_accNH)) : HIST_VUOTO,
           s_accNP ? histScalaP((float)(s_accP / s_accNP)) : HIST_VUOTO);
  s_slotUltimo = s_slotAcc;

  // I buchi. Il tetto e' HIST_SLOT: dopo un giorno di assenza il buffer e'
  // comunque tutto da buttare, e senza il tetto un salto dell'orologio di
  // mesi bloccherebbe loop() in un ciclo lunghissimo.
  uint32_t mancanti = (slotNuovo > s_slotAcc + 1) ? (slotNuovo - s_slotAcc - 1) : 0;
  if (mancanti > HIST_SLOT) mancanti = HIST_SLOT;
  for (uint32_t i = 0; i < mancanti; i++) histPushVuoto();
  if (mancanti) s_slotUltimo = slotNuovo - 1;

  s_slotAcc = slotNuovo;
  s_accT = s_accH = s_accP = 0;
  s_accNT = s_accNH = s_accNP = 0;
}

static void histFeed(float t, float h, float p) {
  const uint32_t slot = (uint32_t)(rtctime_now() / (time_t)HIST_PERIODO_S);

  if (!s_slotAvviato) {
    s_slotAcc = slot;
    s_slotUltimo = slot;
    s_slotAvviato = true;
  } else if (slot != s_slotAcc) {
    histChiudiSlot(slot);
  }

  if (!isnan(t)) { s_accT += t; s_accNT++; }
  if (!isnan(h)) { s_accH += h; s_accNH++; }
  if (!isnan(p)) { s_accP += p; s_accNP++; }
}

// Istante dello slot piu' recente memorizzato: la pagina ricostruisce l'asse
// dei tempi da qui e dal passo, senza che si debbano mandare 720 timestamp.
static time_t histUltimoTs() {
  return (time_t)s_slotUltimo * (time_t)HIST_PERIODO_S;
}

// ---------------------------------------------------------------------------
// Trend barometrico
// ---------------------------------------------------------------------------
// La previsione di un barometro di casa e' tutta qui: quanto e' cambiata la
// pressione nelle ultime tre ore. Il valore di tre ore fa si legge dallo
// storico, che e' anche il motivo per cui lo storico non e' solo un vezzo
// grafico. Finche' non ci sono tre ore di dati il trend resta IGNOTO, e la
// pagina lo dice invece di mostrare una previsione inventata.
static forecast_trend_t s_trend   = TREND_IGNOTO;
static float            s_delta3h = NAN;

static void aggiornaTrend() {
  float p3h = NAN;
  if (isnan(s_press) || !histAt(HIST_SLOT_3H, nullptr, nullptr, &p3h) || isnan(p3h)) {
    s_trend = TREND_IGNOTO;
    s_delta3h = NAN;
    return;
  }
  // Il confronto si fa su valori riportati al livello del mare. Con la stessa
  // altitudine per entrambi il fattore e' costante e la differenza cambia di
  // pochissimo, ma tenerlo esplicito evita di dover ricordare perche' era
  // lecito ometterlo il giorno che l'altitudine diventasse variabile.
  s_delta3h = forecast_sea_level_hpa(s_press, s_altitudineM)
            - forecast_sea_level_hpa(p3h,     s_altitudineM);
  s_trend = forecast_classify_hyst(s_delta3h, s_trend);
}

// ---------------------------------------------------------------------------
// Alimentazione del sensore
// ---------------------------------------------------------------------------
// Spegnendo, SDA e SCL vanno messi a ingresso SENZA pull-up prima di togliere
// il VCC. I pull-up dell'I2C stanno sulla basetta e si spengono con lei: un
// pin dell'ESP32 lasciato alto spingerebbe corrente nei piedini di un chip non
// alimentato e, attraverso i diodi di protezione, ne alimenterebbe in parte il
// VDD. Il sensore resterebbe "mezzo acceso", consumando e senza resettarsi
// pulito. Qui non cambia niente di visibile, ma e' la sequenza che servira'
// tale e quale al deep sleep di Fase 4: meglio averla giusta da subito.
static void sensorPower(bool on) {
  if (on) {
    pinMode(PIN_SENSOR_PWR, OUTPUT);
    digitalWrite(PIN_SENSOR_PWR, HIGH);
    delay(SENSOR_BOOT_MS);
    Wire.begin(PIN_SDA, PIN_SCL, I2C_HZ);
    // Timeout corto: il default e' 50 ms, e su un indirizzo vuoto si paga
    // tutto. A 100 kHz una transazione vera dura meno di un millisecondo,
    // quindi 10 ms sono gia' larghissimi e la scansione completa costa
    // frazioni di secondo invece di secondi.
    Wire.setTimeOut(10);
  } else {
    Wire.end();
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);
    pinMode(PIN_SENSOR_PWR, OUTPUT);
    digitalWrite(PIN_SENSOR_PWR, LOW);
    delay(SENSOR_OFF_MS);
  }
  s_powered = on;
}

// ---------------------------------------------------------------------------
// Diagnostica I2C
// ---------------------------------------------------------------------------
static bool i2cPresente(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Scansione completa: 112 indirizzi. Serve a capire un cablaggio sbagliato
// ("c'e' qualcosa, ma non dove me lo aspetto"), quindi si fa SU RICHIESTA -
// comando 's' o pulsante nella pagina - e non dentro sensorsBegin(), che ha
// solo bisogno di sapere se i tre indirizzi noti rispondono. Con il timeout
// di Wire abbassato a 10 ms l'intera scansione costa comunque poco: misurata
// il 2026-08-22, non si vede nemmeno come buco fra due letture.
static uint8_t i2cScan() {
  Serial.println(F("  scansione bus I2C 0x08-0x77..."));
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    found++;
    Serial.printf("    0x%02X  ", addr);
    if (addr == ADDR_AHT20) {
      Serial.print(F("<- AHT20 (temperatura + umidita')"));
    } else if (addr == ADDR_BMP_LOW || addr == ADDR_BMP_HIGH) {
      Serial.print(F("<- BMP280 (pressione)"));
    } else {
      Serial.print(F("<- sconosciuto"));
    }
    Serial.println();
  }
  if (found == 0) {
    Serial.println(F("    NESSUN dispositivo."));
    Serial.println(F("    Controlla, in quest'ordine: GND in comune; VCC sul pad D3;"));
    Serial.println(F("    SDA su D4 e SCL su D2 non invertiti; stagno freddo sui 4 fili."));
  }
  return found;
}

// Il registro 0xD0 e' l'ID del chip, allo stesso indirizzo su BMP280 e BME280.
// Serve a distinguere il modulo atteso dal clone: se qui esce 0x60 il chip e'
// un BME280 e Adafruit_BMP280::begin() fallisce, ma il modulo non e' rotto -
// basta cambiare libreria (Adafruit BME280) e si guadagna una seconda umidita'.
static uint8_t readChipId(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(0xD0);
  if (Wire.endTransmission() != 0) return 0;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0;

  const uint8_t id = Wire.read();
  Serial.printf("  chip ID a 0x%02X: 0x%02X  ", addr, id);
  switch (id) {
    case 0x58:
      Serial.println(F("= BMP280, quello atteso"));
      break;
    case 0x60:
      Serial.println(F("= BME280! ha anche l'umidita': serve Adafruit BME280, non BMP280"));
      break;
    case 0x56:
    case 0x57:
      Serial.println(F("= BMP280 campione di preproduzione, va bene lo stesso"));
      break;
    default:
      Serial.println(F("= sconosciuto"));
      break;
  }
  return id;
}

// ---------------------------------------------------------------------------
// Init dei due chip
// ---------------------------------------------------------------------------
// Chiamata sia al boot sia dopo ogni power-cycle: e' il punto in cui, quando
// arrivera' il deep sleep, si aggancera' anche il risveglio. Il sensore va
// reinizializzato OGNI volta che riceve corrente, non solo al primo avvio.
static void sensorsBegin() {
  s_ahtOk = false;
  s_bmpOk = false;
  s_bmpAddr = 0;
  s_bmpChipId = 0;

  const uint32_t tInizio = millis();
  Serial.println(F("--- init sensore ---"));
  sensorPower(true);

  // Solo i tre indirizzi che ci interessano, non tutto il bus: init corto
  // (vedi la nota su i2cScan()). Se qui non risponde nessuno, il comando 's'
  // fa la scansione completa e dice se c'e' qualcosa altrove.
  Serial.printf("  presenti: 0x38 %s, 0x76 %s, 0x77 %s\n",
                i2cPresente(ADDR_AHT20)    ? "si" : "no",
                i2cPresente(ADDR_BMP_LOW)  ? "si" : "no",
                i2cPresente(ADDR_BMP_HIGH) ? "si" : "no");

  s_ahtOk = aht.begin(&Wire);
  Serial.print(F("  AHT20  : "));
  Serial.println(s_ahtOk ? F("ok") : F("NON risponde"));

  for (uint8_t i = 0; i < 2 && !s_bmpOk; i++) {
    const uint8_t addr = (i == 0) ? ADDR_BMP_LOW : ADDR_BMP_HIGH;
    const uint8_t id = readChipId(addr);
    if (id == 0) continue;               // niente a questo indirizzo
    s_bmpChipId = id;
    if (bmp.begin(addr)) {
      s_bmpOk = true;
      s_bmpAddr = addr;
    }
  }
  Serial.print(F("  BMP280 : "));
  if (s_bmpOk) {
    Serial.printf("ok a 0x%02X\n", s_bmpAddr);
    // MODE_NORMAL va bene per un banco sempre alimentato. In Fase 4, a
    // batteria, questo diventera' MODE_FORCED: una conversione su richiesta e
    // poi il chip torna a dormire da solo.
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,    // temperatura
                    Adafruit_BMP280::SAMPLING_X16,   // pressione
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
  } else {
    Serial.println(F("NON risponde"));
  }

  s_failStreak = 0;
  // Quanto e' durato: e' il tempo in cui loop() non gira, quindi il tempo in
  // cui il web server non risponde. Se un giorno risalisse sopra il secondo,
  // e' qui che si e' rotto qualcosa.
  Serial.printf("  init completato in %lu ms\n", (unsigned long)(millis() - tInizio));
  Serial.println(F("--------------------"));
}

static void sensorsRestart(const __FlashStringHelper *perche) {
  Serial.print(F("\n[power-cycle] "));
  Serial.println(perche);
  s_powerCycles++;
  sensorPower(false);
  sensorsBegin();
}

// ---------------------------------------------------------------------------
// Lettura
// ---------------------------------------------------------------------------
// Punto di rugiada con la formula di Magnus. Qui serve solo a dire "il numero
// e' plausibile": la logica vera del "quando aprire le finestre" andra' in un
// ventilation.h header-only, come comfort.h di EnvNode_C3, e ragionera' di
// umidita' assoluta, non di sola temperatura.
static float dewPointC(float tempC, float rhPct) {
  const float a = 17.62f, b = 243.12f;
  const float g = (a * tempC) / (b + tempC) + logf(rhPct / 100.0f);
  return (b * g) / (a - g);
}

static float readBatteryV() {
#if BATTERY_ADC_ENABLED
  // Il partitore dimezza, quindi la tensione di cella e' il doppio di quella
  // al pin. analogReadMilliVolts() applica gia' la calibrazione di fabbrica
  // dell'ADC, che a occhio nudo vale qualche decina di mV di errore in meno.
  uint32_t mv = 0;
  for (uint8_t i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATTERY);
  return (mv / 8.0f) * 2.0f / 1000.0f;
#else
  return NAN;
#endif
}

static void readAndPrint() {
  float tAht = NAN, rh = NAN, tBmp = NAN, hPa = NAN;
  bool ahtRead = false, bmpRead = false;

  if (s_ahtOk) {
    sensors_event_t evHum, evTemp;
    if (aht.getEvent(&evHum, &evTemp)) {
      tAht = evTemp.temperature;
      rh = evHum.relative_humidity;
      ahtRead = !isnan(tAht) && !isnan(rh);
    }
  }
  if (s_bmpOk) {
    tBmp = bmp.readTemperature();
    hPa = bmp.readPressure() / 100.0f;
    bmpRead = !isnan(tBmp) && !isnan(hPa) && hPa > 300.0f && hPa < 1200.0f;
  }

  Serial.printf("[%7.1f s] ", millis() / 1000.0f);

  if (ahtRead) {
    Serial.printf("AHT20 %6.2f C %5.1f %%RH  |  ", tAht, rh);
  } else {
    Serial.print(F("AHT20   --.-- C  --.- %RH  |  "));
  }

  if (bmpRead) {
    Serial.printf("BMP280 %6.2f C %8.2f hPa", tBmp, hPa);
  } else {
    Serial.print(F("BMP280   --.-- C   ---.-- hPa"));
  }

  if (ahtRead) {
    Serial.printf("  |  rugiada %5.1f C", dewPointC(tAht, rh));
  }
  Serial.println();

  // Le due temperature vengono da chip diversi sulla stessa basetta: devono
  // somigliarsi. Uno scarto grosso e' il primo sintomo di un chip che scalda,
  // di una lettura sballata o di un modulo montato male - roba che a un
  // controllo distratto sembrerebbe comunque una misura buona.
  if (ahtRead && bmpRead && fabsf(tAht - tBmp) > 2.0f) {
    Serial.printf("            ! le due temperature differiscono di %.2f C: da guardare\n",
                  fabsf(tAht - tBmp));
  }

  // Aggiorna lo stato che legge la web UI. Solo con dati validi: un campione
  // fallito non deve cancellare l'ultima lettura buona.
  if (ahtRead) {
    s_tempAht = tAht;
    s_hum = rh;
    s_dewpoint = dewPointC(tAht, rh);
    trackMinMax(tAht, s_tMin, s_tMax);
    trackMinMax(rh, s_hMin, s_hMax);
  }
  if (bmpRead) {
    s_tempBmp = tBmp;
    s_press = hPa;
    trackMinMax(hPa, s_pMin, s_pMax);
  }

  // Nello storico va SOLO quello che e' stato letto davvero: passare il valore
  // di una lettura scartata (una pressione fuori range, per dire) sporcherebbe
  // la media dello slot con un dato che abbiamo gia' deciso di non credere.
  histFeed(ahtRead ? tAht : NAN, ahtRead ? rh : NAN, bmpRead ? hPa : NAN);
  aggiornaTrend();

  if (ahtRead || bmpRead) {
    s_hasReading = true;
    s_lastGoodMs = millis();
    s_reads++;
    s_failStreak = 0;
  } else {
    s_readErrors++;
    if (++s_failStreak >= MAX_FAIL_STREAK) {
      sensorsRestart(F("troppe letture fallite di fila"));
    }
  }

  // Trasmissione all'hub. Si manda ANCHE una lettura fallita, con NAN sui
  // canali mancanti: "sono vivo ma il sensore non risponde" e' una
  // informazione, il silenzio no - da fuori sarebbe indistinguibile da un
  // nodo morto, che e' proprio cio' che l'hub sta cercando di riconoscere.
  // Non fa niente finche' non si e' associati, quindi niente da proteggere
  // con un if. Attenzione: puo' trattenere loop() fino a ~1 s quando l'hub
  // non risponde (ritentativi di EspNowLink), accettabile a questa cadenza.
  const float battV = readBatteryV();
  hub_send_measure(ahtRead ? tAht : NAN,
                   ahtRead ? rh   : NAN,
                   bmpRead ? hPa  : NAN,
                   isnan(battV) ? 0 : (uint16_t)(battV * 1000.0f));
}

// ---------------------------------------------------------------------------
// Ganci per la web UI (dichiarati in web_ui.h)
// ---------------------------------------------------------------------------
void app_get_snapshot(app_snapshot_t &out) {
  out.powered     = s_powered;
  out.aht_ok      = s_ahtOk;
  out.bmp_ok      = s_bmpOk;
  out.bmp_addr    = s_bmpAddr;
  out.bmp_chip_id = s_bmpChipId;

  out.has_reading = s_hasReading;
  out.temp_aht    = s_tempAht;
  out.hum         = s_hum;
  out.temp_bmp    = s_tempBmp;
  out.press_hpa   = s_press;
  out.dewpoint    = s_dewpoint;

  out.temp_min  = s_tMin;  out.temp_max  = s_tMax;
  out.hum_min   = s_hMin;  out.hum_max   = s_hMax;
  out.press_min = s_pMin;  out.press_max = s_pMax;

  out.press_sea = isnan(s_press) ? NAN
                                 : forecast_sea_level_hpa(s_press, s_altitudineM);
  out.delta_3h  = s_delta3h;
  out.trend     = (uint8_t)s_trend;

  out.reads        = s_reads;
  out.read_errors  = s_readErrors;
  out.power_cycles = s_powerCycles;
  out.battery_v    = readBatteryV();

  out.intervallo_s = s_intervalloS;
  out.altitudine_m = s_altitudineM;

  out.espnow_ok      = hub_ready();
  out.espnow_paired  = hub_paired();
  out.espnow_channel = hub_channel();
  out.espnow_sent    = hub_sent_ok();
  out.espnow_failed  = hub_sent_fail();
  out.espnow_hub_mac = hub_hub_mac();
}

// ---------------------------------------------------------------------------
// Configurazione dalla pagina
// ---------------------------------------------------------------------------
bool app_set_intervallo_s(uint32_t secondi) {
  if (secondi < INTERVALLO_MIN_S || secondi > INTERVALLO_MAX_S) return false;
  s_intervalloS = secondi;
  Preferences p;
  if (p.begin("meteonode", false)) {
    p.putULong("intervallo", secondi);
    p.end();
  }
  Serial.printf("[config] intervallo di misura: %lu s\n", (unsigned long)secondi);
  return true;
}

bool app_set_altitudine_m(float metri) {
  if (isnan(metri) || metri < ALTITUDINE_MIN_M || metri > ALTITUDINE_MAX_M) return false;
  s_altitudineM = metri;
  Preferences p;
  if (p.begin("meteonode", false)) {
    p.putFloat("altitudine", metri);
    p.end();
  }
  Serial.printf("[config] altitudine: %.1f m\n", metri);
  return true;
}

bool app_calibra_altitudine(float pressioneLivelloMareHpa) {
  // Serve una pressione misurata adesso: senza, non c'e' niente da cui
  // ricavare la differenza di quota.
  if (isnan(s_press)) return false;
  const float m = forecast_altitude_from_sea_level(s_press, pressioneLivelloMareHpa);
  if (isnan(m)) return false;
  Serial.printf("[config] calibrazione: misurati %.2f hPa, bollettino %.2f hPa -> %.0f m\n",
                s_press, pressioneLivelloMareHpa, m);
  return app_set_altitudine_m(m);
}

// ---------------------------------------------------------------------------
// Accesso allo storico per la web UI
// ---------------------------------------------------------------------------
uint16_t app_hist_count()    { return s_hCount; }
uint32_t app_hist_period_s() { return HIST_PERIODO_S; }
time_t   app_hist_last_ts()  { return histUltimoTs(); }

bool app_hist_at(uint16_t back, float* tempC, float* humPct, float* pressHpa) {
  return histAt(back, tempC, humPct, pressHpa);
}

const char* app_fw_version() { return FW_VERSION; }

uint32_t app_eta_ultima_lettura_s() {
  if (!s_hasReading) return 0;
  return (millis() - s_lastGoodMs) / 1000UL;
}

// ---------------------------------------------------------------------------
// Comandi: esecuzione e coda
// ---------------------------------------------------------------------------
// Le tre azioni vere. Da seriale si chiamano dritte, perche' non c'e' nessuno
// che aspetta una risposta.
static void doScan() {
  if (s_powered) {
    i2cScan();
  } else {
    Serial.println(F("  sensore spento: riaccendilo prima di scansionare"));
  }
}

static void doTogglePower() {
  if (s_powered) {
    s_powerCycles++;
    sensorPower(false);
    s_ahtOk = false;
    s_bmpOk = false;
    Serial.println(F("\n[alimentazione] sensore SPENTO."));
  } else {
    sensorsBegin();
  }
}

// Dalla pagina web invece si ACCODA soltanto: l'handler HTTP deve rispondere
// e liberare il server sincrono, non tenerlo fermo mezzo secondo (il perche'
// esteso e' in testa a web_ui.h). Non serve nessuna protezione fra task: chi
// scrive la bandiera e' l'handler HTTP, che gira anche lui dentro loop() via
// net_loop(). Qui il problema non e' la concorrenza, e' la latenza.
enum : uint8_t { CMD_NESSUNO = 0, CMD_SCAN, CMD_RIAVVIA, CMD_ALIMENTAZIONE };
static uint8_t s_pendingCmd = CMD_NESSUNO;

void app_cmd_scan()           { s_pendingCmd = CMD_SCAN; }
void app_cmd_restart_sensor() { s_pendingCmd = CMD_RIAVVIA; }
void app_cmd_toggle_power()   { s_pendingCmd = CMD_ALIMENTAZIONE; }

static void runPendingCmd() {
  const uint8_t c = s_pendingCmd;
  if (c == CMD_NESSUNO) return;
  s_pendingCmd = CMD_NESSUNO;
  switch (c) {
    case CMD_SCAN:          doScan(); break;
    case CMD_RIAVVIA:       sensorsRestart(F("richiesto dalla pagina web")); break;
    case CMD_ALIMENTAZIONE: doTogglePower(); break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Comandi da seriale
// ---------------------------------------------------------------------------
static void printHelp() {
  Serial.println(F("\ncomandi:  s = scansione I2C   r = power-cycle + re-init"
                   "   p = accendi/spegni sensore   h = aiuto\n"));
}

static void handleSerial() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 's':
        doScan();
        break;
      case 'r':
        sensorsRestart(F("richiesto da seriale"));
        break;
      case 'p':
        doTogglePower();
        if (!s_powered) {
          Serial.println(F("                Le letture devono sparire; se continuano,"));
          Serial.println(F("                VCC non e' su D3/GPIO5."));
        }
        break;
      case 'h':
        printHelp();
        break;
      default:
        break;   // \r, \n e battiture a caso
    }
  }
}

// ---------------------------------------------------------------------------
// OTA: eco del progresso sulla Serial
// ---------------------------------------------------------------------------
static void onOtaProgress(int percent, const char* what) {
  static int last = -1;
  if (percent == last) return;
  last = percent;
  if (percent < 0) Serial.printf("[OTA] %s: in corso...\n", what);
  else             Serial.printf("[OTA] %s: %d%%\n", what, percent);
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Su questa board Serial e' la USB CDC nativa del C3, non una UART. Se un
  // monitor si collega e poi se ne va - o se il cavo non c'e' proprio, che e'
  // la condizione NORMALE di un nodo a batteria - le Serial.print() restano
  // bloccate ad aspettare che l'host svuoti il buffer, fino a un timeout
  // interno. E finche' bloccano, loop() non gira: niente web server, niente
  // OTA, niente letture. Con timeout a zero le scritture si buttano via
  // invece di aspettare, e il nodo continua a vivere anche senza nessuno che
  // legga la Serial.
  //
  // Trovato in prova il 2026-08-22, e vale la pena raccontarlo perche' il
  // sintomo non somiglia alla causa: da rete la pagina moriva subito dopo
  // ogni comando che stampa molte righe insieme (riaccensione, scansione),
  // mentre da monitor seriale collegato le stesse identiche operazioni
  // erano istantanee e pulite. Sembrava un problema di web server; era la
  // Serial che, senza un lettore, si portava dietro tutto il resto.
  // Solo sulle board con USB nativa: li' Serial e' la CDC del chip e senza un
  // host che svuoti il buffer le print() bloccano. Sull'ESP32 "classico" la
  // Serial e' una UART vera, che quel metodo non ce l'ha proprio - senza
  // questa guardia lo sketch non compilerebbe nemmeno per la DOIT.
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif

  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);   // CDC: aspetta il monitor

  Serial.println(F("\n\n============================================"));
  Serial.println(F("MeteoNode_C3 - bring-up sensore AHT20+BMP280"));
  Serial.println(F("============================================"));
  Serial.printf("firmware: %s\n", FW_VERSION);
  Serial.printf("pin: VCC=D3/GPIO%u (commutato)  SDA=D4/GPIO%u  SCL=D2/GPIO%u\n",
                PIN_SENSOR_PWR, PIN_SDA, PIN_SCL);

  settingsLoad();
  Serial.printf("config: misura ogni %lu s, altitudine %.0f m\n",
                (unsigned long)s_intervalloS, s_altitudineM);

  // Orario PRIMA della rete, nell'ordine obbligato che vale in tutto il repo:
  // rtctime_begin() -> seedFromBuild() -> (WiFi) -> onWifiConnected(). Senza
  // il seme da build-time l'orologio ripartirebbe dal 1970, e lo storico si
  // ritroverebbe tutti i campioni in un unico slot preistorico.
  rtctime_begin(TZ_POSIX);
  rtctime_seedFromBuild();

  // Il sensore PRIMA della rete: se il WiFi non c'e', net_begin() ci mette
  // 15 s a rinunciare, e in quei 15 s vogliamo gia' sapere se il modulo
  // risponde. Sono anche due guasti indipendenti, e conviene poterli leggere
  // separati invece che come un unico "non funziona".
  sensorsBegin();

  net_setOtaProgressCb(onOtaProgress);
  net_begin();
  web_ui_begin();

  if (net_isConnected()) {
    rtctime_onWifiConnected();
    Serial.printf("pagina di stato: http://%s/\n", net_ip().c_str());
  } else {
    Serial.println(F("WiFi non connesso: ritento in background, il sensore intanto lavora."));
  }

  // ESP-NOW dopo net_begin(): il canale dipende dall'essere connessi o meno
  // all'AP (vedi hub_link.h). Non blocca e non e' fatale se fallisce.
  hub_begin(NODE_NAME);

  printHelp();
}

void loop() {
  net_loop();          // ArduinoOTA + richieste web: a OGNI giro, o l'OTA muore
  runPendingCmd();     // dopo net_loop(): la risposta HTTP e' gia' partita
  handleSerial();
  hub_loop();          // ESP-NOW: HELLO finche' non associato all'hub

  // NTP a OGNI riconnessione, non solo alla prima: un nodo che riprende la
  // rete dopo ore altrimenti resterebbe con l'orologio alla deriva, e con lui
  // tutto l'asse dei tempi dello storico. Il net_ota di EnvNode_C3 non espone
  // un flag di riconnessione (quello di Timelapse_XIAO si'), quindi il
  // fronte lo rileviamo qui.
  static bool eraConnesso = false;
  const bool oraConnesso = net_isConnected();
  if (oraConnesso && !eraConnesso) rtctime_onWifiConnected();
  eraConnesso = oraConnesso;

  // La prima lettura si fa SUBITO, senza aspettare un intervallo intero:
  // altrimenti con l'intervallo a un'ora il nodo resterebbe senza dati per
  // un'ora dopo ogni riavvio, e chi apre la pagina lo crederebbe rotto.
  static bool primaLettura = true;
  if (primaLettura || millis() - s_lastReadMs >= s_intervalloS * 1000UL) {
    primaLettura = false;
    s_lastReadMs = millis();
    if (s_powered && (s_ahtOk || s_bmpOk)) {
      readAndPrint();
    } else if (s_powered && s_reads == 0) {
      // Nessuno dei due ha risposto all'init: riprova ogni tanto invece di
      // restare muto, cosi' si puo' ritoccare una saldatura a scheda accesa e
      // vedere il momento in cui il contatto tiene.
      sensorsRestart(F("nessun chip al boot, riprovo"));
    }
  }
}
