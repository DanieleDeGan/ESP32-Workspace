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
#include <U8g2_for_Adafruit_GFX.h>   // testo UTF-8 (accenti) sul canvas GxEPD2


#include <WiFi.h>          // solo per WiFi.localIP(), da mostrare sul pannello
#include <esp_system.h>    // esp_reset_reason(): perche' la scheda e' ripartita
#include <Preferences.h>   // boot_count: l'unico contatore che sopravvive al riavvio
#include <EspNowLink.h>    // ESPNOW_LINK_CHANNEL_CURRENT
#include "remote_nodes.h"
#include "meteo_calc.h"
#include "icone.h"  // hub ESP-NOW, trapiantato da projects/EnvNode_C3/
#include "forecast.h"      // i nomi TREND_*: remote_nodes.h non lo include

#include "rtc_time.h"      // serve a remote_nodes per datare i DATA
#include "sd_logger.h"     // microSD della Sense: i CSV dei nodi
#include "net_ota.h"       // WiFi + ArduinoOTA + /update + WebServer condiviso
#include "web_ui.h"        // pagina di stato e API, registrate su net_server()
#include "pages.h"        // il modello delle pagine: cosa mostrare e quando
#include "messages.h"     // il messaggio attivo (NVS) e il suo archivio (SD)
#include "secrets.h"       // OTA_HOSTNAME, per dirlo sul pannello

static const char FW_VERSION[] = "v24";

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

// Diagnostica del boot, gemella di quella di MeteoNode_C3 (v5). Tutti gli
// altri contatori di questa scheda vivono in RAM e ripartono da zero: e'
// proprio questo che rende un riavvio invisibile da remoto, perche' da fuori
// si vede solo un hub che "ha registrato poco". Queste due cose dicono invece
// che un riavvio c'e' stato, e perche'.
//
// Serve davvero: il 2026-08-30, leggendo un uptime di 0,7 h, e' stato
// necessario incrociare l'ora corrente con quella dell'ultimo OTA e i CSV dei
// nodi per stabilire che non era successo niente di strano. Con questi due
// campi sarebbe stata una riga.
static esp_reset_reason_t s_resetReason = ESP_RST_UNKNOWN;
static uint32_t           s_bootCount   = 0;

// I due modi in cui un pacchetto ricevuto NON diventa una riga sulla
// card. Servono a rendere verificabile il conto: senza, "pacchetti
// diversi da righe" non distingue una perdita reale da uno scarto
// voluto, e un controllo che si allarma da solo non lo guarda nessuno.
static bool     s_graficoDirty = false;  // vedi onDatoNodo(): primo dato di un nodo
static uint32_t s_scartatiOra = 0;   // arrivati prima del primo sync NTP
static uint32_t s_scrittureKo = 0;   // card piena, assente o in errore
static uint32_t s_epdRefresh   = 0;
static uint32_t s_epdUltimoMs  = 0;
static uint32_t s_epdOrologioMs = 0;   // quanto costa il refresh del solo orologio

const char* app_fw_version()    { return FW_VERSION; }
const char* app_hub_nome()      { return HUB_NOME; }
uint32_t    app_righe_scritte() { return s_righeScritte; }
uint32_t    app_scartati_ora()  { return s_scartatiOra; }
uint32_t    app_boot_count()    { return s_bootCount; }

