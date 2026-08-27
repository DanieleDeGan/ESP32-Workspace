#include "link_peer.h"
#include <esp_wifi.h>
#include <esp_now.h>

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include "esp_wifi.h"

link_node_type_t g_link_self_type = LINK_NODE_UNKNOWN;
char g_link_self_name[LINK_NAME_LEN] = {0};
uint8_t g_link_channel = ESPNOW_LINK_CHANNEL;

static Link_MessageCb s_message_cb = nullptr;

void Link_OnMessage(Link_MessageCb cb)
{
    s_message_cb = cb;
}

void link_notify_app(const uint8_t mac[6], const link_message_t *msg)
{
    if (s_message_cb) {
        s_message_cb(mac, msg);
    }
}

bool link_parse_message(const uint8_t *data, int len, link_message_t *out)
{
    if (data == nullptr || out == nullptr || len != (int)sizeof(link_message_t)) {
        return false;
    }
    memcpy(out, data, sizeof(link_message_t));
    return out->protocol_version == LINK_PROTOCOL_VERSION;
}

LinkPeer::LinkPeer(const uint8_t *mac_addr, uint8_t channel)
    : ESP_NOW_Peer(mac_addr, channel, WIFI_IF_STA, nullptr)
{
    ackSem = xSemaphoreCreateBinary();
}

LinkPeer::~LinkPeer()
{
    if (ackSem) {
        vSemaphoreDelete(ackSem);
    }
}

bool LinkPeer::addPeer()
{
    return add();
}

bool LinkPeer::sendMessage(const link_message_t &msg)
{
    return send(reinterpret_cast<const uint8_t *>(&msg), (int)sizeof(msg)) == sizeof(msg);
}

bool LinkPeer::sendReliable(const link_message_t &msg, int max_attempts, uint32_t ack_timeout_ms)
{
    if (ackSem == nullptr) {
        return sendMessage(msg);   // fallback estremo: niente semaforo, niente conferma
    }

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        xSemaphoreTake(ackSem, 0);   // scarta un eventuale segnale residuo di un invio precedente
        lastAckSuccess = false;

        bool queued = sendMessage(msg);
        bool gotSem = queued && (xSemaphoreTake(ackSem, pdMS_TO_TICKS(ack_timeout_ms)) == pdTRUE);
        if (gotSem) {
            if (lastAckSuccess) {
                return true;   // consegna confermata via onSent()
            }
        }
        if (attempt < max_attempts - 1) {
            // Backoff crescente + jitter casuale prima del prossimo tentativo:
            // se la collisione e' stata con un altro nodo che ha fallito nello
            // stesso istante, un ritardo fisso identico rischierebbe di farli
            // ricollidere di nuovo al retry successivo.
            delay(30 + attempt * 40 + random(0, 60));
        }
    }
    return false;
}

void LinkPeer::onSent(bool success)
{
    lastAckSuccess = success;
    if (ackSem) {
        xSemaphoreGive(ackSem);
    }
}

void LinkPeer::onReceive(const uint8_t *data, size_t len, bool broadcast)
{
    link_message_t msg;
    if (!link_parse_message(data, (int)len, &msg)) {
        return;   // scarta traffico non nostro (altro protocollo/versione sullo stesso canale)
    }
    lastSeenMs = millis();

    // Nome e tipo si aggiornano ad OGNI messaggio, non solo al primo: sono
    // anagrafica del mittente e viaggiano in tutti i pacchetti. Senza questo,
    // un peer conserva per sempre l'identita' con cui e' stato scoperto, e
    // basta riprogrammare un nodo cambiandogli nome perche' l'hub continui a
    // mostrare quello vecchio - con i valori nuovi, che e' il modo peggiore
    // di sbagliare. Trovato sul serio il 2026-08-23: una scheda che aveva
    // addosso Link_Node_Demo si e' associata come "TempTest", poi e' stata
    // riprogrammata come nodo meteo, e l'hub ha continuato a chiamarla
    // TempTest mostrando temperatura, umidita' e pressione vere.
    //
    // Il tipo conta anche di piu' del nome: la UI ci sceglie le unita' di
    // misura, quindi un tipo vecchio significa numeri etichettati male.
    nodeType = (link_node_type_t)msg.node_type;
    strncpy(name, msg.name, LINK_NAME_LEN - 1);
    name[LINK_NAME_LEN - 1] = '\0';

    if (msg.msg_type == LINK_MSG_DATA) {
        lastData = msg;
        hasData = true;
    } else if (msg.msg_type == LINK_MSG_HELLO) {
        // Un peer gia' noto che manda di nuovo HELLO ha perso il proprio stato
        // di pairing (es. riavvio) senza che l'hub lo dimenticasse (i peer non
        // vengono mai rimossi in questo giro): rimandagli il WELCOME invece di
        // lasciarlo bloccato in attesa per sempre (l'hub non richiamerebbe mai
        // altrimenti hub_on_new_peer per un MAC gia' registrato).
        welcomePending = true;
    }
    link_notify_app(addr(), &msg);
}

