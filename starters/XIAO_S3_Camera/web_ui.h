#pragma once
#include <Arduino.h>

// =====================================================================
//  web_ui — interfaccia web del nodo camera
//
//  Registra le proprie rotte sul WebServer di net_ota (net_server()):
//
//    GET  /                  pagina di controllo (self-contained, no CDN)
//    GET  /stream            video MJPEG live
//    GET  /snapshot.jpg      un fotogramma al volo (non salvato su SD)
//    POST /api/scatta        scatta e SALVA su microSD
//    GET  /api/stato         stato in JSON (rete, hub, SD, PIR, camera)
//    POST /api/config        armato, cooldown, risoluzione, qualita', flip
//    GET  /api/foto          elenco JSON delle foto su microSD
//    GET  /foto?f=NOME       scarica/visualizza una foto
//    POST /api/elimina?f=..  cancella una foto
//
//  Tutte le rotte passano dalla stessa basic-auth di /update
//  (WEB_USER/WEB_PASS in secrets.h).
//
//  NOTA sullo streaming: il WebServer del core e' sincrono, quindi finche'
//  un client tiene aperto /stream la scheda non risponde ad altre richieste
//  (OTA compreso). Lo stream si ferma da solo dopo WEB_STREAM_MAX_MS e la
//  pagina lo tiene spento di default. Nel frattempo PIR ed ESP-NOW restano
//  vivi perche' il ciclo dello stream chiama app_pump() ad ogni frame.
// =====================================================================

// Durata massima di uno stream MJPEG: una scheda del browser dimenticata
// aperta non deve rendere il nodo inaggiornabile per sempre.
#define WEB_STREAM_MAX_MS (5 * 60 * 1000UL)

// Da chiamare in setup() dopo net_begin().
void web_ui_begin();

// Quante volte un invio di file e' stato troncato perche' il client aveva
// smesso di leggere. Zero e' il valore normale; se sale, qualcuno chiude le
// pagine a meta' scaricamento — ed e' l'unico modo di accorgersene, perche'
// senza il taglio il sintomo sarebbe un PIR che sembra non far scattare.
uint32_t web_invii_interrotti();

// ---------------------------------------------------------------------
//  Ganci implementati nello sketch (.ino): la web UI legge lo stato e
//  chiede le azioni attraverso queste funzioni, senza conoscere il PIR.
// ---------------------------------------------------------------------
const char* app_node_name();           // nome del nodo (anche per l'hub)
const char* app_fw_version();          // stringa di versione mostrata nella UI
bool     app_armed();
void     app_set_armed(bool armed);
uint32_t app_cooldown_s();
void     app_set_cooldown_s(uint32_t seconds);
uint32_t app_motion_count();
long     app_seconds_since_motion();   // -1 se nessun rilevamento da quando e' accesa
bool     app_pir_warm();               // false durante il riscaldamento del PIR

// Scatta una foto e la salva su microSD. `sorgente` finisce nel CSV
// ("WEB" per gli scatti manuali). name_out riceve il nome del file.
bool app_capture_and_store(const char* sorgente, char* name_out, size_t name_cap);

// Chiamata ad ogni frame dello stream MJPEG: tiene vivi PIR ed ESP-NOW
// mentre il web server e' occupato a trasmettere.
void app_pump();