// La causa in chiaro. Quelle che contano qui: SW e' ESP.restart(), cioe' un OTA
// riuscito oppure il watchdog di riconnessione di net_ota; PANIC e' un crash;
// BROWNOUT e' l'alimentazione scesa sotto soglia. POWERON e' qualcuno che ha
// tolto e rimesso la corrente — che su una scheda a muro vuol dire quasi
// sempre un intervento umano, non un guasto.
const char* app_reset_reason() {
  switch (s_resetReason) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "WDT_INT";
    case ESP_RST_TASK_WDT:  return "WDT_TASK";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

// Il conteggio sta in NVS perche' deve sopravvivere proprio all'evento che
// misura. Una scrittura per avvio: su una scheda sempre accesa sono una
// manciata nella sua vita, ben lontano dai cicli di erase che altrove in
// questo repo si evitano con cura.
static void bootDiagBegin() {
  s_resetReason = esp_reset_reason();

  Preferences p;
  if (p.begin("meteohub", false)) {
    s_bootCount = p.getULong("boot_n", 0) + 1;
    p.putULong("boot_n", s_bootCount);
    p.end();
  }
}
uint32_t    app_scritture_ko()  { return s_scrittureKo; }
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

// Formato del progetto: quello che dither.html produce, quello che si legge
// da /images/<nome>.bin, e -- da v22 -- anche il formato della tela qui sotto
// e dell'anteprima. Un formato solo per tutta la catena.
static const int    IMG_W      = 400;
static const int    IMG_H      = 300;
static const size_t IMG_STRIDE = IMG_W / 8;            // 50 byte per riga
static const size_t IMG_BYTES  = IMG_STRIDE * IMG_H;   // 15.000 byte esatti

// Il pannello sta tutto in RAM: 400 x 300 / 8 = 15.000 byte, quindi
// full-buffer (secondo parametro = HEIGHT) e niente paginazione.
GxEPD2_BW<EPD_DRIVER, EPD_DRIVER::HEIGHT> pannello(EPD_DRIVER(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// --- la tela, e perche' esiste ---------------------------------------------
// Ogni primitiva di disegno va sulla TELA; il PANNELLO riceve solo la tela
// finita, e da lui passano soltanto finestre, paging e refresh. Il giro non e'
// gratuito (15.000 byte di RAM) e non serve a disegnare meglio: serve a
// rendere il pannello OSSERVABILE.
//
// Prima non lo era, e non per distrazione. GxEPD2 tiene il suo framebuffer
// `private` (GxEPD2_BW.h:815): non c'e' getBuffer() e nemmeno una sottoclasse
// puo' arrivarci. E anche potendo non sarebbe bastato, perche' la pagina
// immagine scriveva dritta al controller con writeImage(), saltando quel
// framebuffer del tutto: l'anteprima non avrebbe mai visto proprio le pagine
// che l'utente compone. La tela invece si legge -- e' cio' che restituisce
// GET /api/pannello/anteprima -- e sono gli STESSI byte finiti sul vetro,
// perche' non esiste una seconda strada per arrivarci.
//
// Il formato coincide con quello dei .bin per costruzione: 1 bit per pixel,
// MSB per primo, 50 byte per riga, 1 = bianco. Quindi il browser li disegna
// con l'unpack() che ha gia', e un'immagine si copia nella tela con memcpy.
GFXcanvas1 tela(IMG_W, IMG_H);

// Testo UTF-8 sopra la stessa tela: serve alla pagina messaggio, dove
// "perché" con i font Adafruit_GFX (ASCII puro) diventerebbe "perch?".
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// --- l'unico punto da cui la tela arriva al vetro ---------------------------
// Se ne esistesse un secondo, l'anteprima potrebbe divergere da cio' che si
// vede: uno strumento di verifica che mente e' peggio del non averlo.
// Il rettangolo (x,y,w,h) serve all'orologio, che si riscrive ogni minuto e
// non deve stressare tutto il pannello: la tela resta intera, cambia solo
// quanta ne finisce sul vetro.
//
// Si copia pixel per pixel DENTRO il paging, non con drawImage(): in GxEPD2
// drawImage() e writeImage() scrivono dritte al controller e fanno il refresh
// da se', quindi dentro firstPage()/nextPage() darebbero DUE refresh -- il
// secondo con il buffer di GxEPD2, che li' e' ancora vuoto. Il pannello
// resterebbe bianco e l'anteprima direbbe di no: la forma peggiore di guasto,
// quella in cui lo strumento di verifica mente.
//
// Cosi' invece la meccanica di finestre e refresh resta IDENTICA a prima --
// ed e' la parte tarata a mano (parziali, completi, ghosting) che non conviene
// rimettere in discussione per avere un'anteprima.
//
// COSTO, misurato sull'hardware il 2026-08-31:
//
//   refresh completo    2200 -> 2630 ms   (+430, il prezzo vero)
//   parziale su tutta la pagina  980 -> 1040 ms
//   solo orologio        810 ->  810 ms   (invariato)
//
// L'orologio non paga niente perche' il ciclo gira SOLO sul rettangolo
// chiesto: 4.092 pixel invece di 120.000. Prima di restringerlo costava 862 ms,
// e il fatto che ne costasse solo 52 in piu' invece di 430 dice come funziona
// il clipping -- drawPixel() scarta subito quello che cade fuori dalla finestra
// parziale, quindi a pesare sono i pixel che entrano, non quelli che si
// tentano. Restringere resta giusto: 50 ms al minuto sono 72 secondi al giorno.
//
// I 430 ms del refresh completo si potrebbero togliere scrivendo la tela con
// writeImage() fuori dal paging, che e' il percorso nativo (ed era quello che
// gia' usava la pagina immagine). NON e' stato fatto, e il motivo e' proprio
// l'anteprima: quel cambiamento tocca il tratto tela->vetro, l'unico che
// l'anteprima NON puo' verificare, perche' lei mostra la tela. Andrebbe
// provato da chi il pannello lo sta guardando.
static void telaSulPannello(bool full, int16_t x = 0, int16_t y = 0,
                            int16_t w = IMG_W, int16_t h = IMG_H)
{
  if (full) pannello.setFullWindow();
  else      pannello.setPartialWindow(x, y, w, h);

  const uint8_t* b = tela.getBuffer();
  pannello.firstPage();
  do
  {
    // bit 1 = bianco, la stessa convenzione dei .bin in tutta la catena.
    for (int16_t yy = y; yy < y + h; yy++)
      for (int16_t xx = x; xx < x + w; xx++)
        pannello.drawPixel(xx, yy,
          (b[yy * IMG_STRIDE + (xx >> 3)] >> (7 - (xx & 7))) & 1 ? GxEPD_WHITE : GxEPD_BLACK);
  }
  while (pannello.nextPage());
}

// ---------------------------------------------------------------------------
// Pannello a riposo e tasto BOOT
// ---------------------------------------------------------------------------
static void screenBlank()
{
  // Sempre a finestra intera: pulire in parziale lascerebbe l'alone di quello
  // che c'era prima, ed e' proprio quello che qui non si vuole.
  tela.fillScreen(GxEPD_WHITE);
  telaSulPannello(true);
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

static const int16_t NODI_TOP       = 38;   // sotto l'intestazione
// I 4 px in piu' rispetto a v22 sono per la barra nera del primo nodo, che
// senza respiro si appiccicava alla doppia linea dell'intestazione e le due
// sembravano un unico blocco.
static const int16_t NODI_BOT       = 266;  // sopra il piede

// Con la fascia del messaggio accesa il corpo si ferma piu' in alto e i nodi
// cedono 70 px. Non e' gratis: con due nodi si passa dal blocco comodo (24pt)
// a quello compatto (18pt), cioe' si guadagna il messaggio sempre visibile e
// si perde corpo sui numeri. La scelta e' dell'utente, da /pannello.
static const int16_t FASCIA_H       = 70;
static const int16_t NODI_BOT_FASCIA = NODI_BOT - FASCIA_H;   // 196
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
  tela.getTextBounds(txt, 0, 0, &bx, &by, &bw, &bh);
  tela.setCursor(xRight - (int16_t)bw - bx, yBase);
  tela.print(txt);
}

// Testo centrato su xCentro. Serve una misura vera: allineare a destra con un
// offset stimato a occhio taglia le stringhe larghe sul bordo sinistro, dove
// il cursore finisce a coordinate negative e Adafruit_GFX non se ne lamenta.
static void drawCenter(const String& txt, int16_t xCentro, int16_t yBase)
{
  int16_t bx, by; uint16_t bw, bh;
  tela.getTextBounds(txt, 0, 0, &bx, &by, &bw, &bh);
  int16_t x = xCentro - (int16_t)bw / 2 - bx;
  if (x < 2) x = 2;                      // meglio storto che tagliato
  tela.setCursor(x, yBase);
  tela.print(txt);
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
  tela.drawCircle(x, y, r, GxEPD_BLACK);
  if (r > 2) tela.drawCircle(x, y, r - 1, GxEPD_BLACK);   // piu' spesso, si vede meglio
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
    tela.drawFastHLine(x - 8, y, 6, GxEPD_BLACK);
    tela.drawFastHLine(x + 2, y, 6, GxEPD_BLACK);
    return;
  }

  const float rad = (float)ANGOLI[trend] * 3.14159265f / 180.0f;
  const float dx  = cosf(rad), dy = -sinf(rad);    // y cresce verso il basso
  const int16_t L = 11;

  const int16_t x0 = x - (int16_t)(dx * L), y0 = y - (int16_t)(dy * L);
  const int16_t x1 = x + (int16_t)(dx * L), y1 = y + (int16_t)(dy * L);

  // Asta doppia: su un e-ink una linea da un pixel sparisce a distanza.
  tela.drawLine(x0, y0, x1, y1, GxEPD_BLACK);
  tela.drawLine(x0, y0 + 1, x1, y1 + 1, GxEPD_BLACK);

  // Punta: triangolo pieno, ruotato come l'asta.
  const float px = -dy, py = dx;                   // versore perpendicolare
  const int16_t bx = x + (int16_t)(dx * (L - 7)),  by = y + (int16_t)(dy * (L - 7));
  tela.fillTriangle(x1, y1,
                       bx + (int16_t)(px * 5), by + (int16_t)(py * 5),
                       bx - (int16_t)(px * 5), by - (int16_t)(py * 5),
                       GxEPD_BLACK);
}

// Il riquadro in negativo del nodo muto. E' l'unica diagnostica che questa
// rete ha finche' i nodi non misurano la batteria: va vista prima dei valori,
// non dopo, e su bianco e nero il negativo e' l'unico "colore" disponibile.
static void drawBadgeMuto(int16_t x, int16_t y)
{
  tela.fillRoundRect(x, y, 54, 18, 4, GxEPD_BLACK);
  tela.setFont(&FreeSansBold9pt7b);
  tela.setTextColor(GxEPD_WHITE);
  tela.setCursor(x + 8, y + 14);
  tela.print("MUTO");
  tela.setTextColor(GxEPD_BLACK);
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
  tela.setFont(&FreeSansBold9pt7b);
  drawRight(riga, 388, yBase);
}

// Minimo, massimo e variazione a 3 ore, dalle stesse 48 mezz'ore che disegna
// la pagina grafico. Nessuna memoria in piu': l'anello c'e' gia', e finora
// serviva a una pagina sola.
//
// Il minimo e il massimo sono quello che da' profondita' a un numero che da
// solo non ne ha: 25 gradi adesso vuol dire una cosa se stanotte erano 12 e
// un'altra se erano 24. La variazione a 3 ore e' invece l'unico modo di
// vedere DOVE STA ANDANDO la temperatura senza guardare una curva.
// Valori di uscita per puntatore e non una struct di ritorno: Arduino genera
// da se' i prototipi e li mette in cima al file, PRIMA di qualunque tipo
// dichiarato nello sketch -- una struct qui darebbe "does not name a type" in
// una riga che non esiste nel sorgente. E' anche lo stile del resto del repo
// (remote_get, rtctime_format). Torna quanti campioni ha trovato.
static int statTemp(int index, float* minC, float* maxC, float* delta3h)
{
  *minC = *maxC = *delta3h = NAN;
  static int16_t serie[REMOTE_TEMP_SLOTS];        // static: 96 byte, non stack
  time_t tsUlt = 0;
  const int n = remote_temp_history(index, serie, REMOTE_TEMP_SLOTS, &tsUlt);
  if (n <= 0) return 0;

  int campioni = 0, ultimo = -1;
  for (int i = 0; i < n; i++)
  {
    if (serie[i] == REMOTE_TEMP_VUOTO) continue;
    const float v = serie[i] / 10.0f;
    if (campioni == 0 || v < *minC) *minC = v;
    if (campioni == 0 || v > *maxC) *maxC = v;
    campioni++;
    ultimo = i;
  }

  // Tre ore sono sei slot da mezz'ora. Si guarda QUELLO slot, non "il piu'
  // vecchio disponibile": un delta misurato su una finestra diversa da tre ore
  // sarebbe un numero con l'etichetta sbagliata.
  if (ultimo >= 6 && serie[ultimo] != REMOTE_TEMP_VUOTO &&
      serie[ultimo - 6] != REMOTE_TEMP_VUOTO)
    *delta3h = (serie[ultimo] - serie[ultimo - 6]) / 10.0f;

  return campioni;
}

// Numero col segno sempre davanti: un "+1,2" e un "1,2" si confondono, e qui
// il segno e' l'informazione.
static String fmtDelta(float v, int dec)
{
  if (!isfinite(v)) return String("--");
  String s = fmtNum(v, dec);
  if (v >= 0 && !s.startsWith("+")) s = "+" + s;
  return s;
}

// --- la barra col nome, in negativo ---------------------------------------
// Separa i nodi molto meglio di una linea: su un pannello a 1 bit il nero
// pieno e' cio' che si vede da piu' lontano, e da tre metri e' la prima cosa
// che dice "qui comincia un altro nodo". Ci sta dentro anche l'ora
// dell'ultimo pacchetto, che prima costava una riga sua.
static void drawBarraNodo(const RemoteNode& n, int16_t y, int16_t h)
{
  const int16_t W = tela.width();
  tela.fillRect(0, y, W, h, GxEPD_BLACK);
  tela.setTextColor(GxEPD_WHITE);

  tela.setFont(&FreeSansBold12pt7b);
  tela.setCursor(10, y + h - 6);
  tela.print(n.nome);

  // "MUTO" scritto, non il badge: dentro una barra gia' nera un badge nero
  // non si vedrebbe, e invertirlo qui costerebbe un rettangolo bianco che
  // spezza la barra proprio dove serve continua.
  if (!n.online && n.hasData) {
    int16_t bx, by; uint16_t bw, bh;
    tela.getTextBounds(n.nome, 10, y + h - 6, &bx, &by, &bw, &bh);
    tela.setFont(&FreeSansBold9pt7b);
    tela.setCursor(10 + (int16_t)bw + 10, y + h - 7);
    tela.print("MUTO");
  }

  if (n.hasData && n.ultimoTs != 0) {
    tela.setFont(&FreeSansBold9pt7b);
    const String ora = fmtOra(n.ultimoTs);
    int16_t bx, by; uint16_t bw, bh;
    tela.getTextBounds(ora, 0, 0, &bx, &by, &bw, &bh);
    tela.drawBitmap(W - 12 - (int16_t)bw - IC_OROLOGIO_W - 5,
                    y + (h - IC_OROLOGIO_H) / 2,
                    IC_OROLOGIO, IC_OROLOGIO_W, IC_OROLOGIO_H, GxEPD_WHITE);
    drawRight(ora, W - 10, y + h - 7);
  }

  tela.setTextColor(GxEPD_BLACK);
}

// Mette in fila i pezzi che ci stanno e si ferma al primo che non entra.
//
// Serve perche' il numero di voci cambia da solo: l'humidex sparisce sotto i
// 20 gradi, il delta a 3 ore manca finche' lo storico non e' pieno, e un nodo
// senza pressione non ne ha affatto. Una riga scritta a lunghezza fissa era
// destinata a sovrapporsi in qualche combinazione -- ed e' successo davvero,
// "3h +1,percepiti 32" nella prima versione di questa pagina.
//
// L'ordine dell'array E' la priorita': quello che si perde e' l'ultimo, cioe'
// il meno importante. Mai il contrario.
static void drawFila(const String* voci, int quante, int16_t x, int16_t y,
                     int16_t xMax, int16_t gap)
{
  for (int i = 0; i < quante; i++)
  {
    if (!voci[i].length()) continue;
    int16_t bx, by; uint16_t bw, bh;
    tela.getTextBounds(voci[i], 0, 0, &bx, &by, &bw, &bh);
    if (x + (int16_t)bw > xMax) return;
    tela.setCursor(x, y);
    tela.print(voci[i]);
    x += (int16_t)bw + gap;
  }
}

// --- blocco COMODO: fino a due nodi ---------------------------------------
static void drawNodoComodo(const RemoteNode& n, int16_t y, int indice)
{
  const int16_t W = tela.width();
  drawBarraNodo(n, y, 22);

  if (!n.hasData) {
    tela.setFont(&FreeSans9pt7b);
    tela.setCursor(14, y + 48);
    tela.print("in attesa del primo dato");
    return;
  }

  // --- riga grande: le tre grandezze misurate, ognuna con la sua icona -----
  // Icone SOLO qui. Sono tre simboli che si riconoscono a colpo d'occhio da
  // tre metri; metterne altri per i valori derivati vorrebbe dire inventare
  // simboli che nessuno conosce, e a 14 px in bianco e nero si somigliano
  // tutti. Per la pressione non c'e' icona apposta: "hPa" e' gia' la sua
  // etichetta, e un simbolo ambiguo e' peggio di nessun simbolo.
  if (isfinite(n.value[0])) {
    // Allineata al centro del numero, non alla sua base: un'icona appoggiata
    // sulla riga di scrittura sembra caduta.
    tela.drawBitmap(10, y + 34, IC_TERMOMETRO, IC_TERMOMETRO_W, IC_TERMOMETRO_H,
                    GxEPD_BLACK);
    tela.setFont(&FreeSansBold24pt7b);
    tela.setCursor(36, y + 62);
    tela.print(fmtNum(n.value[0], 1));
    int16_t bx, by; uint16_t bw, bh;
    tela.getTextBounds(fmtNum(n.value[0], 1), 36, y + 62, &bx, &by, &bw, &bh);
    const int16_t xu = 36 + (int16_t)bw + 7;
    drawGrado(xu + 3, y + 38, 4);
    tela.setFont(&FreeSansBold12pt7b);
    tela.setCursor(xu + 10, y + 62);
    tela.print("C");
  }

  tela.setFont(&FreeSansBold12pt7b);
  if (isfinite(n.value[1])) {
    tela.drawBitmap(184, y + 36, IC_GOCCIA, IC_GOCCIA_W, IC_GOCCIA_H, GxEPD_BLACK);
    tela.setCursor(210, y + 53);
    tela.print(fmtNum(n.value[1], 0) + "%");
  }
  if (isfinite(n.value[2])) {
    tela.setFont(&FreeSansBold12pt7b);
    drawRight(fmtNum(n.value[2], 1), W - 46, y + 53);
    tela.setFont(&FreeSans9pt7b);
    tela.setCursor(W - 42, y + 53);
    tela.print("hPa");
  }

  // --- riga del trend barometrico: l'unica freccia della pagina ------------
  drawFrecciaTrend(26, y + 78, n.trend);
  tela.setFont(&FreeSans9pt7b);
  tela.setCursor(44, y + 84);
  tela.print(n.trend == TREND_IGNOTO ? "raccolgo dati" : remote_trend_label(n.trend));

  const float td = meteo_dewpoint_c(n.value[0], n.value[1]);
  if (isfinite(td)) {
    // Il grado disegnato, come sulla temperatura: scriverlo li' e non qui
    // faceva sembrare la rugiada un numero di un'altra specie.
    tela.setFont(&FreeSansBold9pt7b);
    drawRight("rugiada " + fmtNum(td, 1), W - 22, y + 84);
    drawGrado(W - 18, y + 78, 2);
    tela.setFont(&FreeSans9pt7b);
    tela.setCursor(W - 13, y + 84);
    tela.print("C");
  }

  // --- riga di riepilogo: dove e' stata e dove sta andando -----------------
  float tMin, tMax, tD3;
  const int nCamp = statTemp(indice, &tMin, &tMax, &tD3);
  const float hx  = meteo_humidex_c(n.value[0], n.value[1]);
  const float ah  = meteo_umidita_assoluta_gm3(n.value[0], n.value[1]);

  int16_t x = 14;
  tela.setFont(&FreeSans9pt7b);
  if (nCamp > 0) {
    tela.drawBitmap(12, y + 92, IC_MINMAX, IC_MINMAX_W, IC_MINMAX_H, GxEPD_BLACK);
    x = 30;
  }

  String voci[4];
  int nv = 0;
  if (nCamp > 0)      voci[nv++] = fmtNum(tMin, 1) + " / " + fmtNum(tMax, 1);
  if (isfinite(tD3))  voci[nv++] = "3h " + fmtDelta(tD3, 1);
  if (isfinite(ah))   voci[nv++] = fmtNum(ah, 1) + " g/m3";
  // L'humidex e' l'ultimo perche' e' il primo che si puo' perdere: e' anche
  // l'unico che sotto i 20 gradi non esiste (NAN) e sopra dice qualcosa solo
  // se supera davvero la temperatura.
  if (isfinite(hx) && hx >= n.value[0] + 2.0f)
    voci[nv++] = "percep. " + fmtNum(hx, 0);
  drawFila(voci, nv, x, y + 104, W - 12, 14);
}

// --- blocco COMPATTO: da tre nodi in su -----------------------------------
static void drawNodoCompatto(const RemoteNode& n, int16_t y)
{
  const int16_t W = tela.width();
  drawBarraNodo(n, y, 19);

  if (!n.hasData) {
    tela.setFont(&FreeSans9pt7b);
    tela.setCursor(14, y + 38);
    tela.print("in attesa del primo dato");
    return;
  }

  if (isfinite(n.value[0])) {
    tela.setFont(&FreeSansBold18pt7b);
    tela.setCursor(14, y + 45);
    tela.print(fmtNum(n.value[0], 1));
    int16_t bx, by; uint16_t bw, bh;
    tela.getTextBounds(fmtNum(n.value[0], 1), 14, y + 45, &bx, &by, &bw, &bh);
    const int16_t xu = 14 + (int16_t)bw + 6;
    drawGrado(xu + 2, y + 27, 3);
    tela.setFont(&FreeSansBold9pt7b);
    tela.setCursor(xu + 8, y + 45);
    tela.print("C");
  }

  drawFrecciaTrend(150, y + 39, n.trend);
  if (isfinite(n.value[1]))
    tela.drawBitmap(190, y + 22, IC_GOCCIA, IC_GOCCIA_W, IC_GOCCIA_H, GxEPD_BLACK);
  drawValori(n, y + 34);

  // Con tre o quattro nodi resta una riga sola per nodo: ci va la rugiada, che
  // fra tutti i derivati e' quello che si legge da solo. Il resto sta nella
  // pagina web -- il pannello sceglie, non riassume.
  const float td = meteo_dewpoint_c(n.value[0], n.value[1]);
  if (isfinite(td)) {
    tela.setFont(&FreeSans9pt7b);
    drawRight("rugiada " + fmtNum(td, 1), W - 12, y + 50);
  }
}

// Solo l'orologio, su finestra parziale piccola: il resto della pagina non
// viene nemmeno toccato. Il contenuto dev'essere identico a quello che disegna
// screenNodi() nella stessa posizione, o al primo refresh grande l'ora
// "salterebbe" di qualche pixel.
static void drawOra()
{
  char buf[8] = "";
  if (!rtctime_format(rtctime_now(), "%H:%M", buf, sizeof(buf))) return;
  tela.setFont(&FreeSansBold12pt7b);
  tela.setTextColor(GxEPD_BLACK);
  drawRight(String(rtctime_isSynced() ? "" : "~") + buf, tela.width() - 12, 23);
}

static void screenOrologio()
{
  // Solo il suo rettangolo, sulla tela e sul vetro: il resto della pagina e'
  // gia' li' e non va ridisegnato.
  tela.fillRect(ORA_X, ORA_Y, ORA_W, ORA_H, GxEPD_WHITE);
  drawOra();
  telaSulPannello(false, ORA_X, ORA_Y, ORA_W, ORA_H);
}

static void screenNodi(bool full)
{
  const int16_t W = tela.width();
  const int16_t H = tela.height();

  {
    tela.fillScreen(GxEPD_WHITE);
    tela.setTextColor(GxEPD_BLACK);

    // --- intestazione ---
    tela.setFont(&FreeSansBold12pt7b);
    tela.setCursor(12, 23);
    tela.print("STAZIONE METEO");

    // La tilde dentro drawOra() dice che l'ora e' la stima da build-time e non
    // NTP: senza, un orario preciso e sbagliato sembrerebbe vero.
    drawOra();
    tela.drawFastHLine(0, 32, W, GxEPD_BLACK);
    tela.drawFastHLine(0, 33, W, GxEPD_BLACK);

    // --- la fascia si prende il suo spazio PRIMA che i nodi si dividano il
    //     resto: e' l'unica cosa che cambia il layout, e deve deciderla una
    //     riga sola, non ogni blocco per conto suo.
    const Message* msgFascia = pages_fascia() ? msg_active(time(nullptr)) : nullptr;
    const int16_t  yBot      = msgFascia ? NODI_BOT_FASCIA : NODI_BOT;

    // --- corpo ---
    const int n = remote_count();
    if (n == 0)
    {
      tela.setFont(&FreeSansBold24pt7b);
      drawCenter("NESSUN NODO", W / 2, 145);
      tela.setFont(&FreeSans9pt7b);
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
      // Il blocco comodo vuole ~110 px: con la fascia non ce ne sono, e
      // disegnarlo lo stesso vorrebbe dire numeri che si sovrappongono al
      // separatore. Meglio compatto e leggibile che comodo e sporco.
      const bool comodo = (quanti <= NODI_COMODI_FINO_A) && (msgFascia == nullptr);
      const int16_t h   = (yBot - NODI_TOP) / quanti;

      for (int i = 0; i < quanti; i++)
      {
        RemoteNode nodo;
        if (!remote_get(i, &nodo)) continue;
        const int16_t y = NODI_TOP + (int16_t)i * h;

        if (comodo) drawNodoComodo(nodo, y, i);
        else        drawNodoCompatto(nodo, y);

        // Niente separatore fra un nodo e l'altro: da v23 ogni blocco si apre
        // con la sua barra nera, che separa molto piu' di un tratteggio. Due
        // separatori a poche righe di distanza facevano solo sporco.
      }
    }

    // --- fascia del messaggio ---
    if (msgFascia != nullptr)
    {
      tela.drawFastHLine(0, yBot + 2, W, GxEPD_BLACK);

      // U8g2 e non i font Adafruit: qui il testo lo scrive una persona, e in
      // italiano "perche'" con i font GFX diventerebbe "perch?".
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

      // Due corpi soli, non la scala completa della pagina messaggio: qui lo
      // spazio e' fisso e la domanda e' solo "ci sta su una riga o due".
      const int16_t larg = W - 24;
      String r1 = msgFascia->testo, r2 = "";

      u8g2Fonts.setFont(u8g2_font_helvB14_tf);
      if (u8g2Fonts.getUTF8Width(r1.c_str()) > larg)
      {
        u8g2Fonts.setFont(u8g2_font_helvB12_tf);
        // A capo sull'ultimo spazio che ci sta: spezzare in mezzo a una
        // parola su due righe sole si nota subito.
        int taglio = -1;
        for (int i = 0; i < (int)r1.length(); i++)
        {
          if (r1[i] != ' ') continue;
          if (u8g2Fonts.getUTF8Width(r1.substring(0, i).c_str()) <= larg) taglio = i;
          else break;
        }
        if (taglio > 0) { r2 = r1.substring(taglio + 1); r1 = r1.substring(0, taglio); }
      }

      const int16_t yTesto = yBot + (r2.length() ? 26 : 34);
      int16_t w = u8g2Fonts.getUTF8Width(r1.c_str());
      u8g2Fonts.setCursor((W - w) / 2, yTesto);
      u8g2Fonts.print(r1);

      if (r2.length())
      {
        // Se anche la seconda riga sborda si taglia con l'ellissi: meglio un
        // messaggio troncato che due righe che si mangiano il piede.
        while (r2.length() > 1 && u8g2Fonts.getUTF8Width((r2 + "...").c_str()) > larg)
          r2.remove(r2.length() - 1);
        if (r2 != msgFascia->testo) r2 += "...";
        w = u8g2Fonts.getUTF8Width(r2.c_str());
        u8g2Fonts.setCursor((W - w) / 2, yTesto + 22);
        u8g2Fonts.print(r2);
      }

      // L'urgenza si vede senza leggere: cornice piena attorno alla fascia.
      if (msgFascia->priorita == MSG_URGENTE)
      {
        tela.drawRect(4, yBot + 6, W - 8, FASCIA_H - 10, GxEPD_BLACK);
        tela.drawRect(5, yBot + 7, W - 10, FASCIA_H - 12, GxEPD_BLACK);
      }
    }

    // --- piede ---
    tela.drawFastHLine(0, NODI_BOT + 2, W, GxEPD_BLACK);
    tela.setFont(&FreeSans9pt7b);

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
    tela.setCursor(12, 288);
    tela.print(piede);

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
      tela.fillRect(W - 130, 274, 118, 18, GxEPD_BLACK);
      tela.setTextColor(GxEPD_WHITE);
      drawRight("SD NON MONTATA", W - 16, 288);
      tela.setTextColor(GxEPD_BLACK);
    }
    else {
      char buf[24];
      snprintf(buf, sizeof(buf), "SD %lu MB", (unsigned long)sd_free_mb());
      drawRight(buf, W - 12, 288);
    }
  }
  telaSulPannello(full);
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

  // Solo la coda del file. Da quando c'e' anche il grafico a 24 h la finestra
  // e' un giorno intero, non piu' tre ore: 128 kB coprono ~26 h del nodo piu'
  // veloce (60 s, ~80 byte a riga). Oltre non si va — leggere due file interi
  // per nodo bloccherebbe loop(), e con lui web server, OTA e raccolta dei
  // DATA. Se la coda non arriva abbastanza indietro il grafico parte con
  // qualche slot vuoto, che e' esattamente cio' che gli slot vuoti dicono.
  const size_t size = f.size();
  const size_t CODA = 131072;
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

    // Temperatura (campo 5) e pressione (campo 7) vanno in due storici
    // diversi: 24 h a mezz'ora per il disegno, 3 h a dieci minuti per il
    // trend. Un campo vuoto nel CSV e' una lettura che il nodo non e' riuscito
    // a fare, e resta un buco anche qui.
    if (campo[5][0] != '\0') remote_seed_temp(n->mac, ts, atof(campo[5]));
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
  // 24 h piu' tolleranza: e' la finestra del grafico. Il trend ne usa solo le
  // ultime tre, e i campioni piu' vecchi che gli arrivano scorrono via dal suo
  // anello da soli — costano qualche push, non un errore.
  const time_t minTs = ora - (time_t)(24 * 3600 + 900);

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

// Richieste che arrivano dalla web UI: si ACCODANO e le esegue il loop().
// Un refresh del pannello sono 2,2 s, e dentro un handler HTTP vorrebbe dire
// tenere fermo il server (e con lui l'OTA e il prelievo dei DATA dei nodi):
// e' la stessa regola dei callback ESP-NOW — la richiesta accoda, il loop
// lavora.
static volatile bool    s_refreshChiesto = false;
static volatile int16_t s_paginaChiesta  = -1;

// L'anteprima: gli stessi byte che sono finiti sul vetro, nello stesso
// formato dei .bin. Non e' una copia dello stato del pannello -- e' lo stato
// del pannello, perche' non c'e' nessun'altra strada per arrivarci.
const uint8_t* app_tela()        { return tela.getBuffer(); }
size_t         app_tela_bytes()  { return IMG_BYTES; }

void app_chiedi_refresh()          { s_refreshChiesto = true; }
void app_chiedi_pagina(uint8_t i)  { s_paginaChiesta  = (int16_t)i; }

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
  if (n->pacchetti <= 1) {
    s_nodiUltimoMs = millis() - NODI_MIN_MS;
    // E lo stesso per il grafico, che altrimenti resterebbe fino a mezz'ora
    // con la legenda muta: e' stato disegnato al boot, quando nessun nodo
    // aveva ancora parlato, e la sua cadenza di ridisegno e' lo slot da 30
    // minuti. Visto sul pannello il 2026-08-30: valori assenti in legenda
    // mentre /api/nodi li aveva gia' da un quarto d'ora.
    s_graficoDirty = true;
  }

  // Prima del primo sync NTP l'orologio riporta l'ora di COMPILAZIONE, che e'
  // identica ad ogni riavvio: righe cosi' non datano niente, e il CSV di un
  // nodo remoto e' l'unico posto dove quella lettura esiste. Meglio un buco,
  // che il salto di seq rende comunque visibile, di una riga che si spaccia
  // per un istante sbagliato. Passati cinque minuti si registra lo stesso, con
  // fonte_ora=STIMA: un hub rimasto senza rete che smette di registrare per
  // sempre sarebbe un guasto peggiore di un orario impreciso.
  if (!orario_registrabile()) { s_scartatiOra++; return; }

  char mac[18];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
           n->mac[0], n->mac[1], n->mac[2], n->mac[3], n->mac[4], n->mac[5]);

  if (sd_log_remote(n->nome, mac, n->ultimoTs, rtctime_source(),
                    n->seq, n->value, n->batteria_mv))
  {
    s_righeScritte++;
  }
  else
  {
    // Da quando sd_log_remote() guarda il ritorno delle print(), questo ramo
    // distingue "card che ha rifiutato" da "riga scritta": prima la funzione
    // rispondeva sempre true e il contatore saliva comunque.
    s_scrittureKo++;
  }
  Serial.printf("[hub] %s  %.1f C  %.0f %%  %.1f hPa  seq %lu%s\n",
                n->nome, n->value[0], n->value[1], n->value[2],
                (unsigned long)n->seq, n->online ? "" : "  (era muto)");
}

