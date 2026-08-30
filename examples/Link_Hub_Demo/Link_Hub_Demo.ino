/**
 * Link_Hub_Demo.ino — lato hub di EspNowLink, con UI sulla board AMOLED
 *
 * Mostra i nodi associati (nome, tipo, ultimo valore, "visto N s fa") e un
 * bottone per attivare la modalita' pairing. UI scritta a mano (oggetti LVGL
 * pre-creati e nascosti/mostrati a runtime), stesso stile di
 * examples/Orientation_IMU — non SquareLine.
 *
 * Provalo insieme a examples/Link_Node_Demo su una seconda scheda: attiva il
 * pairing qui, accendi il nodo, deve comparire in una riga entro pochi
 * secondi con un valore che si aggiorna ogni ~5s.
 */

#include "lvgl.h"
#include <AMOLED191_Display.h>
#include <AMOLED191_Touch.h>
#include <EspNowLink.h>

#define MAX_ROWS 6

static lv_obj_t *s_pairing_label = NULL;
static lv_obj_t *s_pairing_btn_lbl = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_row[MAX_ROWS] = {NULL};
static lv_obj_t *s_row_name[MAX_ROWS] = {NULL};
static lv_obj_t *s_row_value[MAX_ROWS] = {NULL};
static lv_obj_t *s_row_seen[MAX_ROWS] = {NULL};

static bool s_pairing_on = false;

static const char *node_type_name(link_node_type_t t)
{
    switch (t) {
        case LINK_NODE_SENSOR_TEMPERATURE: return "Temp";
        case LINK_NODE_SENSOR_WATER_LEVEL: return "Acqua";
        case LINK_NODE_SENSOR_BATTERY:     return "Batteria";
        case LINK_NODE_ACTUATOR:           return "Attuatore";
        default:                           return "?";
    }
}

// ---------------------------------------------------------------------------
static void on_link_message(const uint8_t mac[6], const link_message_t *msg)
{
    const char *type_name;
    switch (msg->msg_type) {
        case LINK_MSG_HELLO:   type_name = "HELLO";   break;
        case LINK_MSG_WELCOME: type_name = "WELCOME"; break;
        case LINK_MSG_DATA:    type_name = "DATA";    break;
        case LINK_MSG_COMMAND: type_name = "COMMAND"; break;
        default:               type_name = "?";       break;
    }
    Serial.printf("[Link] %s da %02X:%02X:%02X:%02X:%02X:%02X (\"%s\") value[0]=%.2f\n",
                  type_name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], msg->name, msg->value[0]);
}

// ---------------------------------------------------------------------------
static void pairing_toggle_cb(lv_event_t *e)
{
    s_pairing_on = !s_pairing_on;
    Link_Hub_SetPairingMode(s_pairing_on);
    lv_label_set_text(s_pairing_label, s_pairing_on ? "Pairing: ON" : "Pairing: OFF");
    lv_obj_set_style_text_color(s_pairing_label, lv_color_hex(s_pairing_on ? 0x1d9e75 : 0xE24B4A), 0);
    lv_label_set_text(s_pairing_btn_lbl, s_pairing_on ? "DISATTIVA" : "ASSOCIA NUOVO NODO");
}

// ---------------------------------------------------------------------------
// Costruzione UI (sotto lvgl_lock)
// ---------------------------------------------------------------------------
static void hub_ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8aa0b0), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_label_set_text(title, "HUB ESP-NOW");

    s_pairing_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_pairing_label, lv_color_hex(0xE24B4A), 0);
    lv_obj_align(s_pairing_label, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_label_set_text(s_pairing_label, "Pairing: OFF");

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 190, 30);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -8, 26);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1d9e75), 0);
    lv_obj_add_event_cb(btn, pairing_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_pairing_btn_lbl = lv_label_create(btn);
    lv_label_set_text(s_pairing_btn_lbl, "ASSOCIA NUOVO NODO");
    lv_obj_center(s_pairing_btn_lbl);

    s_status_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x5a6b78), 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_label_set_text(s_status_label, "");

    int row_y = 70;
    for (int i = 0; i < MAX_ROWS; i++) {
        s_row[i] = lv_obj_create(scr);
        lv_obj_set_size(s_row[i], 520, 24);
        lv_obj_set_pos(s_row[i], 8, row_y);
        lv_obj_clear_flag(s_row[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(s_row[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_row[i], 0, 0);
        lv_obj_set_style_pad_all(s_row[i], 0, 0);
        lv_obj_add_flag(s_row[i], LV_OBJ_FLAG_HIDDEN);

        s_row_name[i] = lv_label_create(s_row[i]);
        lv_obj_set_style_text_color(s_row_name[i], lv_color_hex(0xffffff), 0);
        lv_obj_set_pos(s_row_name[i], 0, 0);

        s_row_value[i] = lv_label_create(s_row[i]);
        lv_obj_set_style_text_color(s_row_value[i], lv_color_hex(0x00e5ff), 0);
        lv_obj_set_pos(s_row_value[i], 220, 0);

        s_row_seen[i] = lv_label_create(s_row[i]);
        lv_obj_set_style_text_color(s_row_seen[i], lv_color_hex(0x5a6b78), 0);
        lv_obj_set_pos(s_row_seen[i], 380, 0);

        row_y += 26;
    }
}

// ---------------------------------------------------------------------------
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

    Display_Init();
    Touch_Init();
    Touch_RegisterLvglIndev();

    if (lvgl_lock(-1)) {
        hub_ui_create();
        lvgl_unlock();
    }

    Link_OnMessage(on_link_message);

    if (!Link_Init(LINK_NODE_HUB, "Hub")) {
        Serial.println("Link_Init() fallito (WiFi/ESP-NOW non avviato).");
        if (lvgl_lock(-1)) {
            lv_label_set_text(s_status_label, "ESP-NOW non avviato!");
            lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xff5555), 0);
            lvgl_unlock();
        }
    }
}

// ---------------------------------------------------------------------------
void loop()
{
    Link_Hub_Poll();

    static uint32_t last_ui = 0;
    if (millis() - last_ui >= 500) {
        last_ui = millis();
        int count = Link_Hub_GetPeerCount();

        if (lvgl_lock(-1)) {
            for (int i = 0; i < MAX_ROWS; i++) {
                if (i >= count) {
                    lv_obj_add_flag(s_row[i], LV_OBJ_FLAG_HIDDEN);
                    continue;
                }

                uint8_t mac[6];
                link_node_type_t type;
                char name[LINK_NAME_LEN];
                uint32_t last_seen_ms;
                link_message_t last_data;
                if (!Link_Hub_GetPeerInfo(i, mac, &type, name, &last_seen_ms, &last_data)) {
                    continue;
                }

                lv_obj_clear_flag(s_row[i], LV_OBJ_FLAG_HIDDEN);

                char buf[48];
                snprintf(buf, sizeof(buf), "%s (%s)", name, node_type_name(type));
                lv_label_set_text(s_row_name[i], buf);

                if (last_data.protocol_version == LINK_PROTOCOL_VERSION) {
                    snprintf(buf, sizeof(buf), "%.1f", last_data.value[0]);
                } else {
                    snprintf(buf, sizeof(buf), "--");
                }
                lv_label_set_text(s_row_value[i], buf);

                uint32_t ago_s = (millis() - last_seen_ms) / 1000;
                snprintf(buf, sizeof(buf), "%lu s fa", (unsigned long)ago_s);
                lv_label_set_text(s_row_seen[i], buf);
            }
            lvgl_unlock();
        }
    }

    delay(5);
}
