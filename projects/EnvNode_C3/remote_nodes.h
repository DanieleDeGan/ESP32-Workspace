#pragma once
#include <Arduino.h>
#include <time.h>

// =====================================================================
//  remote_nodes — nodi ESP-NOW ricevuti da questa scheda
//
//  EnvNode_C3 nasce come nodo a se' stante, ma e' l'unica scheda di casa
//  sempre accesa, con orologio NTP, microSD e una web UI: finche' non
//  esiste MeteoHub_S3 fa anche da HUB per i nodi a batteria, che dormono e
//  si svegliano solo per trasmettere. Il loro storico non puo' stare su di
//  loro (il deep sleep azzera la RAM a ogni risveglio): serve qualcuno di
//  sempre acceso, ed e' questo.
//
//  Questo modulo e' solo la RICEZIONE e lo stato in RAM. Niente log su SD
//  (ancora), niente disegno: web_ui.cpp legge da qui, come gia' fa con
//  sd_logger/rtc_time/settings.
//
//  ---------------------------------------------------------------------
//  CANALE: si usa ESPNOW_LINK_CHANNEL_CURRENT (0), MAI un numero esplicito
//  ---------------------------------------------------------------------
//  Questa scheda e' connessa a un access point, quindi il canale glielo
//  impone il router e la radio e' una sola. Passare un numero esplicito a
//  Link_InitEx() farebbe chiamare esp_wifi_set_channel() su una STA
//  connessa: nel migliore dei casi non serve a niente, nel peggiore fa
//  cadere la connessione. Con lo 0 la libreria non tocca il canale e
//  registra i peer con channel 0 ("quello corrente"), quindi l'ESP-NOW
//  segue da solo l'AP — anche se il WiFi si connette DOPO remote_begin(),
//  o si riconnette su un canale diverso.
//
//  Il prezzo: tutti i partecipanti devono trovarsi sul canale dell'AP.
//  Finche' anche i nodi stanno sul WiFi va da se'. Quando un nodo passera'
//  al deep sleep senza WiFi dovra' impostare quel canale esplicitamente:
//  per questo /api/nodi lo riporta (net_channel()), cosi' si legge da
//  pagina invece di indovinarlo.
//
//  ---------------------------------------------------------------------
//  "NODO MUTO": e' l'unica diagnostica che abbiamo
//  ---------------------------------------------------------------------
//  Finche' i nodi non hanno il partitore per misurare la batteria,
//  battery_mv arriva 0 e non c'e' modo di sapere che una cella si sta
//  scaricando. L'unico segnale che resta e' "ha smesso di parlare" — ed e'
//  anche il piu' importante: senza, si guarda per giorni un numero fermo
//  credendolo vero.
//
//  La soglia non e' configurabile ma OSSERVATA: il nodo decide da solo la
//  propria cadenza (2..3600 s, impostabile dalla sua pagina), e una
//  configurazione duplicata qui sarebbe solo un modo per andare fuori
//  sincrono. Si misura l'intervallo fra un DATA e il successivo (media
//  mobile) e si dichiara muto dopo ~2,5 intervalli persi.
// =====================================================================

#define REMOTE_MAX_NODES 8

struct RemoteNode {
  uint8_t  mac[6];
  char     nome[17];        // LINK_NAME_LEN (16) + terminatore
  uint8_t  tipo;            // link_node_type_t del mittente

  bool     hasData;         // false finche' non e' arrivato il primo DATA
  float    value[3];        // significato per tipo: meteo = C, %RH, hPa
  uint16_t batteria_mv;     // 0 = alimentazione fissa o partitore non cablato

  uint32_t seq;             // ultimo seq visto
  uint32_t pacchetti;       // DATA ricevuti da quando questa scheda e' accesa
  uint32_t persi;           // buchi nel seq: perdita reale sulla tratta radio
  uint32_t riavvii;         // volte che il seq e' tornato indietro

