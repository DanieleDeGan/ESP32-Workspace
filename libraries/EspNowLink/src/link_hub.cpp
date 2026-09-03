/**
 * link_hub.cpp
 *
 * Ruolo hub/master: mentre la modalita' pairing e' attiva, accetta HELLO in
 * broadcast da MAC sconosciuti e li aggiunge al registro; il WELCOME di
 * risposta NON viene inviato da dentro il callback di ricezione (che gira
 * nel task del driver WiFi e va tenuto breve, stessa regola gia' in
 * CLAUDE.md per i callback LVGL) ma accodato e inviato da Link_Hub_Poll(),
 * chiamato dal loop() dello sketch.
 *
 * Il registro peer e' scritto dal callback di ricezione (task del driver
 * WiFi) e letto da loop()/task LVGL quando l'app aggiorna la UI: accesso
 * concorrente reale, protetto da una portMUX_TYPE (a differenza di
 * Core_I2CBusInit(), chiamato solo in sequenza da setup() e quindi senza
 * bisogno di lock).
 */

#include "link_peer.h"

#include <Arduino.h>
#include <string.h>
#include "freertos/FreeRTOS.h"

#define ESPNOW_LINK_MAX_PEERS ESP_NOW_MAX_TOTAL_PEER_NUM

static LinkPeer *s_peers[ESPNOW_LINK_MAX_PEERS] = {nullptr};
static int s_peer_count = 0;
static volatile bool s_pairing_mode = false;
static portMUX_TYPE s_registry_mux = portMUX_INITIALIZER_UNLOCKED;

// Cestino dei peer dimenticati: NON si fa delete subito (vedi
// Link_Hub_ForgetPeer), si libera al giro dopo da Link_Hub_Poll().
static LinkPeer *s_trash[ESPNOW_LINK_MAX_PEERS] = {nullptr};
static int s_trash_count = 0;

/**
 * Inserimento nel registro, unico punto per tutte le strade che portano a un
 * peer nuovo: la scoperta radio (hub_on_new_peer, che gira nel task del driver
 * WiFi) e il ripristino da una persistenza applicativa (Link_Hub_AddPeer, che
 * gira in loop()). Tenerlo uno solo evita che le due divergano.
 *
 * queue_welcome=false serve proprio al ripristino: un nodo riletto da NVS e'
 * gia' associato dal suo punto di vista, mandargli un WELCOME non richiesto
 * sarebbe traffico inutile all'avvio dell'hub, per ogni nodo.
 */
