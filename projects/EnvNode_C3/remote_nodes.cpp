#include "remote_nodes.h"
#include "rtc_time.h"

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
static uint32_t   s_pairingFineMs = 0;

// ---------------------------------------------------------------------
//  Helper
// ---------------------------------------------------------------------
static RemoteNode* trovaOCrea(const uint8_t mac[6]) {
  for (int i = 0; i < s_count; i++) {
    if (memcmp(s_nodi[i].mac, mac, 6) == 0) return &s_nodi[i];
  }
  if (s_count >= REMOTE_MAX_NODES) return nullptr;
  RemoteNode* r = &s_nodi[s_count++];
  memset(r, 0, sizeof(*r));
  memcpy(r->mac, mac, 6);
  r->sogliaMutoS = SOGLIA_DEFAULT_S;
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
        r->persi += (dato.seq - r->seq - 1);
        // La cadenza si impara solo dai messaggi in sequenza: un delta
        // misurato a cavallo di un riavvio del nodo non e' la sua cadenza.
        if (deltaS >= 1 && deltaS <= INTERVALLO_MAX_S) {
          r->intervalloS = (r->intervalloS == 0)
                           ? deltaS
                           : (r->intervalloS * 3 + deltaS) / 4;   // media mobile
        }
      } else {
        // seq tornato indietro: il nodo e' ripartito da zero. Con nodi a
        // batteria e' un'informazione che vale, perche' un nodo che si
        // riavvia spesso ha un problema di alimentazione, non di radio.
        r->riavvii++;
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
  }
}

// ---------------------------------------------------------------------
//  API pubblica
// ---------------------------------------------------------------------
bool remote_begin(const char* selfName) {
  const char* nome = (selfName && selfName[0]) ? selfName : "EnvNode";

  // ESPNOW_LINK_CHANNEL_CURRENT (0) e non un canale esplicito: vedi la nota
  // in testa a remote_nodes.h. Questa scheda sta su un AP, il canale lo
  // decide il router e forzarlo farebbe cadere la connessione.
  s_ready = Link_InitEx(LINK_NODE_HUB, nome, ESPNOW_LINK_CHANNEL_CURRENT);
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
  remote_pairing_open(REMOTE_PAIRING_BOOT_S);
  return true;
}

bool remote_ready() { return s_ready; }

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
    for (int j = i; j < s_count - 1; j++) s_nodi[j] = s_nodi[j + 1];
    s_count--;
    memset(&s_nodi[s_count], 0, sizeof(s_nodi[s_count]));
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
