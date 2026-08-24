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

  // --- previsione dal trend barometrico (solo nodi che mandano pressione) ---
  // Il calcolo sta QUI e non sul nodo perche' un nodo in deep sleep perde la
  // RAM ad ogni risveglio: le tre ore di storico che servono al trend non
  // possono stare su di lui. Vedi la nota "Il trend si calcola sull'hub" in
  // fondo a questo file.
  float    pressSeaHpa;     // pressione riportata al livello del mare (NAN = n/d)
  float    delta3h;         // variazione a 3 h in hPa (NAN = storico insufficiente)
  uint8_t  trend;           // forecast_trend_t, TREND_IGNOTO finche' non si sa
  uint8_t  storicoSlot;     // slot di storico pieni: dice QUANTO manca al trend
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

// Notifica di un DATA NUOVO (seq cambiata), invocata da remote_loop() e
// quindi nel contesto di loop(): puo' fare lavoro lento, per esempio una
// scrittura su microSD. Il puntatore vale solo per la durata della chiamata.
//
// E' una callback e non una chiamata diretta a sd_logger di proposito:
// questo modulo e' scritto per essere copiato su MeteoHub_S3, che avra' un
// modulo di storage diverso. Legarlo qui alla SD di EnvNode_C3 vorrebbe dire
// doverlo scucire al momento del trapianto.
typedef void (*remote_data_cb_t)(const RemoteNode* nodo);
void remote_on_data(remote_data_cb_t cb);

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

// ---------------------------------------------------------------------
//  Il trend barometrico si calcola QUI, non sul nodo
// ---------------------------------------------------------------------
// La previsione di forecast.h vuole la pressione di tre ore fa. Un nodo a
// batteria non ce l'ha e non potra' mai averla: il deep sleep gli azzera la
// RAM ad ogni risveglio, quindi la sua pagina resterebbe per sempre su
// "raccolgo dati: servono tre ore di storico". L'hub invece e' sempre acceso
// ed e' gia' il posto dove quei dati vengono raccolti.
//
// Lo storico e' un anello di slot da 10 minuti: al trend non serve la
// risoluzione dei due minuti che il nodo teneva per i suoi grafici, e cosi'
// tre ore costano ~160 byte per nodo invece di 4,3 kB.
//
// Si calcola solo per i nodi che mandano una pressione PLAUSIBILE in
// value[2] (800..1100 hPa): il campo ha significati diversi per tipo di
// nodo, e senza quel controllo il value[2] di un attuatore diventerebbe una
// previsione del tempo.

// La pressione arriva GREZZA dai nodi (scelta del nodo: la correzione
// dipende da un'altitudine che li' non e' mai stata calibrata, e applicarla
// scriverebbe un errore sistematico dentro lo storico dell'hub, per sempre).
// Riportarla al livello del mare tocca quindi a chi la mostra, ed e' l'unico
// motivo per cui questo modulo ha bisogno di un'altitudine.
//
// E' UN VALORE SOLO per tutti i nodi, non uno per nodo: i nodi di una casa
// stanno entro pochi metri l'uno dall'altro e 8 m valgono 1 hPa. Se un
// giorno ci fossero nodi a quote davvero diverse, questo e' il punto da
// spaccare in un campo per nodo — e allora andra' nel blob NVS del registro,
// con la migrazione di formato che ne consegue.
//
// NB: il TREND non dipende dall'altitudine (e' una differenza, l'offset si
// cancella). L'altitudine serve solo al valore assoluto, che forecast_text()
// usa per distinguere "bel tempo stabile" da "perturbato che non si sblocca".
float remote_altitude_m();
bool  remote_set_altitude_m(float m);   // false se fuori range (-400..4000)

// Azzera lo storico di un nodo, da chiamare PRIMA di una serie di
// remote_seed_pressure().
//
// Serve perche' i DATA veri possono arrivare prima che il seeding parta (il
// seeding aspetta il primo sync NTP, i nodi no): lo storico conterrebbe gia'
// un campione recente, e ogni campione letto dal CSV — piu' vecchio — verrebbe
// rifiutato come fuori ordine. Il risultato sarebbe un seeding che gira,
// non da' errori e non semina niente.
void remote_seed_begin(const uint8_t mac[6]);

// Inserisce nello storico un campione di pressione LETTO ALTROVE, tipicamente
// dai CSV su SD subito dopo un riavvio dell'hub.
//
// Senza questo, ogni riavvio (e ogni OTA) costerebbe tre ore di "non ancora
// noto" — cioe' lo stesso guasto che si sta togliendo al nodo, spostato
// sull'hub. I campioni vanno passati in ordine cronologico; quelli piu'
// vecchi dell'ultimo gia' noto vengono ignorati.
//
// Sta qui e non dentro il modulo perche' remote_nodes non conosce la SD, per
// la stessa ragione per cui non conosce sd_logger: l'incollatura e' nel .ino.
void remote_seed_pressure(const uint8_t mac[6], time_t ts, float pressHpa);

// Etichetta e testo della previsione, per la UI. Incapsulati qui cosi' chi
// disegna non deve includere forecast.h ne' sapere come e' fatto il trend.
const char* remote_trend_label(uint8_t trend);
const char* remote_forecast_text(const RemoteNode* n);
