#pragma once
#include <Arduino.h>

// =====================================================================
//  web_ui — interfaccia web del nodo timelapse
//
//  Registra le proprie rotte sul WebServer di net_ota (net_server()):
//
//    GET  /                      pagina di controllo (self-contained, no CDN)
//    GET  /stream                video MJPEG live (per inquadrare)
//    GET  /snapshot.jpg          un fotogramma al volo (non salvato)
//    POST /api/scatta            scatta e SALVA su microSD
//    GET  /api/stato             stato in JSON (rete, ora, SD, timelapse, camera)
//    POST /api/config            intervallo, finestra oraria, spazio, camera
//    GET  /api/giorni            elenco JSON dei giorni presenti sulla card
//    GET  /api/foto?g=GIORNO     elenco JSON delle foto di un giorno
//    GET  /foto?g=GIORNO&f=NOME  scarica/visualizza una foto
//    GET  /log?g=GIORNO          scarica il CSV del giorno
//    POST /api/elimina?g=&f=     cancella una foto
//    POST /api/elimina-giorno?g= cancella tutte le foto di un giorno
//
//  Tutte le rotte passano dalla stessa basic-auth di /update
//  (WEB_USER/WEB_PASS in secrets.h).
//
//  La galleria mostra le foto a piena risoluzione rimpicciolite dal
//  browser (niente miniature sulla card: generarle costerebbe una seconda
//  codifica JPEG per scatto e il doppio dello spazio). Con `loading=lazy`
//  se ne scaricano solo quelle visibili, ma su una giornata da migliaia di
//  scatti la pagina resta pesante: e' il compromesso scelto.
//
//  NOTA sullo streaming: il WebServer del core e' sincrono, quindi finche'
//  un client tiene aperto /stream la scheda non risponde ad altre
//  richieste (OTA compreso). Lo stream si ferma da solo dopo
//  WEB_STREAM_MAX_MS. Nel frattempo il timer del timelapse resta vivo
//  perche' il ciclo dello stream chiama app_pump() ad ogni frame — ma gli
//  scatti in quella finestra vengono saltati, vedi app_pump() nel .ino.
// =====================================================================

// Durata massima di uno stream MJPEG: una scheda del browser dimenticata
// aperta non deve rendere il nodo inaggiornabile per sempre.
#define WEB_STREAM_MAX_MS (5 * 60 * 1000UL)

// Da chiamare in setup() dopo net_begin().
void web_ui_begin();

// ---------------------------------------------------------------------
//  Ganci implementati nello sketch (.ino): la web UI legge lo stato e
//  chiede le azioni attraverso queste funzioni. Orario, SD e camera li
//  interroga invece direttamente (rtc_time.h / storage.h / camera.h),
//  senza duplicarne lo stato qui.
// ---------------------------------------------------------------------
const char* app_node_name();
const char* app_fw_version();

bool     app_enabled();                        // timelapse attivo?
void     app_set_enabled(bool on);
uint32_t app_interval_s();
void     app_set_interval_s(uint32_t seconds);

// Finestra oraria giornaliera (ore locali 0..23). inizio == fine = sempre
// attivo. inizio > fine = finestra che scavalca la mezzanotte.
int  app_window_start();
int  app_window_end();
void app_set_window(int startHour, int endHour);
bool app_in_window();                          // adesso siamo dentro?

// Cosa fare quando la card e' quasi piena.
#define APP_FULL_STOP 0    // smette di scattare (default: non cancella niente)
#define APP_FULL_RING 1    // elimina il giorno piu' vecchio e continua
int      app_full_policy();
void     app_set_full_policy(int policy);
uint32_t app_min_free_mb();                    // soglia di "quasi piena"
void     app_set_min_free_mb(uint32_t mb);

long        app_next_shot_s();      // secondi al prossimo scatto, -1 se fermo
uint32_t    app_shots_session();    // scatti riusciti da questa accensione
const char* app_last_shot_iso();    // "" se nessuno scatto da questa accensione
const char* app_last_error();       // "" se l'ultimo scatto e' andato bene

// Scatta subito e salva su microSD (bottone della UI). day_out/name_out
// ricevono cartella e nome del file salvato.
bool app_capture_now(char* day_out, size_t day_cap, char* name_out, size_t name_cap);

// Chiamata ad ogni frame dello stream MJPEG: tiene vivo il timer del
// timelapse mentre il web server e' occupato a trasmettere.
void app_pump();
