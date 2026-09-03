#include "remote_nodes.h"
#include "rtc_time.h"
#include "forecast.h"

#include <EspNowLink.h>
#include <Preferences.h>
#include <string.h>

// ---------------------------------------------------------------------
//  Persistenza in NVS
// ---------------------------------------------------------------------
// Namespace proprio: "envcfg" e' delle impostazioni utente, "envnode" dei
// contatori dei log. Tenerli separati vuol dire poter azzerare il registro
// dei nodi senza toccare il resto.
//
// Un unico blob invece di una chiave per nodo: il registro si riscrive
// sempre tutto insieme (e' piccolo, ~200 byte), e una scrittura sola e'
// anche una sola operazione atomica dal punto di vista di chi rilegge.
#define NVS_NS   "envnodi"
#define NVS_KEY  "peers"
#define NVS_VER  1

// mac[6] + tipo + nome(17) = 24 byte per nodo; 8 nodi = 194 byte col
// preambolo. Impacchettata a mano e non con una struct, per non dipendere
// dall'allineamento del compilatore in un formato scritto su disco.
#define REC_LEN  24

// ---------------------------------------------------------------------
//  Costanti
// ---------------------------------------------------------------------
// Finestra di pairing aperta da sola all'avvio. Il registro peer di
// EspNowLink vive solo in RAM: dopo un riavvio di questa scheda i nodi gia'
// associati non sono piu' noti, e un nodo che dorme non ha una pagina web
// da cui farsi riassociare a mano. Cinque minuti bastano a farli rientrare
// tutti (un nodo non associato manda HELLO in broadcast a raffica) e poi la
// porta si richiude.
static const uint32_t REMOTE_PAIRING_BOOT_S = 300;
static const uint32_t REMOTE_PAIRING_MAX_S  = 3600;

// Soglia di silenzio usata finche' la cadenza del nodo non e' ancora nota
// (serve almeno un secondo DATA per misurarla).
static const uint32_t SOGLIA_DEFAULT_S = 900;    // 15 min
static const uint32_t SOGLIA_MIN_S     = 90;
static const uint32_t SOGLIA_MAX_S     = 7200;   // 2 h

// Un intervallo osservato piu' lungo di questo non entra nella media: e' un
// nodo tornato dopo un'assenza, non la sua cadenza. Senza questo filtro una
// notte di silenzio alzerebbe la soglia a giorni, e il "muto" non
// scatterebbe mai piu'.
static const uint32_t INTERVALLO_MAX_S = 21600;  // 6 h

// Oltre questo, un salto di seq non e' una perdita radio ma un contatore
// sporco: mille pacchetti persi di fila sono gia' oltre tre giorni di
// silenzio a 300 s, cioe' molto piu' di quanto un nodo possa tacere restando
// in elenco (la soglia del muto e' al massimo 2 h). Vedi seqAssurdi.
static const uint32_t PERSI_SALTO_MAX = 1000;

// ---------------------------------------------------------------------
//  Stato
// ---------------------------------------------------------------------
// Nessun lock: questa tabella e' scritta e letta solo da loop() (via
// remote_loop() e i getter chiamati dagli handler HTTP, che girano anch'essi
// dentro net_loop()). La concorrenza vera — il callback di ricezione
// ESP-NOW, che gira sul task del driver WiFi — sta tutta dentro EspNowLink,
// che la protegge con la propria portMUX e restituisce uno stato gia'
// consistente da Link_Hub_GetPeerInfo().
//
// E' anche il motivo per cui qui si POLLA il registro invece di registrare
// una Link_OnMessage(): meno codice in un contesto di quasi-interrupt, e
// niente di nostro da sincronizzare. Il poll gira ad ogni giro di loop(),
// cioe' con periodo di millisecondi contro trasmissioni ogni minuti: non si
// perde un DATA, e comunque lo si riconoscerebbe dal salto di seq.
static RemoteNode s_nodi[REMOTE_MAX_NODES];
static int        s_count   = 0;
static bool       s_ready   = false;
static bool       s_pairing = false;
// Alzata quando cambia qualcosa di PERSISTITO (nodo nuovo, oppure nome/tipo
// diversi da quelli salvati): remote_loop() la raccoglie e riscrive la NVS una
// volta sola, invece di farlo dentro il ciclo sui peer.
static bool       s_dirty   = false;
static remote_data_cb_t s_dataCb = nullptr;
static uint32_t   s_pairingFineMs = 0;