// ---------------------------------------------------------------------------
// Pagina MESSAGGIO — il bigliettino sul frigo
// ---------------------------------------------------------------------------
// Il testo e' UTF-8 e i font Adafruit_GFX sono ASCII puro: "perché" uscirebbe
// "perch?". Da qui U8g2_for_Adafruit_GFX, che disegna UTF-8 sullo stesso
// canvas di GxEPD2 senza portarsi dietro un secondo driver di pannello.
//
// Il corpo si sceglie da solo: si prova dal piu' grande al piu' piccolo e si
// tiene il primo che entra nell'area, a capo compresi. Un messaggio corto
// deve leggersi da lontano, uno lungo deve starci — sono due esigenze diverse
// e non esiste un corpo che le soddisfi entrambe.
static const uint8_t* const MSG_FONTS[] = {
  u8g2_font_helvB24_tf, u8g2_font_helvB18_tf,
  u8g2_font_helvB14_tf, u8g2_font_helvB12_tf, u8g2_font_helvB10_tf
};
static const int MSG_FONTS_N = sizeof(MSG_FONTS) / sizeof(MSG_FONTS[0]);

// Area utile del testo: sotto l'intestazione, sopra il piede.
static const int16_t MSG_X0 = 12, MSG_X1 = 388;
static const int16_t MSG_Y0 = 56, MSG_Y1 = 268;