  time_t   ultimoTs;        // ora a muro dell'ultimo DATA (0 = mai)
  uint32_t ultimoMs;        // millis() dell'ultimo DATA
  uint32_t intervalloS;     // cadenza osservata (0 = non ancora nota)
  uint32_t sogliaMutoS;     // oltre questo silenzio il nodo e' dichiarato muto
  uint32_t silenzioS;       // da quanto non parla
  bool     online;          // false = muto
};

// Da chiamare in setup(), DOPO net_begin(): non perche' serva la
// connessione (Link_InitEx sta su col solo driver WiFi avviato), ma perche'
// net_begin() e' gia' il punto in cui lo stack WiFi viene configurato, e
// tenere insieme le due cose evita di doversi chiedere chi comanda sulla
// radio. Ritorna false se l'init ESP-NOW fallisce: lo sketch deve
// continuare a funzionare lo stesso, come fa gia' senza microSD.
bool remote_begin(const char* selfName);

// true se remote_begin() e' andata a buon fine.
bool remote_ready();

// Da chiamare a ogni giro di loop(): manda i WELCOME accodati, rilegge il
// registro peer della libreria e aggiorna lo stato "muto". Non blocca,
// tranne per l'invio di un WELCOME durante una finestra di pairing.
void remote_loop();

// ---------------------------------------------------------------------
//  Pairing: finestra a tempo, non un interruttore
// ---------------------------------------------------------------------
// Fuori dalla finestra un HELLO da un MAC sconosciuto viene ignorato. La
// finestra si apre da sola all'avvio (vedi REMOTE_PAIRING_BOOT_S in
// remote_nodes.cpp) perche' il registro peer vive solo in RAM: dopo un
// riavvio della scheda i nodi gia' associati devono poter rientrare senza
// che qualcuno prema un pulsante. Scade da sola per non lasciare la porta
// aperta per sempre.
void     remote_pairing_open(uint32_t seconds);
void     remote_pairing_close();
bool     remote_pairing_active();
uint32_t remote_pairing_remaining_s();

// ---------------------------------------------------------------------
//  Lettura dello stato (per web_ui / OLED)
// ---------------------------------------------------------------------
int  remote_count();
int  remote_count_online();
bool remote_get(int index, RemoteNode* out);   // false se index non valido

// ---------------------------------------------------------------------
//  Persistenza (NVS) e "dimentica nodo"
// ---------------------------------------------------------------------
// Del registro si salvano SOLO identita' e anagrafica: MAC (che e'
// l'identita' vera, bruciata nel chip e stabile ai riflash), tipo e nome.
//
// NON si salvano i valori: dopo un riavvio l'hub mostrerebbe come "attuale"
// una temperatura vecchia di giorni, che e' esattamente il guasto che il
// rilevamento del nodo muto serve a evitare. Un nodo ripristinato riparte
// da "in attesa del primo DATA" e si riempie al primo pacchetto.
//
// NON si salvano nemmeno i contatori (pacchetti/persi/riavvii): sono "da
// quando questa scheda e' accesa", ed e' giusto che ripartano da zero.
// Persisterli vorrebbe anche dire scrivere in NVS ad ogni pacchetto, e la
// NVS si consuma - stessa regola dei contatori di sd_logger.
//
// Si scrive in NVS solo quando il registro CAMBIA (nodo nuovo o dimenticato):
// nella vita di questo hub saranno una manciata di scritture in tutto.

// Dimentica un nodo: lo toglie dal registro della libreria, dalla tabella in
// RAM e dalla NVS. Serve quando una scheda viene sostituita, perche'
// l'identita' e' il MAC: quella vecchia resterebbe in elenco per sempre come
// nodo muto, cioe' un allarme falso permanente.
//
// Se il nodo e' ancora vivo e si crede associato non manda piu' HELLO: per
// riprenderlo serve una finestra di pairing aperta (o un suo riavvio).
bool remote_forget(const uint8_t mac[6]);

// Converte "AA:BB:CC:DD:EE:FF" (anche con '-' o senza separatori) in sei
// byte. false se la stringa non e' un MAC valido.
bool remote_parse_mac(const char* testo, uint8_t out[6]);

// Nome leggibile di un link_node_type_t, per la UI.
const char* remote_tipo_nome(uint8_t tipo);
