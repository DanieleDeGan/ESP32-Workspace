/**
 * EspNowLink.h
 *
 * Livello di comunicazione ESP-NOW generico per reti di sensori/attuatori:
 * una board fa da "hub" (ruolo LINK_NODE_HUB) e riceve dati da moduli
 * indipendenti ("nodi", altri tipi di link_node_type_t) via radio, senza
 * bisogno di router/broker.
 *
 * Non dipende da LVGL, dal display o da una scheda specifica: gira su
 * qualunque ESP32, con o senza schermo, da entrambi i lati del collegamento.
 *
 * Costruita sopra la libreria ufficiale ESP_NOW/ESP_NOW_Peer del core Arduino
 * ESP32 (non su esp_now.h grezzo): quella gestisce gia' peer, canale WiFi e
 * scoperta di mittenti sconosciuti (onNewPeer).
 *
 * USO TIPICO — nodo sensore (es. temperatura):
 *   Link_Init(LINK_NODE_SENSOR_TEMPERATURE, "TempCucina");
 *   Link_OnMessage(la_tua_callback);   // opzionale
 *   // in loop():
 *   Link_Node_Poll();
 *   if (Link_Node_IsPaired()) { ... Link_Node_SendData(&msg); ... }
 *
 * USO TIPICO — hub:
 *   Link_Init(LINK_NODE_HUB, "Hub");
 *   Link_OnMessage(la_tua_callback);
 *   Link_Hub_SetPairingMode(true);   // finestra di associazione, es. da un bottone touch
 *   // in loop():
 *   Link_Hub_Poll();
 *   // Link_Hub_GetPeerInfo(...) per leggere lo stato dei nodi associati
 *
 * NOTA IMPORTANTE: chiamare le funzioni Link_Node_* dopo aver inizializzato
 * come hub (o viceversa) logga un warning e non fa nulla — Link_Init()
 * decide il ruolo una volta sola.
 */

#ifndef ESPNOW_LINK_H
#define ESPNOW_LINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Canale WiFi fisso usato da ESP-NOW: hub e nodi DEVONO usare lo stesso.
 *  Vale per i dispositivi che NON sono connessi a un access point (il caso
 *  normale di una rete di nodi a batteria). Un nodo che invece sta anche su
 *  una rete WiFi non puo' scegliere il canale — glielo impone l'AP: vedi
 *  Link_InitEx() e ESPNOW_LINK_CHANNEL_CURRENT. */
#define ESPNOW_LINK_CHANNEL 6

/** Canale "quello attuale": non tocca il canale WiFi e registra i peer con
 *  channel 0 (semantica ESP-NOW: "usa il canale corrente"). Da passare a
 *  Link_InitEx() quando il dispositivo e' anche connesso a un AP. */
#define ESPNOW_LINK_CHANNEL_CURRENT 0

#define LINK_PROTOCOL_VERSION 1
#define LINK_NAME_LEN 16

/** Tipo del nodo mittente. Aggiungere nuovi tipi in coda non rompe la
 *  compatibilita' col protocollo esistente (e' solo un uint8_t sul wire). */
typedef enum {
    LINK_NODE_UNKNOWN = 0,
    LINK_NODE_HUB = 1,
    LINK_NODE_SENSOR_TEMPERATURE = 2,
    LINK_NODE_SENSOR_WATER_LEVEL = 3,
    LINK_NODE_SENSOR_BATTERY = 4,
    LINK_NODE_ACTUATOR = 5,
    LINK_NODE_CAMERA = 6,           // nodo camera + PIR (vedi starters/XIAO_S3_Camera/)
} link_node_type_t;

typedef enum {
    LINK_MSG_HELLO = 0,     // nodo -> broadcast: "esisto, associami" (ripetuto finche' non associato)
    LINK_MSG_WELCOME = 1,   // hub -> nodo, unicast: pairing accettato
    LINK_MSG_DATA = 2,      // nodo -> hub, unicast: lettura sensore
    LINK_MSG_COMMAND = 3,   // hub -> nodo, unicast: comando attuatore/config
} link_msg_type_t;

/**
 * Payload generico del protocollo, 37 byte (ben sotto ESP_NOW_MAX_DATA_LEN,
 * 250 byte in v1.0 — usato deliberatamente invece del limite v2.0/1470 per
 * restare compatibili con qualunque chip ESP32 finisca per fare da nodo).
 *
 * __attribute__((packed)) e' necessario, non solo prudente: il payload
 * ricevuto arriva come puntatore grezzo senza garanzie di allineamento —
 * senza packed, un accesso a seq/value[] su un buffer disallineato e'
 * undefined behavior. Il parsing (vedi link_peer.cpp) copia sempre con
 * memcpy in una struct locale naturalmente allineata, non fa mai cast diretto
 * del buffer ricevuto.
 */
