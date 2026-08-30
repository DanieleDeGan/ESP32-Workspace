/**
 * DHT11_SD_Logger.ino  —  TEMPERATURA/UMIDITA' A SCHERMO + LOG SU microSD
 *
 * Legge un DHT11 collegato a un GPIO libero alla cadenza fissata da
 * SAMPLE_PERIOD_MS (default 60 s), mostra a schermo temperatura, umidita' e
 * numero di campioni raccolti, e accoda ogni lettura valida a un CSV sulla
 * microSD onboard.
 *
 * LIBRERIE RICHIESTE (Library Manager, oltre a LVGL 8.3.x):
 *   - "DHT sensor library" di Adafruit
 *   - "Adafruit Unified Sensor" (dipendenza della precedente)
 * Sono le stesse usate dai nodi ESP32-C3 di questo repo (vedi
 * projects/EnvNode_C3): stesso sensore letto allo stesso modo su ogni scheda.
 *
 * CABLAGGIO (modulo DHT11 a 3 pin):
 *     VCC  -> 3V3
 *     GND  -> GND
 *     DATA -> GPIO2      (cambiabile sotto: DHT_DATA_PIN)
 * I GPIO liberi sulla scheda sono 2, 4, 10-16, 21, 38. NON usare 26 e 33-37
 * (bus PSRAM: la scheda va in crash) ne' 3 (strapping JTAG, deve restare
 * flottante al reset, e il modulo DHT11 tiene la linea DATA in pull-up).
 * Nota: sulla versione senza header a pettine (SKU 28596) i GPIO liberi sono
 * piazzole da saldare, non un connettore - scegli il pin in base a quale
 * riesci a raggiungere fisicamente.
 * I moduli a 3 pin hanno gia' a bordo il pull-up da 10k sulla linea DATA; con
 * un sensore "nudo" a 4 pin va aggiunto (4.7k-10k verso 3V3).
 *
 * FILE PRODOTTO: /dht11_log.csv sulla microSD (FAT32), colonne
 *     boot_id,n,secondi_da_accensione,temperatura_C,umidita_pct
 * L'intestazione viene scritta solo alla creazione del file; ai riavvii
 * successivi le righe si accodano in fondo. La scheda non ha un RTC tamponato,
 * quindi l'unico riferimento temporale onesto e' "secondi da accensione" — che
 * pero' riparte da zero ad ogni accensione, insieme al contatore `n`: in un
 * file che sopravvive ai riavvii, le righe di run diverse si sovrappongono
 * senza che si veda dove finisce una e comincia l'altra. Per questo la prima
 * colonna e' `boot_id`, un contatore tenuto in NVS che avanza ad ogni avvio
 * (mostrato anche a schermo, in alto a sinistra): tutte le righe di una stessa
 * accensione lo condividono, e per isolare una run basta filtrare su quello.
 *
 * ATTENZIONE al cambio di formato: `boot_id` e' stato aggiunto dopo, e
 * SDCard_WriteHeaderIfNew() si limita a controllare se il file esiste, non se
 * l'intestazione combacia. Su una card che contiene ancora un log del vecchio
 * formato a 4 colonne le righe nuove si accoderebbero a 5 campi sotto
 * un'intestazione che ne dichiara 4: rinomina (o cancella) il vecchio file
 * prima di riusare quella card.
 *
 * SENZA microSD funziona lo stesso: mostra i valori a schermo, segnala in
 * rosso che la card non c'e' e ritenta il mount ogni 30 s, cosi' puoi
 * infilarla a scheda accesa.
 */

#include <stdio.h>
#include <math.h>

#include <DHT.h>
#include <Preferences.h>    // bundled nel core ESP32: contatore di avvii in NVS

#include "lvgl.h"
#include <AMOLED191_Display.h>
#include <AMOLED191_SD.h>

// --- Configurazione ---------------------------------------------------------
#define DHT_DATA_PIN        2
#define DHT_SENSOR_TYPE     DHT11

