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

/**
 * Cambia il canale radio DOPO l'init, e riallinea tutti i peer gia'
 * registrati. Ritorna false se il canale non e' valido (1..13) o se la radio
 * lo rifiuta.
 *
 * A cosa serve: hub e nodi devono stare sullo stesso canale, e un nodo in
 * deep sleep quel canale se lo porta dietro in RTC memory. Se l'access point
 * lo cambia da solo — cosa che fa, per scansare le reti dei vicini — l'hub lo
 * segue riassociandosi e il nodo che dorme no: diventa muto senza accorgersene,
 * perche' l'ACK che non arriva e' l'unico sintomo. Con questa funzione il nodo
 * puo' provare gli altri canali dentro lo stesso risveglio, invece di
 * aspettare che una rete di sicurezza lo riavvii.
 *
 * ATTENZIONE: chiamarla su un dispositivo CONNESSO a un access point fa
 * cadere la connessione — la radio e' una sola e il canale lo detta l'AP. E'
 * pensata per chi sta su ESP-NOW e basta (nodo a batteria, WiFi spento).
 * Un hub connesso all'AP non deve usarla: gli basta seguire l'AP con
 * ESPNOW_LINK_CHANNEL_CURRENT.
 */
bool Link_SetChannel(uint8_t channel);

/**
 * Riallinea i peer al canale su cui la radio si trova GIA', senza toccarla.
 * Ritorna false se il canale corrente non si riesce a leggere.
 *
 * E' il rovescio di Link_SetChannel(): li' la radio si sposta e i peer la
 * seguono, qui la radio e' gia' al posto giusto e sono i peer a essere
 * rimasti indietro. Per questo — al contrario di Link_SetChannel() — questa
 * si PUO' chiamare su un dispositivo connesso a un access point: non manda
 * nessun comando alla radio.
 *
 * Quando serve: ESP-NOW inizializzato PRIMA che il WiFi fosse connesso.
 * Succede davvero dopo un'interruzione di corrente, quando la scheda si
 * riaccende insieme al router e non lo trova entro il timeout: ripiega sul
 * canale fisso, e quando il WiFi arriva (canale dell'AP) i peer restano dove
 * erano nati. Il guasto e' silenzioso e la diagnostica sembra a posto — la
 * pagina del nodo mostra il canale della RADIO, che e' giusto — ma il nodo
 * non parla piu' con nessuno, e nemmeno la finestra di associazione lo
 * recupera, perche' anche il broadcast esce verso il canale sbagliato.
 *
 * Dopo la chiamata i peer stanno su ESPNOW_LINK_CHANNEL_CURRENT, cioe'
 * seguono la radio da soli: un router che si sposta di nuovo non li lascia
 * indietro una seconda volta.
 */
bool Link_SyncPeersToRadio(void);

/**
 * SOLO PER PROVE: sposta i peer su `channel` senza toccare la radio, cioe'
 * fabbrica il disallineamento che Link_SyncPeersToRadio() ripara. Ritorna
 * quanti peer ha spostato, -1 se il canale non e' valido (1..13).
 *
 * Da qui in avanti il dispositivo e' muto per davvero, finche' qualcuno non
 * ripara: e' il punto. Serve a provare la riparazione senza aspettare la
 * prossima interruzione di corrente — una funzione che si attiva sola una
 * volta all'anno, e mai sotto osservazione, e' una funzione che non si sa se
 * esiste. Non tocca il WiFi, quindi chi la chiama resta raggiungibile in rete.
 */
int Link_TestMisalignPeers(uint8_t channel);

/** Canale su cui si sta parlando davvero. 0 = "canale corrente", cioe' quello
 *  deciso da chi ha configurato la radio (tipicamente l'associazione all'AP). */
uint8_t Link_GetChannel(void);

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
 * Riprende a parlare con un hub gia' noto, saltando del tutto HELLO/WELCOME.
 *
 * Serve a un nodo che si risveglia da deep sleep: la RAM e' persa, quindi si
 * crede non associato e ricomincerebbe da capo il pairing - due secondi buoni
 * fra un HELLO e il successivo (LINK_HELLO_INTERVAL_MS), piu' il tempo che
 * l'hub impiega ad accodare e spedire il WELCOME dal suo loop(). Su un nodo
 * che sta sveglio pochi secondi per volta quella e' la parte piu' lunga e piu'
 * incerta del ciclo, ed e' tempo di radio accesa, cioe' batteria.
 *
 * Il MAC dell'hub e' un dato che il nodo ha gia': lo ha imparato dal WELCOME
 * la prima volta e puo' conservarlo lui (RTC memory, NVS). Con questa
 * chiamata registra il peer e passa direttamente ai DATA in unicast; l'hub li
 * accetta perche' a sua volta tiene il registro dei nodi in NVS.
 *
 * Torna false se il MAC e' nullo, se si e' gia' associati, o se la
 * registrazione del peer fallisce: in quel caso si ricade sul pairing normale
 * chiamando Link_Node_Poll() come sempre.
 */
bool Link_Node_ResumeWithHub(const uint8_t mac[6]);

