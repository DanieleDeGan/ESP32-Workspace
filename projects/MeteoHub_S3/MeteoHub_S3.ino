/*
 * MeteoHub_S3 — hub della stazione meteo e-ink (Seeed XIAO ESP32-S3 Sense)
 * ---------------------------------------------------------------------------
 * STATO: hub ESP-NOW + pannello e-ink. Riceve i DATA dei nodi meteo e li
 * mostra sulla pagina 1/6; restano da fare microSD, orario NTP, web UI e OTA
 * (Fase 3 del piano). Le cinque pagine di prova del bring-up sono ancora tutte
 * qui, in coda: servono a distinguere un guasto del pannello da un guasto
 * della radio, che senza di loro si somiglierebbero (schermo che non cambia).
 * Il piano completo del progetto sta in docs/Stazione-Meteo.md (Fasi 2 e 3).
 *
 * I NODI SI ASSOCIANO A UN HUB SOLO. Un nodo tiene un unico peer (s_hub_peer
 * in libraries/EspNowLink/src/link_node.cpp) e manda i DATA in unicast: finche'
 * i nodi veri sono associati a projects/EnvNode_C3/, questa scheda NON li vede,
 * per quanto sia sul canale giusto e in finestra di associazione. Per provarla
 * serve un nodo che si associ a lei — examples/Link_Node_Demo/ su una board
 * qualsiasi, oppure un nodo vero riacceso mentre EnvNode_C3 ha la finestra
 * chiusa, sapendo che da quel momento smette di scrivere sulla SD dell'altro.
 *
 * COME SI USA: all'accensione stampa su Serial cosa dichiara il driver di se
 * stesso (dimensioni dopo la rotazione, fast partial update, tempi nominali) e
 * disegna la prima pagina. Poi ogni pressione del TASTO BOOT (GPIO0, quello
 * piccolo accanto al reset) passa alla successiva, cronometrando il disegno:
 *
 *   1/6  NODI — i nodi della stazione: nome, temperatura in grande, umidita',
 *        pressione, eta' dell'ultimo dato, trend. Si ridisegna da sola quando
 *        arriva un DATA (non piu' spesso di 20 s) e comunque ogni 5 minuti,
 *        perche' l'eta' dei valori e lo stato "muto" invecchiano da soli.
 *   2/6  GEOMETRIA — cornice, quattro tacche DIVERSE agli angoli (cosi' una
 *        rotazione o uno specchio non possono sembrare giusti), diagonale,
 *        tre corpi di testo, barre di retino 100/50/25/6%. Verifica
 *        orientamento, geometria e resa dei font.
 *   3/6  FORMATO-PROGETTO — un framebuffer da 15.000 byte costruito a mano bit
 *        per bit (1 bpp, MSB-first, 1 = bianco) e spinto con drawImage(). E' il
 *        contratto fra www/dither.html e il firmware, verificato prima che
 *        esista una riga di web UI: se i bit fossero impacchettati al
 *        contrario il righello a passo 8 px scivola rispetto alla cornice, se
 *        il passo riga non fosse 50 byte la diagonale si spezza a scaletta.
 *   4/6  CONTATORE — si aggiorna da solo ogni 20 s in refresh PARZIALE, con un
 *        completo ogni 10: la politica antighosting del piano (parziale
 *        spesso, completo di tanto in tanto), accelerata per vederla lavorare
 *        in pochi minuti invece che in un'ora.
 *   5/6  FOTO — una foto vera passata da www/dither.html e incollata in
 *        foto_prova.h. Chiude la catena browser -> pannello senza che esistano
 *        ancora ne' la microSD ne' la web UI: il firmware riceve 15.000 byte
 *        gia' impacchettati e li spinge, che e' esattamente quello che fara'
 *        leggendoli da /images/<nome>.bin.
 *   6/6  BIANCA — pulita, nessun aggiornamento. Dopo questa si ricomincia da
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
 * Da riga di comando, dalla radice del repo. Ora --libraries SERVE: da quando
 * c'e' l'hub, questo sketch include libraries/EspNowLink.
 *   arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB" --libraries libraries projects/MeteoHub_S3
 *
 * Dipendenze (Library Manager): GxEPD2 (che tira dentro Adafruit GFX).
 * Locali: libraries/EspNowLink. Trapiantati da projects/EnvNode_C3/ e da
 * tenere allineati a mano: remote_nodes.*, forecast.h, rtc_time.*.
 */

