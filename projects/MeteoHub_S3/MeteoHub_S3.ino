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
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

#include "foto_prova.h"   // 15.000 byte usciti da www/dither.html

#include <WiFi.h>          // solo per WiFi.localIP(), da mostrare sul pannello
#include <EspNowLink.h>    // ESPNOW_LINK_CHANNEL_CURRENT
#include "remote_nodes.h"  // hub ESP-NOW, trapiantato da projects/EnvNode_C3/
#include "forecast.h"      // i nomi TREND_*: remote_nodes.h non lo include

#include "rtc_time.h"      // serve a remote_nodes per datare i DATA
#include "sd_logger.h"     // microSD della Sense: i CSV dei nodi
#include "net_ota.h"       // WiFi + ArduinoOTA + /update + WebServer condiviso
#include "web_ui.h"        // pagina di stato e API, registrate su net_server()
#include "secrets.h"       // OTA_HOSTNAME, per dirlo sul pannello

static const char FW_VERSION[] = "v3";

// ---------------------------------------------------------------------------
// Hub ESP-NOW
// ---------------------------------------------------------------------------
// Nome con cui questa scheda si presenta ai nodi.
static const char HUB_NOME[] = "MeteoHub";

// Canale ESP-NOW: ESPNOW_LINK_CHANNEL_CURRENT (0), mai un numero esplicito.
// Da quando c'e' il WiFi (Fase 3) questa scheda sta su un access point, quindi
// il canale glielo impone il router e forzarlo chiamerebbe
// esp_wifi_set_channel() su una STA connessa, facendo cadere la connessione.
// Con lo 0 i peer sono registrati sul "canale corrente" e seguono l'AP da
// soli. Conseguenza da non dimenticare, ed e' il motivo per cui /api/nodi lo
// riporta: i nodi devono stare sul canale dell'AP, e uno che dorme senza WiFi
// dovra' impostarlo esplicitamente.
//
// Prima della Fase 3 qui c'era un numero fisso (1, l'AP di casa) perche' senza
// WiFi nessuno lo imponeva. Per provare con examples/Link_Node_Demo/, che usa
// Link_Init() e quindi il canale 6, serve ancora un numero: si passa 6 a
// remote_begin() invece della costante qui sotto.
static const uint8_t HUB_CANALE = ESPNOW_LINK_CHANNEL_CURRENT;

static const uint32_t PAIRING_MANUALE_S = 120;

// Contatori mostrati da /api/stato e dal pannello. Stanno in RAM: sono "da
// quando questa scheda e' accesa", e scriverli in NVS costerebbe un'erase per
// pacchetto.
static uint32_t s_righeScritte = 0;
static uint32_t s_epdRefresh   = 0;
static uint32_t s_epdUltimoMs  = 0;
static uint32_t s_epdOrologioMs = 0;   // quanto costa il refresh del solo orologio

const char* app_fw_version()    { return FW_VERSION; }
const char* app_hub_nome()      { return HUB_NOME; }
uint32_t    app_righe_scritte() { return s_righeScritte; }
uint32_t    app_epd_refresh()   { return s_epdRefresh; }
uint32_t    app_epd_ultimo_ms() { return s_epdUltimoMs; }
uint32_t    app_epd_orologio_ms() { return s_epdOrologioMs; }

// --- stato della pagina nodi ---
// Un dato nuovo alza il flag, non ridisegna: il refresh lo decide il loop, che
// sa anche da quanto non si disegna. Cosi' due nodi che parlano insieme
// costano un refresh, non due.
static bool     s_nodiDirty     = false;
static uint32_t s_nodiUltimoMs  = 0;
static uint8_t  s_nodiParziali  = 0;
static uint32_t s_oraUltimoMs   = 0;   // ultimo refresh del solo orologio
static uint32_t s_fullUltimoMs  = 0;   // ultimo refresh COMPLETO, per l'alone

