/*
 * MeteoHub_S3 — hub della stazione meteo e-ink (Seeed XIAO ESP32-S3 Sense)
 * ---------------------------------------------------------------------------
 * STATO: bring-up del SOLO pannello e-ink. Della stazione meteo vera qui non
 * c'e' ancora niente (nessun ESP-NOW, nessuna microSD, nessuna web UI, nessun
 * OTA): questo sketch serve a dimostrare tre cose, prima di costruirci sopra —
 * che il WeAct 4.2" e' cablato bene, che la classe GxEPD2 scelta e' quella
 * giusta per questo pannello, e che il refresh parziale funziona davvero.
 * Il piano completo del progetto sta in docs/Stazione-Meteo.md (Fase 2).
 *
 * COME SI USA: all'accensione stampa su Serial cosa dichiara il driver di se
 * stesso (dimensioni dopo la rotazione, fast partial update, tempi nominali) e
 * disegna la prima pagina. Poi ogni pressione del TASTO BOOT (GPIO0, quello
 * piccolo accanto al reset) passa alla successiva, cronometrando il disegno:
 *
 *   1/5  GEOMETRIA — cornice, quattro tacche DIVERSE agli angoli (cosi' una
 *        rotazione o uno specchio non possono sembrare giusti), diagonale,
 *        tre corpi di testo, barre di retino 100/50/25/6%. Verifica
 *        orientamento, geometria e resa dei font.
 *   2/5  FORMATO-PROGETTO — un framebuffer da 15.000 byte costruito a mano bit
 *        per bit (1 bpp, MSB-first, 1 = bianco) e spinto con drawImage(). E' il
 *        contratto fra www/dither.html e il firmware, verificato prima che
 *        esista una riga di web UI: se i bit fossero impacchettati al
 *        contrario il righello a passo 8 px scivola rispetto alla cornice, se
 *        il passo riga non fosse 50 byte la diagonale si spezza a scaletta.
 *   3/5  CONTATORE — si aggiorna da solo ogni 20 s in refresh PARZIALE, con un
 *        completo ogni 10: la politica antighosting del piano (parziale
 *        spesso, completo di tanto in tanto), accelerata per vederla lavorare
 *        in pochi minuti invece che in un'ora.
 *   4/5  FOTO — una foto vera passata da www/dither.html e incollata in
 *        foto_prova.h. Chiude la catena browser -> pannello senza che esistano
 *        ancora ne' la microSD ne' la web UI: il firmware riceve 15.000 byte
 *        gia' impacchettati e li spinge, che e' esattamente quello che fara'
 *        leggendoli da /images/<nome>.bin.
 *   5/5  BIANCA — pulita, nessun aggiornamento. Dopo questa si ricomincia da
 *        1/5, cosi' smettere di premere lascia sempre il pannello pulito: un
 *        e-ink e' bistabile, l'ultima immagine resta li' anche a scheda spenta,
 *        e il bianco e' lo stato in cui conviene lasciarlo per non favorire gli
 *        aloni permanenti.
 *
 * L'elenco di pagine con la loro funzione di disegno e' in piccolo
 * l'astrazione che servira' in Fase 6. Il cambio pagina e' sempre un refresh
 * completo, come da piano.
 *
 * IMPOSTAZIONI Arduino IDE (Tools):
 *   Board:            XIAO_ESP32S3        (non "ESP32S3 Dev Module")
 *   PSRAM:            OPI PSRAM
 *   Partition Scheme: Default 8MB with spiffs (3MB APP/1.5MB SPIFFS)
 *   USB CDC On Boot:  Enabled  (e' gia' il default di questa board)
 *
 * Da riga di comando, dalla radice del repo (niente --libraries: per ora
 * questo sketch non usa nulla di libraries/):
 *   arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB" projects/MeteoHub_S3
 *
 * Dipendenze (Library Manager): GxEPD2 (che tira dentro Adafruit GFX).
 */

#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include "foto_prova.h"   // 15.000 byte usciti da www/dither.html