// ---------------------------------------------------------------------
//  Storico della pressione, per il trend a tre ore
// ---------------------------------------------------------------------
// Un anello di slot da 10 minuti. Al trend non serve la risoluzione fine
// che il nodo teneva per i suoi grafici: 20 slot coprono 3 h 20 con un po'
// di margine sui pacchetti persi, e costano 160 byte per nodo.
//
// Perche' a slot e non "un campione ogni DATA": la cadenza dei nodi la
// decidono i nodi (da 2 s a 1 h), quindi un anello a numero fisso di
// campioni coprirebbe tre ore o tre minuti a seconda di come e' configurato
// chi trasmette. Ancorare gli slot all'orologio rende la finestra la stessa
// per tutti.
static const uint32_t HIST_SLOT_S     = 600;     // 10 min
static const int      HIST_SLOTS      = 20;      // -> 3 h 20 di copertura
static const uint32_t TREND_WINDOW_S  = 10800;   // le tre ore canoniche
static const uint32_t TREND_TOLL_S    = 900;     // +-15 min sul campione di 3 h fa

// Range di plausibilita' di una pressione atmosferica al suolo. value[2] ha
// significati diversi per tipo di nodo: senza questo controllo il terzo
// canale di un attuatore diventerebbe una previsione del tempo.
static const float PRESS_MIN_HPA = 800.0f;
static const float PRESS_MAX_HPA = 1100.0f;

// --- storico della temperatura, per il grafico a 24 h ------------------
//
// Anello separato da quello della pressione, non un campo in piu' li' dentro:
// le due cose hanno finestre diverse (3 h contro 24) e scopi diversi (una
// soglia contro un disegno). Tenere la risoluzione da 10 minuti per un giorno
// intero costerebbe dodici volte la memoria per una curva che a 376 px di
// larghezza non potrebbe comunque mostrarla.
//
// Niente timestamp per slot: l'indice E' il tempo. Lo slot assoluto
// (ts / 1800) modulo 48 da' la cella, e `slotUltimo` dice dove sta "adesso";
// tutto cio' che sta piu' indietro di 48 slot e' fuori finestra per
// costruzione. Costa 101 byte per nodo invece di 288.
static const uint32_t TH_SLOT_S = 1800;   // 30 min
static const int      TH_SLOTS  = 48;     // -> 24 h esatte
static const int16_t  TH_VUOTO  = INT16_MIN;

struct TempHist {
  int16_t  t[TH_SLOTS];     // decimi di grado; TH_VUOTO = nessun campione
  uint32_t slotUltimo;      // slot assoluto piu' recente scritto
  bool     avviato;
};
static TempHist s_thist[REMOTE_MAX_NODES];

static void thReset(int idx) {
  if (idx < 0 || idx >= REMOTE_MAX_NODES) return;
  for (int i = 0; i < TH_SLOTS; i++) s_thist[idx].t[i] = TH_VUOTO;
  s_thist[idx].slotUltimo = 0;
  s_thist[idx].avviato    = false;
}

// Una temperatura plausibile per l'aria di casa o di un giardino. Come per la
// pressione: value[0] ha significati diversi per tipo di nodo, e senza questo
// il primo canale di un attuatore finirebbe dentro un grafico del meteo.
static bool tempPlausibile(float t) {
  return !isnan(t) && t >= -60.0f && t <= 80.0f;
}

static void thPush(int idx, time_t ts, float tempC) {
  if (idx < 0 || idx >= REMOTE_MAX_NODES) return;
  if (ts <= 0 || !tempPlausibile(tempC)) return;

  TempHist& h = s_thist[idx];
  const uint32_t slot = (uint32_t)(ts / (time_t)TH_SLOT_S);

  if (!h.avviato) {
    for (int i = 0; i < TH_SLOTS; i++) h.t[i] = TH_VUOTO;
    h.slotUltimo = slot;
    h.avviato    = true;
  } else if (slot > h.slotUltimo) {
    // Si e' cambiato slot: le celle scavalcate vanno SVUOTATE, o dopo un giro
    // completo dell'anello si leggerebbero i valori di ieri come se fossero di
    // oggi. E' la stessa insidia dello storico della pressione, che li' non
    // esiste perche' ogni slot porta il proprio timestamp.
    const uint32_t salti = slot - h.slotUltimo;
    if (salti >= (uint32_t)TH_SLOTS) {
      for (int i = 0; i < TH_SLOTS; i++) h.t[i] = TH_VUOTO;
    } else {
      for (uint32_t k = 1; k <= salti; k++) {
        h.t[(h.slotUltimo + k) % TH_SLOTS] = TH_VUOTO;
      }
    }
    h.slotUltimo = slot;
  } else if (h.slotUltimo - slot >= (uint32_t)TH_SLOTS) {
    return;   // piu' vecchio della finestra: non ha una cella dove stare
  }

  h.t[slot % TH_SLOTS] = (int16_t)lroundf(tempC * 10.0f);
}

