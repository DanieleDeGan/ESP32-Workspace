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

void web_ui_begin();

// ---------------------------------------------------------------------
//  Ganci verso il .ino: web_ui non tiene stato proprio, lo chiede a chi
//  ce l'ha gia'. Tutto il resto (nodi, SD, orario, rete) lo legge dai
//  moduli, senza duplicare niente.
// ---------------------------------------------------------------------
const char* app_fw_version();      // versione del firmware, per /api/stato
const char* app_hub_nome();        // nome con cui l'hub si presenta ai nodi
uint32_t    app_righe_scritte();   // righe di CSV scritte da questo avvio
uint32_t    app_epd_refresh();     // refresh del pannello da questo avvio
uint32_t    app_epd_ultimo_ms();
 uint32_t    app_epd_orologio_ms();

// Comandi verso il pannello: si ACCODANO e li esegue il loop(). Un refresh
// sono ~2,2 s: dentro un handler HTTP terrebbe fermo il server, l'OTA e il
// prelievo dei DATA dei nodi dal driver ESP-NOW, che tiene solo l'ultimo.
void app_chiedi_refresh();
void app_chiedi_pagina(uint8_t indice); // e quanto costa il solo orologio   // quanto e' costato l'ultimo refresh