// Un dato nuovo si aspetta almeno questo prima di finire sul pannello. Con i
// nodi a 60 e 300 s la cadenza reale diventa un refresh ogni due minuti: il
// piano ne chiedeva uno ogni 5-10, i 20 s di prima erano molto piu' aggressivi
// del necessario e pagavano in ghosting per mostrare numeri che cambiano di un
// decimo di grado. L'ora dell'ultimo pacchetto, che e' quello che si legge a
// schermo, non invecchia comunque: e' un istante, non un conto alla rovescia.
static const uint32_t NODI_MIN_MS = 120000UL;
// ...e comunque si ridisegna ogni tanto anche senza dati nuovi, o l'eta' dei
// valori, lo stato "muto" e il conto alla rovescia dell'associazione
// resterebbero fermi a quello che erano. E' la cadenza del piano.
static const uint32_t NODI_MAX_MS = 300000UL;

// L'orologio in alto a destra si aggiorna da solo ogni minuto. Costa poco
// perche' NON ridisegna la pagina: la finestra parziale copre solo il suo
// rettangolo, poche decine di righe invece di trecento.
static const uint32_t ORA_MS = 60000UL;

// ...ma un rettangolo riscritto sessanta volte l'ora si sporca, mentre il resto
// della pagina resta pulito: l'alone diventa una macchia localizzata, ed e' il
// modo peggiore in cui un e-ink invecchia. Un completo ogni ora lo cancella.
// E' la cadenza che il piano prevedeva fin dall'inizio e che finora non c'era:
// il completo arrivava solo contando i parziali, quindi in una giornata calma
// poteva non arrivare mai.
static const uint32_t FULL_OGNI_MS = 3600000UL;

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

static const int16_t NODI_TOP       = 34;   // sotto l'intestazione
static const int16_t NODI_BOT       = 266;  // sopra il piede
static const int     NODI_VISIBILI  = 4;    // oltre non c'e' spazio leggibile
static const uint8_t NODI_FULL_OGNI = 10;

// Fino a due nodi si usa il blocco COMODO: c'e' spazio per la temperatura
// grande e per il trend scritto per esteso. Da tre in su si passa a quello
// compatto, che sacrifica il corpo del carattere per farceli stare tutti.
// Il pannello e' appeso a un muro e si legge da lontano: la dimensione del
// numero non e' vezzo grafico, e' la distanza a cui la pagina funziona.
static const int NODI_COMODI_FINO_A = 2;

// Il rettangolo dell'ora, in alto a destra. Sta qui e non dentro le due
// funzioni che lo usano perche' devono per forza combaciare: se la finestra
// parziale e il disegno non coincidono, l'ora vecchia resta sotto la nuova.
static const int16_t ORA_X = 268, ORA_Y = 0, ORA_W = 132, ORA_H = 31;

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

// L'ORA dell'ultimo pacchetto, non da quanto tempo e' arrivato. Un istante non
// invecchia: resta vero anche quando il pannello non si ridisegna da un pezzo,
// mentre un "38 s fa" diventa una bugia dopo trenta secondi — e su un e-ink
// che si aggiorna ogni due minuti sarebbe sbagliato quasi sempre.
static String fmtOra(time_t t)
{
  char buf[8];
  if (t <= 0 || !rtctime_format(t, "%H:%M", buf, sizeof(buf))) return String("--:--");
  return String(buf);
}

// Il grado: nei font Adafruit GFX non c'e' (coprono 0x20-0x7E), si disegna.
static void drawGrado(int16_t x, int16_t y, int16_t r)
{
  display.drawCircle(x, y, r, GxEPD_BLACK);
  if (r > 2) display.drawCircle(x, y, r - 1, GxEPD_BLACK);   // piu' spesso, si vede meglio
}