struct PressHist {
  time_t ts[HIST_SLOTS];
  float  p[HIST_SLOTS];
  int    head;    // ultimo scritto (-1 = vuoto)
  int    count;
};
static PressHist s_hist[REMOTE_MAX_NODES];

// Altitudine dei nodi, per riportare al livello del mare. Vedi la nota in
// remote_nodes.h: e' una sola per tutti, di proposito.
static const float ALT_DEFAULT_M = 29.0f;
static const float ALT_MIN_M     = -400.0f;
static const float ALT_MAX_M     = 4000.0f;
static float s_altitudeM = ALT_DEFAULT_M;

// Definita in fondo, insieme al resto della gestione dell'altitudine; qui
// serve solo il prototipo, perche' remote_begin() la chiama e sta prima.
static void altCarica();

static void histReset(int idx) {
  if (idx < 0 || idx >= REMOTE_MAX_NODES) return;
  s_hist[idx].head  = -1;
  s_hist[idx].count = 0;
  thReset(idx);
}

static bool pressPlausibile(float p) {
  return !isnan(p) && p >= PRESS_MIN_HPA && p <= PRESS_MAX_HPA;
}

// Un campione nello storico. Stesso slot dell'ultimo -> lo sovrascrive
// (l'ultima lettura di quei dieci minuti va bene quanto le altre); slot piu'
// vecchio -> si ignora, perche' l'anello e' ordinato e un campione fuori
// ordine romperebbe la ricerca. Capita davvero: il seeding da SD legge righe
// gia' passate, e l'orologio puo' spostarsi all'indietro al primo sync NTP.
static void histPush(int idx, time_t ts, float p) {
  if (idx < 0 || idx >= REMOTE_MAX_NODES) return;
  if (ts <= 0 || !pressPlausibile(p)) return;

  PressHist& h = s_hist[idx];
  const uint32_t slot = (uint32_t)(ts / (time_t)HIST_SLOT_S);

  if (h.count > 0 && h.head >= 0) {
    const uint32_t slotUltimo = (uint32_t)(h.ts[h.head] / (time_t)HIST_SLOT_S);
    if (slot == slotUltimo) { h.ts[h.head] = ts; h.p[h.head] = p; return; }
    if (slot <  slotUltimo) return;
  }

  h.head = (h.head + 1) % HIST_SLOTS;
  h.ts[h.head] = ts;
  h.p[h.head]  = p;
  if (h.count < HIST_SLOTS) h.count++;
}

// Il campione piu' vicino a 'target', se cade entro la tolleranza. NAN se lo
// storico non arriva ancora cosi' indietro: e' il caso normale nelle prime
// tre ore, e va detto, non arrotondato al campione piu' vecchio che c'e' -
// un delta misurato su venti minuti letto come se fosse su tre ore
// sparerebbe fuori scala tutte le soglie.
static float histLookup(int idx, time_t target, uint32_t toll) {
  if (idx < 0 || idx >= REMOTE_MAX_NODES) return NAN;
  const PressHist& h = s_hist[idx];

  float migliore = NAN;
  uint32_t distanza = toll + 1;
  // Si scorre TUTTO l'anello e si saltano gli slot vuoti (ts == 0), invece di
  // fermarsi a h.count: quel conteggio combacia con gli indici occupati solo
  // finche' il primo push parte da head == -1, che e' vero oggi ma e' un
  // invariante che non si vede da qui. Venti confronti non si sentono, un
  // campione saltato in silenzio si', e sarebbe pure intermittente.
  for (int i = 0; i < HIST_SLOTS; i++) {
    const time_t t = h.ts[i];
    if (t <= 0) continue;
    const uint32_t d = (uint32_t)((t > target) ? (t - target) : (target - t));
    if (d <= toll && d < distanza) { distanza = d; migliore = h.p[i]; }
  }
  return migliore;
}

// Ricalcola pressione al livello del mare, delta a 3 h e trend di un nodo.
// Da chiamare quando e' appena arrivato un DATA nuovo.
static void forecastUpdate(int idx) {
  if (idx < 0 || idx >= REMOTE_MAX_NODES) return;
  RemoteNode* r = &s_nodi[idx];

  const float pNow = r->value[2];
  if (!pressPlausibile(pNow)) {
    r->pressSeaHpa = NAN;
    r->delta3h     = NAN;
    r->trend       = (uint8_t)TREND_IGNOTO;
    r->storicoSlot = 0;
    return;
  }

  histPush(idx, r->ultimoTs, pNow);
  r->storicoSlot = (uint8_t)s_hist[idx].count;
  r->pressSeaHpa = forecast_sea_level_hpa(pNow, s_altitudeM);

  const float pAllora = histLookup(idx, r->ultimoTs - (time_t)TREND_WINDOW_S, TREND_TOLL_S);
  if (!pressPlausibile(pAllora)) {
    r->delta3h = NAN;
    r->trend   = (uint8_t)TREND_IGNOTO;
    return;
  }

  // Stesso conto che faceva il nodo: la correzione si applica a entrambi i
  // termini. Sul trend si cancella quasi del tutto, ma tenerla esplicita
  // evita di doversi ricordare perche' era lecito ometterla il giorno che
  // l'altitudine diventasse un campo per nodo.
  r->delta3h = forecast_sea_level_hpa(pNow,     s_altitudeM)
             - forecast_sea_level_hpa(pAllora,  s_altitudeM);
  r->trend   = (uint8_t)forecast_classify_hyst(r->delta3h, (forecast_trend_t)r->trend);
}

