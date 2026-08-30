/**
 * Orientation_IMU.ino  —  LIVELLAMENTO CAMPER con IMU QMI8658
 *
 * Vista dall'alto del camper con le 4 ruote: ogni ruota mostra di quanti cm
 * va rialzata per mettere il mezzo in piano (ruota piu' alta = 0, non si tocca;
 * sotto le altre vanno i cunei dell'altezza indicata). Colore: verde = ok,
 * ambra = poco, rosso = molto. Bolla centrale + indicazione della ruota peggiore.
 * Il pulsante CALIBRA azzera la posizione corrente (utile su fondo che
 * consideri "buono", o per compensare il montaggio della IMU).
 *
 * IMPORTANTE: imposta TRACK_MM (carreggiata) e WHEELBASE_MM (passo) sul TUO
 * camper: la DIREZIONE e' corretta comunque, ma i CENTIMETRI dipendono dalle
 * dimensioni reali. Se sinistra/destra o avanti/dietro risultano invertiti,
 * cambia i segni di roll/pitch nel loop().
 */

#include <math.h>
#include "lvgl.h"
#include <AMOLED191_Display.h>
#include <AMOLED191_Touch.h>
#include <AMOLED191_IMU.h>

// --- Dimensioni del camper (mm): Adria Matrix Axess 680 SP (Fiat Ducato Maxi) ---
#define TRACK_MM       1800.0f    // carreggiata Ducato (distanza ruote sx-dx)
#define WHEELBASE_MM   4035.0f    // passo Maxi (distanza assi ant-post)

#define DEG2RAD        0.01745329f

// Geometria pianta (coordinate dentro il contenitore "plan", 240x210)
#define BODY_CX        120        // centro corpo X
#define BODY_CY        105        // centro corpo Y

// Indici ruote: 0=Ant-SX 1=Ant-DX 2=Post-SX 3=Post-DX
static lv_obj_t *s_wheel[4]     = { NULL, NULL, NULL, NULL };
static lv_obj_t *s_wheel_lbl[4] = { NULL, NULL, NULL, NULL };
static lv_obj_t *s_bubble       = NULL;
static lv_obj_t *s_hint_label   = NULL;
static lv_obj_t *s_pitch_label  = NULL;
static lv_obj_t *s_roll_label   = NULL;
static lv_obj_t *s_status_label = NULL;

static const char *WHEEL_NAME[4] = { "Ant-SX", "Ant-DX", "Post-SX", "Post-DX" };

// Valori filtrati RAW + offset calibrazione
static float s_f_pitch = 0.0f, s_f_roll = 0.0f;
static float s_off_pitch = 0.0f, s_off_roll = 0.0f;
static volatile bool s_calibrate_req = false;

// ---------------------------------------------------------------------------
static void calibrate_cb(lv_event_t *e) { s_calibrate_req = true; }

static lv_color_t lift_color(float cm)
{
    if (cm < 0.5f) return lv_color_hex(0x1d9e75);   // verde: ok
    if (cm < 3.0f) return lv_color_hex(0xEF9F27);   // ambra: poco
    return lv_color_hex(0xE24B4A);                  // rosso: molto
}

