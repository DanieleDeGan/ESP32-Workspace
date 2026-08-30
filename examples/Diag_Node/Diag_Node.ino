/**
 * Diag_Node.ino — nodo diagnostico ESP-NOW in BROADCAST (per ESP32-C3 Supermini)
 *
 * Il piu' semplice dei sistemi per capire i pacchetti persi/ritardi: nessuna
 * libreria EspNowLink, nessun pairing, nessun unicast, nessun retry. Solo
 * esp_now.h grezzo. Spara un contatore in broadcast a intervallo fisso; l'hub
 * (examples/Diag_Hub) misura quanti ne arrivano contando i buchi nel seq.
 *
 * Gira su QUALUNQUE ESP32, ma e' pensato per il nodo C3.
 *
 * Riferimenti alle sezioni del documento di analisi ESP-NOW:
 *  §1 canale fisso impostato DOPO esp_now_init e verificato a runtime
 *  §4 esp_reset_reason() + boot_count in RTC (un brownout non e' un "pacchetto perso")
 *  §6 in broadcast la send callback riporta SEMPRE successo: non e' un ACK
 *  §8 struct packed a larghezza fissa, definizione da tenere IDENTICA sull'hub
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>

// ===== CONFIG condivisa hub<->nodo: TIENI IDENTICA NEI DUE SKETCH =====
#define DIAG_CHANNEL      6      // canale WiFi fisso, uguale su hub e nodo (§1)
#define DIAG_VERSION      1
// ======================================================================

// ID di QUESTO nodo. Cambialo (2, 3, ...) se accendi piu' nodi insieme, cosi'
// l'hub li distingue e tiene statistiche separate.
#define NODE_ID           1

#define SEND_INTERVAL_MS  500   // 2 pacchetti/s: gentile, ma se qui gia' si perde e' significativo

// Pacchetto diagnostico — 12 byte, tipi a larghezza fissa + packed (§8: mai
// int/long/bool/enum nudi tra chip di architettura diversa, S3 Xtensa vs C3
// RISC-V). TIENI QUESTA STRUCT IDENTICA A QUELLA DELL'HUB.
typedef struct __attribute__((packed)) {
    uint8_t  version;      // = DIAG_VERSION
    uint8_t  node_id;      // NODE_ID del mittente
    uint16_t boot_count;   // incrementa a ogni boot; l'hub azzera il tracking se cambia
    uint32_t seq;          // contatore per-boot, +1 a ogni invio
    uint32_t t_ms;         // millis() all'invio
} diag_packet_t;
static_assert(sizeof(diag_packet_t) == 12, "diag_packet_t deve essere 12 byte su entrambi i lati");

static const uint8_t BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Sopravvive a reset/deep-sleep, azzerato solo a un power-on freddo: distingue
// un riavvio del nodo da una perdita radio (§4).
RTC_DATA_ATTR static uint16_t s_boot_count = 0;

static uint32_t s_seq = 0;
static uint32_t s_tx_ok = 0;
static uint32_t s_tx_fail = 0;
static int8_t   s_tx_pwr_default = 0;   // potenza TX letta prima di forzarla al max (unita' 0.25 dBm)

// Send callback (firma IDF 5.5 / core 3.3.x: prende esp_now_send_info_t*, NON
// piu' const uint8_t* come negli esempi vecchi del documento §5).
static void on_sent(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    // §6: in broadcast lo status e' SEMPRE SUCCESS — non e' un ACK applicativo,
    // dice solo che il frame e' uscito dalla radio locale. Lo contiamo comunque
    // per intercettare eventuali fallimenti LOCALI di TX (radio occupata, ecc.).
    if (status == ESP_NOW_SEND_SUCCESS) s_tx_ok++; else s_tx_fail++;
}

static void print_boot_banner(void)
{
    esp_reset_reason_t rr = esp_reset_reason();
    uint8_t primary = 0; wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    Serial.printf("\n[BOOT] node_id=%u boot_count=%u reset_reason=%d canale_reale=%u MAC=%s\n",
                  NODE_ID, s_boot_count, (int)rr, primary, WiFi.macAddress().c_str());
    int8_t pwr_now = 0;
    esp_wifi_get_max_tx_power(&pwr_now);
    Serial.printf("[BOOT] tx_power: default=%d (%.2f dBm) -> attuale=%d (%.2f dBm)\n",
                  s_tx_pwr_default, s_tx_pwr_default * 0.25f, pwr_now, pwr_now * 0.25f);
    uint8_t proto = 0;
    esp_wifi_get_protocol(WIFI_IF_STA, &proto);
    Serial.printf("[BOOT] protocol=0x%02X (LR %s)\n", proto,
                  (proto & WIFI_PROTOCOL_LR) ? "ATTIVO" : "OFF");
    if (primary != DIAG_CHANNEL) {
        Serial.printf("[BOOT] ATTENZIONE: canale reale %u != DIAG_CHANNEL %u — i pacchetti non arriveranno!\n",
                      primary, DIAG_CHANNEL);
    }
}

void setup()
{
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    // Su C3/S3 la Serial dell'USB e' la CDC del chip, non una UART: se il PC
    // ha riconosciuto la porta e nessuno la sta leggendo, il buffer si riempie
    // e ogni print() BLOCCA loop(). Con timeout 0 il log si butta invece di
    // fermare lo sketch. Il #if serve perche' con CDC On Boot: Disabled la
    // Serial torna a essere una UART, che quel metodo non ce l'ha.
    Serial.setTxTimeoutMs(0);
#endif
    delay(300);
    s_boot_count++;   // conta questo boot (in RTC memory)

    WiFi.mode(WIFI_STA);
    // §1: nessuna connessione ad AP. Disabilitiamo l'autoreconnect e cancelliamo
    // eventuali credenziali salvate, altrimenti un WiFi.begin() precedente puo'
    // riportare la radio sul canale dell'AP e sordificare l'ESP-NOW.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, true);

    uint32_t t0 = millis();
    while (!WiFi.STA.started() && millis() - t0 < 5000) delay(50);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERR] esp_now_init fallito");
        return;
    }

    // Modalita' Long Range (LR): PHY proprietaria ESP32, ~+10-20 dB di
    // sensibilita' in ricezione a scapito del bitrate (irrilevante: 12 byte
    // ogni 500ms). DEVE essere attiva su ENTRAMBI i lati (hub e nodo) o non si
    // parlano. In LR-only la radio non fa piu' WiFi 11b/g/n normale.
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

    // Canale fisso DOPO esp_now_init (§1): impostato prima verrebbe perso.
    esp_wifi_set_channel(DIAG_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_ps(WIFI_PS_NONE);   // niente modem-sleep (§7)

    // Potenza TX: leggiamo il default, poi la portiamo al massimo del chip.
    // Unita' = 0.25 dBm, quindi 84 = 21 dBm (max del C3). Alza il segnale
    // IRRADIATO (piu' RSSI al ricevente) ma NON migliora la sensibilita' in
    // ricezione, e non compensa un'antenna scadente. Il default e' di norma
    // gia' vicino al massimo: guadagno atteso pochi dB.
    esp_wifi_get_max_tx_power(&s_tx_pwr_default);
    esp_wifi_set_max_tx_power(84);

    esp_now_register_send_cb(on_sent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
    peer.channel = DIAG_CHANNEL;      // forziamo 6 (0 = "canale corrente")
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ERR] add_peer broadcast fallito");
        return;
    }

    print_boot_banner();
    Serial.printf("[READY] broadcast ogni %d ms su canale %d\n", SEND_INTERVAL_MS, DIAG_CHANNEL);
}

void loop()
{
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < SEND_INTERVAL_MS) return;
    last = now;

    diag_packet_t p;
    p.version    = DIAG_VERSION;
    p.node_id    = NODE_ID;
    p.boot_count = s_boot_count;
    p.seq        = s_seq++;
    p.t_ms       = now;

    esp_err_t r = esp_now_send(BROADCAST_ADDR, (const uint8_t *)&p, sizeof(p));

    // Log ogni 20 invii: una riga seriale a 115200 costa ~7ms bloccanti (§2),
    // non vogliamo che la stampa stessa introduca ritardo su ogni invio.
    if ((p.seq % 20) == 0) {
        Serial.printf("[TX] seq=%lu tx_ok=%lu tx_fail=%lu send()=%s\n",
                      (unsigned long)p.seq, (unsigned long)s_tx_ok,
                      (unsigned long)s_tx_fail, (r == ESP_OK) ? "OK" : "ERR");
    }
}