// ---------------------------------------------------------------------
//  Helper
// ---------------------------------------------------------------------
static RemoteNode* trovaOCrea(const uint8_t mac[6]) {
  for (int i = 0; i < s_count; i++) {
    if (memcmp(s_nodi[i].mac, mac, 6) == 0) return &s_nodi[i];
  }
  if (s_count >= REMOTE_MAX_NODES) return nullptr;
  const int idx = s_count++;
  RemoteNode* r = &s_nodi[idx];
  memset(r, 0, sizeof(*r));
  memcpy(r->mac, mac, 6);
  r->sogliaMutoS = SOGLIA_DEFAULT_S;

  // memset() ha appena messo a ZERO i campi della previsione, e uno zero qui
  // sarebbe una bugia: 0.0 hPa non e' "non lo so", e un grafico o una soglia
  // lo prenderebbero per una misura. NAN e TREND_IGNOTO dicono la verita'.
  r->pressSeaHpa = NAN;
  r->delta3h     = NAN;
  r->trend       = (uint8_t)TREND_IGNOTO;
  histReset(idx);
  return r;
}

// ---------------------------------------------------------------------
//  NVS: salva / rileggi il registro
// ---------------------------------------------------------------------
static void nvsSalva() {
  uint8_t buf[2 + REC_LEN * REMOTE_MAX_NODES];
  buf[0] = NVS_VER;
  buf[1] = (uint8_t)s_count;
  for (int i = 0; i < s_count; i++) {
    uint8_t* p = &buf[2 + i * REC_LEN];
    memcpy(p, s_nodi[i].mac, 6);
    p[6] = s_nodi[i].tipo;
    memset(&p[7], 0, 17);
    strncpy((char*)&p[7], s_nodi[i].nome, 16);
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NS, /*readOnly=*/false)) {
    Serial.println("[ESP-NOW] NVS non disponibile: registro non salvato.");
    return;
  }
  prefs.putBytes(NVS_KEY, buf, 2 + s_count * REC_LEN);
  prefs.end();
  Serial.printf("[ESP-NOW] registro salvato in NVS (%d nodi)\n", s_count);
}

static void nvsRipristina() {
  uint8_t buf[2 + REC_LEN * REMOTE_MAX_NODES];

  Preferences prefs;
  if (!prefs.begin(NVS_NS, /*readOnly=*/true)) return;   // mai scritto: normale
  const size_t letti = prefs.getBytes(NVS_KEY, buf, sizeof(buf));
  prefs.end();

  if (letti < 2 || buf[0] != NVS_VER) return;
  int n = buf[1];
  if (n > REMOTE_MAX_NODES) n = REMOTE_MAX_NODES;
  if (letti < (size_t)(2 + n * REC_LEN)) return;   // blob troncato: si ignora

  int ripristinati = 0;
  for (int i = 0; i < n; i++) {
    const uint8_t* p = &buf[2 + i * REC_LEN];
    char nome[17] = {0};
    memcpy(nome, &p[7], 16);

    // Rimettere il peer nel driver e' l'unica cosa che conta davvero: da qui
    // in poi i DATA di quel nodo vengono consegnati a onReceive() anche a
    // finestra di pairing CHIUSA, che e' esattamente il buco che questa
    // persistenza serve a tappare.
    if (!Link_Hub_AddPeer(p, (link_node_type_t)p[6], nome)) continue;

    RemoteNode* r = trovaOCrea(p);
    if (r == nullptr) continue;
    r->tipo = p[6];
    strncpy(r->nome, nome, sizeof(r->nome) - 1);
    // hasData resta false di proposito: si riparte da "in attesa del primo
    // DATA" invece di mostrare valori vecchi come se fossero di adesso.
    ripristinati++;
  }
  if (ripristinati) {
    Serial.printf("[ESP-NOW] %d nodi ripristinati da NVS\n", ripristinati);
  }
  // Quello appena letto e' identico a quello su disco: non c'e' niente da
  // riscrivere, e riscriverlo sarebbe un ciclo di NVS bruciato ad ogni avvio.
  s_dirty = false;
}

