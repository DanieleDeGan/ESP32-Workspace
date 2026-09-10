#pragma once

// ---------------------------------------------------------------------
//  Pagina di stato e API HTTP dell'hub, registrate sul WebServer che vive
//  in net_ota (net_server()): un server solo per tutto, come su
//  projects/EnvNode_C3/. Va chiamata DOPO net_begin(), che e' quella che
//  il server lo crea.
//
//  Il WebServer del core e' SINCRONO: mentre serve una richiesta, loop()
//  e' fermo, quindi nessuno preleva i DATA dei nodi dal driver ESP-NOW
//  (che tiene solo l'ultimo) e l'OTA non avanza. Gli handler qui dentro
//  devono restare corti, e i file grossi passano da streamFileLimitato().
// ---------------------------------------------------------------------

#include <Arduino.h>
#include <time.h>   // time_t: app_epd_ultimo_full_ts()

void web_ui_begin();

// ---------------------------------------------------------------------
//  Ganci verso il .ino: web_ui non tiene stato proprio, lo chiede a chi
//  ce l'ha gia'. Tutto il resto (nodi, SD, orario, rete) lo legge dai
//  moduli, senza duplicare niente.
// ---------------------------------------------------------------------
const char* app_fw_version();      // versione del firmware, per /api/stato
const char* app_hub_nome();        // nome con cui l'hub si presenta ai nodi
uint32_t    app_righe_scritte();   // righe di CSV scritte da questo avvio
uint32_t    app_scartati_ora();    // DATA arrivati prima del primo sync NTP
uint32_t    app_boot_count();      // avvii totali (NVS): sopravvive al riavvio
const char* app_reset_reason();    // perche' e' ripartita: SW, PANIC, BROWNOUT...
uint32_t    app_scritture_ko();    // DATA che la card ha rifiutato
uint32_t    app_epd_refresh();     // refresh del pannello da questo avvio
uint32_t    app_epd_ultimo_ms();   // quanto e' costato l'ultimo refresh
uint32_t    app_epd_orologio_ms(); // e quanto costa il solo orologio
uint32_t    app_epd_parziali_da_full();  // parziali accumulati (pagina nodi)
time_t      app_epd_ultimo_full_ts();    // ora dell'ultimo completo (0 = mai)

// Quanto e' durato il giro piu' lungo, e dove. NON comprende il disegno del
// pannello, che e' legittimamente lungo e ha i suoi contatori: qui c'e' solo
// cio' che nel loop puo' bloccare senza doverlo.
uint32_t    app_loop_max_ms();
const char* app_loop_max_dove();
time_t      app_loop_max_ts();
uint32_t    app_loop_lenti();

// Una riga nel diario degli eventi su card, con il tetto per tipo che sta in
// MeteoHub_S3.ino (dieci l'ora, poi il conto delle soppresse). Serve al web
// server: un invio troncato e' una transizione come un nodo che tace, e finche'
// finiva solo sulla Serial -- che su questa scheda non e' leggibile -- era
// diagnostica scritta dove nessuno la puo' leggere.
void        app_evento(const char* tipo, const char* dettaglio);

// Il watchdog del loop e' iscritto? Un watchdog configurato male e uno giusto
// sono indistinguibili da fuori finche' non serve, e allora e' tardi.
bool        app_wdt_armato();
uint32_t    app_wdt_timeout_s();

// Fabbrica il guasto che il watchdog deve riprendere: blocca il loop() per
// `secondi` senza alimentarlo. ACCODA e basta -- il blocco lo fa il loop, o la
// risposta HTTP non partirebbe mai. Vedi la nota in fondo a MeteoHub_S3.ino.
void        app_chiedi_blocco(uint32_t secondi);

// La tela: i 15.000 byte che sono finiti sul vetro, nel formato dei .bin
// (1 bit per pixel, MSB per primo, 50 byte per riga, 1 = bianco). Non e' una
// copia dello stato del pannello: e' lo stato del pannello, perche' ogni
// disegno passa di li' e non c'e' nessun'altra strada per arrivare al vetro.
const uint8_t* app_tela();
size_t         app_tela_bytes();

// true mentre il pannello e' fermo per le ore di silenzio.
bool app_pannello_sospeso();

// Il nome dell'immagine sorteggiata per la notte (modalita' PAG_SIL_CARD),
// stringa vuota se non ce n'e' una a schermo. Va esposto: quell'immagine non
// e' una pagina dell'elenco, quindi "corrente" descrive il modello e non il
// vetro -- senza questo campo /api/pannello direbbe una cosa vera su una
// cosa che non e' quella mostrata.
const char* app_silenzio_immagine();

// Quante volte il pannello NON e' stato ridisegnato perche' il contenuto era
// identico a quello gia' a schermo.
uint32_t app_refresh_evitati();

// Comandi verso il pannello: si ACCODANO e li esegue il loop(). Un refresh
// sono ~2,2 s: dentro un handler HTTP terrebbe fermo il server, l'OTA e il
// prelievo dei DATA dei nodi dal driver ESP-NOW, che tiene solo l'ultimo.
void app_chiedi_refresh();

// Rimette l'immagine della notte: azzera lo stato del silenzio e lascia
// rientrare il loop(). Vale solo dentro la fascia.
void app_chiedi_rientro_silenzio();

// La taratura della marea barometrica: quanti giorni sono entrati nella media,
// quando e' stata aggiornata, e l'ampiezza da picco a picco. Servono a vedere
// da FUORI se la correzione e' viva: una tabella che non si aggiorna piu' e una
// che non e' mai partita si somigliano troppo.
uint16_t app_marea_giorni();
time_t   app_marea_ultima();
float    app_marea_ampiezza();

// Il livello della batteria in cinque tacche, 0..5, o 0xFF se quel nodo non ne
// ha (alimentato dalla rete, o partitore non cablato).
//
// Lo calcola il FIRMWARE e non chi disegna, ed e' il punto: le soglie e
// l'isteresi vivono in un posto solo. Il pannello le usa per le tacche, il web
// per la stessa icona -- una terza copia in JavaScript sarebbe divergere
// silenziosamente il giorno che si ritoccano.
uint8_t app_batteria_livello(int indice, uint16_t mv);

// E le soglie stesse (mV, crescenti) con il margine d'isteresi, per la
// stessa ragione: /api/nodi le esporta e la pagina /batteria ci disegna le
// bande del grafico invece di ricopiarsi la curva di scarica.
const uint16_t* app_batteria_soglie();
uint16_t app_batteria_isteresi();

// La tabella intera, 24 scarti orari in CENTESIMI di hPa (indice = ora
// locale). L'ampiezza dice se la taratura e' viva; questa dice cosa sta
// facendo, ed e' l'unica con cui si puo' controllare la FASE.
void     app_marea_tab(int8_t out[24]);
void app_chiedi_pagina(uint8_t indice);

// Quanti giorni di riepilogo sono stati chiusi da quando la scheda e' accesa.
uint32_t app_riepiloghi_scritti();
uint32_t app_loop_giri_s();

// Cancella il riepilogo (di un nodo, o di tutti con nodo vuoto) e lo fa
// ricostruire dal loop(), un giorno per giro. Stessa regola del refresh: la
// richiesta arriva dall'handler, il lavoro lo fa il loop.
bool app_riepilogo_ricalcola(const char* nodo);