#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include "foto_prova.h"   // 15.000 byte usciti da www/dither.html

#include "remote_nodes.h"  // hub ESP-NOW, trapiantato da projects/EnvNode_C3/
#include "forecast.h"      // i nomi TREND_*: remote_nodes.h non lo include

#include "rtc_time.h"      // serve a remote_nodes per datare i DATA

// ---------------------------------------------------------------------------
// Hub ESP-NOW
// ---------------------------------------------------------------------------
// Nome con cui questa scheda si presenta ai nodi.
static const char HUB_NOME[] = "MeteoHub";

// Canale ESP-NOW. Qui va un numero ESPLICITO, al contrario di EnvNode_C3 che
// usa ESPNOW_LINK_CHANNEL_CURRENT (0): quella scheda sta su un access point e
// il canale glielo impone il router, questa (finche' non ha il WiFi, Fase 3)
// non ha nessuno che glielo imponga.
//
// 1 = il canale dell'access point di casa (verificato il 2026-08-27 su
// /api/stato dei nodi, campo "canale"). Ci vuole quello e non il 6 di
// Link_Init(), perche' i nodi veri stanno la': il DOIT e' connesso al WiFi e
// il canale glielo impone il router.
//
// IL ROVESCIO, da tenere a mente: un nodo tiene un hub solo, e chi lo adotta
// e' il primo hub in finestra di associazione che risponde al suo HELLO. Il
// nodo a batteria fa HELLO ad ogni power-cycle — e si riavvia da solo, cinque
// volte in tre giorni (uscita di sicurezza dei cinque risvegli muti) — mentre
// EnvNode_C3 ha la finestra normalmente CHIUSA. Un hub di sviluppo lasciato in
// pairing su questo canale se lo porterebbe via insieme al suo log su SD, che
// qui ancora non c'e'. Per questo la finestra NON si apre da sola all'avvio:
// si apre a mano, col tasto BOOT, e solo per il tempo che serve.
//
// Per provare invece con examples/Link_Node_Demo/, che usa Link_Init() e
// quindi ESPNOW_LINK_CHANNEL: mettere 6 qui. Se hub e nodo non sono sullo
// stesso canale non si sentono, e il guasto e' silenzioso da entrambe le parti.
static const uint8_t HUB_CANALE = 1;

// Durata della finestra aperta a mano col tasto BOOT.
static const uint32_t PAIRING_MANUALE_S = 120;

// Fuso orario. Costante di compilazione come in Timelapse_XIAO, non
// un'impostazione da web: questa scheda non viaggia. Senza WiFi l'orologio
// resta alla stima da __DATE__/__TIME__, che basta a datare i DATA in modo
// relativo; l'ora vera arriva con NTP in Fase 3.
static const char TZ_POSIX[] = "CET-1CEST,M3.5.0,M10.5.0/3";

// CS della microSD della scheda di espansione Sense. Non si monta la card (e'
// Fase 3), ma il pin va comunque pilotato ALTO prima di parlare con l'e-ink:
// lasciato flottante, la card puo' rispondere sul bus condiviso e sporcare i
// byte destinati al pannello. E' il prezzo di avere due periferiche sullo
// stesso SPI, ed e' una riga.
static const int8_t PIN_SD_CS = 21;

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
// Il tasto BOOT fa due cose: premuto e rilasciato cambia pagina, tenuto giu'
// apre (o chiude) la finestra di associazione. Serve un secondo gesto perche'
// l'hub, a differenza di EnvNode_C3, non ha una pagina web da cui comandarlo:
// finche' non c'e' la Fase 3 questo tasto e' l'unica interfaccia.
enum BootEvt : uint8_t { BOOT_NULLA = 0, BOOT_BREVE, BOOT_LUNGO };

static const uint32_t BOOT_LUNGO_MS = 1200;