static uint32_t sogliaMuto(const RemoteNode* r) {
  if (r->intervalloS == 0) return SOGLIA_DEFAULT_S;
  uint32_t s = r->intervalloS * 5 / 2 + 30;   // ~2,5 trasmissioni perse
  if (s < SOGLIA_MIN_S) s = SOGLIA_MIN_S;
  if (s > SOGLIA_MAX_S) s = SOGLIA_MAX_S;
  return s;
}

// ---------------------------------------------------------------------
//  Rilettura del registro peer della libreria
// ---------------------------------------------------------------------
static void aggiornaDaLibreria() {
  const int n = Link_Hub_GetPeerCount();

  for (int i = 0; i < n; i++) {
    uint8_t          mac[6];
    link_node_type_t tipo = LINK_NODE_UNKNOWN;
    char             nome[LINK_NAME_LEN] = {0};
    uint32_t         lastSeenMs = 0;
    link_message_t   dato;

    if (!Link_Hub_GetPeerInfo(i, mac, &tipo, nome, &lastSeenMs, &dato)) continue;

    // I peer si abbinano per MAC, non per indice: la libreria oggi non
    // rimuove mai un peer (quindi gli indici sarebbero stabili), ma farci
    // affidamento legherebbe questo file a un dettaglio interno che domani
    // puo' cambiare. Con al massimo 8 nodi la ricerca non si sente.
    RemoteNode* r = trovaOCrea(mac);
    if (r == nullptr) continue;   // tabella piena: il nodo in piu' si ignora

    // Nome e tipo possono cambiare sotto i piedi: basta riprogrammare un nodo
    // con un NODE_NAME diverso. Quando succede va riscritta anche la NVS, o al
    // prossimo riavvio l'hub ripristinerebbe l'anagrafica vecchia.
    if (r->tipo != (uint8_t)tipo || strncmp(r->nome, nome, sizeof(r->nome) - 1) != 0) {
      if (r->hasData) {
        Serial.printf("[ESP-NOW] il nodo %s ora si presenta come %s\n", r->nome, nome);
      }
      s_dirty = true;
    }
    r->tipo = (uint8_t)tipo;
    strncpy(r->nome, nome, sizeof(r->nome) - 1);
    r->nome[sizeof(r->nome) - 1] = '\0';

    // Link_Hub_GetPeerInfo() azzera last_data se da quel nodo non e' ancora
    // arrivato nessun DATA: protocol_version a 0 non e' un messaggio
    // valido, e' proprio quel caso.
    if (dato.protocol_version == 0) continue;
    if (r->hasData && dato.seq == r->seq) continue;   // niente di nuovo

    if (r->hasData) {
      // millis() e' unsigned: la differenza resta corretta anche
      // all'overflow dei 49 giorni, che su una scheda accesa da settimane
      // non e' un caso di scuola.
      const uint32_t deltaS = (millis() - r->ultimoMs) / 1000;

      if (dato.seq > r->seq) {
        const uint32_t salto = dato.seq - r->seq - 1;

        // Il salto ha un TETTO. Il seq attraversa il deep sleep passando dalla
        // RTC memory, e un valore sporco letto da li' (un byte, una migrazione
        // di formato a meta') diventerebbe qualche milione di "pacchetti
        // persi" permanenti: un contatore avvelenato da un pacchetto solo, che
        // poi nessuno puo' piu' azzerare se non riavviando l'hub.
        //
        // Il pacchetto NON si scarta: il dato e' buono, e' la numerazione a
        // essere strana. E il salto si conta a parte invece di sparire, o si
        // sostituirebbe un numero sbagliato con un silenzio.
        if (salto <= PERSI_SALTO_MAX) r->persi += salto;
        else                          r->seqAssurdi++;

        // La cadenza si impara SOLO dai messaggi CONSECUTIVI, e non basta che
        // il seq cresca. Con un pacchetto perso il delta e' due periodi, con
        // due buchi tre, e la media mobile a peso 1/4 se li porta dentro: da
        // 300 s si passa a 375 con un buco solo, e servono otto pacchetti per
        // rientrare. Nel frattempo si sposta tutto quello che dipende dalla
        // cadenza — sogliaMuto (2,5 x intervallo, cioe' tre minuti di ritardo
        // nel dichiarare morto un nodo che lo e' davvero), nodoInRitardo() e
        // cadenzaNodiMs(), che decide ogni quanto si ridisegna il pannello.
        //
        // Un delta misurato a cavallo di un buco non e' un periodo rumoroso da
        // mediare: e' il periodo di un'altra grandezza. Prima qui c'era la sola
        // guardia `dato.seq > r->seq`, che esclude i riavvii del nodo (il seq
        // che torna indietro) — cioe' il caso di cui parlava il commento — e
        // non i buchi.
        if (salto == 0 && deltaS >= 1 && deltaS <= INTERVALLO_MAX_S) {
          // ...e nemmeno il PRIMO delta dopo un riavvio del nodo, che e'
          // consecutivo nel seq ma non e' un periodo: in mezzo c'e' il boot,
          // e su un nodo a batteria pure la finestra di veglia da 5 minuti.
          //
          // E' lo stesso difetto dei buchi visto dall'altro lato, ed e' stato
          // MISURATO il 2026-09-03 sul nodo a batteria appena aggiornato: il
          // primo DATA dopo il riavvio e' arrivato 671 s dopo il precedente
          // invece di 300, e la media mobile lo ha portato a 393 s di cadenza
          // appresa (300*3 + 671)/4 -- con la soglia del muto salita da 780 a
          // 1012 s, cioe' quattro minuti di ritardo nel dichiarare morto un
          // nodo che lo e' davvero.
          if (r->saltaDelta) {
            r->saltaDelta = false;
          } else {
            r->intervalloS = (r->intervalloS == 0)
                             ? deltaS
                             : (r->intervalloS * 3 + deltaS) / 4;   // media mobile
            if (r->intervalloCampioni < UINT16_MAX) r->intervalloCampioni++;
          }
        }
      } else {
        // seq tornato indietro: il nodo e' ripartito da zero. Con nodi a
        // batteria e' un'informazione che vale, perche' un nodo che si
        // riavvia spesso ha un problema di alimentazione, non di radio.
        r->riavvii++;
        r->saltaDelta = true;   // il prossimo delta comprende il boot: non e' un periodo
      }
    }

    r->seq         = dato.seq;
    r->batteria_mv = dato.battery_mv;
    memcpy(r->value, dato.value, sizeof(r->value));
    r->pacchetti++;
    r->hasData = true;

    // L'istante di ricezione e' quello registrato dalla libreria, non
    // "adesso": fra il callback e questo giro di loop() puo' esserci di
    // mezzo una scrittura su SD o una richiesta HTTP lenta, e un timestamp
    // spostato in avanti falserebbe sia la cadenza appresa sia l'ora
    // mostrata in pagina.
    r->ultimoMs = lastSeenMs;
    r->ultimoTs = rtctime_now() - (time_t)((millis() - lastSeenMs) / 1000);

    // Prima della callback: cosi' chi ascolta (il .ino, che ci aggancia il
    // log su SD) trova il record completo di trend e previsione, non a meta'.
    forecastUpdate((int)(r - s_nodi));
    thPush((int)(r - s_nodi), r->ultimoTs, r->value[0]);

    // In fondo, quando il record e' completo: chi ascolta (il .ino, che ci
    // aggancia il log su SD) deve vedere il dato gia' assestato. Siamo nel
    // contesto di loop(), quindi una scrittura su card qui e' lecita.
    if (s_dataCb) s_dataCb(r);
  }
}

