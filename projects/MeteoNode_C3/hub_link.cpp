#include "hub_link.h"
#include <esp_wifi.h>

#include <WiFi.h>
#include <string.h>

static bool     s_ready   = false;
static bool     s_paired  = false;
static uint8_t  s_channel = 0;
static uint32_t s_ok      = 0;
static uint32_t s_fail    = 0;
static char     s_hubMac[18] = "-";
static uint8_t  s_hubMacBytes[6] = {0};
static bool     s_hubMacOk = false;

// Chiamata dal task del driver WiFi: deve restare cortissima. Qui copia
// sei byte e alza un flag, niente di piu' — la stessa regola dei callback
// LVGL e di quelli ESP-NOW sull'hub.
static void onLinkMessage(const uint8_t mac[6], const link_message_t *msg) {
  if (msg == nullptr || msg->msg_type != LINK_MSG_WELCOME) return;
  snprintf(s_hubMac, sizeof(s_hubMac), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  memcpy(s_hubMacBytes, mac, 6);
  s_hubMacOk = true;
}

bool hub_begin(const char* node_name) {
  return hub_begin_ex(node_name, 0);
}

bool hub_begin_ex(const char* node_name, uint8_t canale_scelto) {
  const char* nome = (node_name && node_name[0]) ? node_name : "MeteoNode";

  // Connessi all'AP: il canale lo detta il router, quindi non si tocca
  // (vedi la nota in hub_link.h). Senza AP si usa il canale fisso della
  // libreria, che e' anche la strada che servira' col deep sleep.
  const bool suWifi = (WiFi.status() == WL_CONNECTED);
  uint8_t canale = suWifi ? ESPNOW_LINK_CHANNEL_CURRENT : ESPNOW_LINK_CHANNEL;
  if (canale_scelto != 0) canale = canale_scelto;   // risveglio: lo sa il chiamante

  Link_OnMessage(onLinkMessage);
  s_ready = Link_InitEx(LINK_NODE_SENSOR_TEMPERATURE, nome, canale);
  if (!s_ready) {
    Serial.println(F("[ESP-NOW] init fallita: il nodo continua senza trasmettere."));
    return false;
  }

  // Il canale VERO, non quello richiesto: con ESPNOW_LINK_CHANNEL_CURRENT
  // il numero lo conosce solo lo stack WiFi, ed e' quello che deve
  // combaciare con l'hub. Stamparlo qui e' meta' della diagnostica di un
  // pairing che non parte.
  s_channel = (canale != ESPNOW_LINK_CHANNEL_CURRENT) ? canale : WiFi.channel();
  Serial.printf("[ESP-NOW] nodo \"%s\" attivo sul canale %u (%s)\n",
                nome, s_channel,
                canale_scelto ? "imparato prima di dormire"
                              : (suWifi ? "quello dell'AP" : "fisso, nessun AP"));
  return true;
}

void hub_loop() {
  if (!s_ready) return;
  Link_Node_Poll();   // HELLO in broadcast finche' non associato

  const bool ora = Link_Node_IsPaired();
  if (ora && !s_paired) {
    Serial.printf("[ESP-NOW] associato all'hub %s\n", s_hubMac);
  } else if (!ora && s_paired) {
    Serial.println(F("[ESP-NOW] associazione persa."));
  }
  s_paired = ora;
}

bool hub_ready()  { return s_ready; }
bool hub_paired() { return s_ready && Link_Node_IsPaired(); }
uint8_t hub_channel() { return s_channel; }
const char* hub_hub_mac() { return s_hubMac; }

bool hub_send_measure(float tempC, float humPct, float pressHpa, uint16_t battery_mv) {
  if (!s_ready || !Link_Node_IsPaired()) return false;

  link_message_t msg = {};
  // protocol_version/msg_type/node_type/name/seq li timbra la libreria: qui
  // si riempiono solo batteria e valori.
  msg.battery_mv = battery_mv;
  msg.value[0] = tempC;
  msg.value[1] = humPct;
  // Pressione GREZZA della stazione, non riportata al livello del mare: la
  // correzione dipende dall'altitudine, che su questo nodo e' ancora il
  // default da 40 m mai calibrato. Trasmettendo il valore corretto si
  // scriverebbe un errore sistematico dentro lo storico dell'hub, per
  // sempre; trasmettendo la misura, l'hub puo' applicare la quota giusta il
  // giorno che la si conosce. Il trend barometrico — cioe' la previsione —
  // non cambia in nessuno dei due casi: e' un offset costante.
  msg.value[2] = pressHpa;

  const bool ok = Link_Node_SendData(&msg);
  if (ok) s_ok++; else s_fail++;
  return ok;
}

bool hub_hub_mac_bytes(uint8_t out[6]) {
  if (!s_hubMacOk || out == nullptr) return false;
  memcpy(out, s_hubMacBytes, 6);
  return true;
}

bool hub_resume(const char* node_name, uint8_t canale, const uint8_t hub_mac[6]) {
  if (!hub_begin_ex(node_name, canale)) return false;
  if (hub_mac == nullptr) return false;
  if (!Link_Node_ResumeWithHub(hub_mac)) return false;

  memcpy(s_hubMacBytes, hub_mac, 6);
  s_hubMacOk = true;
  snprintf(s_hubMac, sizeof(s_hubMac), "%02X:%02X:%02X:%02X:%02X:%02X",
           hub_mac[0], hub_mac[1], hub_mac[2], hub_mac[3], hub_mac[4], hub_mac[5]);
  s_paired = true;
  Serial.printf("[ESP-NOW] ripreso con l'hub %s senza rifare il pairing\n", s_hubMac);
  return true;
}

void     hub_seq_set(uint32_t seq) { Link_Node_SetSeq(seq); }
uint32_t hub_seq_get()             { return Link_Node_GetSeq(); }

uint32_t hub_sent_ok()   { return s_ok; }
uint32_t hub_sent_fail() { return s_fail; }

// I tre non sovrapposti per primi: e' dove sta la quasi totalita' dei router
// domestici, quindi nel caso normale la ricerca finisce in un colpo o due. Gli
// altri dieci restano in coda perche' un AP puo' comunque finirci, e provarli
// costa solo quando i primi tre hanno gia' fallito.
static const uint8_t CANALI_PROVA[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};

// Timeout per tentativo. L'ACK di ESP-NOW e' di livello radio: se il peer e'
// su quel canale risponde in millisecondi, se non c'e' non risponde affatto.
// 200 ms sono larghi per il primo caso e corti abbastanza da tenere il caso
// peggiore (13 canali, hub spento) sotto i tre secondi.
static const uint32_t SCAN_ACK_MS = 200;

uint8_t hub_scan_channels() {
  if (!s_ready || !Link_Node_IsPaired()) return 0;

  // Il canale da cui si parte, per poterci tornare. Con
  // ESPNOW_LINK_CHANNEL_CURRENT (0) il numero non lo sa la libreria: lo si
  // chiede alla radio, o al ripristino si passerebbe uno zero non valido.
  uint8_t partenza = Link_GetChannel();
  if (partenza == 0) {
    uint8_t primario = 0;
    wifi_second_chan_t secondario;
    if (esp_wifi_get_channel(&primario, &secondario) == ESP_OK) partenza = primario;
  }

  for (size_t i = 0; i < sizeof(CANALI_PROVA); i++) {
    const uint8_t c = CANALI_PROVA[i];
    if (c == partenza) continue;              // gia' provato: e' quello fallito
    if (!Link_SetChannel(c)) continue;

    // Lo STESSO messaggio, non uno nuovo: Link_Node_SendData incrementerebbe
    // il seq ad ogni tentativo e l'hub leggerebbe quei salti come pacchetti
    // persi sulla tratta radio.
    if (Link_Node_ResendLast(1, SCAN_ACK_MS)) {
      s_channel = c;
      s_ok++;
      Serial.printf("[canale] hub ritrovato sul canale %u (era %u)\n", c, partenza);
      return c;
    }
  }

  // Nessuno risponde: l'hub non e' altrove, e' giu'. Si torna da dove si era
  // partiti, o il prossimo risveglio comincerebbe dall'ultimo canale provato -
  // scelto a caso, e per giunta quello meno probabile visto che ha appena
  // fallito.
  if (partenza >= 1 && partenza <= 13) {
    Link_SetChannel(partenza);
    s_channel = partenza;
  }
  Serial.println(F("[canale] nessun canale risponde: l'hub sembra giu'"));
  return 0;
}