#define SAMPLE_PERIOD_MS    60000UL     // cadenza di campionamento
#define FIRST_SAMPLE_MS     2500UL      // il DHT11 vuole ~1 s dopo l'accensione
#define SD_RETRY_PERIOD_MS  30000UL     // ritenta il mount se la card manca

#define LOG_PATH            "/dht11_log.csv"
#define LOG_HEADER          "boot_id,n,secondi_da_accensione,temperatura_C,umidita_pct"

// Namespace NVS in cui vive il contatore di avvii (max 15 caratteri).
#define NVS_NAMESPACE       "dht11log"
#define NVS_KEY_BOOT_ID     "boot_id"

// --- Palette ---------------------------------------------------------------
#define COL_BG        0x101418
#define COL_CARD      0x1b232a
#define COL_TEXT      0xffffff
#define COL_MUTED     0x8aa0b0
#define COL_TEMP      0xEF9F27      // ambra
#define COL_HUM       0x33A8DF      // azzurro
#define COL_COUNT     0x1d9e75      // verde
#define COL_ERR       0xE24B4A      // rosso

// --- Geometria (schermo 536x240) -------------------------------------------
#define CARD_Y        38
#define CARD_W        165
#define CARD_H        118
#define CARD_GAP      10
#define CARD_X0       12

// ---------------------------------------------------------------------------
static DHT dht(DHT_DATA_PIN, DHT_SENSOR_TYPE);

// --- Oggetti LVGL aggiornati a runtime -------------------------------------
static lv_obj_t *s_temp_val  = NULL;
static lv_obj_t *s_hum_val   = NULL;
static lv_obj_t *s_count_val = NULL;
static lv_obj_t *s_status    = NULL;
static lv_obj_t *s_bar       = NULL;

// --- Stato applicativo ------------------------------------------------------
static uint32_t s_boot_id     = 0;      // identifica questa accensione nel CSV
static float    s_last_temp   = 0.0f;
static float    s_last_hum    = 0.0f;
static bool     s_have_data   = false;
static uint32_t s_samples     = 0;      // letture DHT11 valide
static uint32_t s_logged      = 0;      // righe effettivamente finite sulla SD
static uint32_t s_errors      = 0;      // letture fallite
static bool     s_sd_write_ok = true;

// ---------------------------------------------------------------------------
/** Legge il contatore di avvii dalla NVS, lo incrementa e lo riscrive.
 *  La NVS sopravvive al reset e al riflash dello sketch (non a un erase del
 *  flash), quindi il numero non si ripete mai: e' l'unico modo, senza RTC, di
 *  distinguere nel CSV una run dall'altra. Una scrittura per accensione e'
 *  irrilevante per l'usura del flash (la NVS fa gia' wear levelling). */
static uint32_t boot_id_next(void)
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return 0;   // 0 = "NVS non disponibile"

    uint32_t id = prefs.getUInt(NVS_KEY_BOOT_ID, 0) + 1;
    prefs.putUInt(NVS_KEY_BOOT_ID, id);
    prefs.end();
    return id;
}

// ---------------------------------------------------------------------------
// Costruzione UI (sotto lvgl_lock)
// ---------------------------------------------------------------------------

/** Crea una "card" con didascalia in alto e valore grande al centro.
 *  Ritorna l'etichetta del valore: e' l'unica cosa che poi aggiorniamo. */
static lv_obj_t *make_card(lv_obj_t *parent, int x, const char *caption, uint32_t accent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, CARD_Y);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(accent), 0);

    lv_obj_t *cap = lv_label_create(card);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(accent), 0);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 12, 10);
    lv_label_set_text(cap, caption);

    lv_obj_t *val = lv_label_create(card);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_label_set_text(val, "--");

    return val;
}