// ---------------------------------------------------------------------------
// Quale pannello
// ---------------------------------------------------------------------------
// L'etichetta sul flat del modulo dice E042A87, controller SSD1683. Per quel
// controller GxEPD2 ha due classi che differiscono in UN SOLO byte (il valore
// scritto nel registro del fast full update: 0x6E contro 0x64) piu' il
// selettore a runtime selectFastFullUpdate(), che ce l'ha solo la prima:
//
//   1 = GxEPD2_420_GDEY042T81  <- default. E' la classe usata dall'esempio
//                                 ufficiale WeAct per questo modulo, quindi e'
//                                 provata su questo hardware. Il commento nel
//                                 driver dice che il suo 0x6E va bene anche
//                                 per i pannelli della serie precedente.
//   2 = GxEPD2_420_GYE042A87   <- ripiego 1: corrisponde alla sigla del
//                                 pannello alla lettera. Da provare se il
//                                 refresh veloce lascia aloni o schiarisce.
//   3 = GxEPD2_420             <- ripiego 2: lotti vecchi con UC8176. Niente
//                                 refresh parziale. Se anche questo non
//                                 disegna niente, il problema e' il cablaggio.
//
// Cambiare qui e ricompilare: e' una riga, non si perde tempo a indovinare.
#define EPD_PANEL 1

#if   EPD_PANEL == 1
  #define EPD_DRIVER  GxEPD2_420_GDEY042T81
  #define EPD_DRIVER_NAME "GxEPD2_420_GDEY042T81 (SSD1683)"
#elif EPD_PANEL == 2
  #define EPD_DRIVER  GxEPD2_420_GYE042A87
  #define EPD_DRIVER_NAME "GxEPD2_420_GYE042A87 (SSD1683)"
#elif EPD_PANEL == 3
  #define EPD_DRIVER  GxEPD2_420
  #define EPD_DRIVER_NAME "GxEPD2_420 (GDEW042T2, UC8176)"
#else
  #error "EPD_PANEL deve valere 1, 2 o 3"
#endif

// ---------------------------------------------------------------------------
// Cablaggio (XIAO ESP32-S3 Sense <-> WeAct e-Paper Module 4.2")
// ---------------------------------------------------------------------------
//   modulo      XIAO        GPIO   nota
//   --------    ---------   ----   ------------------------------------------
//   VCC         3V3          --    3,3 V. Mai 5 V: il pannello NON li tollera
//   GND         GND          --
//   SCL (SCK)   D8            7    condiviso con la microSD della Sense
//   SDA (MOSI)  D10           9    condiviso con la microSD della Sense
//   CS          D1            2    dedicato (la SD ha il suo, GPIO21)
//   DC          D2            3    dedicato
//   RES (RST)   D3            4    dedicato — serve anche a uscire da hibernate
//   BUSY        D0            1    dedicato, ingresso
//
// L'e-ink non usa MISO (si scrive e basta), quindi convive con la microSD sul
// bus SPI: bastano CS separati. In questo sketch la SD non viene toccata, la
// convivenza vera si prova in Fase 3.
// Restano liberi D4/D5 (GPIO5/6) per l'I2C e il tasto BOOT (GPIO0) per il
// cambio pagina. GPIO3 e' un pin di strapping dell'S3 (sorgente JTAG): come
// uscita pilotata dopo il boot va bene, l'importante e' non forzarlo al reset.
static const int16_t EPD_BUSY = 1;   // D0
static const int16_t EPD_CS   = 2;   // D1
static const int16_t EPD_DC   = 3;   // D2
static const int16_t EPD_RST  = 4;   // D3
static const int8_t  EPD_SCK  = 7;   // D8
static const int8_t  EPD_MISO = 8;   // D9  (inutilizzato dall'e-ink)
static const int8_t  EPD_MOSI = 9;   // D10

// Tasto BOOT gia' montato sulla XIAO: a massa quando premuto, quindi
// INPUT_PULLUP e fronte di discesa. E' un pin di strapping (tenuto basso al
// reset si entra nel bootloader), ma premerlo a scheda avviata e' innocuo.
static const int8_t  PIN_BOOT = 0;

