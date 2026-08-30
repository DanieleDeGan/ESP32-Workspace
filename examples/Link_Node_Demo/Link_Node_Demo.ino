/**
 * Link_Node_Demo.ino — nodo sensore finto per validare EspNowLink
 *
 * Sketch minimo, solo Serial: nessuna dipendenza da display/touch/pin della
 * board AMOLED, pensato per girare su QUALUNQUE scheda ESP32 (e' proprio
 * questo il punto — un vero nodo sensore, in genere, uno schermo non ce
 * l'ha). Manda un valore di temperatura finto ogni 5s una volta associato
 * a un hub in modalita' pairing (vedi examples/Link_Hub_Demo).
 *
 * Nessun lv_conf.h/build_opt.h qui: non serve LVGL.
 */

#include <EspNowLink.h>

static void on_message(const uint8_t mac[6], const link_message_t *msg)
{
    if (msg->msg_type == LINK_MSG_WELCOME) {
        Serial.printf("Associato all'hub %02X:%02X:%02X:%02X:%02X:%02X (\"%s\")\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], msg->name);
    } else if (msg->msg_type == LINK_MSG_COMMAND) {
        Serial.printf("Comando ricevuto: value[0]=%.2f\n", msg->value[0]);
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
    delay(500);

    if (!Link_Init(LINK_NODE_SENSOR_TEMPERATURE, "TempTest")) {
        Serial.println("Link_Init() fallito (WiFi/ESP-NOW non avviato).");
        return;
    }
    Link_OnMessage(on_message);

    Serial.println("Nodo avviato. In attesa che l'hub attivi la modalita' pairing...");
}

void loop()
{
    Link_Node_Poll();

    // Intervallo con jitter casuale (+-250ms): con piu' nodi identici sullo
    // stesso hub, un periodo fisso puo' farli convergere a mandare dati nello
    // stesso istante (specie se i clock derivano nel tempo); un po' di jitter
    // li disallinea invece di farli collidere sempre alla stessa cadenza.
    static uint32_t last_send = 0;
    static uint32_t next_interval_ms = 5000;
    if (Link_Node_IsPaired() && millis() - last_send >= next_interval_ms) {
        last_send = millis();
        next_interval_ms = 5000 + random(-250, 250);

        link_message_t msg = {};
        msg.battery_mv = 0;                              // alimentazione fissa in questo test
        msg.value[0] = 20.0f + (float)(millis() % 100) / 10.0f;   // finto valore variabile, 20.0-30.0 C

        if (Link_Node_SendData(&msg)) {
            Serial.printf("Inviato: %.1f C\n", msg.value[0]);
        } else {
            Serial.println("Invio fallito.");
        }
    }

    delay(50);
}