static void ui_create(void)
{
    char buf[48];

    lcd_set_brightness(50);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);

    // Il numero di avvio e' lo stesso che finisce in prima colonna nel CSV:
    // averlo a schermo permette di dire a colpo d'occhio quali righe del file
    // sta scrivendo la scheda che si ha davanti.
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, CARD_X0, 10);
    snprintf(buf, sizeof(buf), "DHT11 -> microSD   (avvio #%lu)",
             (unsigned long)s_boot_id);
    lv_label_set_text(title, buf);

    // Derivata da SAMPLE_PERIOD_MS, non scritta a mano: cambiare la cadenza in
    // un punto solo deve bastare, altrimenti lo schermo mente.
    lv_obj_t *period = lv_label_create(scr);
    lv_obj_set_style_text_font(period, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(period, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(period, LV_ALIGN_TOP_RIGHT, -CARD_X0, 14);
    snprintf(buf, sizeof(buf), "un campione ogni %lu s",
             (unsigned long)(SAMPLE_PERIOD_MS / 1000));
    lv_label_set_text(period, buf);

    s_temp_val  = make_card(scr, CARD_X0,                           "TEMPERATURA", COL_TEMP);
    s_hum_val   = make_card(scr, CARD_X0 + (CARD_W + CARD_GAP),     "UMIDITA'",    COL_HUM);
    s_count_val = make_card(scr, CARD_X0 + 2 * (CARD_W + CARD_GAP), "CAMPIONI",    COL_COUNT);
    lv_label_set_text(s_count_val, "0");

    // Riga di stato: com'e' messa la SD e quante letture sono fallite.
    s_status = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, CARD_X0, -30);
    lv_label_set_text(s_status, "avvio...");

    // Barra sottile: avanzamento verso il prossimo campione. Serve a vedere a
    // colpo d'occhio che il ciclo gira davvero, senza guardare la seriale.
    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, 536 - 2 * CARD_X0, 6);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COL_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COL_COUNT), LV_PART_INDICATOR);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
}

// ---------------------------------------------------------------------------
// Aggiornamento UI: prende il lock, quindi va chiamato dal loop(), non da
// dentro una callback LVGL.
// ---------------------------------------------------------------------------
static void ui_refresh(void)
{
    char buf[96];

    if (!lvgl_lock(-1)) return;

    if (s_have_data) {
        // "\xC2\xB0" e' il grado in UTF-8, spezzato dalla "C" perche' altrimenti
        // il compilatore leggerebbe \xB0C come un unico escape esadecimale.
        snprintf(buf, sizeof(buf), "%.1f \xC2\xB0" "C", s_last_temp);
        lv_label_set_text(s_temp_val, buf);
        snprintf(buf, sizeof(buf), "%.1f %%", s_last_hum);
        lv_label_set_text(s_hum_val, buf);
    }

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_samples);
    lv_label_set_text(s_count_val, buf);

    uint32_t color;
    if (!SDCard_IsMounted() || !s_sd_write_ok) {
        snprintf(buf, sizeof(buf), "SD: %s", SDCard_LastError());
        color = COL_ERR;
    } else {
        snprintf(buf, sizeof(buf), "SD: %s  -  %lu righe scritte",
                 LOG_PATH, (unsigned long)s_logged);
        color = COL_COUNT;
    }
    if (s_errors > 0) {
        size_t n = strlen(buf);
        snprintf(buf + n, sizeof(buf) - n, "   |   letture fallite: %lu",
                 (unsigned long)s_errors);
    }
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
    lv_label_set_text(s_status, buf);

    lvgl_unlock();
}

