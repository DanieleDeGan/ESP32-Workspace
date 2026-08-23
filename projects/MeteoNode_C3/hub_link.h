#pragma once
#include <Arduino.h>
#include <EspNowLink.h>

// =====================================================================
//  hub_link — il nodo meteo visto dall'hub (ESP-NOW)
//
//  Strato sottile sopra EspNowLink (libreria condivisa in libraries/): si
//  presenta come nodo, si associa all'hub e manda un DATA ad ogni ciclo di
//  misura. Gemello di quello di starters/XIAO_S3_Camera/, con la stessa
//  logica sul canale ma senza i comandi (l'hub non ne manda ancora; il
//  posto dove aggiungerli, quando servira' il "resta sveglio" del deep
//  sleep, e' una Link_OnMessage che accoda e loop() che esegue).
//
//  ---------------------------------------------------------------------
//  IL CANALE
//  ---------------------------------------------------------------------
//  ESP-NOW ha una radio sola: hub e nodi devono stare sullo stesso canale.
//  Finche' questo nodo e' connesso all'access point (gli servono web UI e
//  OTA) il canale lo decide il router, quindi hub_begin() usa
//  ESPNOW_LINK_CHANNEL_CURRENT e non forza niente. E' la stessa scelta che
//  fa l'hub in projects/EnvNode_C3/remote_nodes.cpp: entrambi seguono l'AP,
//  e per questo funziona senza toccare il router.
//
//  Senza WiFi si ripiega sul canale fisso ESPNOW_LINK_CHANNEL (6). Oggi e'
//  solo una rete di sicurezza, ma diventera' la strada normale col deep
//  sleep, quando il nodo non si connettera' piu' all'AP: da li' in avanti
//  il canale andra' saputo e impostato, ed e' il motivo per cui l'hub lo
//  espone su /api/nodi. Se il router cambiasse canale da solo, un nodo che
//  dorme diventerebbe muto in silenzio.
//  ---------------------------------------------------------------------
//
//  CONTRATTO DEI TRE FLOAT (node_type LINK_NODE_SENSOR_TEMPERATURE):
//      value[0] = temperatura in gradi C   (dall'AHT20)
//      value[1] = umidita' relativa in %
//      value[2] = pressione in hPa, GREZZA (vedi hub_send_measure)
//  Si riusa il tipo "temperatura" invece di aggiungerne uno nuovo in coda
//  all'enum: e' una scelta rimandabile, e finche' i tipi di nodo sono due
//  aggiungerne uno cambierebbe la libreria condivisa senza guadagno.
// =====================================================================

// Inizializza ESP-NOW come nodo. Da chiamare DOPO net_begin(): il canale
// dipende dall'essere o meno connessi all'AP (vedi sopra). Ritorna false se
// l'init fallisce — lo sketch deve continuare a funzionare lo stesso, come
// gia' fa senza sensore.
bool hub_begin(const char* node_name);

// Da chiamare a ogni giro di loop(): manda gli HELLO in broadcast finche'
// non associato. Non blocca.
void hub_loop();

bool    hub_ready();     // ESP-NOW inizializzato
bool    hub_paired();    // associato all'hub (WELCOME ricevuto)
uint8_t hub_channel();   // canale su cui parla davvero

// MAC dell'hub associato, "-" finche' non lo si conosce.
const char* hub_hub_mac();

// Manda una misura. Un canale non letto va passato come NAN, e si trasmette
// lo stesso: un nodo che dice "sono vivo ma il sensore non risponde" e' una
// informazione, il silenzio no — da fuori sarebbe indistinguibile da un nodo
// morto, che e' esattamente cio' che l'hub sta cercando di riconoscere.
// battery_mv = 0 significa "non misurata" (il partitore non e' cablato).
//
// BLOCCANTE fino a ~1 s: Link_Node_SendData() attende la conferma di
// consegna e ritenta. Accettabile a cadenza di minuti, da tenere presente
// se un giorno l'intervallo scendesse a pochi secondi.
bool hub_send_measure(float tempC, float humPct, float pressHpa, uint16_t battery_mv);

// Contatori, per la pagina di stato: sono l'unico modo di accorgersi che la
// radio ha smesso di consegnare mentre tutto il resto sembra a posto.
uint32_t hub_sent_ok();
uint32_t hub_sent_fail();