// La freccia del trend barometrico: l'inclinazione E' il dato. Una parola
// ("in salita lenta") va letta, una freccia si vede da tre metri — ed e' la
// distanza da cui questo pannello viene guardato di solito.
static void drawFrecciaTrend(int16_t x, int16_t y, uint8_t trend)
{
  // Gradi rispetto all'orizzontale, uno per livello dell'enum forecast_trend_t.
  static const int8_t ANGOLI[] = {0, -70, -45, -30, -15, 0, 15, 30, 45, 70};
  if (trend >= sizeof(ANGOLI)) return;
  if (trend == TREND_IGNOTO) {
    // Storico insufficiente: un punto interrogativo sarebbe rumore, meglio un
    // trattino che dice "non lo so ancora" senza somigliare a "stabile".
    display.drawFastHLine(x - 8, y, 6, GxEPD_BLACK);
    display.drawFastHLine(x + 2, y, 6, GxEPD_BLACK);
    return;
  }

  const float rad = (float)ANGOLI[trend] * 3.14159265f / 180.0f;
  const float dx  = cosf(rad), dy = -sinf(rad);    // y cresce verso il basso
  const int16_t L = 11;

  const int16_t x0 = x - (int16_t)(dx * L), y0 = y - (int16_t)(dy * L);
  const int16_t x1 = x + (int16_t)(dx * L), y1 = y + (int16_t)(dy * L);

  // Asta doppia: su un e-ink una linea da un pixel sparisce a distanza.
  display.drawLine(x0, y0, x1, y1, GxEPD_BLACK);
  display.drawLine(x0, y0 + 1, x1, y1 + 1, GxEPD_BLACK);

  // Punta: triangolo pieno, ruotato come l'asta.
  const float px = -dy, py = dx;                   // versore perpendicolare
  const int16_t bx = x + (int16_t)(dx * (L - 7)),  by = y + (int16_t)(dy * (L - 7));
  display.fillTriangle(x1, y1,
                       bx + (int16_t)(px * 5), by + (int16_t)(py * 5),
                       bx - (int16_t)(px * 5), by - (int16_t)(py * 5),
                       GxEPD_BLACK);
}