typedef struct __attribute__((packed)) {
    uint8_t  protocol_version;      // deve combaciare con LINK_PROTOCOL_VERSION
    uint8_t  msg_type;              // link_msg_type_t
    uint8_t  node_type;             // link_node_type_t del MITTENTE
    char     name[LINK_NAME_LEN];   // nome amichevole, significativo in HELLO
    uint32_t seq;
    uint16_t battery_mv;            // 0 = alimentazione fissa/sconosciuta
    float    value[3];              // significato dipende da node_type/msg_type
                                     // (es. DATA temperatura: value[0] = gradi C;
                                     //  COMMAND attuatore: value[0] = 1.0/0.0 on/off)
} link_message_t;

/**
 * Notifica applicativa opzionale per ogni messaggio valido ricevuto
 * (HELLO/WELCOME/DATA/COMMAND), sia lato hub sia lato nodo.
 *
 * NOTA: niente RSSI qui. La libreria ESP_NOW ufficiale su cui si basa
 * EspNowLink lo espone solo nel callback di scoperta di un peer sconosciuto
 * (onNewPeer), non nel dispatch onReceive() dei peer gia' aggiunti — fornirlo
 * qui sarebbe disponibile solo per il primissimo messaggio (l'HELLO/WELCOME
 * di pairing) e non per i DATA/COMMAND successivi, un'incoerenza non
 * necessaria per l'uso attuale.
 */
typedef void (*Link_MessageCb)(const uint8_t mac[6], const link_message_t *msg);

/**
 * Inizializza WiFi in modalita' STA sul canale fisso ESPNOW_LINK_CHANNEL e
 * avvia ESP-NOW. Decide il ruolo (hub se self_type == LINK_NODE_HUB,
 * altrimenti nodo) e registra internamente il gestore onNewPeer giusto.
 * Da chiamare una sola volta in setup(). Ritorna false se l'init WiFi/ESP-NOW
 * fallisce.
 */
bool Link_Init(link_node_type_t self_type, const char *self_name);

/**
 * Come Link_Init(), ma con il canale ESP-NOW esplicito.
 *
 *  - channel 1..13  -> forza quel canale con esp_wifi_set_channel() e registra
 *                      i peer su quel canale. E' quello che fa Link_Init()
 *                      passando ESPNOW_LINK_CHANNEL.
 *  - channel ESPNOW_LINK_CHANNEL_CURRENT (0) -> NON tocca il canale WiFi e
 *                      registra i peer con channel 0 ("canale corrente").
 *
 * Lo 0 serve ai dispositivi che sono anche connessi a un access point: li' il
 * canale lo decide l'AP e un esp_wifi_set_channel() farebbe cadere la
 * connessione. ATTENZIONE: ESP-NOW resta una radio sola, quindi in quel caso
 * TUTTI i partecipanti devono trovarsi sul canale dell'AP — l'hub va
 * inizializzato con lo stesso numero (es. Link_InitEx(LINK_NODE_HUB, "Hub",
 * canale_del_tuo_AP)), altrimenti non si sentono. Il caso d'uso e' il nodo
 * camera di starters/XIAO_S3_Camera/, che deve stare sul WiFi per web UI e OTA.
 */
bool Link_InitEx(link_node_type_t self_type, const char *self_name, uint8_t channel);

/** Registra la callback opzionale per ogni messaggio valido ricevuto. */
void Link_OnMessage(Link_MessageCb cb);

/* ---------------------------------------------------------------------- */
/* Ruolo nodo/periferica                                                   */
/* ---------------------------------------------------------------------- */

/** Da chiamare ad ogni iterazione di loop(): finche' non associato, invia
 *  un HELLO in broadcast a intervalli regolari. */
void Link_Node_Poll(void);

/** true se il nodo ha ricevuto un WELCOME e conosce il MAC dell'hub. */
bool Link_Node_IsPaired(void);