static uint8_t bootEvent()
{
  static bool     s_stable      = HIGH;
  static bool     s_last        = HIGH;
  static uint32_t s_since       = 0;
  static uint32_t s_giu         = 0;
  static bool     s_lungoFatto  = false;

  const bool raw = digitalRead(PIN_BOOT);
  if (raw != s_last)
  {
    s_last  = raw;
    s_since = millis();
  }
  if (millis() - s_since > 40 && raw != s_stable)
  {
    s_stable = raw;
    if (s_stable == LOW)            // fronte di discesa: parte il cronometro
    {
      s_giu        = millis();
      s_lungoFatto = false;
      return BOOT_NULLA;
    }
    // rilascio: se la lunga e' gia' scattata, il rilascio non vale niente
    return s_lungoFatto ? BOOT_NULLA : BOOT_BREVE;
  }

  // La pressione lunga scatta mentre il tasto e' ancora giu', non al rilascio:
  // cosi' il pannello reagisce e si sa quando lasciare, invece di indovinare.
  if (s_stable == LOW && !s_lungoFatto && millis() - s_giu >= BOOT_LUNGO_MS)
  {
    s_lungoFatto = true;
    return BOOT_LUNGO;
  }
  return BOOT_NULLA;
}

// ---------------------------------------------------------------------------
// Le pagine
// ---------------------------------------------------------------------------
// Elenco di pagine + una funzione che le disegna: e' in piccolo l'astrazione
// che servira' in Fase 6. Ogni pressione di BOOT avanza di una, l'ultima e' la
// pagina bianca, e dopo quella si ricomincia — cosi' fermarsi lascia sempre il
// pannello pulito. Il cambio pagina e' un refresh completo, come da piano.
// ---------------------------------------------------------------------------
// Pagina NODI — quello per cui l'hub esiste
// ---------------------------------------------------------------------------
// Disegnata a mano con Adafruit_GFX, non come immagine: e' l'unica pagina che
// cambia da sola, e ridisegnarla deve costare un refresh, non una conversione.
//
// Politica di refresh (quella del piano): parziale quando arriva un dato
// nuovo, completo all'ingresso nella pagina e ogni NODI_FULL_OGNI parziali,
// altrimenti il ghosting si accumula.

static const int16_t NODI_TOP       = 28;   // sotto l'intestazione
static const int16_t NODI_BOT       = 272;  // sopra il piede
static const int16_t NODI_RIGA_H    = 61;   // 4 nodi entrano in 244 px
static const int     NODI_VISIBILI  = 4;    // oltre non c'e' spazio: vedi nota
static const uint8_t NODI_FULL_OGNI = 10;

// Virgola decimale: e' un pannello che sta in casa, non un log da macchina.
static String fmtNum(float v, int dec)
{
  if (!isfinite(v)) return String("--");
  String t(v, dec);
  t.replace('.', ',');
  return t;
}

// Testo allineato a destra: xRight e' il bordo destro, non l'inizio.
static void drawRight(const String& txt, int16_t xRight, int16_t yBase)
{
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(txt, 0, 0, &bx, &by, &bw, &bh);
  display.setCursor(xRight - (int16_t)bw - bx, yBase);
  display.print(txt);
}

// Testo centrato su xCentro. Serve una misura vera: allineare a destra con un
// offset stimato a occhio taglia le stringhe larghe sul bordo sinistro, dove
// il cursore finisce a coordinate negative e Adafruit_GFX non se ne lamenta.
static void drawCenter(const String& txt, int16_t xCentro, int16_t yBase)
{
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(txt, 0, 0, &bx, &by, &bw, &bh);
  int16_t x = xCentro - (int16_t)bw / 2 - bx;
  if (x < 2) x = 2;                      // meglio storto che tagliato
  display.setCursor(x, yBase);
  display.print(txt);
}

// "38 s", "12 min", "3 h": un e-ink non fa il cronometro, tre cifre bastano.
static String fmtEta(uint32_t s)
{
  if (s < 90)   return String(s) + " s";
  if (s < 5400) return String(s / 60) + " min";
  return String(s / 3600) + " h";
}