// Spezza `testo` in righe che stanno in `larghezza` con il font gia'
// selezionato. Ritorna quante righe servono; se `out` non e' nullptr ci
// scrive le prime `maxRighe`. Va a capo sugli spazi; una parola piu' larga
// dell'area viene lasciata intera e sbordera' — meglio una riga lunga che un
// taglio a meta' parola in mezzo a un avviso.
static int msgWrap(const char* testo, int16_t larghezza, String* out, int maxRighe)
{
  int n = 0;
  String riga = "";
  const char* p = testo;

  while (*p)
  {
    // prossima parola (con lo spazio che la precede, se c'e')
    String parola = "";
    while (*p == ' ') { p++; if (riga.length()) parola += ' '; }
    while (*p && *p != ' ') parola += *p++;
    if (parola.length() == 0) continue;

    const String prova = riga + parola;
    if (riga.length() && u8g2Fonts.getUTF8Width(prova.c_str()) > larghezza)
    {
      if (out && n < maxRighe) out[n] = riga;
      n++;
      riga = parola;
      riga.trim();
    }
    else
    {
      riga = prova;
    }
  }
  if (riga.length()) { if (out && n < maxRighe) out[n] = riga; n++; }
  return n;
}

#define MSG_RIGHE_MAX 12

static void screenMessaggio(const Message* m)
{
  {
    tela.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

    // Intestazione: cosa e' questa pagina, e quando e' stato scritto. L'ora
    // dello scritto, non "quanto tempo fa": un istante non invecchia, e su un
    // pannello che si ridisegna ogni tanto un "12 min fa" e' sbagliato quasi
    // sempre.
    u8g2Fonts.setFont(u8g2_font_helvB10_tf);
    u8g2Fonts.setCursor(MSG_X0, 26);
    u8g2Fonts.print("MESSAGGIO");
    tela.drawFastHLine(MSG_X0, 36, MSG_X1 - MSG_X0, GxEPD_BLACK);

    if (m == nullptr)
    {
      u8g2Fonts.setFont(u8g2_font_helvR12_tf);
      const char* vuoto = "nessun messaggio";
      const int16_t w = u8g2Fonts.getUTF8Width(vuoto);
      u8g2Fonts.setCursor((400 - w) / 2, 160);
      u8g2Fonts.print(vuoto);
      // Qui la pagina e' finita. Era un `continue` del ciclo di paging: ora
      // che il paging non c'e' piu', si spinge e si esce - che e' anche piu'
      // chiaro di quanto fosse prima.
      telaSulPannello(true);
      return;
    }

    if (m->creato != 0)
    {
      const String q = fmtOra(m->creato);
      u8g2Fonts.setFont(u8g2_font_helvR10_tf);
      const int16_t w = u8g2Fonts.getUTF8Width(q.c_str());
      u8g2Fonts.setCursor(MSG_X1 - w, 26);
      u8g2Fonts.print(q);
    }

    // Il corpo piu' grande che ci sta, righe comprese.
    String righe[MSG_RIGHE_MAX];
    int    nRighe = 0;
    int    scelto = MSG_FONTS_N - 1;
    int16_t passo = 0;

    for (int f = 0; f < MSG_FONTS_N; f++)
    {
      u8g2Fonts.setFont(MSG_FONTS[f]);
      const int16_t h = u8g2Fonts.getFontAscent() - u8g2Fonts.getFontDescent();
      const int16_t interlinea = (int16_t)(h * 1.25f);
      const int n = msgWrap(m->testo, MSG_X1 - MSG_X0, nullptr, 0);
      if (n * interlinea <= (MSG_Y1 - MSG_Y0) || f == MSG_FONTS_N - 1)
      {
        scelto = f;
        passo  = interlinea;
        break;
      }
    }

    u8g2Fonts.setFont(MSG_FONTS[scelto]);
    nRighe = msgWrap(m->testo, MSG_X1 - MSG_X0, righe, MSG_RIGHE_MAX);
    if (nRighe > MSG_RIGHE_MAX) nRighe = MSG_RIGHE_MAX;

    // Blocco centrato verticalmente nell'area: un messaggio di una riga in
    // cima a una pagina vuota sembra un errore di disegno.
    const int16_t alt = nRighe * passo;
    int16_t y = MSG_Y0 + (MSG_Y1 - MSG_Y0 - alt) / 2 + u8g2Fonts.getFontAscent();
    for (int i = 0; i < nRighe; i++)
    {
      const int16_t w = u8g2Fonts.getUTF8Width(righe[i].c_str());
      u8g2Fonts.setCursor((400 - w) / 2, y);
      u8g2Fonts.print(righe[i]);
      y += passo;
    }

    // Piede: urgenza e scadenza, le due cose che cambiano come va letto.
    u8g2Fonts.setFont(u8g2_font_helvR10_tf);
    tela.drawFastHLine(MSG_X0, 278, MSG_X1 - MSG_X0, GxEPD_BLACK);
    u8g2Fonts.setCursor(MSG_X0, 294);
    if (m->priorita == MSG_URGENTE) u8g2Fonts.print("URGENTE");
    if (m->scadenza != 0)
    {
      const String s = "fino alle " + fmtOra(m->scadenza);
      const int16_t w = u8g2Fonts.getUTF8Width(s.c_str());
      u8g2Fonts.setCursor(MSG_X1 - w, 294);
      u8g2Fonts.print(s);
    }
  }
  telaSulPannello(true);
}

