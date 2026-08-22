#pragma once
#include <Arduino.h>

// =====================================================================
//  web_ui — pagina di prova del nodo sensore
//
//  Registra le proprie rotte sul WebServer condiviso di net_ota
//  (net_server()):
//
//    GET  /                 pagina di stato (HTML/CSS/JS inline, niente CDN)
//    GET  /api/stato        stato corrente in JSON, quello che la pagina
//                           ricarica da sola ogni 2 s
//    GET  /api/comando?c=   gli stessi comandi del monitor seriale:
//                           c=scan | c=riavvia | c=alimentazione
//
//  Tutte dietro la stessa basic-auth di /update (net_webAuthOk()).
//  /update resta di net_ota.cpp, invariata.
//
//  A COSA SERVE: leggere il sensore senza cavo USB. Con il nodo a batteria
//  la Serial non c'e' piu', quindi tutto quello che il bring-up stampa sul
//  monitor deve poter uscire anche da qui - comandi compresi, o il primo
//  test a batteria diventa cieco.
//
//  I COMANDI SI ACCODANO, non si eseguono nell'handler. Il WebServer del
//  core e' sincrono e serve un client per volta: una scansione I2C o un
//  power-cycle dentro la richiesta bloccano il server per quasi un
//  secondo, e con la pagina che fa polling ogni 2 s le richieste si
//  accavallano finche' qualcuna va in timeout. Da fuori sembra che il
//  nodo sia morto, mentre il comando e' stato eseguito benissimo (visto
//  davvero in prova il 2026-08-22: il contatore dei power-cycle
//  avanzava, ma la risposta non tornava). Quindi app_cmd_*() alza solo
//  una bandiera, loop() la raccoglie, e la pagina vede l'effetto al giro
//  di polling successivo. E' la stessa regola che il repo applica gia'
//  ai callback ESP-NOW e LVGL: la callback accoda, loop() lavora.
//
//  Conseguenza da conoscere: il WebServer serve UN client per volta.
//  Telefono e PC insieme sulla stessa pagina funzionano, ma si alternano;
//  se una scheda del browser resta appesa, l'altra aspetta il timeout.
// =====================================================================

// Da chiamare in setup(), dopo net_begin().
void web_ui_begin();

// ---------------------------------------------------------------------
//  Ganci implementati nello sketch (.ino)
// ---------------------------------------------------------------------
// Uno snapshot unico invece di quindici getter separati come in
// EnvNode_C3: qui i campi non sono stati indipendenti ma pezzi della
// STESSA lettura, presa tutta insieme ogni 2 s. Restituirli uno per uno
// vorrebbe dire poter servire una pagina che mescola la temperatura di un
// campione con l'umidita' del successivo.
struct app_snapshot_t {
  // stato dei chip
  bool     powered;        // il sensore ha corrente (D3/GPIO5 alto)
  bool     aht_ok;         // ha risposto all'init
  bool     bmp_ok;
  uint8_t  bmp_addr;       // 0x76 o 0x77, 0 se non trovato
  uint8_t  bmp_chip_id;    // 0x58 = BMP280, 0x60 = BME280, 0 = non letto

  // ultima lettura valida
  bool     has_reading;
  float    temp_aht;       // gradi C
  float    hum;            // %RH
  float    temp_bmp;       // gradi C, dall'altro chip
  float    press_hpa;
  float    dewpoint;       // gradi C

  // estremi dall'ultimo avvio (RAM, azzerati a ogni boot)
  float    temp_min, temp_max;
  float    hum_min,  hum_max;
  float    press_min, press_max;

  // pressione e previsione
  float    press_sea;      // riportata al livello del mare, confrontabile
                           // con i bollettini; NAN se non c'e' lettura
  float    delta_3h;       // variazione della pressione sulle ultime 3 ore
  uint8_t  trend;          // forecast_trend_t, gia' filtrato con isteresi

  // contatori e diagnostica
  uint32_t reads;          // letture riuscite
  uint32_t read_errors;    // letture fallite (totale, non di fila)
  uint32_t power_cycles;   // quante volte il sensore e' stato riavviato
  float    battery_v;      // NAN finche' il partitore su D1/GPIO3 non c'e'

  // configurazione corrente
  uint32_t intervallo_s;
  float    altitudine_m;
};

void app_get_snapshot(app_snapshot_t &out);

// ---------------------------------------------------------------------
//  Configurazione, modificabile dalla pagina
// ---------------------------------------------------------------------
// Ogni setter valida, persiste in NVS e aggiorna la copia in RAM; torna
// false senza toccare niente se il valore e' fuori range.
bool app_set_intervallo_s(uint32_t secondi);
bool app_set_altitudine_m(float metri);

// Calibrazione comoda: invece dell'altitudine si inserisce la pressione al
// livello del mare letta da un bollettino locale, e l'altitudine la ricava
// il firmware dalla pressione che sta misurando in questo momento. Evita
// di doverla cercare su una mappa, e soprattutto evita di indovinarla.
bool app_calibra_altitudine(float pressioneLivelloMareHpa);

// ---------------------------------------------------------------------
//  Storico per i grafici
// ---------------------------------------------------------------------
// Il buffer sta nel .ino; qui si espone solo quel che serve a servirlo.
// L'asse dei tempi si ricostruisce da ultimo istante + passo, cosi' non si
// spediscono 720 timestamp per niente.
uint16_t app_hist_count();       // quanti slot ci sono (0..720)
uint32_t app_hist_period_s();    // passo della griglia, in secondi
time_t   app_hist_last_ts();     // istante dello slot piu' recente

// back = 0 e' il piu' recente. false se lo slot non esiste o e' vuoto;
// i singoli valori possono comunque essere NAN se quel canale mancava.
bool app_hist_at(uint16_t back, float* tempC, float* humPct, float* pressHpa);

// Comandi, gemelli di quelli da seriale.
void app_cmd_scan();
void app_cmd_restart_sensor();
void app_cmd_toggle_power();

const char* app_fw_version();

// Da quanti secondi risale l'ultima lettura. Con l'intervallo configurabile
// fino a un'ora, un numero senza eta' sarebbe ambiguo: la pagina deve poter
// dire "27,8 gradi, letti 43 minuti fa" e non far credere che sia di adesso.
uint32_t app_eta_ultima_lettura_s();