// Il riquadro in negativo del nodo muto. E' l'unica diagnostica che questa
// rete ha finche' i nodi non misurano la batteria: va vista prima dei valori,
// non dopo, e su bianco e nero il negativo e' l'unico "colore" disponibile.
static void drawBadgeMuto(int16_t x, int16_t y)
{
  display.fillRoundRect(x, y, 54, 18, 4, GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(x + 8, y + 14);
  display.print("MUTO");
  display.setTextColor(GxEPD_BLACK);
}

// Riga di dettaglio comune ai due formati: umidita' e pressione, allineate a
// destra sotto la temperatura.
static void drawValori(const RemoteNode& n, int16_t yBase)
{
  String riga;
  if (isfinite(n.value[1])) riga += fmtNum(n.value[1], 0) + "%";
  if (isfinite(n.value[2])) {
    if (riga.length()) riga += "   ";
    riga += fmtNum(n.value[2], 1) + " hPa";
  }
  if (!riga.length()) return;
  display.setFont(&FreeSansBold9pt7b);
  drawRight(riga, 388, yBase);
}

// --- blocco COMODO: fino a due nodi ---------------------------------------
static void drawNodoComodo(const RemoteNode& n, int16_t y)
{
  display.setFont(&FreeSansBold12pt7b);
  display.setCursor(12, y + 22);
  display.print(n.nome);

  if (!n.online && n.hasData) {
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(n.nome, 12, y + 22, &bx, &by, &bw, &bh);
    drawBadgeMuto(12 + (int16_t)bw + 12, y + 6);
  }

  if (!n.hasData) {
    display.setFont(&FreeSans9pt7b);
    display.setCursor(12, y + 48);
    display.print("in attesa del primo dato");
    return;
  }

  // Ora dell'ultimo pacchetto, sotto il nome.
  display.setFont(&FreeSans9pt7b);
  display.setCursor(12, y + 46);
  display.print("ultimo alle " + fmtOra(n.ultimoTs));

  // Temperatura in grande: e' il numero per cui la pagina esiste.
  if (isfinite(n.value[0])) {
    display.setFont(&FreeSansBold24pt7b);
    drawRight(fmtNum(n.value[0], 1), 352, y + 52);
    drawGrado(362, y + 26, 4);
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(370, y + 52);
    display.print("C");
  }

  drawValori(n, y + 76);

  // Trend: freccia inclinata piu' la parola, a sinistra.
  if (n.trend != TREND_IGNOTO || n.hasData) {
    drawFrecciaTrend(26, y + 72, n.trend);
    display.setFont(&FreeSans9pt7b);
    display.setCursor(44, y + 78);
    display.print(n.trend == TREND_IGNOTO ? "trend: raccolgo dati"
                                          : remote_trend_label(n.trend));
  }
}

// --- blocco COMPATTO: da tre nodi in su -----------------------------------
static void drawNodoCompatto(const RemoteNode& n, int16_t y)
{
  display.setFont(&FreeSansBold12pt7b);
  display.setCursor(12, y + 20);
  display.print(n.nome);

  if (!n.online && n.hasData) {
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(n.nome, 12, y + 20, &bx, &by, &bw, &bh);
    drawBadgeMuto(12 + (int16_t)bw + 10, y + 4);
  }

  if (!n.hasData) {
    display.setFont(&FreeSans9pt7b);
    display.setCursor(12, y + 42);
    display.print("in attesa del primo dato");
    return;
  }

  if (isfinite(n.value[0])) {
    display.setFont(&FreeSansBold18pt7b);
    drawRight(fmtNum(n.value[0], 1), 356, y + 30);
    drawGrado(365, y + 12, 3);
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(371, y + 30);
    display.print("C");
  }

  display.setFont(&FreeSans9pt7b);
  display.setCursor(12, y + 44);
  display.print(fmtOra(n.ultimoTs));
  drawFrecciaTrend(70, y + 39, n.trend);

  drawValori(n, y + 48);
}

// Solo l'orologio, su finestra parziale piccola: il resto della pagina non
// viene nemmeno toccato. Il contenuto dev'essere identico a quello che disegna
// screenNodi() nella stessa posizione, o al primo refresh grande l'ora
// "salterebbe" di qualche pixel.
static void drawOra()
{
  char buf[8] = "";
  if (!rtctime_format(rtctime_now(), "%H:%M", buf, sizeof(buf))) return;
  display.setFont(&FreeSansBold12pt7b);
  display.setTextColor(GxEPD_BLACK);
  drawRight(String(rtctime_isSynced() ? "" : "~") + buf, display.width() - 12, 23);
}

static void screenOrologio()
{
  display.setPartialWindow(ORA_X, ORA_Y, ORA_W, ORA_H);
  display.firstPage();
  do
  {
    display.fillRect(ORA_X, ORA_Y, ORA_W, ORA_H, GxEPD_WHITE);
    drawOra();
  }
  while (display.nextPage());
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
    display.setCursor(12, 23);
    display.print("STAZIONE METEO");

    // La tilde dentro drawOra() dice che l'ora e' la stima da build-time e non
    // NTP: senza, un orario preciso e sbagliato sembrerebbe vero.
    drawOra();
    display.drawFastHLine(0, 32, W, GxEPD_BLACK);
    display.drawFastHLine(0, 33, W, GxEPD_BLACK);

    // --- corpo ---
    const int n = remote_count();
    if (n == 0)
    {
      display.setFont(&FreeSansBold24pt7b);
      drawCenter("NESSUN NODO", W / 2, 145);
      display.setFont(&FreeSans9pt7b);
      if (remote_pairing_active()) {
        drawCenter("finestra di associazione aperta", W / 2, 180);
        drawCenter("accendi o riavvia il nodo", W / 2, 200);
      } else {
        drawCenter("tieni premuto BOOT", W / 2, 180);
        drawCenter("per associare un nodo", W / 2, 200);
      }
    }
    else
    {
      const int quanti  = (n < NODI_VISIBILI) ? n : NODI_VISIBILI;
      const bool comodo = (quanti <= NODI_COMODI_FINO_A);
      const int16_t h   = (NODI_BOT - NODI_TOP) / quanti;

      for (int i = 0; i < quanti; i++)
      {
        RemoteNode nodo;
        if (!remote_get(i, &nodo)) continue;
        const int16_t y = NODI_TOP + (int16_t)i * h;

        if (comodo) drawNodoComodo(nodo, y);
        else        drawNodoCompatto(nodo, y);

        // Separatore tratteggiato fra un nodo e l'altro: divide senza pesare
        // come una riga piena, che su bianco e nero grida.
        if (i + 1 < quanti) {
          const int16_t ys = y + h - 6;
          for (int16_t x = 12; x < W - 12; x += 6) {
            display.drawFastHLine(x, ys, 3, GxEPD_BLACK);
          }
        }
      }
    }

    // --- piede ---
    display.drawFastHLine(0, NODI_BOT + 2, W, GxEPD_BLACK);
    display.setFont(&FreeSans9pt7b);

    int muti = 0;
    for (int i = 0; i < n; i++) {
      RemoteNode nodo;
      if (remote_get(i, &nodo) && !nodo.online) muti++;
    }
    String piede = String(n) + (n == 1 ? " nodo" : " nodi");
    if (muti > 0)             piede += String(", ") + muti + " muto";
    if (n > NODI_VISIBILI)    piede += String(" (+") + (n - NODI_VISIBILI) + " non mostrati)";
    if (net_isConnected())    piede += "   " + WiFi.localIP().toString();
    else                      piede += "   WiFi assente";
    display.setCursor(12, 288);
    display.print(piede);

    // A destra: cio' che sta succedendo adesso, in ordine di urgenza.
    if (remote_pairing_active()) {
      const uint32_t r = remote_pairing_remaining_s();
      char buf[24];
      snprintf(buf, sizeof(buf), "ASSOCIAZIONE %lu:%02lu",
               (unsigned long)(r / 60), (unsigned long)(r % 60));
      drawRight(buf, W - 12, 288);
    }
    else if (!remote_ready()) {
      drawRight("ESP-NOW NON ATTIVO", W - 12, 288);
    }
    else if (!sd_mounted()) {
      // In negativo: senza card i DATA non li registra nessuno, ed e' il
      // guasto piu' silenzioso che questa scheda possa avere - tutto il resto
      // continua a funzionare come se niente fosse.
      display.fillRect(W - 130, 274, 118, 18, GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);
      drawRight("SD NON MONTATA", W - 16, 288);
      display.setTextColor(GxEPD_BLACK);
    }
    else {
      char buf[24];
      snprintf(buf, sizeof(buf), "SD %lu MB", (unsigned long)sd_free_mb());
      drawRight(buf, W - 12, 288);
    }
  }
  while (display.nextPage());
}