// Il pannello sta tutto in RAM: 400 x 300 / 8 = 15.000 byte, quindi
// full-buffer (secondo parametro = HEIGHT) e niente paginazione.
GxEPD2_BW<EPD_DRIVER, EPD_DRIVER::HEIGHT> display(EPD_DRIVER(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Formato del progetto: quello che dither.html produce e che un domani
// arrivera' dalla SD gia' impacchettato.
static const int    IMG_W      = 400;
static const int    IMG_H      = 300;
static const size_t IMG_STRIDE = IMG_W / 8;            // 50 byte per riga
static const size_t IMG_BYTES  = IMG_STRIDE * IMG_H;   // 15.000 byte esatti

// ---------------------------------------------------------------------------
// Schermata 2 — prova disegnata con Adafruit_GFX
// ---------------------------------------------------------------------------
static void screenGfx()
{
  const int16_t W = display.width();
  const int16_t H = display.height();

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    // Cornice doppia: se il pannello fosse ritagliato o disallineato, il bordo
    // e' la prima cosa che lo mostra.
    display.drawRect(0, 0, W, H, GxEPD_BLACK);
    display.drawRect(1, 1, W - 2, H - 2, GxEPD_BLACK);

    // Tacche DIVERSE nei quattro angoli: cosi' una rotazione o uno specchio si
    // riconoscono a colpo d'occhio, invece di sembrare "giusti".
    display.fillRect(4, 4, 24, 24, GxEPD_BLACK);           // alto-sx: pieno
    display.drawRect(W - 28, 4, 24, 24, GxEPD_BLACK);      // alto-dx: vuoto
    display.fillRect(4, H - 28, 24, 8, GxEPD_BLACK);       // basso-sx: barra
    display.fillCircle(W - 16, H - 16, 12, GxEPD_BLACK);   // basso-dx: cerchio

    // Diagonale: qualunque errore di passo riga la trasforma in una scaletta.
    display.drawLine(0, 0, W - 1, H - 1, GxEPD_BLACK);

    display.setTextColor(GxEPD_BLACK);

    display.setFont(&FreeSansBold24pt7b);
    display.setCursor(40, 96);
    display.print("MeteoHub S3");

    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(40, 128);
    display.print("e-ink bring-up");

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(40, 154);
    display.print(EPD_DRIVER_NAME);
    display.setCursor(40, 172);
    display.printf("%d x %d  fast partial: %s", W, H,
                   display.epd2.hasFastPartialUpdate ? "si" : "no");

    // Barre di retino: e' cosi' che rendera' il dithering delle pagine
    // immagine. Se meta' e un quarto non si distinguono, il Floyd-Steinberg di
    // dither.html non avra' molto senso su questo pannello.
    const int16_t bx = 40, by = 196, bh = 40;
    const int16_t bw = W - 80;
    for (int16_t x = bx; x < bx + bw; x++)
    {
      const int16_t band = (x - bx) / (bw / 4);
      for (int16_t y = by; y < by + bh; y++)
      {
        bool black;
        switch (band)
        {
          case 0:  black = true;                            break;  // pieno
          case 1:  black = ((x + y) & 1) == 0;              break;  // 1/2
          case 2:  black = ((x & 1) == 0) && ((y & 1) == 0); break;  // 1/4
          default: black = ((x & 3) == 0) && ((y & 3) == 0); break;  // 1/16
        }
        if (black) display.drawPixel(x, y, GxEPD_BLACK);
      }
    }
    display.setCursor(bx, by + bh + 18);
    display.print("100%    50%    25%     6%");
  }
  while (display.nextPage());
}

// ---------------------------------------------------------------------------
// Schermata 3 — prova nel formato del progetto (15.000 byte grezzi)
// ---------------------------------------------------------------------------
// Le regole, identiche a quelle scritte in cima a dither.html:
//   riga = 50 byte, il bit 7 del primo byte di ogni riga e' il pixel x = 0
//   1 = bianco, 0 = nero
static inline void fbSet(uint8_t* fb, int x, int y, bool black)
{
  if (x < 0 || y < 0 || x >= IMG_W || y >= IMG_H) return;
  uint8_t& b = fb[(size_t)y * IMG_STRIDE + (size_t)(x >> 3)];
  const uint8_t mask = 0x80 >> (x & 7);
  if (black) b &= (uint8_t)~mask;
  else       b |= mask;
}

static void fbHLine(uint8_t* fb, int x0, int x1, int y)
{
  for (int x = x0; x <= x1; x++) fbSet(fb, x, y, true);
}

static void fbVLine(uint8_t* fb, int x, int y0, int y1)
{
  for (int y = y0; y <= y1; y++) fbSet(fb, x, y, true);
}

static void fbRect(uint8_t* fb, int x, int y, int w, int h)
{
  fbHLine(fb, x, x + w - 1, y);
  fbHLine(fb, x, x + w - 1, y + h - 1);
  fbVLine(fb, x, y, y + h - 1);
  fbVLine(fb, x + w - 1, y, y + h - 1);
}

static bool screenRawFormat()
{
  uint8_t* fb = (uint8_t*)malloc(IMG_BYTES);
  if (!fb)
  {
    Serial.println("[epd] malloc dei 15.000 byte fallita");
    return false;
  }
  memset(fb, 0xFF, IMG_BYTES);   // 0xFF = tutto bianco

  fbRect(fb, 0, 0, IMG_W, IMG_H);
  fbRect(fb, 1, 1, IMG_W - 2, IMG_H - 2);

  // Marcatore d'angolo pieno, solo in alto a sinistra: se comparisse altrove,
  // l'immagine e' specchiata o ruotata.
  for (int y = 6; y < 30; y++) fbHLine(fb, 6, 29, y);

  // Righello a passo 8 px: ogni linea nera cade sul bit 7 di un byte. Se
  // l'impacchettamento fosse LSB-first, il righello scivola di 7 px rispetto
  // alla cornice — visibile appoggiandolo al bordo sinistro.
  for (int x = 0; x < IMG_W; x += 8) fbVLine(fb, x, 40, 64);

  // Diagonale su tutto il pannello: prova del passo riga (50 byte).
  for (int i = 0; i < IMG_W; i++) fbSet(fb, i, (i * (IMG_H - 1)) / (IMG_W - 1), true);

  // Scacchiere: a 1 px deve sembrare grigio uniforme. Qualunque sfrangiatura
  // verticale a periodo 8 significa bit invertiti dentro il byte.
  for (int y = 90; y < 150; y++)
    for (int x = 40; x < 160; x++)
      if (((x + y) & 1) == 0) fbSet(fb, x, y, true);

  for (int y = 90; y < 150; y++)
    for (int x = 200; x < 320; x++)
      if ((((x >> 1) + (y >> 1)) & 1) == 0) fbSet(fb, x, y, true);

  // Cunei pieni: nero pieno e bianco pieno adiacenti, per giudicare il
  // contrasto reale del pannello.
  for (int y = 180; y < 230; y++) fbHLine(fb, 40, 199, y);
  fbRect(fb, 200, 180, 160, 50);

  // writeImage() + refresh(false), non drawImage(): quest'ultima manda i byte e
  // poi fa un refresh PARZIALE (misurato: 737 ms in tutto, contro i ~2,2 s di un
  // completo). Veloce, ma sotto resta l'alone della pagina precedente, e qui si
  // sta cambiando pagina — che da piano e' sempre un refresh completo. I 737 ms
  // restano il numero buono per uno slideshow che accetti un po' di ghosting.
  uint32_t t0 = millis();
  display.writeImage(fb, 0, 0, IMG_W, IMG_H, false, false, false);
  const uint32_t t_write = millis() - t0;
  t0 = millis();
  display.refresh(false);
  Serial.printf("[epd] %u byte scritti in %lu ms, refresh completo %lu ms\n",
                (unsigned)IMG_BYTES, (unsigned long)t_write,
                (unsigned long)(millis() - t0));

  free(fb);
  return true;
}

// ---------------------------------------------------------------------------
// Contatore in refresh parziale
// ---------------------------------------------------------------------------
// La finestra parziale sta in basso a destra e viene riscritta da sola: e'
// esattamente il gesto che servira' all'hub per aggiornare i numeri senza far
// lampeggiare tutta la pagina.
static const int16_t BOX_W = 220, BOX_H = 60;

static void drawCounter(uint32_t n, uint32_t uptime_s, bool full)
{
  const int16_t x = display.width() - BOX_W - 8;
  const int16_t y = display.height() - BOX_H - 8;

  if (full) display.setFullWindow();
  else      display.setPartialWindow(x, y, BOX_W, BOX_H);

  display.firstPage();
  do
  {
    if (full) display.fillScreen(GxEPD_WHITE);
    else      display.fillRect(x, y, BOX_W, BOX_H, GxEPD_WHITE);

    display.drawRect(x, y, BOX_W, BOX_H, GxEPD_BLACK);
    display.setTextColor(GxEPD_BLACK);

    display.setFont(&FreeSansBold24pt7b);
    display.setCursor(x + 10, y + 44);
    display.printf("%lu", (unsigned long)n);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(x + 110, y + 26);
    display.print(full ? "COMPLETO" : "parziale");
    display.setCursor(x + 110, y + 46);
    display.printf("%lus", (unsigned long)uptime_s);
  }
  while (display.nextPage());
}

// ---------------------------------------------------------------------------
// Pannello a riposo e tasto BOOT
// ---------------------------------------------------------------------------
static void screenBlank()
{
  // Sempre a finestra intera: pulire in parziale lascerebbe l'alone di quello
  // che c'era prima, ed e' proprio quello che qui non si vuole.
  display.setFullWindow();
  display.firstPage();
  do { display.fillScreen(GxEPD_WHITE); } while (display.nextPage());
}

// Ritorna true una volta sola per ogni pressione. Antirimbalzo a 40 ms.
// Nota: durante un refresh (mezzo secondo abbondante) il loop e' fermo dentro
// GxEPD2 e una pressione si perde. Va bene per un tasto premuto a mano.
static bool bootPressed()
{
  static bool     s_stable = HIGH;
  static bool     s_last   = HIGH;
  static uint32_t s_since  = 0;

  const bool raw = digitalRead(PIN_BOOT);
  if (raw != s_last)
  {
    s_last  = raw;
    s_since = millis();
  }
  if (millis() - s_since > 40 && raw != s_stable)
  {
    s_stable = raw;
    if (s_stable == LOW) return true;   // fronte di discesa = premuto
  }
  return false;
}

// ---------------------------------------------------------------------------
// Le pagine
// ---------------------------------------------------------------------------
// Elenco di pagine + una funzione che le disegna: e' in piccolo l'astrazione
// che servira' in Fase 6. Ogni pressione di BOOT avanza di una, l'ultima e' la
// pagina bianca, e dopo quella si ricomincia — cosi' fermarsi lascia sempre il
// pannello pulito. Il cambio pagina e' un refresh completo, come da piano.
enum Page : uint8_t
{
  PAGE_GFX = 0,     // geometria, font, retini
  PAGE_RAW,         // 15.000 byte grezzi nel formato del progetto
  PAGE_COUNTER,     // contatore che si aggiorna in parziale ogni 20 s
  PAGE_PHOTO,       // una foto vera, uscita da dither.html
  PAGE_BLANK,       // bianca, nessun aggiornamento
  PAGE_COUNT
};

static const char* const PAGE_NAMES[PAGE_COUNT] =
{
  "1/5 geometria", "2/5 formato-progetto", "3/5 contatore",
  "4/5 foto", "5/5 bianca"
};

static uint8_t  s_page     = PAGE_GFX;
static uint32_t s_next     = 0;   // prossimo aggiornamento del contatore
static uint32_t s_n        = 0;
static uint32_t s_partials = 0;

static void showPage(uint8_t p)
{
  const uint32_t t0 = millis();

  switch (p)
  {
    case PAGE_GFX:
      screenGfx();
      break;
    case PAGE_RAW:
      screenRawFormat();
      break;
    case PAGE_COUNTER:
      // Si entra sempre con un completo: la pagina precedente e' ancora nella
      // memoria del controller e un parziale la lascerebbe sotto.
      s_n = 0;
      s_partials = 0;
      drawCounter(s_n, millis() / 1000, true);
      s_next = millis() + 20000UL;
      break;
    case PAGE_PHOTO:
      // Il percorso vero di una pagina immagine: 15.000 byte gia' impacchettati
      // spinti in RAM del controller e basta, nessuna conversione. L'unica
      // differenza dal futuro e' da dove arrivano i byte — qui dalla flash,
      // domani da /images/<nome>.bin sulla microSD.
      display.writeImage(FOTO_PROVA, 0, 0, IMG_W, IMG_H, false, false, true);
      display.refresh(false);
      break;
    case PAGE_BLANK:
    default:
      screenBlank();
      break;
  }

  display.hibernate();
  Serial.printf("[epd] pagina %s: %lu ms\n", PAGE_NAMES[p],
                (unsigned long)(millis() - t0));
}

// ---------------------------------------------------------------------------

void setup()
{
  Serial.begin(115200);
  // Scritture su Serial mai bloccanti. Senza questo, quando il cavo USB non e'
  // collegato (o c'e' ma nessuno legge la porta) il buffer del CDC si riempie e
  // ogni printf aspetta il timeout: misurato, un aggiornamento da 827 ms e'
  // diventato 10.639 ms, tutto passato dentro le diagnostiche di GxEPD2. Su una
  // scheda che deve stare accesa da sola per settimane il log non deve poter
  // rallentare niente: se nessuno ascolta, si butta.
  Serial.setTxTimeoutMs(0);
  const uint32_t t_wait = millis();
  while (!Serial && millis() - t_wait < 3000) delay(10);

  pinMode(PIN_BOOT, INPUT_PULLUP);

  Serial.println();
  Serial.println("=== MeteoHub_S3 — bring-up pannello e-ink ===");
  Serial.printf("[epd] driver: %s\n", EPD_DRIVER_NAME);

  // Pin SPI espliciti: sono gia' i default della XIAO, ma scriverli qui rende
  // il cablaggio leggibile invece di lasciarlo a un file di variante. La
  // SPI.begin() interna di GxEPD2 e' un no-op dopo questa.
  SPI.end();
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);

  // 50 ms di reset e' quello che usa l'esempio ufficiale WeAct (il default di
  // GxEPD2 e' 10 ms). Se il pannello sembrasse morto, e' il primo numero da
  // toccare.
  display.init(115200, true, 50, false);
  display.setRotation(0);          // 0 = 400x300 nativo, come il formato .bin

  Serial.printf("[epd] dopo la rotazione: %d x %d\n", display.width(), display.height());
  Serial.printf("[epd] partial update: %s, fast partial: %s\n",
                display.epd2.hasPartialUpdate ? "si" : "no",
                display.epd2.hasFastPartialUpdate ? "si" : "no");
  Serial.printf("[epd] tempi nominali: completo %u ms, parziale %u ms\n",
                (unsigned)EPD_DRIVER::full_refresh_time,
                (unsigned)EPD_DRIVER::partial_refresh_time);

  Serial.println("[epd] BOOT: pagina successiva. L'ultima e' la bianca.");
  showPage(s_page);
}

void loop()
{
  if (bootPressed())
  {
    s_page = (uint8_t)((s_page + 1) % PAGE_COUNT);
    showPage(s_page);
    return;
  }

  // Solo la pagina del contatore ha qualcosa da aggiornare da sola.
  if (s_page != PAGE_COUNTER) return;
  if ((int32_t)(millis() - s_next) < 0) return;
  s_next = millis() + 20000UL;

  s_n++;
  // Politica antighosting del piano: parziale spesso, completo ogni tanto. Qui
  // uno completo ogni 10 parziali per vederlo succedere in pochi minuti;
  // sull'hub vero sara' "ogni ora o al cambio pagina".
  const bool full = (s_partials >= 10);
  const uint32_t t0 = millis();
  drawCounter(s_n, millis() / 1000, full);
  display.hibernate();

  Serial.printf("[epd] contatore #%lu %s: %lu ms\n", (unsigned long)s_n,
                full ? "COMPLETO" : "parziale", (unsigned long)(millis() - t0));
  s_partials = full ? 0 : s_partials + 1;
}