// Crea una ruota + la sua etichetta cm dentro il contenitore "plan"
static void make_wheel(lv_obj_t *plan, int i, int wx, int wy, int lx, bool lbl_right)
{
    s_wheel[i] = lv_obj_create(plan);
    lv_obj_set_size(s_wheel[i], 18, 42);
    lv_obj_set_pos(s_wheel[i], wx, wy);
    lv_obj_clear_flag(s_wheel[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_wheel[i], 6, 0);
    lv_obj_set_style_border_width(s_wheel[i], 0, 0);
    lv_obj_set_style_bg_color(s_wheel[i], lv_color_hex(0x1d9e75), 0);

    s_wheel_lbl[i] = lv_label_create(plan);
    lv_obj_set_style_text_color(s_wheel_lbl[i], lv_color_hex(0xffffff), 0);
    lv_obj_set_pos(s_wheel_lbl[i], lx, wy + 12);
    lv_label_set_text(s_wheel_lbl[i], "0.0");
}

// ---------------------------------------------------------------------------
// Costruzione UI (sotto lvgl_lock)
// ---------------------------------------------------------------------------
static void leveling_ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    // Contenitore pianta (sinistra), trasparente e senza bordo
    lv_obj_t *plan = lv_obj_create(scr);
    lv_obj_set_size(plan, 240, 210);
    lv_obj_align(plan, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_clear_flag(plan, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(plan, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(plan, 0, 0);
    lv_obj_set_style_pad_all(plan, 0, 0);

    // Corpo camper
    lv_obj_t *body = lv_obj_create(plan);
    lv_obj_set_size(body, 100, 178);
    lv_obj_set_pos(body, BODY_CX - 50, BODY_CY - 89);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(body, 12, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x243038), 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(0x3a4a55), 0);

    // Striscia "FRONTE"
    lv_obj_t *cab = lv_label_create(plan);
    lv_obj_set_style_text_color(cab, lv_color_hex(0xbcd0dc), 0);
    lv_obj_set_pos(cab, BODY_CX - 24, BODY_CY - 82);
    lv_label_set_text(cab, "FRONTE");

    // Ruote (x interno=102, esterno per etichetta)
    make_wheel(plan, 0, BODY_CX - 50 - 18, BODY_CY - 47, BODY_CX - 50 - 18 - 30, false); // Ant-SX
    make_wheel(plan, 1, BODY_CX + 50,      BODY_CY - 47, BODY_CX + 50 + 22,      true);  // Ant-DX
    make_wheel(plan, 2, BODY_CX - 50 - 18, BODY_CY + 35, BODY_CX - 50 - 18 - 30, false); // Post-SX
    make_wheel(plan, 3, BODY_CX + 50,      BODY_CY + 35, BODY_CX + 50 + 22,      true);  // Post-DX

    // Bolla centrale
    s_bubble = lv_obj_create(plan);
    lv_obj_set_size(s_bubble, 14, 14);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_bubble, 0, 0);
    lv_obj_set_style_bg_color(s_bubble, lv_color_hex(0x00e5ff), 0);
    lv_obj_set_pos(s_bubble, BODY_CX - 7, BODY_CY - 7);

    // --- Pannello destro ---
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8aa0b0), 0);
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_label_set_text(title, "LIVELLAMENTO");

    s_hint_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0x1d9e75), 0);
    lv_obj_align(s_hint_label, LV_ALIGN_TOP_RIGHT, -16, 48);
    lv_label_set_text(s_hint_label, "IN PIANO");

    s_pitch_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_pitch_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_pitch_label, LV_ALIGN_TOP_RIGHT, -16, 86);
    lv_label_set_text(s_pitch_label, "PITCH  +0.0");

    s_roll_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_roll_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_roll_label, LV_ALIGN_TOP_RIGHT, -16, 108);
    lv_label_set_text(s_roll_label, "ROLL   +0.0");

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 130, 42);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -16, 140);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1d9e75), 0);
    lv_obj_add_event_cb(btn, calibrate_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "CALIBRA");
    lv_obj_center(btn_lbl);

    s_status_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x5a6b78), 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_RIGHT, -16, -10);
    lv_label_set_text(s_status_label, "rialzi in cm");
}

