/**
 * link_node.cpp
 *
 * Ruolo nodo/periferica: annuncia HELLO in broadcast finche' non associato,
 * impara il MAC dell'hub dal WELCOME (via il proprio onNewPeer, simmetrico a
 * quello dell'hub — vedi la nota in EspNowLink.h sulla scoperta
 * bidirezionale), poi invia DATA in unicast.
 */

#include "link_peer.h"

#include <Arduino.h>
#include <string.h>

#define LINK_HELLO_INTERVAL_MS 2000

static LinkPeer *s_broadcast_peer = nullptr;
static LinkPeer *s_hub_peer = nullptr;
static bool s_paired = false;

// Ultimo DATA timbrato e spedito, conservato per poterlo rimandare tale e
// quale su un altro canale (vedi Link_Node_ResendLast).
static link_message_t s_last_data = {};
static bool s_has_last = false;
static uint32_t s_last_hello_ms = 0;
static uint32_t s_seq = 0;

static void node_on_new_peer(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg)
{
    if (s_paired) {
        return;   // gia' associato: non ci si "ri-accoppia" con un secondo hub
    }

    link_message_t msg;
    if (!link_parse_message(data, len, &msg)) {
        return;
    }
    if (msg.msg_type != LINK_MSG_WELCOME || msg.node_type != LINK_NODE_HUB) {
        return;
    }

    LinkPeer *hub = new LinkPeer(info->src_addr, g_link_channel);
    if (!hub->addPeer()) {
        delete hub;
        return;
    }
    s_hub_peer = hub;
    s_paired = true;
    link_notify_app(info->src_addr, &msg);
}

void link_node_start(void)
{
    s_broadcast_peer = new LinkPeer(ESP_NOW.BROADCAST_ADDR, g_link_channel);
    s_broadcast_peer->addPeer();
    ESP_NOW.onNewPeer(node_on_new_peer, nullptr);
}

void Link_Node_Poll(void)
{
    if (s_paired || s_broadcast_peer == nullptr) {
        return;
    }
    uint32_t now = millis();
    if (now - s_last_hello_ms < LINK_HELLO_INTERVAL_MS) {
        return;
    }
    s_last_hello_ms = now;

    link_message_t hello = {};
    hello.protocol_version = LINK_PROTOCOL_VERSION;
    hello.msg_type = LINK_MSG_HELLO;
    hello.node_type = g_link_self_type;
    strncpy(hello.name, g_link_self_name, LINK_NAME_LEN - 1);
    hello.seq = s_seq++;
    s_broadcast_peer->sendMessage(hello);
}

bool Link_Node_IsPaired(void)
{
    return s_paired;
}

void Link_Node_SetSeq(uint32_t seq)
{
    s_seq = seq;
}

uint32_t Link_Node_GetSeq(void)
{
    return s_seq;
}

bool Link_Node_ResumeWithHub(const uint8_t mac[6])
{
    if (mac == nullptr || s_paired) {
        return false;
    }

    LinkPeer *hub = new LinkPeer(mac, g_link_channel);
    if (!hub->addPeer()) {
        delete hub;
        return false;
    }

    // Nessun WELCOME da aspettare: si dichiara l'associazione e si passa ai
    // DATA.
    //
    // ATTENZIONE al seq: vive in RAM, quindi qui varrebbe zero ad ogni
    // risveglio - e l'hub scarta un DATA con seq uguale all'ultimo visto.
    // Chi si risveglia da deep sleep DEVE rimetterlo con Link_Node_SetSeq()
    // dopo questa chiamata, o si fa sentire una volta sola e poi mai piu'.
    s_hub_peer = hub;
    s_paired = true;
    return true;
}

bool Link_Node_SendData(link_message_t *msg)
{
    if (!s_paired || s_hub_peer == nullptr || msg == nullptr) {
        return false;
    }
    msg->protocol_version = LINK_PROTOCOL_VERSION;
    msg->msg_type = LINK_MSG_DATA;
    msg->node_type = g_link_self_type;
    strncpy(msg->name, g_link_self_name, LINK_NAME_LEN - 1);
    msg->name[LINK_NAME_LEN - 1] = '\0';
    msg->seq = s_seq++;
    s_last_data = *msg;      // per Link_Node_ResendLast(): stesso seq, altro canale
    s_has_last  = true;
    return s_hub_peer->sendReliable(*msg);
}

bool Link_Node_ResendLast(int max_attempts, uint32_t ack_timeout_ms)
{
    if (!s_has_last || !s_paired || s_hub_peer == nullptr) {
        return false;
    }
    // Volutamente NON si tocca s_seq: e' lo stesso messaggio, mandato altrove.
    return s_hub_peer->sendReliable(s_last_data, max_attempts, ack_timeout_ms);
}