static LinkPeer *hub_insert_peer(const uint8_t mac[6], link_node_type_t type,
                                 const char *name, bool queue_welcome,
                                 const link_message_t *first_data)
{
    // Primo controllo: serve solo a non costruire un LinkPeer che quasi
    // certamente andrebbe buttato. Quello che DECIDE e' il secondo, in fondo
    // alla funzione — qui il lock si rilascia prima di allocare.
    portENTER_CRITICAL(&s_registry_mux);
    bool full = (s_peer_count >= ESPNOW_LINK_MAX_PEERS);
    bool duplicato = false;
    for (int i = 0; i < s_peer_count && !duplicato; i++) {
        if (s_peers[i] != nullptr && memcmp(s_peers[i]->addr(), mac, 6) == 0) {
            duplicato = true;
        }
    }
    portEXIT_CRITICAL(&s_registry_mux);
    if (full || duplicato) {
        return nullptr;
    }

    LinkPeer *peer = new LinkPeer(mac, g_link_channel);
    if (!peer->addPeer()) {
        delete peer;
        return nullptr;
    }
    peer->nodeType = type;
    if (name) {
        strncpy(peer->name, name, LINK_NAME_LEN - 1);
        peer->name[LINK_NAME_LEN - 1] = '\0';
    }
    peer->lastSeenMs = millis();
    peer->welcomePending = queue_welcome;

    // Il messaggio che ha fatto scoprire il nodo va conservato qui:
    // onReceive() non e' stato chiamato per quel pacchetto (il peer non
    // esisteva ancora), quindi senza queste righe la prima lettura andrebbe
    // persa e l'hub mostrerebbe un nodo "senza dati" fino alla successiva.
    if (first_data != nullptr) {
        peer->lastData = *first_data;
        peer->hasData = true;
    }

    // Il controllo di capienza e doppioni si RIFA' qui, e non e' pignoleria:
    // fra il primo e questo il lock e' stato rilasciato, perche' new e
    // addPeer() allocano memoria e chiamano il driver, cose che dentro una
    // portENTER_CRITICAL (spinlock con interrupt disabilitati) non si
    // possono fare. In quella finestra l'ALTRO contesto puo' aver inserito:
    // le due strade che arrivano qui girano su task diversi — la scoperta
    // radio sul task del driver WiFi, il ripristino da NVS in loop() — e si
    // sovrappongono davvero all'avvio dell'hub, quando si rimettono i peer
    // salvati mentre i nodi stanno gia' trasmettendo.
    //
    // Il caso che fa danno e' il registro pieno: due inserimenti che passano
    // entrambi il primo controllo con s_peer_count == MAX-1 scrivono uno
    // oltre la fine di s_peers[]. E' improbabile — servono venti nodi — ma
    // una scrittura fuori array su una scheda accesa per settimane si
    // manifesta come un riavvio inspiegabile, cioe' nel modo piu' difficile
    // da ricondurre a qui.
    bool inserito = false;
    portENTER_CRITICAL(&s_registry_mux);
    if (s_peer_count < ESPNOW_LINK_MAX_PEERS) {
        bool doppione = false;
        for (int i = 0; i < s_peer_count && !doppione; i++) {
            if (s_peers[i] != nullptr && memcmp(s_peers[i]->addr(), mac, 6) == 0) {
                doppione = true;
            }
        }
        if (!doppione) {
            s_peers[s_peer_count++] = peer;
            inserito = true;
        }
    }
    portEXIT_CRITICAL(&s_registry_mux);

    if (!inserito) {
        // Perso il ballottaggio: si disfa quello che si era preparato. Prima
        // si toglie dal driver (addPeer() lo ha registrato), poi si libera —
        // qui il delete immediato e' sicuro, perche' questo peer non e' mai
        // entrato nel registro e nessuna callback puo' averlo in mano.
        ESP_NOW.removePeer(*peer);
        delete peer;
        return nullptr;
    }
    return peer;
}

static void hub_on_new_peer(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg)
{
    if (!s_pairing_mode) {
        return;   // fuori dalla finestra di pairing: ignora mittenti sconosciuti
    }

    const bool broadcast = memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0;

    link_message_t msg;
    if (!link_parse_message(data, len, &msg)) {
        return;
    }

    // Due modi di conoscere un nodo nuovo, e servono entrambi:
    //
    //  - HELLO in broadcast: il nodo NON si crede associato e sta cercando un
    //    hub. E' il caso normale della prima accensione.
    //
    //  - DATA in unicast: il nodo si crede gia' associato e sta parlando
    //    proprio a noi, ma noi non lo conosciamo piu' - il registro peer vive
    //    solo in RAM, quindi ogni riavvio dell'hub lo perde. Senza questo
    //    ramo il nodo resterebbe invisibile per sempre: non manda piu' HELLO
    //    (si crede associato) e i suoi DATA verrebbero scartati qui.
    //
    //    E' un guasto SILENZIOSO DA ENTRAMBE LE PARTI, che e' il motivo per
    //    cui vale la pena gestirlo: l'ACK di ESP-NOW e' di livello radio e
    //    arriva comunque, quindi il nodo conta i pacchetti come consegnati e
    //    la sua pagina dice che va tutto bene, mentre l'hub non mostra nulla.
    //    Verificato su hardware il 2026-08-23 fra EnvNode_C3 e MeteoNode_C3:
    //    quindici DATA "riusciti" e zero nodi visti dall'hub.
    const bool hello = broadcast  && msg.msg_type == LINK_MSG_HELLO;
    const bool orfano = !broadcast && msg.msg_type == LINK_MSG_DATA;
    if (!hello && !orfano) {
        return;
    }

    LinkPeer *peer = hub_insert_peer(info->src_addr, (link_node_type_t)msg.node_type,
                                     msg.name, /*queue_welcome=*/true,
                                     orfano ? &msg : nullptr);
    if (peer == nullptr) {
        return;   // registro pieno, o gia' presente
    }

    link_notify_app(info->src_addr, &msg);
}

