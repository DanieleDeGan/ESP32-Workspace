/**
 * Diag_Hub.ino — hub diagnostico ESP-NOW, SOLO Serial (per ESP32-S3, gira su
 * qualunque ESP32)
 *
 * Display / LVGL / touch / SD SPENTI di proposito: e' il Test 1 del §10 del
 * documento di analisi. Riceve i broadcast di Diag_Node e misura il tasso di
 * perdita REALE contando i buchi nel numero di sequenza. Nessuna libreria
 * EspNowLink, nessun pairing, nessun peer: per ricevere un broadcast in
 * esp_now.h grezzo basta registrare la recv callback.
 *
 * ARCHITETTURA (§2, il punto piu' importante su questa board): la recv callback
 * NON stampa e NON fa lavoro — copia il pacchetto in una coda FreeRTOS e basta.
 * Tutto il parsing/statistiche/stampa avviene in loop() (che qui fa da task
 * consumatore, non avendo altro da fare). Cosi' lo stack radio non resta mai
 * bloccato dalla seriale e non perde i pacchetti successivi.
 *
 * Riferimenti al documento:
 *  §1 canale fisso impostato DOPO esp_now_init e verificato a runtime
 *  §2 recv callback cortissima -> coda -> consumatore
 *  §6 seq/dup: i buchi danno il tasso di perdita, i duplicati vanno scartati
 *  §8 struct packed a larghezza fissa, IDENTICA a quella del nodo
 *  §10 contatori: rx, gap, dup, rssi last/min, drop di coda
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ===== CONFIG condivisa hub<->nodo: TIENI IDENTICA NEI DUE SKETCH =====
#define DIAG_CHANNEL   6
#define DIAG_VERSION   1
// ======================================================================

// TIENI QUESTA STRUCT IDENTICA A QUELLA DEL NODO (§8).
typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint8_t  node_id;
    uint16_t boot_count;
    uint32_t seq;
    uint32_t t_ms;
} diag_packet_t;
static_assert(sizeof(diag_packet_t) == 12, "diag_packet_t deve essere 12 byte su entrambi i lati");

// Elemento di coda: pacchetto grezzo + metadati di ricezione. Payload
// sovradimensionato per catturare anche traffico ESP-NOW piu' lungo del nostro.
typedef struct {
    uint8_t  mac[6];
    int8_t   rssi;
    uint8_t  len;
    uint32_t rx_ms;
    uint8_t  payload[64];
} rx_item_t;

static QueueHandle_t s_rx_queue = nullptr;
static volatile uint32_t s_queue_drop = 0;   // coda piena = consumatore in ritardo (§10)

// Statistiche per-nodo (§10), piccola tabella indicizzata per node_id.
#define MAX_NODES 8
typedef struct {
    bool     used;
    uint8_t  node_id;
    uint8_t  mac[6];
    uint16_t boot_count;
    uint32_t last_seq;
    uint32_t rx_count;
    uint32_t gap_count;    // pacchetti mancanti (somma dei buchi nel seq)
    uint32_t dup_count;
    int8_t   rssi_last;
    int8_t   rssi_min;     // 0 = non ancora impostato (RSSI reale e' sempre < 0)
    uint32_t last_rx_ms;
} node_stat_t;
static node_stat_t s_nodes[MAX_NODES];

// ---------------------------------------------------------------------------
// recv callback: SOLO enqueue, zero lavoro/log (§2). Firma IDF 5.5 / core 3.3.x.
// ---------------------------------------------------------------------------
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    rx_item_t it;
    memcpy(it.mac, info->src_addr, 6);
    it.rssi  = (info->rx_ctrl) ? (int8_t)info->rx_ctrl->rssi : 0;
    it.len   = (len > (int)sizeof(it.payload)) ? sizeof(it.payload) : (uint8_t)len;
    it.rx_ms = millis();
    memcpy(it.payload, data, it.len);

    // La recv callback ESP-NOW gira nel TASK WiFi (non in un ISR hardware):
    // xQueueSend normale con timeout 0 e' la primitiva corretta. Il documento
    // usa la variante FromISR per prudenza, ma qui non siamo in contesto ISR.
    if (xQueueSend(s_rx_queue, &it, 0) != pdTRUE) {
        s_queue_drop++;
    }
}

static node_stat_t *get_slot(uint8_t node_id, const uint8_t mac[6])
{
    for (int i = 0; i < MAX_NODES; i++)
        if (s_nodes[i].used && s_nodes[i].node_id == node_id) return &s_nodes[i];
    for (int i = 0; i < MAX_NODES; i++)
        if (!s_nodes[i].used) {
            s_nodes[i].used     = true;
            s_nodes[i].node_id  = node_id;
            memcpy(s_nodes[i].mac, mac, 6);
            s_nodes[i].rssi_min = 0;
            return &s_nodes[i];
        }
    return nullptr;   // tabella piena
}

static void handle_packet(const rx_item_t *it)
{
    if (it->len != sizeof(diag_packet_t)) return;   // non e' un nostro pacchetto
    diag_packet_t p;
    memcpy(&p, it->payload, sizeof(p));
    if (p.version != DIAG_VERSION) return;

    node_stat_t *n = get_slot(p.node_id, it->mac);
    if (!n) return;

    bool restarted = (n->rx_count > 0) && (p.boot_count != n->boot_count);
    if (n->rx_count == 0 || restarted) {
        // Primo pacchetto, o nodo riavviato: (ri)aggancio il tracking al seq
        // corrente senza contarlo come buco.
        n->boot_count = p.boot_count;
        n->last_seq   = p.seq;
        if (restarted) {
            Serial.printf("[NODE %u] RIAVVIO rilevato (boot_count=%u): reset tracking seq\n",
                          p.node_id, p.boot_count);
        }
    } else if (p.seq == n->last_seq + 1) {
        // sequenza perfetta, niente da segnalare
        n->last_seq = p.seq;
    } else if (p.seq <= n->last_seq) {
        // Un salto all'indietro GRANDE = il nodo e' ripartito (reset/reflash)
        // con seq azzerato ma boot_count NON incrementato (su questo C3 la RTC
        // memory non sopravvive al reset USB, vedi note): ri-aggancia invece di
        // contare un diluvio di falsi duplicati. Un salto piccolo e' invece un
        // vero duplicato/riordino di livello MAC (§6).
        if (n->last_seq - p.seq > 100) {
            Serial.printf("[NODE %u] seq indietro (%lu -> %lu): ri-aggancio (nodo ripartito)\n",
                          p.node_id, (unsigned long)n->last_seq, (unsigned long)p.seq);
            n->last_seq = p.seq;
        } else {
            n->dup_count++;
        }
    } else {
        uint32_t missed = p.seq - n->last_seq - 1;
        n->gap_count += missed;
        Serial.printf("[NODE %u] BUCO: atteso seq %lu, arrivato %lu (persi %lu)\n",
                      p.node_id, (unsigned long)(n->last_seq + 1),
                      (unsigned long)p.seq, (unsigned long)missed);
        n->last_seq = p.seq;
    }

    n->rx_count++;
    n->rssi_last = it->rssi;
    if (n->rssi_min == 0 || it->rssi < n->rssi_min) n->rssi_min = it->rssi;
    n->last_rx_ms = it->rx_ms;
}

static void print_summary(void)
{
    for (int i = 0; i < MAX_NODES; i++) {
        node_stat_t *n = &s_nodes[i];
        if (!n->used) continue;
        uint32_t expected = n->rx_count + n->gap_count;   // ricevuti + persi
        float loss = expected ? (100.0f * n->gap_count / expected) : 0.0f;
        Serial.printf("[STAT node %u %02X:%02X:%02X:%02X:%02X:%02X] rx=%lu persi=%lu (%.1f%%) dup=%lu rssi=%d/min%d drop_coda=%lu\n",
                      n->node_id,
                      n->mac[0], n->mac[1], n->mac[2], n->mac[3], n->mac[4], n->mac[5],
                      (unsigned long)n->rx_count, (unsigned long)n->gap_count, loss,
                      (unsigned long)n->dup_count, n->rssi_last, n->rssi_min,
                      (unsigned long)s_queue_drop);
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

    s_rx_queue = xQueueCreate(32, sizeof(rx_item_t));
    if (s_rx_queue == nullptr) {
        Serial.println("[ERR] creazione coda fallita");
        return;
    }

    WiFi.mode(WIFI_STA);
    // §1: nessun AP. Via l'autoreconnect e le credenziali salvate, altrimenti
    // la radio puo' finire sul canale dell'AP e diventare sorda all'ESP-NOW.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, true);

    uint32_t t0 = millis();
    while (!WiFi.STA.started() && millis() - t0 < 5000) delay(50);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERR] esp_now_init fallito");
        return;
    }

    // Modalita' Long Range (LR): deve combaciare col nodo (vedi Diag_Node).
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

    esp_wifi_set_channel(DIAG_CHANNEL, WIFI_SECOND_CHAN_NONE);   // §1: dopo l'init
    esp_wifi_set_ps(WIFI_PS_NONE);                               // §7: niente modem-sleep
    esp_now_register_recv_cb(on_recv);

    uint8_t primary = 0; wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    uint8_t proto = 0;
    esp_wifi_get_protocol(WIFI_IF_STA, &proto);
    Serial.printf("\n[BOOT] HUB canale_reale=%u protocol=0x%02X (LR %s) MAC=%s\n",
                  primary, proto, (proto & WIFI_PROTOCOL_LR) ? "ATTIVO" : "OFF",
                  WiFi.macAddress().c_str());
    if (primary != DIAG_CHANNEL) {
        Serial.printf("[BOOT] ATTENZIONE: canale reale %u != DIAG_CHANNEL %u\n", primary, DIAG_CHANNEL);
    }
    Serial.println("[READY] in ascolto broadcast (nessun pairing, nessun peer). Riepilogo ogni 5s.");
}

void loop()
{
    // Consumatore della coda: fa TUTTO il lavoro fuori dalla recv callback (§2).
    rx_item_t it;
    while (xQueueReceive(s_rx_queue, &it, pdMS_TO_TICKS(100)) == pdTRUE) {
        handle_packet(&it);
    }

    static uint32_t last_summary = 0;
    if (millis() - last_summary >= 5000) {
        last_summary = millis();
        print_summary();
    }
}