// Un nodo: nome e stato a sinistra, temperatura grande a destra, il resto su
// una riga sola sotto.
static void drawNodo(const RemoteNode& n, int16_t y)
{
  display.setFont(&FreeSansBold12pt7b);
  display.setCursor(6, y + 18);
  display.print(n.nome);

  // Il nodo muto e' la diagnostica principale finche' i nodi non hanno il
  // partitore della batteria: va vista prima dei valori, non dopo. Riquadro
  // pieno con testo in negativo — su bianco e nero e' l'unico "colore" che c'e'.
  if (!n.online && n.hasData)
  {
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(n.nome, 6, y + 18, &bx, &by, &bw, &bh);
    const int16_t x = 6 + (int16_t)bw + 10;
    display.fillRect(x, y + 2, 62, 20, GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(x + 6, y + 17);
    display.print("MUTO");
    display.setTextColor(GxEPD_BLACK);
  }

  if (!n.hasData)
  {
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(6, y + 44);
    display.print("in attesa del primo dato");
    return;
  }

  // Temperatura in grande. Il simbolo del grado non esiste nei font Adafruit
  // GFX (coprono 0x20-0x7E): si disegna, un cerchietto costa meno di un font.
  if (isfinite(n.value[0]))
  {
    display.setFont(&FreeSansBold24pt7b);
    drawRight(fmtNum(n.value[0], 1), 366, y + 38);
    display.drawCircle(374, y + 20, 3, GxEPD_BLACK);
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(381, y + 38);
    display.print("C");
  }

  // Riga di dettaglio: umidita', pressione, eta' del dato, trend. In
  // FreeMonoBold9pt ci stanno ~36 caratteri, e questi ci stanno tutti.
  String riga;
  if (isfinite(n.value[1])) riga += fmtNum(n.value[1], 0) + "%  ";
  if (isfinite(n.value[2])) riga += fmtNum(n.value[2], 1) + " hPa  ";
  riga += fmtEta(n.silenzioS) + " fa";
  if (n.trend != TREND_IGNOTO) riga += String("  ") + remote_trend_label(n.trend);

  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(6, y + 55);
  display.print(riga);
}

static void screenNodi(bool full)
{
  const int16_t W = display.width();
  const int16_t H = display.height();

  if (full) display.setFullWindow();
  else      display.setPartialWindow(0, 0, W, H);

  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    // --- intestazione ---
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(6, 20);
    display.print("STAZIONE METEO");

    char ora[8] = "";
    if (rtctime_format(rtctime_now(), "%H:%M", ora, sizeof(ora)))
    {
      // La tilde dice che l'ora e' la stima da build-time e non NTP: senza,
      // un orario preciso e sbagliato sembrerebbe vero. Sparisce in Fase 3,
      // quando arriva la rete.
      display.setFont(&FreeMonoBold9pt7b);
      drawRight(String(rtctime_isSynced() ? "" : "~") + ora, W - 6, 20);
    }
    display.drawLine(0, 26, W, 26, GxEPD_BLACK);

    // --- corpo ---
    const int n = remote_count();
    if (n == 0)
    {
      display.setFont(&FreeSansBold24pt7b);
      drawCenter("NESSUN NODO", W / 2, 150);
      display.setFont(&FreeMonoBold9pt7b);
      if (remote_pairing_active())
      {
        drawCenter("finestra di associazione aperta", W / 2, 182);
        drawCenter("accendi o riavvia il nodo", W / 2, 200);
      }
      else
      {
        drawCenter("tieni premuto BOOT", W / 2, 182);
        drawCenter("per associare un nodo", W / 2, 200);
      }
    }
    else
    {
      const int quanti = (n < NODI_VISIBILI) ? n : NODI_VISIBILI;
      for (int i = 0; i < quanti; i++)
      {
        RemoteNode nodo;
        if (!remote_get(i, &nodo)) continue;
        const int16_t y = NODI_TOP + (int16_t)i * NODI_RIGA_H;
        drawNodo(nodo, y);
        if (i + 1 < quanti) display.drawLine(6, y + NODI_RIGA_H - 4, W - 6,
                                             y + NODI_RIGA_H - 4, GxEPD_BLACK);
      }
    }

    // --- piede ---
    display.drawLine(0, NODI_BOT + 2, W, NODI_BOT + 2, GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);

    int muti = 0;
    for (int i = 0; i < n; i++)
    {
      RemoteNode nodo;
      if (remote_get(i, &nodo) && !nodo.online) muti++;
    }
    String piede = String(n) + (n == 1 ? " nodo" : " nodi");
    if (muti > 0) piede += String("  ") + muti + " muto";
    if (n > NODI_VISIBILI) piede += String("  (+") + (n - NODI_VISIBILI) + " non mostrati)";
    display.setCursor(6, 292);
    display.print(piede);

    if (remote_pairing_active())
    {
      const uint32_t r = remote_pairing_remaining_s();
      char buf[24];
      snprintf(buf, sizeof(buf), "ASSOCIAZIONE %lu:%02lu",
               (unsigned long)(r / 60), (unsigned long)(r % 60));
      drawRight(buf, W - 6, 292);
    }
    else if (!remote_ready())
    {
      // Da qui si distingue "nessun nodo ha ancora parlato" da "questa scheda
      // non ha nemmeno acceso la radio": senza, le due cose sono la stessa
      // pagina vuota. E il log seriale non e' un posto su cui contare — con
      // setTxTimeoutMs(0) le righe si buttano appena nessuno legge.
      drawRight("ESP-NOW NON ATTIVO", W - 6, 292);
    }
    else
    {
      drawRight(String("canale ") + HUB_CANALE, W - 6, 292);
    }
  }
  while (display.nextPage());
}