// ---------------------------------------------------------------------------
// Pagina IMMAGINE — 15.000 byte dalla card, spinti nel controller come sono
// ---------------------------------------------------------------------------
// E' il percorso gia' provato dal bring-up, con l'unica differenza che conta:
// i byte non stanno piu' in flash ma in /images/<nome>.bin. Nessuna
// conversione a bordo — niente decoder JPEG/PNG, nessun dithering: quello lo
// ha fatto il browser (www/dither.html), ed e' tutto il motivo per cui la
// catena e' fatta cosi'.
//
// Il malloc da 15 kB non serve piu' (c'era per non tenere un buffer fisso che
// una pagina immagine poteva non usare mai): la tela e' gia' esattamente
// quello, stesso formato e stessa dimensione, quindi il file si legge dritto
// dentro di lei. Un giro di copia in meno E un'allocazione in meno.
//
// Prima si andava anche piu' corti: writeImage() spingeva i byte dritti al
// controller senza passare da nessun buffer. Quella scorciatoia e' proprio
// cio' che rendeva le pagine immagine invisibili a qualunque anteprima.
static void screenImmagine(const char* nome)
{
  File f = (nome && *nome) ? sd_img_open(nome) : File();
  size_t letti = 0;

  if (f) letti = f.read(tela.getBuffer(), IMG_BYTES);
  if (f) f.close();

  if (letti == IMG_BYTES)
  {
    telaSulPannello(true);
    return;
  }
  // Lettura corta o file assente: la tela ora contiene mezza immagine, e la
  // schermata qui sotto riparte comunque da un fillScreen().

  // Il perche' si scrive sul pannello, non solo sulla seriale: il log di boot
  // di questa scheda non e' leggibile via USB, e una pagina che resta com'era
  // somiglia a un display rotto invece che a un file mancante.
  {
    tela.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_helvB14_tf);
    const char* t1 = sd_mounted() ? "immagine non disponibile" : "microSD non montata";
    int16_t w = u8g2Fonts.getUTF8Width(t1);
    u8g2Fonts.setCursor((400 - w) / 2, 140);
    u8g2Fonts.print(t1);

    u8g2Fonts.setFont(u8g2_font_helvR10_tf);
    String t2 = String("/images/") + (nome && *nome ? nome : "?") + ".bin";
    if (letti && letti != IMG_BYTES) t2 += "  (" + String((unsigned)letti) + " byte, ne servono 15000)";
    w = u8g2Fonts.getUTF8Width(t2.c_str());
    u8g2Fonts.setCursor((400 - w) / 2, 170);
    u8g2Fonts.print(t2);
  }
  telaSulPannello(true);
}

