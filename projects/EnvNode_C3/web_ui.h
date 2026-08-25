#pragma once
#include <Arduino.h>
#include <time.h>

// =====================================================================
//  web_ui — dashboard del nodo ambientale
//
//  Registra le proprie rotte sul WebServer condiviso di net_ota (net_server()):
//
//    GET  /                     dashboard (da /www/dashboard.html su SD se
//                               presente, altrimenti quella di default
//                               incorporata nel firmware, HTML/CSS/JS
//                               inline, no CDN)
//    GET  /dashboard-upload     form per caricare una dashboard.html
//                               personalizzata su SD (SEMPRE la versione in
//                               PROGMEM, indipendente da cosa c'e' sulla SD:
//                               e' la via di recupero se quella caricata a
//                               mano e' rotta)
//    POST /dashboard-upload     riceve il file e lo scrive su
//                               /www/dashboard.html
//    POST /dashboard-ripristina elimina /www/dashboard.html: si torna alla
//                               dashboard di default
//    GET  /api/stato            stato corrente in JSON
//    GET  /api/giorni           elenco date disponibili nei log (JSON array)
//    GET  /api/giorno?d=YYYY-MM-DD   campioni di un giorno, in streaming (JSON array)
//    GET  /api/scarica?d=YYYY-MM-DD scarica il CSV grezzo di un giorno
//    POST /api/elimina?d=YYYY-MM-DD elimina il file di log di un giorno
//    GET  /api/config           configurazione corrente in JSON
//    POST /api/config           imposta parametri (query args)
//    GET  /nodi                 pagina dei nodi ESP-NOW ricevuti (PROGMEM,
//                               come /dashboard-upload: resta raggiungibile
//                               anche con una dashboard personalizzata
//                               vecchia o rotta sulla SD)
//    GET  /api/nodi             stato dei nodi remoti in JSON
//    POST /api/pairing?on=1|0[&s=secondi]   apre/chiude la finestra di
//                               associazione (vedi remote_nodes.h)
//    POST /api/nodi/dimentica?mac=...       toglie un nodo da registro e NVS
//    GET  /api/nodi/giorni?nodo=<nome>      registri su SD di quel nodo
//    GET  /api/nodi/scarica?nodo=&d=        CSV grezzo di un giorno
//
//  Tutte dietro la stessa basic-auth di /update (net_webAuthOk()).
//  /update resta di net_ota.cpp, invariata.
//
//  web_ui.cpp legge direttamente settings_get()/sd_logger.*/rtc_time.*/
//  comfort_eval(): sono gia' moduli disaccoppiati, non servono ganci per
//  quelli. I ganci qui sotto coprono solo le letture correnti e i min/max
//  dall'ultimo avvio, che vivono nello stato del .ino.
// =====================================================================

// Da chiamare in setup(), dopo net_begin().
void web_ui_begin();

// ---------------------------------------------------------------------
//  Ganci implementati nello sketch (.ino).
// ---------------------------------------------------------------------
bool  app_has_reading();          // false finche' non c'e' stata una lettura DHT11 valida

// "Correnti" = media mobile delle ultime letture valide (smoothing contro il
// rumore del DHT11): stesso valore mostrato sull'OLED, per coerenza fra le
// due UI. Il CSV logga invece sempre il dato grezzo (vedi sd_logger.h).
float app_temp_now();
float app_hum_now();

// Estremi "dal'ultimo avvio" (stato in RAM, azzerato a ogni boot) sul dato
// GREZZO (non sulla media mobile): sono i valori realmente misurati.
float  app_temp_min();
time_t app_temp_min_ts();
float  app_temp_max();
time_t app_temp_max_ts();
float  app_hum_min();
time_t app_hum_min_ts();
float  app_hum_max();
time_t app_hum_max_ts();

uint32_t    app_dht_errors();
const char* app_fw_version();

// Diagnostica del giro di loop(), misurata nel .ino. Serve a distinguere
// "la scheda si e' riavviata" da "la scheda e' rimasta ferma dentro una
// chiamata": nei CSV le due cose hanno lo stesso identico aspetto - un
// buco - e il 2026-08-25 distinguerle e' costato un'indagine incrociando
// uptime, log locale e log dei nodi. Ora la risposta e' in /api/stato.
uint32_t    app_loop_max_ms();     // la fase piu' lunga vista da questo avvio
const char* app_loop_max_dove();   // quale fase era ("web", "nodi", "campione"...)
time_t      app_loop_max_ts();     // quando, 0 se non c'e' ancora nulla
uint32_t    app_loop_lenti();      // quante fasi hanno sforato LOOP_LENTO_MS