bool Link_Init(link_node_type_t self_type, const char *self_name)
{
    return Link_InitEx(self_type, self_name, ESPNOW_LINK_CHANNEL);
}

bool Link_InitEx(link_node_type_t self_type, const char *self_name, uint8_t channel)
{
    g_link_self_type = self_type;
    g_link_channel = channel;
    strncpy(g_link_self_name, self_name ? self_name : "", LINK_NAME_LEN - 1);
    g_link_self_name[LINK_NAME_LEN - 1] = '\0';

    WiFi.mode(WIFI_STA);
    uint32_t wait_start = millis();
    while (!WiFi.STA.started()) {
        if (millis() - wait_start > 5000) {
            return false;   // evita un hang infinito se il driver WiFi non parte
        }
        delay(100);
    }

    if (!ESP_NOW.begin()) {
        return false;
    }

    // Le impostazioni sotto vanno fatte DOPO esp_now_init()/ESP_NOW.begin()
    // (che chiama anche esp_wifi_start()), non prima: un WiFi.setChannel()
    // chiamato prima viene ignorato silenziosamente su alcune combinazioni di
    // chip (confermato empiricamente tra un hub ESP32-S3 e un nodo ESP32
    // "classico") — l'invio locale sembra riuscire (in coda) ma la consegna
    // over-the-air fallisce sempre (onSent() sempre false) perche' il frame
    // esce sul canale sbagliato.
    //
    // Con ESPNOW_LINK_CHANNEL_CURRENT (0) il canale NON si tocca: e' il caso
    // del dispositivo connesso a un AP, dove forzarlo farebbe cadere la
    // connessione WiFi (il canale lo detta l'AP).
    if (channel != ESPNOW_LINK_CHANNEL_CURRENT) {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }

    // Stesso bitmask di protocollo su entrambi i lati: chip ESP32 di
    // generazioni diverse possono avere default diversi, causa nota di
    // mancata consegna unicast ESP-NOW pur con invio locale apparentemente
    // riuscito.
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    // Il risparmio energetico del modem WiFi (power-save) e' un'altra causa
    // nota di mancata consegna di frame ESP-NOW unicast (il broadcast arriva
    // comunque, l'unicast puo' essere perso se il ricevente e' addormentato
    // nel momento sbagliato): disabilitato esplicitamente su entrambi i lati.
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (self_type == LINK_NODE_HUB) {
        link_hub_start();
    } else {
        link_node_start();
    }
    return true;
}

bool Link_SetChannel(uint8_t channel)
{
    if (channel < 1 || channel > 13) {
        return false;
    }
    if (channel == g_link_channel) {
        return true;   // gia' li': non si tocca la radio per niente
    }

    if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        return false;
    }
    g_link_channel = channel;

    // I peer gia' registrati portano dentro il driver il canale VECCHIO, e un
    // invio userebbe quello: spostare la radio senza spostare loro non
    // servirebbe a niente. Si itera il registro del driver invece dei peer
    // dei due ruoli, cosi' questa funzione non deve sapere se gira su un hub
    // o su un nodo — e prende anche il peer broadcast.
    esp_now_peer_info_t info;
    bool from_head = true;
    while (esp_now_fetch_peer(from_head, &info) == ESP_OK) {
        from_head = false;
        info.channel = channel;
        esp_now_mod_peer(&info);
    }
    return true;
}

uint8_t Link_GetChannel(void)
{
    return g_link_channel;
}
