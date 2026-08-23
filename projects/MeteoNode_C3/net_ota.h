#pragma once
#include <Arduino.h>
#include <WebServer.h>

// =====================================================================
//  net_ota — WiFi + OTA (boilerplate isolato, variante "server condiviso"
//  dello stesso modulo che sta negli starter C3_OLED_OTA e XIAO_S3_Camera)
//
//  Porta su:
//   1) la connessione WiFi (station),
//   2) ArduinoOTA  -> upload da Arduino IDE (Tools > Port > porta di rete),
//   3) un web server con pagina  /update  -> upload del .bin da browser.
//
//  A differenza del template C3_OLED_OTA (dove il server e' privato al
//  modulo), qui la rotta "/" NON e' registrata: la home e' la dashboard
//  (web_ui.cpp), che si aggancia allo stesso WebServer tramite
//  net_server(). Di norma questo file non si tocca.
// =====================================================================

// Callback opzionale invocata durante un aggiornamento OTA (log/UI).
// percent = 0..100, oppure -1 se sconosciuto (upload web a dimensione
// ignota). Impostala PRIMA di net_begin().
typedef void (*ota_progress_cb_t)(int percent, const char* what);
void net_setOtaProgressCb(ota_progress_cb_t cb);

// Da chiamare una volta in setup(), dopo Serial.
void net_begin();

// Da chiamare a ogni giro di loop(): gestisce ArduinoOTA + richieste web.
void net_loop();

// Stato utile alla UI / all'applicazione.
bool    net_isConnected();
String  net_ip();
int     net_rssi();
uint8_t net_channel();   // canale WiFi corrente: e' anche quello dell'ESP-NOW

// Diagnostica della connessione. Serve a capire DA REMOTO perche' il nodo si
// e' riavviato: il watchdog in net_ota.cpp riavvia la scheda dopo 15 minuti
// di AP assente, e senza questi contatori quel riavvio e' indistinguibile da
// un crash o da un calo di alimentazione.
//
// Il punto e' che l'ESP-NOW continua a consegnare anche con l'AP giu' (gli
// serve il canale, non l'associazione): il nodo puo' restare irraggiungibile
// via web per un quarto d'ora, riavviarsi da solo, e dall'altra parte i dati
// arrivano puntuali come se niente fosse. Successo due volte il 2026-08-23 sul
// nodo su DOIT, e ce ne siamo accorti solo ricostruendolo a posteriori dai
// salti di seq nel log dell'hub.
uint32_t net_wifi_drops();        // cadute dell'AP da questo boot
uint32_t net_wifi_down_max_s();   // la piu' lunga assenza osservata, in secondi
uint32_t net_wifi_down_now_s();   // da quanto e' giu' ADESSO (0 se connesso)

// Il WebServer condiviso: web_ui.cpp ci registra sopra le proprie rotte.
// Registrarle dopo net_begin() e' lecito (WebServer consulta la lista delle
// rotte a ogni richiesta, non solo all'avvio).
WebServer& net_server();

// Basic-auth condivisa (WEB_USER/WEB_PASS di secrets.h): true se la
// richiesta e' autorizzata oppure se l'autenticazione e' disattivata.
// Chi la usa deve rispondere con server.requestAuthentication() se false.
bool net_webAuthOk();