// ---------------------------------------------------------------------------
// Ricostruzione dello storico di pressione dopo un riavvio
// ---------------------------------------------------------------------------
// Copiata da projects/EnvNode_C3/. Il trend a tre ore lo calcola l'hub, ma il
// suo storico vive in RAM: senza questo, ogni riavvio - e ogni OTA, che qui
// saranno frequenti - costerebbe tre ore di "non ancora noto", cioe' lo stesso
// guasto che si sta togliendo al nodo, spostato di una scheda. I dati per
// rifarlo ci sono gia': sono i CSV che questo stesso hub scrive.
static bool s_seedFatto = false;

// Una riga dal File senza allocare String: su un CSV di un giorno intero
// sarebbero migliaia di allocazioni in un colpo solo.
static size_t leggiRiga(File& f, char* buf, size_t cap)
{
  size_t n = 0;
  while (f.available() && n < cap - 1)
  {
    const char c = (char)f.read();
    if (c == '\n') break;
    if (c != '\r') buf[n++] = c;
  }
  buf[n] = '\0';
  return n;
}

// Colonne: ts_iso,ts_unix,fonte_ora,mac,seq,temp_c,hum_pct,press_hpa,batt_mv
static void seedNodoDaCsv(const RemoteNode* n, const char* giorno, time_t minTs, int* righe)
{
  File f = sd_open_remote_day(n->nome, giorno);
  if (!f) return;

  // Solo la coda del file: le tre ore che servono sono al massimo qualche
  // migliaio di byte, mentre un giorno intero a cadenza fitta ne fa oltre
  // centomila. Leggerlo tutto bloccherebbe loop() - e con lui web server, OTA
  // e raccolta dei DATA - per secondi, ad ogni avvio.
  const size_t size = f.size();
  const size_t CODA = 65536;
  if (size > CODA)
  {
    f.seek(size - CODA);
    char scarto[160];
    leggiRiga(f, scarto, sizeof(scarto));   // la prima riga e' tagliata a meta'
  }

  char buf[160];
  while (leggiRiga(f, buf, sizeof(buf)) > 0)
  {
    char* campo[9];
    int nc = 0;
    campo[nc++] = buf;
    for (char* p = buf; *p && nc < 9; p++)
    {
      if (*p == ',') { *p = '\0'; campo[nc++] = p + 1; }
    }
    if (nc < 8) continue;

    // L'intestazione cade da sola: "ts_unix" non e' un numero, strtoul da' 0 e
    // remote_seed_pressure scarta i timestamp non validi.
    const time_t ts = (time_t)strtoul(campo[1], nullptr, 10);
    if (ts < minTs) continue;
    if (campo[7][0] == '\0') continue;   // campo vuoto = valore non finito

    remote_seed_pressure(n->mac, ts, atof(campo[7]));
    (*righe)++;
  }
  f.close();
}