// ---------------------------------------------------------------------
//  API pubblica
// ---------------------------------------------------------------------
bool remote_begin(const char* selfName) {
  // ESPNOW_LINK_CHANNEL_CURRENT (0) e non un canale esplicito: vedi la nota
  // in testa a remote_nodes.h. Un hub connesso a un AP sta sul canale che gli
  // impone il router, e forzarlo farebbe cadere la connessione.
  return remote_begin(selfName, ESPNOW_LINK_CHANNEL_CURRENT);
}

bool remote_begin(const char* selfName, uint8_t channel) {
  const char* nome = (selfName && selfName[0]) ? selfName : "EnvNode";

  s_ready = Link_InitEx(LINK_NODE_HUB, nome, channel);
  if (!s_ready) {
    Serial.println("[ESP-NOW] init fallita: si continua senza nodi remoti.");
    return false;
  }

  Serial.printf("[ESP-NOW] hub attivo come %s, pairing aperto per %lu s\n",
                nome, (unsigned long)REMOTE_PAIRING_BOOT_S);

  // Prima i nodi gia' noti, poi la finestra: i ripristinati non ne hanno
  // bisogno (il driver li riconosce di nuovo), la finestra resta per chi non
  // era ancora in elenco.
  nvsRipristina();
  altCarica();
  remote_pairing_open(REMOTE_PAIRING_BOOT_S);
  return true;
}

bool remote_ready() { return s_ready; }

void remote_on_data(remote_data_cb_t cb) { s_dataCb = cb; }