// ---------------------------------------------------------------------------
// Pagina GRAFICO — la temperatura dei nodi nelle ultime 24 ore
// ---------------------------------------------------------------------------
// Perche' una pagina intera e non un grafichino dentro la pagina dei nodi:
// vale la stessa regola gia' scritta per la fascia del messaggio — su e-ink il
// tempo e' la dimensione in piu', e per vedere tutto c'e' la rotazione, che
// alterna pagine intere e leggibili invece di comprimerne tre in 400x300. Una
// sparkline da 180x18 accanto ai numeri sarebbe stata un ornamento; qui la
// curva si legge da lontano quanto i numeri della pagina nodi.
//
// I dati arrivano da remote_temp_history(): 48 mezz'ore in decimi di grado,
// gia' in ordine cronologico, con REMOTE_TEMP_VUOTO nei buchi. Lo storico si
// ricostruisce dai CSV al primo sync NTP, quindi la pagina e' piena subito
// dopo un riavvio invece di impiegare un giorno a riempirsi.
static const int16_t GR_X0 = 44,  GR_X1 = 388;    // area di disegno
static const int16_t GR_Y0 = 52,  GR_Y1 = 236;
static const int     GR_MAX_NODI = 3;             // oltre, il bianco e nero non basta piu'

// Un pixel ogni due slot non basterebbe: con 48 campioni su 344 px ogni
// mezz'ora ha 7 px, che e' anche la distanza minima perche' due curve vicine
// restino distinguibili.
static int16_t grX(int i, int n)
{
  if (n <= 1) return GR_X0;
  return GR_X0 + (int16_t)(((int32_t)(GR_X1 - GR_X0) * i) / (n - 1));
}