void link_hub_start(void)
{
    ESP_NOW.onNewPeer(hub_on_new_peer, nullptr);
}

bool Link_Hub_AddPeer(const uint8_t mac[6], link_node_type_t type, const char *name)
{
    if (mac == nullptr || g_link_self_type != LINK_NODE_HUB) {
        return false;
    }
    return hub_insert_peer(mac, type, name, /*queue_welcome=*/false, nullptr) != nullptr;
}

bool Link_Hub_ForgetPeer(const uint8_t mac[6])
{
    if (mac == nullptr) {
        return false;
    }

    LinkPeer *vittima = nullptr;
    portENTER_CRITICAL(&s_registry_mux);
    for (int i = 0; i < s_peer_count; i++) {
        if (s_peers[i] != nullptr && memcmp(s_peers[i]->addr(), mac, 6) == 0) {
            vittima = s_peers[i];
            // Compatta l'array: il registro non ha buchi, cosi' chi lo scorre
            // per indice non deve saltare i nullptr.
            for (int j = i; j < s_peer_count - 1; j++) {
                s_peers[j] = s_peers[j + 1];
            }
            s_peers[--s_peer_count] = nullptr;
            break;
        }
    }
    portEXIT_CRITICAL(&s_registry_mux);

    if (vittima == nullptr) {
        return false;
    }

    // Prima si deregistra dal core, POI si butta via. Si passa da
    // ESP_NOW.removePeer() e non da peer->remove(), che e' protetta: e' il
    // punto d'ingresso pubblico (dichiarato friend dalla classe peer).
    // L'effetto e' quello che serve: azzera anche lo slot nell'array di
    // dispatch di ESP_NOW, quindi da qui in poi nessun pacchetto puo' piu'
    // arrivare a questo oggetto.
    ESP_NOW.removePeer(*vittima);

    // Il delete pero' NON si fa adesso: questa funzione gira in loop(), mentre
    // il dispatch dei pacchetti gira sul task del driver WiFi, e potrebbe
    // essere gia' dentro onReceive() di questo peer proprio in questo istante.
    // La finestra e' minuscola ma reale, e una use-after-free su una scheda
    // che sta accesa per settimane si manifesterebbe come un riavvio
    // inspiegabile mesi dopo. Si accoda al cestino e si libera al giro
    // successivo di Link_Hub_Poll(), quando la callback ha certamente finito.
    portENTER_CRITICAL(&s_registry_mux);
    if (s_trash_count < ESPNOW_LINK_MAX_PEERS) {
        s_trash[s_trash_count++] = vittima;
        vittima = nullptr;
    }
    portEXIT_CRITICAL(&s_registry_mux);

    // Cestino pieno (non dovrebbe mai: si svuota ad ogni loop): meglio
    // perdere un centinaio di byte che rischiare la use-after-free.
    (void)vittima;
    return true;
}