void remote_loop() {
  if (!s_ready) return;

  Link_Hub_Poll();   // invia i WELCOME accodati dal callback di ricezione

  if (s_pairing && (int32_t)(millis() - s_pairingFineMs) >= 0) {
    remote_pairing_close();
    Serial.println("[ESP-NOW] finestra di pairing chiusa.");
  }

  const int primaCount = s_count;
  aggiornaDaLibreria();
  if (s_count > primaCount) {
    Serial.printf("[ESP-NOW] nuovo nodo associato: %s (%d in totale)\n",
                  s_nodi[s_count - 1].nome, s_count);
    s_dirty = true;
  }

  // Solo quando il registro cambia davvero, mai ad ogni pacchetto: la NVS ha
  // cicli di scrittura finiti.
  if (s_dirty) {
    s_dirty = false;
    nvsSalva();
  }

  // Stato "muto", ricalcolato ad ogni giro: e' una funzione del tempo, non
  // di un evento, quindi non esiste un momento in cui aggiornarlo se non
  // continuamente.
  for (int i = 0; i < s_count; i++) {
    RemoteNode* r = &s_nodi[i];
    r->sogliaMutoS = sogliaMuto(r);
    r->silenzioS   = r->hasData ? (millis() - r->ultimoMs) / 1000 : 0;
    r->online      = r->hasData && (r->silenzioS < r->sogliaMutoS);
  }
}

void remote_pairing_open(uint32_t seconds) {
  if (!s_ready) return;
  if (seconds == 0) seconds = REMOTE_PAIRING_BOOT_S;
  if (seconds > REMOTE_PAIRING_MAX_S) seconds = REMOTE_PAIRING_MAX_S;
  s_pairingFineMs = millis() + seconds * 1000UL;
  s_pairing = true;
  Link_Hub_SetPairingMode(true);
}

void remote_pairing_close() {
  s_pairing = false;
  Link_Hub_SetPairingMode(false);
}

bool remote_pairing_active() { return s_pairing; }

uint32_t remote_pairing_remaining_s() {
  if (!s_pairing) return 0;
  const int32_t restaMs = (int32_t)(s_pairingFineMs - millis());
  return (restaMs > 0) ? (uint32_t)(restaMs / 1000) : 0;
}

int remote_count() { return s_count; }

int remote_count_online() {
  int n = 0;
  for (int i = 0; i < s_count; i++) if (s_nodi[i].online) n++;
  return n;
}

bool remote_get(int index, RemoteNode* out) {
  if (index < 0 || index >= s_count || out == nullptr) return false;
  *out = s_nodi[index];
  return true;
}

bool remote_parse_mac(const char* testo, uint8_t out[6]) {
  if (testo == nullptr || out == nullptr) return false;

  int nibble = 0;
  uint8_t acc = 0;
  for (const char* p = testo; *p; p++) {
    const char c = *p;
    if (c == ':' || c == '-' || c == '.') continue;   // separatori ammessi
    uint8_t v;
    if      (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return false;                                // carattere non esadecimale

    if (nibble >= 12) return false;                   // piu' di sei byte
    acc = (uint8_t)((acc << 4) | v);
    if (++nibble % 2 == 0) out[nibble / 2 - 1] = acc;
  }
  return nibble == 12;
}

bool remote_forget(const uint8_t mac[6]) {
  if (!s_ready || mac == nullptr) return false;

  // Prima la libreria: se quel MAC non e' nel suo registro non c'e' niente da
  // dimenticare, e non si tocca nemmeno la NVS.
  if (!Link_Hub_ForgetPeer(mac)) return false;

  for (int i = 0; i < s_count; i++) {
    if (memcmp(s_nodi[i].mac, mac, 6) != 0) continue;
    Serial.printf("[ESP-NOW] nodo dimenticato: %s\n", s_nodi[i].nome);
    // s_hist e' un array PARALLELO indicizzato come s_nodi: va compattato
    // insieme, o dopo un "dimentica" ogni nodo si ritrova lo storico di
    // pressione del vicino e il trend diventa una misura di due posti diversi.
    for (int j = i; j < s_count - 1; j++) {
      s_nodi[j] = s_nodi[j + 1];
      s_hist[j]  = s_hist[j + 1];
      s_thist[j] = s_thist[j + 1];
    }
    s_count--;
    memset(&s_nodi[s_count], 0, sizeof(s_nodi[s_count]));
    histReset(s_count);
    break;
  }

  nvsSalva();
  return true;
}

const char* remote_tipo_nome(uint8_t tipo) {
  switch (tipo) {
    case LINK_NODE_HUB:                 return "hub";
    case LINK_NODE_SENSOR_TEMPERATURE:  return "temperatura";
    case LINK_NODE_SENSOR_WATER_LEVEL:  return "livello acqua";
    case LINK_NODE_SENSOR_BATTERY:      return "batteria";
    case LINK_NODE_ACTUATOR:            return "attuatore";
    case LINK_NODE_CAMERA:              return "camera";
    default:                            return "sconosciuto";
  }
}

// ---------------------------------------------------------------------
//  Altitudine, storico seminato da fuori, etichette
// ---------------------------------------------------------------------
// Chiave a se' nella NVS, non dentro il blob del registro: quel blob ha un
// formato impacchettato a mano con il suo numero di versione, e allargarlo
// per un singolo float vorrebbe dire scriverne la migrazione. Una chiave
// separata non tocca niente di cio' che gia' funziona.
#define NVS_KEY_ALT  "alt"

static void altCarica() {
  Preferences p;
  if (!p.begin(NVS_NS, true)) return;
  const float v = p.getFloat(NVS_KEY_ALT, ALT_DEFAULT_M);
  p.end();
  if (v >= ALT_MIN_M && v <= ALT_MAX_M) s_altitudeM = v;
}

float remote_altitude_m() { return s_altitudeM; }

bool remote_set_altitude_m(float m) {
  if (isnan(m) || m < ALT_MIN_M || m > ALT_MAX_M) return false;
  if (fabsf(m - s_altitudeM) < 0.01f) return true;   // niente da scrivere
  s_altitudeM = m;

  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.putFloat(NVS_KEY_ALT, s_altitudeM);
    p.end();
  }

  // Le pressioni al livello del mare gia' calcolate sono ora sbagliate di un
  // offset: si rifanno subito, senza aspettare il prossimo DATA (che con un
  // nodo a cinque minuti di cadenza lascerebbe in pagina un numero vecchio
  // proprio mentre l'utente sta guardando l'effetto di cio' che ha appena
  // cambiato). Il TREND non cambia: e' una differenza.
  for (int i = 0; i < s_count; i++) {
    if (pressPlausibile(s_nodi[i].value[2])) {
      s_nodi[i].pressSeaHpa = forecast_sea_level_hpa(s_nodi[i].value[2], s_altitudeM);
    }
  }
  return true;
}

