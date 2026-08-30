/**
 * AMOLED_1.91_LVGL.ino  —  starter LVGL + SquareLine Studio (sketch vuoto)
 *
 * Tutto il boilerplate display/touch/LVGL e' nelle librerie condivise
 * AMOLED191_Display/AMOLED191_Touch (cartella libraries/ del repo). Qui
 * resta SOLO la tua logica: UI, WiFi, SD, sensori.
 *
 * Per un esempio funzionante vedi examples/Orientation_IMU.
 *
 * REGOLA FONDAMENTALE:
 *   Il rendering LVGL gira in un task dedicato. Ogni volta che leggi o
 *   modifichi un oggetto LVGL da QUI (loop, task tuoi, callback WiFi),
 *   devi avvolgere il codice in lvgl_lock() / lvgl_unlock().
 *
 * ARDUINO IDE (Tools):
 *   Board=ESP32S3 Dev Module | Flash 16MB
 *   Partizione="16M Flash (3MB APP/9.9MB FATFS)"
 *   PSRAM=OPI PSRAM | USB CDC On Boot=Enabled
 */

#include <AMOLED191_Display.h>
#include <AMOLED191_Touch.h>
#include "ui.h"

// #include <WiFi.h>
// #include <SD.h>

static uint32_t last_update = 0;

static float read_sensor(void)
{
    // TODO: leggi qui il tuo sensore reale.
    return 23.5f;
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

    // 1) Display + LVGL + task di rendering
    Display_Init();

    // 1b) Touch (opzionale: ometti queste due righe per un progetto senza touch)
    Touch_Init();
    Touch_RegisterLvglIndev();

    // 2) UI generata da SquareLine: si creano oggetti LVGL -> serve il lock
    if (lvgl_lock(-1)) {
        ui_init();
        lvgl_unlock();
    }

    // 3) La tua periferica: WiFi, SD, sensori...
    //    SD: ATTENZIONE, su questa scheda la microSD CONDIVIDE i pin col
    //    display (CLK=GPIO47, MISO=GPIO8); MOSI=GPIO42, CS=GPIO9. Non e' un
    //    bus indipendente: segui il demo SD_Test di Waveshare.
}

void loop()
{
    if (millis() - last_update >= 1000) {
        last_update = millis();

        float t = read_sensor();
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f C", t);

        if (lvgl_lock(-1)) {
            // lv_label_set_text(ui_LabelTemp, buf);   // <- tuo oggetto SquareLine
            lvgl_unlock();
        }
    }

    delay(5);
}