// Le curve si distinguono per TRATTO, non per colore: piena, tratteggiata,
// punteggiata. Su un pannello a 1 bit e' l'unica differenza che sopravvive.
static bool grVisibile(int serie, int i)
{
  switch (serie) {
    case 0:  return true;              // piena
    case 1:  return (i % 6) < 4;       // tratteggiata
    default: return (i % 4) < 2;       // punteggiata
  }
}

static void screenGrafico()
{
  // Si raccoglie prima, si disegna dopo: firstPage()/nextPage() ripete il
  // corpo del ciclo, e rileggere lo storico ad ogni passata sarebbe lavoro
  // buttato.
  static int16_t serie[GR_MAX_NODI][REMOTE_TEMP_SLOTS];
  static char    nomi[GR_MAX_NODI][18];
  static int16_t ultimo[GR_MAX_NODI];
  int nSerie = 0, nCampioni = 0;
  time_t tsUltimo = 0;
  int16_t vMin = 32767, vMax = -32768;

  const int nodi = remote_count();
  for (int i = 0; i < nodi && nSerie < GR_MAX_NODI; i++) {
    RemoteNode n;
    if (!remote_get(i, &n)) continue;

    time_t ts = 0;
    const int k = remote_temp_history(i, serie[nSerie], REMOTE_TEMP_SLOTS, &ts);
    if (k <= 0) continue;

    bool almenoUno = false;
    for (int j = 0; j < k; j++) {
      const int16_t v = serie[nSerie][j];
      if (v == REMOTE_TEMP_VUOTO) continue;
      almenoUno = true;
      if (v < vMin) vMin = v;
      if (v > vMax) vMax = v;
    }
    if (!almenoUno) continue;

    strlcpy(nomi[nSerie], n.nome, sizeof(nomi[0]));
    // hasData PRIMA di isfinite, e non e' pedanteria: i valori non si
    // persistono, quindi dopo un riavvio value[0] vale ZERO finche' non
    // arriva il primo DATA — e isfinite(0.0) e' vero. La legenda scriveva
    // "0,0 C", che d'inverno e' una lettura perfettamente plausibile: mente
    // in modo credibile, ed e' peggio di una casella vuota. Stesso schema del
    // NAN emesso come "nan" nel JSON.
    ultimo[nSerie] = (n.hasData && isfinite(n.value[0]))
                       ? (int16_t)lroundf(n.value[0] * 10.0f)
                       : REMOTE_TEMP_VUOTO;
    if (k > nCampioni) nCampioni = k;
    if (ts > tsUltimo) tsUltimo  = ts;
    nSerie++;
  }

  // Scala verticale: almeno due gradi di respiro, cosi' una giornata piatta
  // non diventa una linea che ondeggia di dieci pixel per due decimi.
  if (nSerie > 0) {
    if (vMax - vMin < 20) {
      const int16_t centro = (int16_t)((vMax + vMin) / 2);
      vMin = centro - 10;
      vMax = centro + 10;
    }
    const int16_t margine = (int16_t)((vMax - vMin) / 12 + 1);
    vMin -= margine;
    vMax += margine;
  }

  {
    tela.fillScreen(GxEPD_WHITE);
    tela.setTextColor(GxEPD_BLACK);

    tela.setFont(&FreeSansBold12pt7b);
    tela.setCursor(12, 30);
    tela.print("TEMPERATURA");
    tela.setFont(&FreeSans9pt7b);
    drawRight("ultime 24 ore", 388, 30);
    tela.drawFastHLine(12, 38, 376, GxEPD_BLACK);

    if (nSerie == 0) {
      tela.setFont(&FreeSans9pt7b);
      drawCenter("nessuno storico ancora", 200, 150);
      drawCenter("si riempie con i dati dei nodi", 200, 172);
      return;
    }

    // Cornice e tacche orizzontali: tre linee tratteggiate, che su e-ink si
    // leggono senza rubare contrasto alla curva.
    tela.drawRect(GR_X0, GR_Y0, GR_X1 - GR_X0, GR_Y1 - GR_Y0, GxEPD_BLACK);
    tela.setFont(&FreeSans9pt7b);
    for (int t = 0; t <= 2; t++) {
      const int16_t y = GR_Y0 + (int16_t)(((int32_t)(GR_Y1 - GR_Y0) * t) / 2);
      const int16_t v = (int16_t)(vMax - (int32_t)(vMax - vMin) * t / 2);
      if (t != 0 && t != 2) {
        for (int16_t x = GR_X0 + 4; x < GR_X1; x += 8) tela.drawPixel(x, y, GxEPD_BLACK);
      }
      drawRight(fmtNum(v / 10.0f, 1), GR_X0 - 5, y + 5);
    }

    // Asse dei tempi: l'ORA vera dei campioni, non "-24h/-12h/ora". Un istante
    // resta vero anche quando il pannello non si ridisegna da un pezzo, una
    // distanza no — la stessa ragione per cui la pagina nodi mostra l'ora
    // dell'ultimo pacchetto invece di "38 s fa".
    for (int t = 0; t <= 4; t++) {
      const int16_t x = GR_X0 + (int16_t)(((int32_t)(GR_X1 - GR_X0) * t) / 4);
      tela.drawFastVLine(x, GR_Y1, 4, GxEPD_BLACK);
      const time_t q = tsUltimo - (time_t)(24 * 3600) + (time_t)(6 * 3600 * t);
      char ora[8];
      if (rtctime_format(q, "%H:%M", ora, sizeof(ora))) {
        int16_t bx, by; uint16_t bw, bh;
        tela.getTextBounds(ora, 0, 0, &bx, &by, &bw, &bh);
        // Il limite e' 388, non 398: la cornice di plastica del pannello copre
        // gli ultimi pixel dell'area disegnabile, quindi un'etichetta centrata
        // sull'ultima tacca ci finisce sotto e si legge "19:3" invece di
        // "19:30". Visto sul pannello vero il 2026-08-30 — sul buffer era
        // tutto dentro i 400 px, ma i 400 px non si vedono tutti. Stesso
        // margine del resto del layout, che si ferma gia' a 388.
        int16_t cx = x - (int16_t)bw / 2;
        if (cx < 12) cx = 12;
        if (cx + (int16_t)bw > 388) cx = 388 - (int16_t)bw;
        tela.setCursor(cx, GR_Y1 + 18);
        tela.print(ora);
      }
    }

    // Le curve. Un buco NON si attraversa con una retta: un segmento lungo
    // sopra un'ora senza dati direbbe che la temperatura e' passata di li',
    // che nessuno ha misurato. Meglio la linea che si interrompe.
    for (int k = 0; k < nSerie; k++) {
      int16_t xPrec = 0, yPrec = 0;
      bool hoPrec = false;
      for (int i = 0; i < nCampioni; i++) {
        const int16_t v = serie[k][i];
        if (v == REMOTE_TEMP_VUOTO) { hoPrec = false; continue; }

        const int16_t x = grX(i, nCampioni);
        const int16_t y = GR_Y1 - (int16_t)(((int32_t)(v - vMin) * (GR_Y1 - GR_Y0)) /
                                            (vMax - vMin ? vMax - vMin : 1));
        if (hoPrec && grVisibile(k, i)) {
          tela.drawLine(xPrec, yPrec, x, y, GxEPD_BLACK);
          if (k == 0) tela.drawLine(xPrec, yPrec + 1, x, y + 1, GxEPD_BLACK);
        }
        xPrec = x; yPrec = y; hoPrec = true;
      }
    }

    // Legenda: nome, tratto e valore di adesso. Il campione di destra e' di
    // mezz'ora fa al massimo, il valore corrente e' quello vero.
    int16_t ly = GR_Y1 + 40;
    tela.setFont(&FreeSans9pt7b);
    for (int k = 0; k < nSerie; k++) {
      const int16_t x0 = 14;
      for (int16_t x = x0; x < x0 + 26; x++) {
        if (grVisibile(k, (int)(x - x0))) tela.drawPixel(x, ly - 4, GxEPD_BLACK);
        if (k == 0 && grVisibile(k, (int)(x - x0))) tela.drawPixel(x, ly - 3, GxEPD_BLACK);
      }
      tela.setCursor(x0 + 34, ly);
      tela.print(nomi[k]);
      if (ultimo[k] != REMOTE_TEMP_VUOTO) {
        drawRight(fmtNum(ultimo[k] / 10.0f, 1) + " C", 388, ly);
      }
      ly += 20;
      if (ly > 296) break;
    }
  }
  telaSulPannello(true);
}