void Link_Hub_Poll(void)
{
    // Cestino di Link_Hub_ForgetPeer(): i peer rimossi al giro precedente si
    // liberano adesso, quando nessuna callback puo' piu' averli in mano.
    portENTER_CRITICAL(&s_registry_mux);
    LinkPeer *daButtare[ESPNOW_LINK_MAX_PEERS];
    const int nButtare = s_trash_count;
    for (int i = 0; i < nButtare; i++) {
        daButtare[i] = s_trash[i];
        s_trash[i] = nullptr;
    }
    s_trash_count = 0;
    portEXIT_CRITICAL(&s_registry_mux);
    for (int i = 0; i < nButtare; i++) {
        delete daButtare[i];
    }

    // UN SOLO WELCOME PER GIRO, e a turno.
    //
    // sendReliable() blocca fino a ~1 s (3 tentativi x 300 ms piu' backoff).
    // Mandarli tutti nello stesso giro costava, con otto nodi, fino a otto
    // secondi di loop() fermo -- e proprio nel momento peggiore: un nodo si
    // mette in welcomePending quando manda un HELLO, cioe' quando NON si crede
    // associato, e un blackout riavvia tutti i nodi alimentati dalla rete
    // nello stesso istante. Otto secondi in cui il WebServer non risponde, un
    // OTA si pianta, e soprattutto i DATA non vengono prelevati dal driver,
    // che ne tiene uno solo per nodo.
    //
    // Spalmarli non ritarda niente: i nodi ripetono l'HELLO ogni
    // LINK_HELLO_INTERVAL_MS (2 s) e il loop gira migliaia di volte in quel
    // tempo, quindi otto nodi si servono comunque nello stesso secondo.
    //
    // A TURNO, e non sempre dal primo: il flag si pulisce solo se l'invio
    // riesce (di proposito, cosi' un fallimento transitorio si ritenta invece
    // di perdere la finestra di pairing di quel nodo). Ripartendo sempre da
    // zero, un nodo spento a meta' associazione resterebbe pendente per
    // sempre e si prenderebbe l'unico invio di ogni giro: gli altri non
    // riceverebbero mai il loro WELCOME. Il turno lo rende impossibile.
    static int s_welcome_prossimo = 0;

    portENTER_CRITICAL(&s_registry_mux);
    for (int k = 0; k < s_peer_count; k++) {
        const int i = (s_welcome_prossimo + k) % s_peer_count;
        LinkPeer *peer = s_peers[i];
        if (peer != nullptr && peer->welcomePending) {
            s_welcome_prossimo = i + 1;   // il prossimo giro riparte da dopo
            portEXIT_CRITICAL(&s_registry_mux);

            link_message_t welcome = {};
            welcome.protocol_version = LINK_PROTOCOL_VERSION;
            welcome.msg_type = LINK_MSG_WELCOME;
            welcome.node_type = LINK_NODE_HUB;
            strncpy(welcome.name, g_link_self_name, LINK_NAME_LEN - 1);
            bool sent = peer->sendReliable(welcome);

            portENTER_CRITICAL(&s_registry_mux);
            // Pulisce il flag SOLO se l'invio e' andato a buon fine: un fallimento
            // (es. transitorio, appena dopo l'aggiunta del peer) viene ritentato
            // al prossimo giro di loop() invece di perdere per sempre la finestra
            // di pairing di quel nodo.
            if (sent) {
                peer->welcomePending = false;
            }
            break;
        }
    }
    portEXIT_CRITICAL(&s_registry_mux);
}

void Link_Hub_SetPairingMode(bool enable)
{
    s_pairing_mode = enable;
}

int Link_Hub_GetPeerCount(void)
{
    portENTER_CRITICAL(&s_registry_mux);
    int n = s_peer_count;
    portEXIT_CRITICAL(&s_registry_mux);
    return n;
}

bool Link_Hub_GetPeerInfo(int index, uint8_t mac_out[6], link_node_type_t *type_out,
                          char name_out[LINK_NAME_LEN], uint32_t *last_seen_ms_out,
                          link_message_t *last_data_out)
{
    portENTER_CRITICAL(&s_registry_mux);
    if (index < 0 || index >= s_peer_count || s_peers[index] == nullptr) {
        portEXIT_CRITICAL(&s_registry_mux);
        return false;
    }
    LinkPeer *peer = s_peers[index];
    if (mac_out) memcpy(mac_out, peer->addr(), 6);
    if (type_out) *type_out = peer->nodeType;
    if (name_out) {
        strncpy(name_out, peer->name, LINK_NAME_LEN - 1);
        name_out[LINK_NAME_LEN - 1] = '\0';
    }
    if (last_seen_ms_out) *last_seen_ms_out = peer->lastSeenMs;
    if (last_data_out) {
        if (peer->hasData) {
            *last_data_out = peer->lastData;
        } else {
            memset(last_data_out, 0, sizeof(link_message_t));
        }
    }
    portEXIT_CRITICAL(&s_registry_mux);
    return true;
}

bool Link_Hub_SendCommand(const uint8_t mac[6], link_message_t *msg)
{
    if (mac == nullptr || msg == nullptr) {
        return false;
    }

    portENTER_CRITICAL(&s_registry_mux);
    LinkPeer *target = nullptr;
    for (int i = 0; i < s_peer_count; i++) {
        if (s_peers[i] != nullptr && memcmp(s_peers[i]->addr(), mac, 6) == 0) {
            target = s_peers[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_registry_mux);
    if (target == nullptr) {
        return false;
    }

    msg->protocol_version = LINK_PROTOCOL_VERSION;
    msg->msg_type = LINK_MSG_COMMAND;
    msg->node_type = LINK_NODE_HUB;
    return target->sendReliable(*msg);
}