enum Page : uint8_t
{
  PAGE_NODI = 0,    // i nodi della stazione: la pagina per cui l'hub esiste
  PAGE_GFX,         // geometria, font, retini
  PAGE_RAW,         // 15.000 byte grezzi nel formato del progetto
  PAGE_COUNTER,     // contatore che si aggiorna in parziale ogni 20 s
  PAGE_PHOTO,       // una foto vera, uscita da dither.html
  PAGE_BLANK,       // bianca, nessun aggiornamento
  PAGE_COUNT
};

static const char* const PAGE_NAMES[PAGE_COUNT] =
{
  "1/6 nodi", "2/6 geometria", "3/6 formato-progetto", "4/6 contatore",
  "5/6 foto", "6/6 bianca"
};

static uint8_t  s_page     = PAGE_NODI;
static uint32_t s_next     = 0;   // prossimo aggiornamento del contatore
static uint32_t s_n        = 0;
static uint32_t s_partials = 0;

// --- stato della pagina nodi ---
// Un dato nuovo alza il flag, non ridisegna: il refresh lo decide il loop, che
// sa anche da quanto non si disegna. Cosi' due nodi che parlano insieme
// costano un refresh, non due.
static bool     s_nodiDirty     = false;
static uint32_t s_nodiUltimoMs  = 0;
static uint8_t  s_nodiParziali  = 0;

// Un dato nuovo si aspetta almeno questo prima di finire sul pannello: i nodi
// possono parlare a raffica (un DATA per nodo, piu' quelli di chi si associa)
// e un e-ink non e' un monitor.
static const uint32_t NODI_MIN_MS = 20000UL;
// ...e comunque si ridisegna ogni tanto anche senza dati nuovi, o l'eta' dei
// valori, lo stato "muto" e il conto alla rovescia dell'associazione
// resterebbero fermi a quello che erano. E' la cadenza del piano.
static const uint32_t NODI_MAX_MS = 300000UL;

// Chiamata da remote_loop() quando un nodo consegna un DATA. Gira nel contesto
// di loop(), non in un callback della radio: dentro remote_nodes il lavoro
// sulla coda ESP-NOW e' gia' stato fatto.
static void onDatoNodo(const RemoteNode* n)
{
  if (n == nullptr) return;
  s_nodiDirty = true;
  Serial.printf("[hub] %s  %.1f C  %.0f %%  %.1f hPa  seq %lu%s\n",
                n->nome, n->value[0], n->value[1], n->value[2],
                (unsigned long)n->seq, n->online ? "" : "  (era muto)");
}