// ---------------------------------------------------------------------------
// Un campione: leggi il sensore, scrivi la riga, aggiorna lo schermo.
//
// Ne' la lettura del DHT11 ne' la scrittura su SD girano sotto lvgl_lock: la
// prima tiene le interruzioni disabilitate ~5 ms (il protocollo del DHT si
// misura al microsecondo), la seconda blocca per decine di millisecondi.
// Bloccare anche il rendering per tutto quel tempo non servirebbe a niente: il
// lock si prende solo alla fine, dentro ui_refresh().
// ---------------------------------------------------------------------------
static void take_sample(void)
{
    uint32_t sec = millis() / 1000;

    // readTemperature() fa la lettura vera sul filo; readHumidity() subito
    // dopo riusa la stessa trama (la libreria tiene in cache 2 s).
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
        s_errors++;
        Serial.printf("[%lus] DHT11: lettura fallita (cablaggio/pin/alimentazione?)\n",
                      (unsigned long)sec);
        ui_refresh();
        return;
    }

    s_last_temp = t;
    s_last_hum  = h;
    s_have_data = true;
    s_samples++;

    Serial.printf("[%lus] campione %lu:  %.1f C   %.1f %%RH\n",
                  (unsigned long)sec, (unsigned long)s_samples, t, h);

    if (SDCard_IsMounted()) {
        char line[80];
        snprintf(line, sizeof(line), "%lu,%lu,%lu,%.1f,%.1f",
                 (unsigned long)s_boot_id, (unsigned long)s_samples,
                 (unsigned long)sec, t, h);

        s_sd_write_ok = SDCard_AppendLine(LOG_PATH, line);
        if (s_sd_write_ok) s_logged++;
        else               Serial.printf("         SD: %s\n", SDCard_LastError());
    }

    ui_refresh();
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

    // Prima della UI: ui_create() scrive il numero di avvio nel titolo.
    s_boot_id = boot_id_next();

    Display_Init();
    if (lvgl_lock(-1)) {
        ui_create();
        lvgl_unlock();
    }

    dht.begin();

    Serial.println();
    Serial.println("=== DHT11 -> microSD logger ===");
    Serial.printf("Avvio #%lu\n", (unsigned long)s_boot_id);
    Serial.printf("DHT11 su GPIO%d, un campione ogni %lu s\n",
                  DHT_DATA_PIN, (unsigned long)(SAMPLE_PERIOD_MS / 1000));

    if (SDCard_Init()) {
        bool nuovo = !SDCard_Exists(LOG_PATH);
        Serial.printf("microSD montata (%lu MB) -> %s\n",
                      (unsigned long)SDCard_SizeMB(), LOG_PATH);
        SDCard_WriteHeaderIfNew(LOG_PATH, LOG_HEADER);
        Serial.println(nuovo ? "File creato con intestazione."
                             : "File gia' presente: le righe si accodano.");
    } else {
        Serial.printf("microSD NON disponibile: %s\n", SDCard_LastError());
        Serial.println("Continuo comunque: valori a schermo, nessun file scritto.");
    }

    ui_refresh();
}

// ---------------------------------------------------------------------------
void loop()
{
    // Finestra corrente: riparte alla fine di ogni campione, cosi' la barra
    // riflette la cadenza reale invece di accumulare ritardo se una scrittura
    // sulla SD e' andata lunga.
    static uint32_t win_start_ms  = 0;
    static uint32_t win_span_ms   = FIRST_SAMPLE_MS;
    static uint32_t next_retry_ms = SD_RETRY_PERIOD_MS;
    static uint32_t last_bar_ms   = 0;

    uint32_t now = millis();

    if ((int32_t)(now - (win_start_ms + win_span_ms)) >= 0) {
        take_sample();
        win_start_ms = millis();
        win_span_ms  = SAMPLE_PERIOD_MS;
    }

    // Card infilata a scheda accesa: ritenta il mount ogni tanto.
    if (!SDCard_IsMounted() && (int32_t)(now - next_retry_ms) >= 0) {
        next_retry_ms = now + SD_RETRY_PERIOD_MS;
        if (SDCard_Init()) {
            Serial.printf("microSD montata ora (%lu MB)\n",
                          (unsigned long)SDCard_SizeMB());
            SDCard_WriteHeaderIfNew(LOG_PATH, LOG_HEADER);
            s_sd_write_ok = true;
            ui_refresh();
        }
    }

    if (now - last_bar_ms >= 200) {
        last_bar_ms = now;
        uint32_t elapsed = now - win_start_ms;
        int32_t  value   = (int32_t)((elapsed * 1000UL) / win_span_ms);
        if (value > 1000) value = 1000;
        if (lvgl_lock(-1)) {
            lv_bar_set_value(s_bar, value, LV_ANIM_OFF);
            lvgl_unlock();
        }
    }

    delay(5);
}