static void seedForecastDaSD()
{
  if (s_seedFatto) return;

  // Serve l'orologio VERO: qui i timestamp non datano una riga, compongono il
  // NOME del file da aprire. Con l'ora di compilazione si leggerebbe il CSV di
  // un giorno sbagliato.
  if (!rtctime_isSynced() || !sd_mounted()) return;
  if (remote_count() == 0) return;

  s_seedFatto = true;

  const time_t ora   = rtctime_now();
  const time_t minTs = ora - (time_t)(3 * 3600 + 900);   // 3 h + tolleranza

  char giornoOggi[12] = "", giornoPrima[12] = "";
  rtctime_format(ora,   "%Y-%m-%d", giornoOggi,  sizeof(giornoOggi));
  rtctime_format(minTs, "%Y-%m-%d", giornoPrima, sizeof(giornoPrima));

  int totale = 0;
  for (int i = 0; i < remote_count(); i++)
  {
    RemoteNode n;
    if (!remote_get(i, &n)) continue;

    int righe = 0;
    // I DATA veri arrivano anche prima che il seeding parta (aspetta NTP, i
    // nodi no) e l'anello rifiuta i campioni fuori ordine: senza azzerarlo, il
    // seeding girerebbe senza errori e senza seminare niente.
    remote_seed_begin(n.mac);

    // In ordine cronologico: ieri PRIMA di oggi, e il file di ieri si apre solo
    // se la finestra di tre ore ci cade davvero dentro.
    if (strcmp(giornoPrima, giornoOggi) != 0) seedNodoDaCsv(&n, giornoPrima, minTs, &righe);
    seedNodoDaCsv(&n, giornoOggi, minTs, &righe);

    if (righe > 0)
      Serial.printf("[trend] %s: storico ricostruito da SD, %d campioni\n", n.nome, righe);
    totale += righe;
  }

  if (totale == 0) Serial.println("[trend] nessuno storico da ricostruire");
  s_nodiDirty = true;
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

// Chiamata da remote_loop() quando un nodo consegna un DATA. Gira nel contesto
// di loop(), non in un callback della radio: dentro remote_nodes il lavoro
// sulla coda ESP-NOW e' gia' stato fatto.
// La finestra di grazia dell'orologio, come su EnvNode_C3 e Timelapse_XIAO.
static const uint32_t ORARIO_GRAZIA_MS = 5UL * 60UL * 1000UL;

static bool orario_registrabile()
{
  return rtctime_isSynced() || millis() >= ORARIO_GRAZIA_MS;
}

static void onDatoNodo(const RemoteNode* n)
{
  if (n == nullptr) return;
  s_nodiDirty = true;

  // Il PRIMO pacchetto di un nodo salta l'''attesa dei due minuti: dopo un
  // riavvio dell'''hub il pannello mostrerebbe "in attesa del primo dato" per
  // tutto quel tempo, pur avendo gia''' i valori in mano. Vale solo qui, dove
  // il ritardo si vedrebbe come uno schermo che non sa niente.
  if (n->pacchetti <= 1) s_nodiUltimoMs = millis() - NODI_MIN_MS;

  // Prima del primo sync NTP l'orologio riporta l'ora di COMPILAZIONE, che e'
  // identica ad ogni riavvio: righe cosi' non datano niente, e il CSV di un
  // nodo remoto e' l'unico posto dove quella lettura esiste. Meglio un buco,
  // che il salto di seq rende comunque visibile, di una riga che si spaccia
  // per un istante sbagliato. Passati cinque minuti si registra lo stesso, con
  // fonte_ora=STIMA: un hub rimasto senza rete che smette di registrare per
  // sempre sarebbe un guasto peggiore di un orario impreciso.
  if (!orario_registrabile()) return;

  char mac[18];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
           n->mac[0], n->mac[1], n->mac[2], n->mac[3], n->mac[4], n->mac[5]);

  if (sd_log_remote(n->nome, mac, n->ultimoTs, rtctime_source(),
                    n->seq, n->value, n->batteria_mv))
  {
    s_righeScritte++;
  }
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
  s_epdRefresh++;
  s_epdUltimoMs   = millis() - t0;
  s_fullUltimoMs  = millis();   // il cambio pagina e' sempre un completo
  s_oraUltimoMs   = millis();
  Serial.printf("[epd] pagina %s: %lu ms\n", PAGE_NAMES[p],
                (unsigned long)s_epdUltimoMs);
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
  // La microSD: da qui in poi il bus SPI ha due padroni. Il CS del pannello lo
  // gestisce GxEPD2, quello della card la libreria SD, e ognuno apre la sua
  // transazione — ma il bus e' uno solo, quindi una scrittura su card e un
  // refresh non possono sovrapporsi. Non e' un problema finche' tutto gira in
  // loop(), che e' il caso: qui non ci sono task propri.
  if (sd_begin())
  {
    Serial.printf("[sd] montata: %lu MB liberi su %lu\n",
                  (unsigned long)sd_free_mb(), (unsigned long)sd_total_mb());
  }
  else
  {
    // Non si ferma niente: il pannello e i nodi funzionano lo stesso, e il
    // guasto si legge sul pannello (riquadro "SD NON MONTATA"). Ma i DATA non
    // vengono registrati da nessuna parte, ed e' il motivo per cui quella
    // scritta e' in negativo invece che in grigetto.
    Serial.printf("[sd] NON montata: %s\n", sd_last_error());
  }

  rtctime_begin(TZ_POSIX);
  rtctime_seedFromBuild();

  // Rete: net_begin() e' bloccante per al massimo 15 s, poi ritenta in
  // background. Il pannello e' gia' stato inizializzato ma non ancora
  // disegnato: la prima pagina esce dopo, cosi' porta gia' IP e ora.
  net_begin();
  web_ui_begin();
  if (net_isConnected()) rtctime_onWifiConnected();

  // DOPO net_begin(): non perche' serva la connessione (Link_InitEx sta su col
  // solo driver WiFi avviato), ma perche' il canale dei peer e' quello dell'AP
  // — e con ESPNOW_LINK_CHANNEL_CURRENT lo si prende da chi ha gia' configurato
  // la radio.
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
                  HUB_NOME, (unsigned)net_channel(), remote_count());
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
  Serial.printf("[hub] pronto: ESP-NOW %s, canale %u, %d nodi, SD %s, http://%s.local/\n",
                remote_ready() ? "attivo" : "NON attivo",
                (unsigned)net_channel(), remote_count(),
                sd_mounted() ? "ok" : "assente", OTA_HOSTNAME);
}