void remote_seed_begin(const uint8_t mac[6]) {
  if (mac == nullptr) return;
  for (int i = 0; i < s_count; i++) {
    if (memcmp(s_nodi[i].mac, mac, 6) != 0) continue;
    histReset(i);
    s_nodi[i].storicoSlot = 0;
    // Il campione corrente NON si perde davvero: il delta si misura fra
    // value[2] di adesso e lo storico, quindi la lettura in corso non deve
    // stare nell'anello. Ci rientra da sola al prossimo DATA.
    return;
  }
}

int remote_temp_history(int index, int16_t* out, int maxOut, time_t* tsUltimo) {
  if (out == nullptr || maxOut <= 0) return 0;
  if (index < 0 || index >= s_count) return 0;

  const TempHist& h = s_thist[index];
  if (!h.avviato) return 0;

  const int n = (maxOut < TH_SLOTS) ? maxOut : TH_SLOTS;

  // Si parte dallo slot piu' vecchio della finestra e si cammina in avanti,
  // cosi' chi disegna riceve i campioni gia' in ordine di tempo e non deve
  // sapere niente di come e' fatto l'anello.
  const uint32_t primo = h.slotUltimo - (uint32_t)(n - 1);
  for (int i = 0; i < n; i++) {
    out[i] = h.t[(primo + (uint32_t)i) % TH_SLOTS];
  }
  if (tsUltimo) *tsUltimo = (time_t)h.slotUltimo * (time_t)TH_SLOT_S;
  return n;
}

int remote_temp_campioni(int index) {
  if (index < 0 || index >= s_count) return 0;
  const TempHist& h = s_thist[index];
  if (!h.avviato) return 0;
  int n = 0;
  for (int i = 0; i < TH_SLOTS; i++) if (h.t[i] != TH_VUOTO) n++;
  return n;
}

void remote_seed_temp(const uint8_t mac[6], time_t ts, float tempC) {
  if (mac == nullptr) return;
  for (int i = 0; i < s_count; i++) {
    if (memcmp(s_nodi[i].mac, mac, 6) != 0) continue;
    thPush(i, ts, tempC);
    return;
  }
}

void remote_seed_pressure(const uint8_t mac[6], time_t ts, float pressHpa) {
  if (mac == nullptr) return;
  for (int i = 0; i < s_count; i++) {
    if (memcmp(s_nodi[i].mac, mac, 6) != 0) continue;
    histPush(i, ts, pressHpa);
    s_nodi[i].storicoSlot = (uint8_t)s_hist[i].count;
    return;
  }
}

const char* remote_trend_label(uint8_t trend) {
  return forecast_trend_label((forecast_trend_t)trend);
}

const char* remote_forecast_text(const RemoteNode* n) {
  if (n == nullptr) return "non ancora noto";
  return forecast_text((forecast_trend_t)n->trend, n->pressSeaHpa);
}
