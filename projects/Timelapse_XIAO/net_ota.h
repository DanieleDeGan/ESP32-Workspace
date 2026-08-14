#pragma once
#include <Arduino.h>
#include <WebServer.h>

// =====================================================================
//  net_ota — WiFi + OTA (boilerplate isolato, gemello di quello del nodo
//  camera e dello starter C3)
//
//  Porta su:
//   1) la connessione WiFi (station) + watchdog di riconnessione,
//   2) ArduinoOTA  -> upload da Arduino IDE (Tools > Port > porta di rete),
//   3) un web server con pagina  /update  -> upload del .bin da browser.
//
//  La rotta "/" NON e' registrata qui: la home e' la web UI del timelapse
//  (web_ui.cpp), che si aggancia allo stesso WebServer tramite
//  net_server(). Di norma questo file non si tocca.
//
//  Il WiFi serve anche all'orario (rtc_time): senza rete il nodo continua
//  a scattare, ma con l'ora stimata da build-time.
// =====================================================================

// Callback opzionale invocata durante un aggiornamento OTA (log/UI).
// percent = 0..100, oppure -1 se sconosciuto (upload web a dimensione
// ignota). Impostala PRIMA di net_begin().
typedef void (*ota_progress_cb_t)(int percent, const char* what);
void net_setOtaProgressCb(ota_progress_cb_t cb);

// Da chiamare una volta in setup(), dopo Serial.
void net_begin();

// Da chiamare a ogni giro di loop(): watchdog WiFi + ArduinoOTA +
// richieste web.
void net_loop();

// Stato utile alla UI / all'applicazione.
bool    net_isConnected();
String  net_ip();
int     net_rssi();
uint8_t net_channel();

// true al primo giro dopo che il WiFi si e' (ri)connesso: lo sketch la
// usa per rilanciare il sync NTP ad ogni riconnessione, non solo alla
// prima (vedi rtc_time.h). Consumata alla lettura.
bool net_takeReconnectedFlag();

// Il WebServer condiviso: web_ui.cpp ci registra sopra le proprie rotte.
// Registrarle dopo net_begin() e' lecito (WebServer consulta la lista
// delle rotte a ogni richiesta, non solo all'avvio).
WebServer& net_server();

// Basic-auth condivisa (WEB_USER/WEB_PASS di secrets.h): true se la
// richiesta e' autorizzata oppure se l'autenticazione e' disattivata.
// Chi la usa deve rispondere con server.requestAuthentication() se false.
bool net_webAuthOk();
