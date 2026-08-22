/*
 * C3_OLED_OTA — ESP32-C3 Supermini + OLED 0.96" I2C (SSD1306 128x64) con OTA
 * --------------------------------------------------------------------------
 * Starter: display SSD1306 via Adafruit + aggiornamento firmware OTA sia da
 * Arduino IDE (ArduinoOTA) sia da browser (pagina web /update).
 *
 * Il boilerplate di rete/OTA sta in net_ota.cpp; qui resta la logica app
 * e il disegno a schermo. Compila secrets.h con la tua rete WiFi.
 *
 * Impostazioni Arduino IDE (Tools):
 *   Board:            ESP32C3 Dev Module
 *   USB CDC On Boot:  Enabled            (Serial via USB nativo)
 *   Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA)   <-- consigliato
 *                     (oppure "Default 4MB with spiffs"; entrambi hanno OTA,
 *                      ma "Default" e' gia' all'84% con questo sketch)
 *                     NON usare "Huge APP (3MB No OTA)".
 *   Flash Size:       4MB
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "net_ota.h"
#include "secrets.h"   // per stampare OTA_HOSTNAME a schermo

// ============================ Hardware ============================
// ESP32-C3 Supermini. Pin liberi scelti per NON toccare il LED (GPIO8)
// ne' il tasto BOOT (GPIO9). Cambiali qui se il tuo cablaggio e' diverso.
static constexpr int     PIN_SDA   = 5;      // -> SDA dell'OLED
static constexpr int     PIN_SCL   = 6;      // -> SCL dell'OLED
static constexpr int     PIN_LED   = 8;      // LED blu onboard (attivo LOW)
static constexpr uint8_t OLED_ADDR = 0x3C;   // indirizzo I2C tipico (0x3C, a volte 0x3D)
static constexpr int     OLED_W    = 128;
static constexpr int     OLED_H    = 64;
static constexpr int     OLED_RST  = -1;     // moduli a 4 pin: nessun reset

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, OLED_RST);

// Versione firmware: cambiala a ogni build, cosi' a schermo riconosci quale
// firmware sta girando dopo un aggiornamento OTA.
// Da incrementare a ogni firmware caricato: la pagina lo mostra, ed e' l'unico
// modo per sapere da remoto quale versione sta davvero girando.
//   v4  2026-08-22  Serial.setTxTimeoutMs(0), vedi la nota in setup()
static const char* FW_VERSION = "v4";

static bool     otaActive = false;   // true mentre un update e' in corso
static uint32_t lastDraw  = 0;

// Pallina che rimbalza nella fascia bassa dello schermo (prova visibile OTA).
static constexpr int BALL_R   = 3;
static constexpr int BALL_TOP = 36;            // bordo alto della fascia
static constexpr int BALL_BOT = OLED_H - 1;    // bordo basso
static float bx = 24, by = 50, vx = 2.4f, vy = 1.7f;

// ---------------------------------------------------------------------
//  Feedback a schermo durante un update OTA (chiamata da net_ota).
//  percent = 0..100, oppure -1 = sconosciuto (upload web).
// ---------------------------------------------------------------------
static void onOtaProgress(int percent, const char* what) {
  otaActive = true;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("Aggiornamento OTA");
  oled.setCursor(0, 14);
  oled.println(what);
  if (percent >= 0) {
    oled.drawRect(0, 34, OLED_W, 12, SSD1306_WHITE);
    oled.fillRect(2, 36, (OLED_W - 4) * percent / 100, 8, SSD1306_WHITE);
    oled.setCursor(0, 52);
    oled.printf("%d%%", percent);
  } else {
    oled.setCursor(0, 40);
    oled.println("in corso...");
  }
  oled.display();
}

// ---------------------------------------------------------------------
//  Schermata di stato "a riposo" — QUI va la tua UI applicativa.
// ---------------------------------------------------------------------
static void drawStatus() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  // Header con versione firmware (cambia a ogni build -> prova OTA).
  oled.setCursor(0, 0);
  oled.print("C3-OLED-OTA   FW ");
  oled.println(FW_VERSION);
  oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

  oled.setCursor(0, 14);
  if (net_isConnected()) {
    oled.print("IP ");
    oled.println(net_ip());
  } else {
    oled.println("WiFi: off (ritento)");
  }
  oled.setCursor(0, 24);
  oled.print("up ");
  oled.print(millis() / 1000);
  oled.print(" s   OTA ok");

  // Fascia bassa: pallina che rimbalza (segno visibile che gira il nuovo FW).
  oled.drawFastHLine(0, BALL_TOP - 2, OLED_W, SSD1306_WHITE);
  bx += vx;
  by += vy;
  if (bx <= BALL_R || bx >= OLED_W - 1 - BALL_R) vx = -vx;
  if (by <= BALL_TOP + BALL_R || by >= BALL_BOT - BALL_R) vy = -vy;
  bx = constrain(bx, (float)BALL_R, (float)(OLED_W - 1 - BALL_R));
  by = constrain(by, (float)(BALL_TOP + BALL_R), (float)(BALL_BOT - BALL_R));
  oled.fillCircle((int)bx, (int)by, BALL_R, SSD1306_WHITE);

  // <-- Aggiungi qui il resto della tua logica di visualizzazione.

  oled.display();
}

// ============================ setup / loop ============================
void setup() {
  Serial.begin(115200);

  // Scritture su Serial mai bloccanti. Su C3 e S3 la Serial dell'USB non e'
  // una UART ma la CDC del chip: se il PC ha riconosciuto la porta e nessuno
  // la sta leggendo, il buffer si riempie e ogni print() aspetta un timeout
  // interno. Finche' aspetta, loop() e' fermo - e con lui net_loop(), quindi
  // web server e OTA. Un nodo alimentato da un caricatore non se ne accorge
  // (senza pin dati la porta non viene mai riconosciuta e le scritture si
  // buttano da sole), ma basta collegarlo a un PC perche' cominci a bloccarsi.
  //
  // Qui le stampe periodiche sono solo quelle del watchdog WiFi, ogni 30 s
  // mentre la rete e' giu': cioe' proprio quando il codice di riconnessione
  // deve poter girare. Sta in questo STARTER anche perche' ogni progetto che
  // nasce copiandolo se lo porta dietro - EnvNode_C3 aveva ereditato la
  // mancanza da qui.
  //
  // Il guardia serve perche' con "USB CDC On Boot: Disabled" la Serial torna
  // a essere una UART, che questo metodo non ce l'ha e non ne ha bisogno.
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);   // LED spento (attivo LOW)

  // I2C + OLED. periphBegin=false: teniamo i pin passati a Wire.begin(),
  // altrimenti Adafruit reinizializza Wire sui pin di default.
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, /*reset=*/true, /*periphBegin=*/false)) {
    Serial.println("[OLED] init fallita: controlla cablaggio SDA/SCL e indirizzo (0x3C/0x3D).");
    // Si continua lo stesso: l'OTA deve funzionare anche senza display.
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("C3-OLED-OTA");
  oled.println("WiFi...");
  oled.display();

  net_setOtaProgressCb(onOtaProgress);
  net_begin();   // WiFi + ArduinoOTA + web /update
}

void loop() {
  net_loop();               // gestisce ArduinoOTA + web server
  if (otaActive) return;    // durante un update non disegnare altro

  if (millis() - lastDraw > 50) {   // ~20 fps (pallina fluida)
    lastDraw = millis();
    drawStatus();
  }

  // Heartbeat: LED acceso mezzo secondo ogni secondo (attivo LOW).
  digitalWrite(PIN_LED, (millis() % 1000 < 500) ? LOW : HIGH);
}