// ---------------------------------------------------------------------------
// Aggiornamento (sotto lvgl_lock). pitch/roll gia' con offset di calibrazione.
// ---------------------------------------------------------------------------
static void leveling_update(float pitch, float roll)
{
    float rr = roll * DEG2RAD, pp = pitch * DEG2RAD;
    // Il cuneo va sotto le ruote PIU' BASSE: con questi segni la ruota piu' alta
    // resta a 0 (verde, riferimento) e le piu' basse ricevono il rialzo (rosse).
    float sr = -sinf(rr), sp = -sinf(pp);
    const float hx = TRACK_MM * 0.5f, hy = WHEELBASE_MM * 0.5f;

    // Altezza relativa dei 4 appoggi (mm). + = piu' alto.
    float z[4];
    z[0] = -hx * sr + hy * sp;   // Ant-SX
    z[1] =  hx * sr + hy * sp;   // Ant-DX
    z[2] = -hx * sr - hy * sp;   // Post-SX
    z[3] =  hx * sr - hy * sp;   // Post-DX

    float zmax = z[0];
    for (int i = 1; i < 4; i++) if (z[i] > zmax) zmax = z[i];

    char b[16];
    int worst = 0;
    float worst_cm = 0.0f;
    for (int i = 0; i < 4; i++) {
        float cm = (zmax - z[i]) / 10.0f;     // mm -> cm, sempre >= 0
        snprintf(b, sizeof(b), "%.1f", cm);
        lv_label_set_text(s_wheel_lbl[i], b);
        lv_obj_set_style_bg_color(s_wheel[i], lift_color(cm), 0);
        if (cm > worst_cm) { worst_cm = cm; worst = i; }
    }

    if (worst_cm < 0.5f) {
        lv_label_set_text(s_hint_label, "IN PIANO");
        lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0x1d9e75), 0);
    } else {
        snprintf(b, sizeof(b), "%s  +%.1f cm", WHEEL_NAME[worst], worst_cm);
        lv_label_set_text(s_hint_label, b);
        lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0xE24B4A), 0);
    }

    // Bolla: si sposta verso il lato ALTO (alzi il lato opposto)
    float bdx = 60.0f * sr;      // + = destra alta
    float bdy = -60.0f * sp;     // pitch + (fronte alto) -> bolla in alto
    if (bdx >  70) bdx =  70; if (bdx < -70) bdx = -70;
    if (bdy >  80) bdy =  80; if (bdy < -80) bdy = -80;
    lv_obj_set_pos(s_bubble, (lv_coord_t)(BODY_CX - 7 + bdx),
                             (lv_coord_t)(BODY_CY - 7 + bdy));

    snprintf(b, sizeof(b), "PITCH  %+5.1f", pitch);
    lv_label_set_text(s_pitch_label, b);
    snprintf(b, sizeof(b), "ROLL   %+5.1f", roll);
    lv_label_set_text(s_roll_label, b);
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
        leveling_ui_create();
        lvgl_unlock();
    }

    if (!imu_init()) {
        Serial.println("IMU QMI8658 non trovata (controlla indirizzo/bus).");
        if (lvgl_lock(-1)) {
            lv_label_set_text(s_status_label, "IMU non trovata!");
            lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xff5555), 0);
            lvgl_unlock();
        }
    }
}

// ---------------------------------------------------------------------------
void loop()
{
    static uint32_t last_read = 0;
    static uint32_t last_ui   = 0;

    // Lettura IMU ogni 20 ms: media di 8 campioni per ridurre il rumore
    if (millis() - last_read >= 20) {
        last_read = millis();

        float sx = 0, sy = 0, sz = 0;
        int n = 0;
        for (int i = 0; i < 8; i++) {
            float ax, ay, az;
            if (imu_read_accel(&ax, &ay, &az)) { sx += ax; sy += ay; sz += az; n++; }
        }
        if (n > 0) {
            float ax = sx / n, ay = sy / n, az = sz / n;

            // Se sx/dx o avanti/dietro sono invertiti, cambia i segni qui.
            float roll  = atan2f(ay, az) * 57.29578f;
            float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.29578f;

            // Filtro esponenziale lento: il camper e' fermo, serve stabilita'
            // piu' che reattivita'. Abbassa ancora (0.03) per piu' calma.
            s_f_roll  += 0.05f * (roll  - s_f_roll);
            s_f_pitch += 0.05f * (pitch - s_f_pitch);
        }
    }

    // Aggiorna la UI ~3 volte al secondo: cosi' l'ultima cifra non "balla"
    if (millis() - last_ui >= 300) {
        last_ui = millis();
        if (lvgl_lock(-1)) {
            if (s_calibrate_req) {
                s_off_pitch = s_f_pitch;
                s_off_roll  = s_f_roll;
                s_calibrate_req = false;
                lv_label_set_text(s_status_label, "azzerato");
            }
            leveling_update(s_f_pitch - s_off_pitch, s_f_roll - s_off_roll);
            lvgl_unlock();
        }
    }

    delay(5);
}