/**
 * Legge e reimposta il contatore di sequenza del nodo.
 *
 * Il seq vive in RAM, quindi un nodo che si risveglia da deep sleep
 * ricomincerebbe da zero ad ogni ciclo. Non e' un dettaglio estetico: l'hub
 * scarta un DATA il cui seq e' UGUALE all'ultimo visto (e' cosi' che ignora i
 * doppioni), quindi un nodo che manda per sempre seq=0 viene sentito una volta
 * sola e poi ignorato - mentre dalla sua parte tutto sembra a posto, perche'
 * l'ACK di ESP-NOW e' di livello radio e arriva comunque.
 *
 * Il nodo deve percio' conservare il seq attraverso il sonno (RTC memory) e
 * rimetterlo qui al risveglio, cosi' la sequenza resta crescente. Torna a zero
 * solo quando si toglie corrente davvero, ed e' giusto: quello l'hub lo legge
 * come "il nodo e' ripartito".
 *
 * Osservato su hardware il 2026-08-23: 19 risvegli, 19 invii confermati dal
 * nodo, UNO solo visto dall'hub.
 */
void     Link_Node_SetSeq(uint32_t seq);
uint32_t Link_Node_GetSeq(void);

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

/**
 * Rimanda l'ULTIMO DATA gia' inviato con Link_Node_SendData(), senza toccare
 * il contatore di sequenza e con tentativi/timeout scelti dal chiamante.
 *
 * Serve alla ricerca del canale (vedi Link_SetChannel): se un DATA non viene
 * consegnato perche' l'hub e' altrove, lo si rimanda altrove — ma dev'essere
 * LO STESSO messaggio, con lo stesso seq. Richiamare Link_Node_SendData()
 * incrementerebbe il seq ad ogni tentativo, e l'hub leggerebbe quei numeri
 * saltati come pacchetti persi sulla tratta radio: buchi inventati in un
 * registro che serve proprio a contare i buchi veri.
 *
 * false se non c'e' ancora un ultimo messaggio, se non si e' associati, o se
 * la consegna non e' stata confermata.
 */
bool Link_Node_ResendLast(int max_attempts, uint32_t ack_timeout_ms);

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

/* -------------------------------------------------------------------------
 *  L'orecchio: chi bussa e non entra
 * -------------------------------------------------------------------------
 * Quando un nodo non si associa, l'hub non aveva NIENTE da mostrare: non un
 * elenco di tentativi, non un contatore, non un log. Le cause possibili sono
 * almeno cinque -- canale sbagliato, nodo ancora legato a un altro hub,
 * registro pieno, versione di protocollo diversa, nodo che non trasmette
 * affatto -- e da fuori si presentavano tutte allo stesso modo: una lista di
 * nodi che non cresce.
 *
 * L'informazione c'era gia' e veniva buttata via due volte, in
 * hub_on_new_peer(): un return silenzioso fuori dalla finestra di pairing, e
 * un altro quando il payload non si parsa. Adesso si annota SEMPRE, anche
 * fuori dalla finestra -- contare non e' adottare -- e con l'RSSI, che
 * quel callback riceve in info->rx_ctrl e nessuno leggeva.
 *
 * IL LIMITE VA DETTO IN PAGINA, o il silenzio inganna: un nodo che si crede
 * gia' associato a un altro hub NON manda HELLO, e i suoi DATA vanno in
 * unicast a quell'altro MAC, quindi questa scheda non li riceve nemmeno a
 * livello radio. Un elenco vuoto non significa "non c'e' nessun nodo acceso":
 * significa "nessun nodo sta cercando un hub".
 */

/** Perche' un messaggio da un MAC sconosciuto non e' diventato un nodo. */
typedef enum {
    LINK_UNK_HELLO       = 0,  /* HELLO valido: adottato, o in attesa che si apra la finestra */
    LINK_UNK_DATA_ORFANO = 1,  /* DATA unicast da sconosciuto: si crede gia' associato a noi */
    LINK_UNK_LUNGHEZZA   = 2,  /* payload di lunghezza diversa: non e' il nostro protocollo */
    LINK_UNK_VERSIONE    = 3,  /* protocol_version diversa dalla nostra */
    LINK_UNK_ALTRO       = 4,  /* messaggio valido ma non HELLO ne' DATA */
    LINK_UNK_PIENO       = 5,  /* registro pieno: non c'e' posto per un altro nodo */
} link_unknown_esito_t;

/** Quante voci tiene l'anello. E' un anello e non un log: se crescesse senza
 *  limite, un vicino con un dispositivo rumoroso lo riempirebbe. */
#define LINK_UNKNOWN_MAX 6

/** Legge la voce `i` (0..LINK_UNKNOWN_MAX-1). false se lo slot e' vuoto.
 *  Ogni puntatore di uscita puo' essere NULL. */
bool Link_Hub_Unknown(int i, uint8_t mac_out[6], int8_t *rssi_out,
                      uint32_t *last_ms_out, uint16_t *quante_out,
                      uint8_t *esito_out);

/** Svuota l'anello. */
void Link_Hub_UnknownClear(void);

/** Testo dell'esito, per la UI. */
const char *Link_Hub_UnknownEsito(uint8_t esito);

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_LINK_H
