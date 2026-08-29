#pragma once
#include <Arduino.h>
#include <EspNowLink.h>

// =====================================================================
//  hub_link — il nodo camera visto dall'hub (ESP-NOW)
//
//  Sottile strato sopra EspNowLink (libreria condivisa in libraries/):
//   - si presenta come nodo LINK_NODE_CAMERA e si associa all'hub,
//   - manda un DATA ad ogni rilevamento di movimento,
//   - riceve i COMMAND dell'hub e li consegna allo sketch nel contesto di
//     loop(), non dentro la callback radio.
//
//  IL CANALE. ESP-NOW ha una radio sola: hub e nodi devono stare sullo
//  stesso canale WiFi. Questo nodo pero' e' anche connesso all'access point
//  (gli servono web UI e OTA), e li' il canale lo decide l'AP. Percio'
//  hub_begin() guarda se la connessione WiFi e' attiva: se si', registra i
//  peer con ESPNOW_LINK_CHANNEL_CURRENT e non forza nulla, restando sul
//  canale dell'AP. => L'HUB VA INIZIALIZZATO SULLO STESSO CANALE DELL'AP:
//      Link_InitEx(LINK_NODE_HUB, "Hub", canale_del_tuo_AP);
//  al posto della Link_Init() che usa il canale fisso 6. Se il router
//  cambia canale (molti lo fanno da soli), il nodo lo segue al riavvio ma
//  l'hub no: e' il punto fragile di questa architettura, tienilo presente
//  se un giorno le notifiche smettono di arrivare.
//
//  Senza WiFi (nessun AP raggiungibile) lo sketch ripiega sul canale fisso
//  ESPNOW_LINK_CHANNEL: in quel caso l'hub standard va bene com'e'.
// =====================================================================

// Comandi accettati dall'hub. Viaggiano in msg->value[0] (che e' un float:
// l'hub scrive msg.value[0] = HUB_CMD_ARM, qui si ri-arrotonda a int).
#define HUB_CMD_DISARM  0   // sorveglianza off
#define HUB_CMD_ARM     1   // sorveglianza on
#define HUB_CMD_CAPTURE 2   // scatta subito una foto

typedef void (*hub_command_cb_t)(int cmd);

// Inizializza ESP-NOW come nodo camera. Va chiamata DOPO net_begin(): il
// canale dipende dall'essere o meno connessi all'AP (vedi sopra).
bool hub_begin(const char* node_name);

// Canale su cui sta effettivamente parlando ESP-NOW (per la web UI: e' il
// numero che deve combaciare con quello dell'hub). Chiesto alla radio ad ogni
// chiamata: se l'AP si sposta, il nodo lo segue e il numero cambia sotto.
uint8_t hub_channel();

// Da chiamare a ogni giro di loop(): manda gli HELLO finche' non associato
// ed esegue i COMMAND ricevuti.
void hub_loop();

bool hub_ready();    // ESP-NOW inizializzato
bool hub_paired();   // associato all'hub (WELCOME ricevuto)

// Callback per i comandi dell'hub, invocata da hub_loop() (quindi nel
// contesto di loop(): puo' fare lavoro lento senza bloccare la radio).
void hub_on_command(hub_command_cb_t cb);

// Notifica un rilevamento: DATA con value[0]=n. evento, value[1]=indice
// della foto (-1 se non salvata), value[2]=1 se la foto e' finita su SD.
// BLOCCANTE: attende la conferma di consegna e ritenta, quindi puo'
// trattenere loop() per qualche secondo se l'hub non risponde.
bool hub_notify_motion(uint32_t event_n, long photo_index, bool photo_saved);

// MAC dell'hub associato in forma leggibile, "-" se non ancora associato.
const char* hub_mac_str();
