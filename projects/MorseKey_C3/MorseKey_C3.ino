/*
 * MorseKey_C3 — decoder Morse per tasto telegrafico verticale
 * -----------------------------------------------------------
 * ESP32-C3 Supermini + tasto J. Junker (contatto in scambio) + OLED SSD1306
 * 128x64. Si batte col tasto, il testo decodificato scorre a schermo e in alto
 * compaiono i WPM misurati e i segni della lettera in corso.
 *
 * Cablaggio del tasto (tre morsetti):
 *   B comune (leva)          -> GND
 *   A lavoro (chiuso premendo) -> GPIO3
 *   C riposo (chiuso a riposo) -> GPIO4
 *
 * Antirimbalzo senza tempi: latch SR sui due contatti. A chiuso = premuto,
 * C chiuso = rilasciato, entrambi aperti = leva in corsa o in rimbalzo, e lo
 * stato resta quello di prima. Il rimbalzo di A non arriva mai a toccare C.
 *
 * Il latch gira in interrupt e mette in coda ogni cambio con il suo istante:
 * oled.display() tiene fermo loop() per ~25 ms, e un segno letto da loop()
 * verrebbe misurato con quell'errore.
 *
 * Decoder adattivo: punto e linea sono due medie mobili e la soglia fra i due
 * sta a meta'. Su un tasto verticale la linea non e' mai 3 punti esatti
 * (misurato il 2026-10-13: punti 60-130 ms, linee 150-270, circa il doppio),
 * quindi le soglie fisse da manuale sbaglierebbero.
 *
 * Le soglie delle PAUSE derivano dalle stesse due medie, non da medie proprie
 * delle pause. La v3 le aveva, e su SOS veloci si e' vista la trappola: le
 * pause fra lettere (150-320 ms) stavano sotto la soglia iniziale, venivano
 * contate come pause fra segni, alzavano quella media e con lei la soglia, e
 * la media delle pause fra lettere non si aggiornava mai: S e O si fondevano
 * in "...---". Le pause poi seguono il segno che precede: dopo un punto quella
 * fra segni arriva a ~110 ms e quella fra lettere parte da ~150, dopo una linea
 * 140 e 235. Da qui la soglia di fine lettera: dopo un punto la meta' fra punto
 * e linea, dopo una linea la linea media. Rigiocati i tempi registrati, 7 SOS
 * su 9 escono giusti (prima nessuno); gli altri due hanno un punto lungo quanto
 * una linea, e li' nessuna soglia sulle pause puo' aiutare.
 *
 * Otto o piu' punti di fila (il segnale di errore) cancellano l'ultimo carattere;
 * il tasto tenuto premuto per un secondo azzera lo schermo.
 *
 * Impostazioni Arduino IDE (Tools):
 *   Board:            ESP32C3 Dev Module
 *   USB CDC On Boot:  Enabled
 *   Flash Size:       4MB
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================ Hardware ============================
static constexpr int     PIN_LAVORO = 3;     // contatto A del tasto
static constexpr int     PIN_RIPOSO = 4;     // contatto C del tasto
static constexpr int     PIN_SDA    = 5;     // -> SDA dell'OLED (come lo starter C3_OLED_OTA)
static constexpr int     PIN_SCL    = 6;     // -> SCL dell'OLED
static constexpr int     PIN_LED    = 8;     // LED blu onboard (attivo LOW): acceso a tasto premuto
static constexpr int     PIN_BUZZER = 7;     // buzzer PASSIVO, + via 100 ohm, - a GND
static constexpr uint32_t TONO_HZ   = 650;   // tono di ascolto, il classico dei telegrafisti
static constexpr uint8_t OLED_ADDR  = 0x3C;
static constexpr int     OLED_W     = 128;
static constexpr int     OLED_H     = 64;

// Da incrementare a ogni firmware caricato: compare all'avvio.
//   v1  2026-10-13  prima versione: latch in interrupt, decoder adattivo, OLED
//   v2  2026-10-13  tono di ascolto sul buzzer; OLED assente rilevato davvero
//   v3  2026-10-13  tasto tenuto premuto 1 s: azzera lo schermo
//   v4  2026-10-13  soglie delle pause ricavate da punto/linea: a velocita' le lettere si fondevano
//   v5  2026-10-13  spazio fra parole da 4 a 3 linee medie
//   v6  2026-10-13  diagnostica su Serial spenta (DIAGNOSTICA = true per riaccenderla)
static const char* FW_VERSION = "v6";

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
static bool oledPresente = false;       // deciso all'avvio: si collega a scheda spenta

// ============================ Latch in interrupt ============================
struct Evento { bool premuto; uint32_t t; };
static constexpr uint8_t CODA = 32;
static volatile Evento  coda[CODA];
static volatile uint8_t codaTesta = 0, codaFondo = 0;
static volatile bool    latch = false;

static void IRAM_ATTR isrTasto() {
  bool a = digitalRead(PIN_LAVORO) == LOW;
  bool c = digitalRead(PIN_RIPOSO) == LOW;
  bool nuovo = latch;
  if (a && !c) nuovo = true;
  else if (c && !a) nuovo = false;
  if (nuovo == latch) return;               // rimbalzo o leva in corsa
  latch = nuovo;
  uint8_t succ = (codaTesta + 1) % CODA;
  if (succ == codaFondo) return;            // coda piena: a mano non succede
  coda[codaTesta].premuto = nuovo;
  coda[codaTesta].t = millis();
  codaTesta = succ;
}

// ============================ Decoder ============================
// Valori iniziali dai segni misurati il 2026-10-13; poi si adattano da soli.
static float mediaPunto        = 100;   // ms
static float mediaLinea        = 200;
static constexpr float PESO    = 0.2f;  // quanto conta l'ultimo segno nella media
static bool  ultimoLinea       = false; // l'ultimo segno battuto era una linea

static char    simbolo[9];              // punti e linee della lettera in corso
static uint8_t nSimbolo = 0;
static bool    simboloTroppoLungo = false;
static bool    premuto = false;
static bool    primoSegno = true;
static uint32_t tUltimo = 0;            // istante dell'ultimo cambio del tasto
static bool    letteraAperta = false;   // segni battuti ma lettera non ancora chiusa
static bool    parolaAperta  = false;   // lettere scritte ma spazio non ancora messo
static bool    azzeramento   = false;   // la pressione in corso ha gia' azzerato lo schermo
static constexpr uint32_t AZZERA_MS = 1000;   // una linea lenta resta sotto i 300 ms
static constexpr bool DIAGNOSTICA = false;    // su Serial: ogni segno con durata, pausa, medie e soglie
static uint32_t pausaPrima = 0;               // pausa prima del segno in corso, per la diagnostica

static float sogliaSegno()   { return (mediaPunto + mediaLinea) / 2; }
static float sogliaLettera() { return ultimoLinea ? mediaLinea : sogliaSegno(); }
// Fra lettere si arriva a ~330 ms (445 battendo lento), fra parole si parte
// da ~400: le due fasce si toccano, e 3 linee (~480 ms) e' il compromesso.
// Con 4 linee una pausa di 655 ms fra due SOS non dava lo spazio.
static float sogliaParola()  { return mediaLinea * 3; }

static const struct { const char* codice; char c; } MORSE[] = {
  {".-", 'A'},   {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},  {".", 'E'},
  {"..-.", 'F'}, {"--.", 'G'},  {"....", 'H'}, {"..", 'I'},   {".---", 'J'},
  {"-.-", 'K'},  {".-..", 'L'}, {"--", 'M'},   {"-.", 'N'},   {"---", 'O'},
  {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'},  {"...", 'S'},  {"-", 'T'},
  {"..-", 'U'},  {"...-", 'V'}, {".--", 'W'},  {"-..-", 'X'}, {"-.--", 'Y'},
  {"--..", 'Z'},
  {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'}, {"....-", '4'},
  {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'}, {"----.", '9'},
  {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {"-..-.", '/'}, {"-...-", '='},
  {"-....-", '-'}, {".----.", '\''}, {"-.--.", '('}, {"-.--.-", ')'}, {"---...", ':'},
  {".-.-.", '+'}, {".--.-.", '@'}, {".-..-.", '"'},
};

// ============================ Testo a schermo ============================
static constexpr int COLONNE = 21;      // font 6x8 su 128 px
static constexpr int RIGHE   = 6;       // sotto la riga di intestazione
static char testo[COLONNE * RIGHE + 1];
static int  lunTesto = 0;
static bool daRidisegnare = true;

static void aggiungiCarattere(char c) {
  if (c == ' ' && (lunTesto == 0 || testo[lunTesto - 1] == ' ')) return;
  if (lunTesto == COLONNE * RIGHE) {    // pieno: scorre di una riga
    memmove(testo, testo + COLONNE, COLONNE * (RIGHE - 1));
    lunTesto -= COLONNE;
  }
  testo[lunTesto++] = c;
  testo[lunTesto] = '\0';
  Serial.print(c);
  daRidisegnare = true;
}

static void cancellaUltimo() {
  while (lunTesto > 0 && testo[lunTesto - 1] == ' ') lunTesto--;
  if (lunTesto > 0) lunTesto--;
  testo[lunTesto] = '\0';
  Serial.print("[canc]");
  daRidisegnare = true;
}

static void chiudiLettera() {
  simbolo[nSimbolo] = '\0';
  bool tuttiPunti = simboloTroppoLungo || nSimbolo >= 8;
  for (uint8_t i = 0; i < nSimbolo && tuttiPunti; i++) if (simbolo[i] != '.') tuttiPunti = false;
  if (simboloTroppoLungo || nSimbolo >= 8) {
    if (tuttiPunti) cancellaUltimo();
    else aggiungiCarattere('*');
  } else {
    char c = '*';                       // codice sconosciuto
    for (const auto& m : MORSE) if (strcmp(m.codice, simbolo) == 0) { c = m.c; break; }
    aggiungiCarattere(c);
  }
  nSimbolo = 0;
  simboloTroppoLungo = false;
  letteraAperta = false;
  parolaAperta = true;
  daRidisegnare = true;
}

static void aggiorna(float& media, float valore) { media += PESO * (valore - media); }

static void tastoPremuto(uint32_t t) {
  uint32_t pausa = t - tUltimo;
  pausaPrima = primoSegno ? 0 : pausa;
  if (!primoSegno) {
    if (letteraAperta && pausa >= sogliaLettera()) chiudiLettera();
    if (parolaAperta && pausa >= sogliaParola()) { aggiungiCarattere(' '); parolaAperta = false; }
  }
  premuto = true;
  tUltimo = t;
}

// Pressione tenuta per AZZERA_MS: si cancella tutto mentre il tasto e' ancora
// giu', cosi' si vede subito che e' successo e si puo' lasciare.
static void azzera() {
  lunTesto = 0;
  testo[0] = '\0';
  nSimbolo = 0;
  simboloTroppoLungo = false;
  letteraAperta = false;
  parolaAperta = false;
  azzeramento = true;
  Serial.println("\n[azzerato]");
  daRidisegnare = true;
}

static void tastoRilasciato(uint32_t t) {
  uint32_t durata = t - tUltimo;
  premuto = false;
  tUltimo = t;
  if (azzeramento) {                    // non e' un segno: niente medie, niente lettera,
    azzeramento = false;                // e la pausa che segue non entra nelle medie
    primoSegno = true;
    return;
  }
  primoSegno = false;
  if (durata < 10) return;              // disturbo, non un segno
  bool linea = durata >= sogliaSegno();
  if (linea) aggiorna(mediaLinea, min((float)durata, mediaLinea * 2));   // una pressione lunghissima non sposta tutto
  else       aggiorna(mediaPunto, durata);
  if (mediaLinea < mediaPunto * 1.5f) mediaLinea = mediaPunto * 1.5f;     // le due medie non devono collassare
  ultimoLinea = linea;
  if (DIAGNOSTICA)
    Serial.printf("\n  %c %4lu ms dopo pausa %4lu | medie punto %3.0f linea %3.0f | soglie segno %3.0f lettera %3.0f parola %3.0f\n",
                  linea ? '-' : '.', durata, pausaPrima, mediaPunto, mediaLinea,
                  sogliaSegno(), sogliaLettera(), sogliaParola());
  if (nSimbolo < 8) simbolo[nSimbolo++] = linea ? '-' : '.';
  else simboloTroppoLungo = true;
  simbolo[nSimbolo] = '\0';
  letteraAperta = true;
  daRidisegnare = true;
}

// ============================ Disegno ============================
static void disegna() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 0);
  oled.printf("WPM %d", (int)(1200.0f / mediaPunto + 0.5f));   // PARIS: unita' ms = 1200 / WPM
  simbolo[nSimbolo] = '\0';
  oled.setCursor(OLED_W - 6 * 8, 0);
  oled.print(simbolo);
  oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

  for (int i = 0; i < lunTesto; i++) {
    oled.setCursor((i % COLONNE) * 6, 14 + (i / COLONNE) * 8);
    oled.write(testo[i]);
  }
  if (lunTesto < COLONNE * RIGHE)        // cursore
    oled.drawFastHLine((lunTesto % COLONNE) * 6, 14 + (lunTesto / COLONNE) * 8 + 7, 5, SSD1306_WHITE);

  oled.display();
}

// ============================ setup / loop ============================
void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);             // senza monitor aperto, print() non blocca il loop
#endif

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_LAVORO, INPUT_PULLUP);
  pinMode(PIN_RIPOSO, INPUT_PULLUP);
  ledcAttach(PIN_BUZZER, TONO_HZ, 8);
  ledcWriteTone(PIN_BUZZER, 0);

  // periphBegin=false: teniamo i pin passati a Wire.begin().
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  // oled.begin() fallisce solo se manca la RAM per il buffer: non guarda se
  // il display risponde. Lo si chiede al bus.
  Wire.beginTransmission(OLED_ADDR);
  oledPresente = Wire.endTransmission() == 0
              && oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, /*reset=*/true, /*periphBegin=*/false);
  if (oledPresente) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.printf("MorseKey_C3 %s\n\nbatti col tasto", FW_VERSION);
    oled.display();
  }
  Serial.printf("MorseKey_C3 %s pronto, OLED %s\n", FW_VERSION,
                oledPresente ? "presente" : "assente (0x3C non risponde): il testo esce solo qui");
  delay(1200);

  latch = digitalRead(PIN_LAVORO) == LOW;
  premuto = latch;
  attachInterrupt(digitalPinToInterrupt(PIN_LAVORO), isrTasto, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_RIPOSO), isrTasto, CHANGE);
  daRidisegnare = true;
}

void loop() {
  while (codaFondo != codaTesta) {
    Evento e = { coda[codaFondo].premuto, coda[codaFondo].t };
    codaFondo = (codaFondo + 1) % CODA;
    if (e.premuto) tastoPremuto(e.t); else tastoRilasciato(e.t);
    digitalWrite(PIN_LED, e.premuto ? LOW : HIGH);
    ledcWriteTone(PIN_BUZZER, e.premuto ? TONO_HZ : 0);
  }

  if (premuto && !azzeramento && millis() - tUltimo >= AZZERA_MS) azzera();

  // A tasto fermo, le pause che superano le soglie chiudono lettera e parola.
  if (!premuto && !primoSegno) {
    uint32_t pausa = millis() - tUltimo;
    if (letteraAperta && pausa >= sogliaLettera()) chiudiLettera();
    if (parolaAperta && pausa >= sogliaParola()) { aggiungiCarattere(' '); parolaAperta = false; }
  }

  static uint32_t tDisegno = 0;
  if (oledPresente && daRidisegnare && millis() - tDisegno >= 50) {
    tDisegno = millis();
    daRidisegnare = false;
    disegna();
  }
}