static void showPage(uint8_t p)
{
  const uint32_t t0 = millis();

  switch (p)
  {
    case PAGE_NODI:
      // Sempre completo entrando: la pagina precedente e' ancora nella memoria
      // del controller e un parziale la lascerebbe sotto.
      screenNodi(true);
      s_nodiParziali = 0;
      s_nodiDirty    = false;
      s_nodiUltimoMs = millis();
      break;
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

  // PRIMA di toccare il bus: la microSD della Sense e' sullo stesso SPI e il
  // suo CS, lasciato flottante, la lascerebbe libera di rispondere insieme al
  // pannello. Va alzato anche se la card non si monta (Fase 3).
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  Serial.println();
  Serial.println("=== MeteoHub_S3 — hub della stazione meteo ===");
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

  // Orologio prima dell'hub: remote_nodes data i DATA con rtctime_now(), e un
  // modulo che parte con l'orologio a zero attribuisce il primo pacchetto al
  // 1970. Senza WiFi resta la stima da __DATE__/__TIME__ — imprecisa ma
  // monotona, che e' quello che serve per misurare la cadenza dei nodi.
  rtctime_begin(TZ_POSIX);
  rtctime_seedFromBuild();

  remote_on_data(onDatoNodo);
  if (remote_begin(HUB_NOME, HUB_CANALE))
  {
    // remote_begin() apre da sola una finestra di associazione all'avvio: su
    // EnvNode_C3 e' quello che fa rientrare i nodi gia' noti dopo un riavvio,
    // qui sarebbe un hub di sviluppo che adotta il primo nodo che si riavvia
    // in casa (vedi la nota su HUB_CANALE). I nodi gia' adottati non ne hanno
    // bisogno: sono in NVS e il driver li riconosce a finestra chiusa.
    remote_pairing_close();
    Serial.printf("[hub] in ascolto come %s sul canale %u, %d nodi noti\n",
                  HUB_NOME, (unsigned)HUB_CANALE, remote_count());
    Serial.println("[hub] associazione CHIUSA: tieni premuto BOOT per aprirla.");
  }
  else
  {
    // Stessa regola della microSD altrove nel repo: un pezzo che non parte non
    // ferma la scheda. Il pannello resta utile anche senza radio.
    Serial.println("[hub] ESP-NOW non attivo: il pannello funziona lo stesso.");
  }

  Serial.println("[epd] BOOT: pagina successiva. L'ultima e' la bianca.");
  showPage(s_page);
  Serial.printf("[hub] pronto: ESP-NOW %s, canale %u, %d nodi noti\n",
                remote_ready() ? "attivo" : "NON attivo",
                (unsigned)HUB_CANALE, remote_count());
}

void loop()
{
  // Sempre, su qualunque pagina: i nodi non aspettano che si stia guardando la
  // loro. remote_loop() preleva i DATA dal driver — se non gira, i pacchetti
  // arrivano alla radio e nessuno li raccoglie.
  remote_loop();

  const uint8_t ev = bootEvent();
  if (ev == BOOT_LUNGO)
  {
    if (remote_pairing_active()) remote_pairing_close();
    else                         remote_pairing_open(PAIRING_MANUALE_S);
    Serial.printf("[hub] associazione %s\n",
                  remote_pairing_active() ? "APERTA" : "chiusa");
    // Si va sulla pagina nodi e si ridisegna subito: un comando che non si
    // vede sul pannello e' un comando di cui non si sa se e' arrivato.
    s_page = PAGE_NODI;
    showPage(s_page);
    return;
  }
  if (ev == BOOT_BREVE)
  {
    s_page = (uint8_t)((s_page + 1) % PAGE_COUNT);
    showPage(s_page);
    return;
  }

  // La finestra scade da sola: quando succede, il pannello mostrerebbe ancora
  // il conto alla rovescia fino al refresh di cadenza (5 minuti dopo).
  {
    static bool s_pairingPrec = false;
    const bool ora = remote_pairing_active();
    if (ora != s_pairingPrec) { s_pairingPrec = ora; s_nodiDirty = true; }
  }

  if (s_page == PAGE_NODI)
  {
    const uint32_t da = millis() - s_nodiUltimoMs;
    const bool perDato   = s_nodiDirty && da >= NODI_MIN_MS;
    const bool perTempo  = da >= NODI_MAX_MS;
    if (!perDato && !perTempo) return;

    // Parziale spesso, completo ogni tanto: senza, il ghosting si accumula.
    const bool full = (s_nodiParziali >= NODI_FULL_OGNI);
    const uint32_t t0 = millis();
    screenNodi(full);
    display.hibernate();

    s_nodiParziali = full ? 0 : s_nodiParziali + 1;
    s_nodiDirty    = false;
    s_nodiUltimoMs = millis();
    Serial.printf("[epd] nodi %s (%s): %lu ms\n",
                  full ? "COMPLETO" : "parziale",
                  perDato ? "dato nuovo" : "cadenza",
                  (unsigned long)(millis() - t0));
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