/**
 * Invia (unicast) un messaggio DATA all'hub associato. La funzione timbra
 * automaticamente protocol_version/msg_type=LINK_MSG_DATA/node_type/name/seq:
 * il chiamante deve riempire solo battery_mv/value[]. Attende la conferma di
 * consegna e ritenta automaticamente (fino a 3 volte) se l'ACK non arriva —
 * la perdita occasionale di frame unicast e' normale su ESP-NOW, non solo
 * tra chip di generazioni diverse. Bloccante per al massimo ~1s in caso di
 * ritentativi. Ritorna false se non ancora associato o se anche l'ultimo
 * tentativo fallisce.
 */
bool Link_Node_SendData(link_message_t *msg);

/* ---------------------------------------------------------------------- */
/* Ruolo hub/master                                                        */
/* ---------------------------------------------------------------------- */

/** Da chiamare ad ogni iterazione di loop(): invia i WELCOME accodati dal
 *  callback di ricezione (che non invia mai direttamente, per restare
 *  breve). */
void Link_Hub_Poll(void);

/**
 * Mentre true, un MAC sconosciuto viene accettato automaticamente come nuovo
 * peer in DUE casi; mentre false, entrambi vengono ignorati.
 *
 *  - HELLO in broadcast: il nodo non si crede associato e cerca un hub. E' il
 *    caso normale della prima accensione.
 *  - DATA in unicast: il nodo si crede gia' associato e sta parlando proprio a
 *    noi, ma il registro peer (che vive solo in RAM) l'ha perso a un riavvio
 *    dell'hub. Senza questo secondo caso il nodo resterebbe invisibile per
 *    sempre - non manda piu' HELLO - e in modo SILENZIOSO da entrambe le
 *    parti, perche' l'ACK di ESP-NOW e' di livello radio: il nodo continua a
 *    contare i propri invii come riusciti mentre l'hub non vede niente.
 *
 * Finche' il registro peer non viene persistito, tenere quindi una finestra di
 * pairing aperta per qualche minuto ad ogni avvio dell'hub e' il modo per far
 * rientrare i nodi gia' noti senza intervento.
 */
void Link_Hub_SetPairingMode(bool enable);

/**
 * Aggiunge un peer gia' noto SENZA passare dalla finestra di pairing e senza
 * mandargli un WELCOME. Serve a chi tiene una propria persistenza del registro
 * (che qui vive solo in RAM) e vuole ripristinarlo all'avvio: un nodo riletto
 * da NVS e' gia' associato dal proprio punto di vista, quindi non va
 * risvegliato con un WELCOME che non ha chiesto.
 *
 * Il peer riparte senza dati (hasData=false): valori e contatori NON vanno
 * persistiti, o dopo un riavvio l'hub mostrerebbe come "attuale" una lettura
 * vecchia di giorni — esattamente il guasto che il rilevamento del nodo muto
 * serve a evitare. Meglio un "in attesa del primo DATA" onesto.
 *
 * Ritorna false se il ruolo non e' hub, se il registro e' pieno o se quel MAC
 * c'e' gia'.
 */
bool Link_Hub_AddPeer(const uint8_t mac[6], link_node_type_t type, const char *name);

/**
 * Dimentica un peer: lo toglie dal registro e dal driver ESP-NOW. Da usare
 * quando una scheda viene sostituita — l'identita' di un nodo e' il suo MAC,
 * quindi la scheda vecchia resterebbe per sempre in elenco come nodo muto.
 *
 * ATTENZIONE: se il nodo e' ancora vivo e si crede associato non manda piu'
 * HELLO, quindi per riprenderlo servira' una finestra di pairing aperta (o un
 * suo riavvio). Ritorna false se quel MAC non era nel registro.
 */
bool Link_Hub_ForgetPeer(const uint8_t mac[6]);

/** Numero di nodi attualmente associati. */
int Link_Hub_GetPeerCount(void);

/**
 * Informazioni sul peer associato all'indice `index` (0..Link_Hub_GetPeerCount()-1).
 * `last_data_out` viene azzerato (tutti campi a 0) se non e' ancora arrivato
 * nessun messaggio DATA da quel nodo. Ritorna false se l'indice non e' valido.
 */
bool Link_Hub_GetPeerInfo(int index, uint8_t mac_out[6], link_node_type_t *type_out,
                          char name_out[LINK_NAME_LEN], uint32_t *last_seen_ms_out,
                          link_message_t *last_data_out);

/** Invia (unicast) un messaggio COMMAND al nodo con quel MAC, con conferma
 *  di consegna e ritentativi automatici (vedi Link_Node_SendData()).
 *  Ritorna false se il MAC non e' un peer associato o se anche l'ultimo
 *  tentativo fallisce. */
bool Link_Hub_SendCommand(const uint8_t mac[6], link_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_LINK_H