void loop()
{
  // net_loop() PRIMA di tutto e ad ogni giro: e' quella che serve il web server
  // e fa avanzare l'OTA. Se salta un giro, un aggiornamento via rete si pianta
  // a meta' — ed e' l'unico modo di aggiornare questa scheda una volta montata.
  net_loop();

  // Riconnessione WiFi: il sync NTP va rilanciato ad OGNI ritorno della rete,
  // non solo al primo. Senza, una scheda che perde l'AP per un giorno resta con
  // l'orologio alla deriva anche dopo che la rete e' tornata.
  {
    static bool s_eraConnesso = false;
    const bool ora = net_isConnected();
    if (ora && !s_eraConnesso) { rtctime_onWifiConnected(); s_nodiDirty = true; }
    s_eraConnesso = ora;
  }

  // Sempre, su qualunque pagina: i nodi non aspettano che si stia guardando la
  // loro. remote_loop() preleva i DATA dal driver — se non gira, i pacchetti
  // arrivano alla radio e nessuno li raccoglie.
  remote_loop();

  // Aspetta il primo sync NTP e poi gira una volta sola.
  seedForecastDaSD();

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
    // L'orologio per primo: e' il refresh piu' economico e il piu' frequente.
    // Se scatta anche quello della pagina intera, quello ridisegna comunque
    // l'ora e rimette a zero questo timer, quindi non si sovrappongono.
    if (millis() - s_oraUltimoMs >= ORA_MS)
    {
      const uint32_t t0 = millis();
      screenOrologio();
      display.hibernate();
      s_oraUltimoMs  = millis();
      s_epdRefresh++;
      s_epdOrologioMs = millis() - t0;
    }

    const uint32_t da = millis() - s_nodiUltimoMs;
    const bool perDato   = s_nodiDirty && da >= NODI_MIN_MS;
    const bool perTempo  = da >= NODI_MAX_MS;
    if (!perDato && !perTempo) return;

    // Parziale spesso, completo ogni tanto: senza, il ghosting si accumula.
    // Il tempo conta quanto il conteggio — in una giornata senza novita' i
    // parziali sono pochi e il completo non arriverebbe mai, mentre l'orologio
    // continua a riscrivere il suo angolo ogni minuto.
    const bool full = (s_nodiParziali >= NODI_FULL_OGNI) ||
                      (millis() - s_fullUltimoMs >= FULL_OGNI_MS);
    const uint32_t t0 = millis();
    screenNodi(full);
    display.hibernate();

    s_nodiParziali = full ? 0 : s_nodiParziali + 1;
    s_nodiDirty    = false;
    s_nodiUltimoMs = millis();
    s_oraUltimoMs  = millis();     // l'ora e' appena stata ridisegnata con tutto il resto
    if (full) s_fullUltimoMs = millis();
    s_epdRefresh++;
    s_epdUltimoMs  = millis() - t0;
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