// ---------------------------------------------------------------------------
// Il modello delle pagine (pages.*) decide COSA mostrare; qui si disegna.
// ---------------------------------------------------------------------------
static void showPage(uint8_t i)
{
  const PageCfg* pg = pages_get(i);
  if (pg == nullptr || !pg->usato) return;

  const uint32_t t0 = millis();

  switch (pg->tipo)
  {
    case PT_NODI:
      // Sempre completo entrando: la pagina precedente e' ancora nella memoria
      // del controller e un parziale la lascerebbe sotto.
      screenNodi(true);
      s_nodiParziali = 0;
      s_nodiDirty    = false;
      s_nodiUltimoMs = millis();
      break;

    case PT_MESSAGGIO:
      screenMessaggio(msg_active(time(nullptr)));
      break;

    case PT_IMMAGINE:
      screenImmagine(pg->param);
      break;

    case PT_GRAFICO:
      screenGrafico();
      break;

    case PT_BIANCA:
    default:
      screenBlank();
      break;
  }

  pannello.hibernate();
  s_epdRefresh++;
  s_epdUltimoMs   = millis() - t0;
  s_fullUltimoMs  = millis();   // il cambio pagina e' sempre un completo
  s_oraUltimoMs   = millis();
  pages_disegnata(millis());
  Serial.printf("[epd] pagina %u (%s): %lu ms\n", (unsigned)i,
                pages_tipo_nome(pg->tipo), (unsigned long)s_epdUltimoMs);
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

  // Prima di ogni altra cosa: se il boot si interrompesse piu' avanti, questo
  // e' comunque gia' registrato.
  bootDiagBegin();
  Serial.printf("[boot] avvio n. %lu, causa %s\n",
                (unsigned long)s_bootCount, app_reset_reason());

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
  pannello.init(115200, true, 50, false);
  pannello.setRotation(0);          // 0 = 400x300 nativo, come il formato .bin

  // U8g2 disegna sopra la STESSA tela di Adafruit_GFX, e da qui in poi le due
  // strade convivono (i font
  // Adafruit per i numeri grandi della pagina nodi, U8g2 dove serve UTF-8).
  u8g2Fonts.begin(tela);

  Serial.printf("[epd] dopo la rotazione: %d x %d\n", pannello.width(), pannello.height());
  Serial.printf("[epd] partial update: %s, fast partial: %s\n",
                pannello.epd2.hasPartialUpdate ? "si" : "no",
                pannello.epd2.hasFastPartialUpdate ? "si" : "no");
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

  // Elenco delle pagine e messaggio attivo: entrambi da NVS, quindi
  // disponibili anche senza microSD e prima che la rete sia su.
  pages_begin();
  msg_begin();

  Serial.printf("[epd] BOOT breve: pagina successiva. Rotazione %s.\n",
                pages_rotazione() ? "attiva" : "spenta");
  showPage(pages_current());
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
    // Si va sulla pagina dei nodi (slot 0, che esiste sempre) e si ridisegna
    // subito: un comando che non si vede sul pannello e' un comando di cui non
    // si sa se e' arrivato.
    pages_goto(0);
    showPage(0);
    return;
  }
  if (ev == BOOT_BREVE)
  {
    // A mano si scorrono anche le pagine escluse dalla rotazione: il tasto e'
    // la via di governo quando la rete non c'e'.
    showPage(pages_manual_next());
    return;
  }

  // La finestra scade da sola: quando succede, il pannello mostrerebbe ancora
  // il conto alla rovescia fino al refresh di cadenza (5 minuti dopo).
  {
    static bool s_pairingPrec = false;
    const bool ora = remote_pairing_active();
    if (ora != s_pairingPrec) { s_pairingPrec = ora; s_nodiDirty = true; }
  }

  // --- comandi che arrivano dalla web UI (accodati, vedi app_chiedi_*) -----
  if (s_paginaChiesta >= 0)
  {
    const uint8_t i = (uint8_t)s_paginaChiesta;
    s_paginaChiesta = -1;
    if (pages_goto(i)) { showPage(i); return; }
  }
  if (s_refreshChiesto)
  {
    // Refresh completo su richiesta: serve a togliere il ghosting quando si
    // accumula, senza aspettare il ciclo. Ridisegna la pagina corrente.
    s_refreshChiesto = false;
    showPage(pages_current());
    return;
  }

  // Un messaggio URGENTE scavalca tutto e porta il pannello sulla sua pagina:
  // e' la differenza fra una lavagnetta e un modo per lasciare un avviso a chi
  // torna a casa.
  if (msg_take_urgent())
  {
    for (uint8_t i = 0; i < pages_slots(); i++)
    {
      const PageCfg* pg = pages_get(i);
      if (pg && pg->usato && pg->tipo == PT_MESSAGGIO)
      {
        pages_goto(i);
        showPage(i);
        return;
      }
    }
  }

  // Rotazione automatica: pages_tick() tiene conto di rotazione spenta, ore di
  // silenzio e del caso "una sola pagina attiva" (dove non si tocca il
  // pannello: un completo per tornare sulla stessa pagina sono 2,2 s di
  // lampeggio per niente).
  {
    const int prossima = pages_tick(millis(), time(nullptr));
    if (prossima >= 0)
    {
      pages_goto((uint8_t)prossima);
      showPage((uint8_t)prossima);
      return;
    }
  }

  const PageCfg* pgCur = pages_get(pages_current());
  const uint8_t  tipoCur = pgCur ? pgCur->tipo : PT_NODI;

  // Il messaggio e' cambiato (o e' appena scaduto) mentre lo si sta
  // mostrando: senza questo il pannello resterebbe indietro fino al refresh
  // di cadenza successivo.
  if (msg_take_dirty(time(nullptr)))
  {
    if (tipoCur == PT_MESSAGGIO)
    {
      showPage(pages_current());
      return;
    }
    // Con la fascia accesa il messaggio sta anche sulla pagina dei nodi:
    // senza questo resterebbe quello vecchio fino al refresh di cadenza.
    if (tipoCur == PT_NODI && pages_fascia()) s_nodiDirty = true;
  }

  // Il grafico si ridisegna quando entra un campione nuovo nell'anello, cioe'
  // ogni mezz'ora: e' la risoluzione dello storico, e ridisegnare piu' spesso
  // mostrerebbe esattamente la stessa curva al prezzo di un refresh completo.
  // Serve perche' con il grafico come unica pagina attiva la rotazione non
  // scatta mai, e senza questo resterebbe fermo all'ora in cui e' comparso.
  if (tipoCur == PT_GRAFICO)
  {
    // Primo dato di un nodo: si ridisegna subito, senza aspettare lo slot.
    // Costa al massimo un refresh per nodo dopo un riavvio.
    if (s_graficoDirty)
    {
      s_graficoDirty = false;
      showPage(pages_current());
      return;
    }

    static uint32_t s_graficoUltimoSlot = 0;
    const uint32_t slotOra = (uint32_t)(time(nullptr) / (time_t)REMOTE_TEMP_SLOT_S);
    if (s_graficoUltimoSlot == 0) s_graficoUltimoSlot = slotOra;
    else if (slotOra != s_graficoUltimoSlot)
    {
      s_graficoUltimoSlot = slotOra;
      showPage(pages_current());
      return;
    }
  }

  if (tipoCur == PT_NODI)
  {
    // L'orologio per primo: e' il refresh piu' economico e il piu' frequente.
    // Se scatta anche quello della pagina intera, quello ridisegna comunque
    // l'ora e rimette a zero questo timer, quindi non si sovrappongono.
    if (millis() - s_oraUltimoMs >= ORA_MS)
    {
      const uint32_t t0 = millis();
      screenOrologio();
      pannello.hibernate();
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
    pannello.hibernate();

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

}
