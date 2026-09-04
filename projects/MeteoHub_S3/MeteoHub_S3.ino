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
#include <esp_task_wdt.h>  // il watchdog del loop: vedi wdtBegin()
#include <Preferences.h>   // boot_count: l'unico contatore che sopravvive al riavvio
#include <EspNowLink.h>    // ESPNOW_LINK_CHANNEL_CURRENT
#include "remote_nodes.h"
#include "meteo_calc.h"
#include "icone.h"  // hub ESP-NOW, trapiantato da projects/EnvNode_C3/
#include "forecast.h"      // i nomi TREND_*: remote_nodes.h non lo include

#include "rtc_time.h"      // serve a remote_nodes per datare i DATA
#include "sd_logger.h"
#include "daily.h"     // microSD della Sense: i CSV dei nodi
#include "net_ota.h"       // WiFi + ArduinoOTA + /update + WebServer condiviso
#include "web_ui.h"        // pagina di stato e API, registrate su net_server()
#include "pages.h"        // il modello delle pagine: cosa mostrare e quando
#include "messages.h"     // il messaggio attivo (NVS) e il suo archivio (SD)
#include "secrets.h"       // OTA_HOSTNAME, per dirlo sul pannello

// v44 (2026-09-03) — le cinque correzioni del "Blocco A" di
// docs/Proposte-2026-09-02.md. Nessuna feature nuova: tolgono difetti che
// falsavano numeri o rendevano invisibile uno stato.
//
//   - la cadenza di un nodo si impara SOLO dai DATA consecutivi. Prima
//     bastava un seq crescente, quindi un pacchetto perso portava la cadenza
//     da 300 a 375 s e con lei la soglia del muto (+3 minuti sul rilevamento
//     di un nodo davvero morto). Nuovo campo intervallo_campioni: dice se
//     quel numero e' vecchio o calcolato sui buchi.
//   - tetto al salto di seq (PERSI_SALTO_MAX): un contatore sporco letto
//     dalla RTC memory non puo' piu' inventare milioni di "persi"
//     permanenti. I salti fuori scala si contano a parte (seq_assurdi) e
//     compaiono in /api/salute.
//   - i timer del ritardo si tengono per MAC e non per posizione, cosi'
//     remote_forget() -- che compatta il registro -- non li disallinea.
//   - un WELCOME per giro e a turno (in EspNowLink): mandarli tutti insieme
//     costava fino a 8 s di loop() fermo dopo un blackout, proprio mentre
//     tutti i nodi ritrasmettono.
//   - /api/stato dice quanti parziali sono passati dall'ultimo completo e
//     quando il completo e' stato: il ghosting e' l'unica cosa che
//     l'anteprima del pannello non puo' mostrare.
// v45 (2026-09-03) — il "Blocco B" di docs/Proposte-2026-09-02.md. Anche qui
// nessuna feature: aggiunge solo OCCHI, cioe' non cambia il comportamento di
// niente e rende visibile quello che prima non lo era.
//
//   - il tempo di giro (loop_max_ms/dove/ora, loop_lenti): distingue "si e'
//     riavviata" da "e' rimasta ferma dentro una chiamata", che nei CSV hanno
//     lo stesso aspetto. Il pannello NON entra nel massimo: 2,6 s li' sono
//     normali e coprirebbero tutto il resto.
//   - il watchdog e' armato (60 s): fino a v44 app_reset_reason() traduceva
//     WDT_TASK senza che nessuno potesse produrlo. Alimentato durante l'OTA
//     invece che sospeso, cosi' resta armato anche li'.
//   - il diario degli eventi su card (/eventi/AAAA-MM.csv): una riga per
//     TRANSIZIONE -- boot, NTP, nodo muto, nodo che torna, card, OTA,
//     associazione -- con un tetto per tipo e le soppressioni dichiarate.
//   - l'ascolto durante l'associazione (/api/pairing/ascolto): chi bussa e
//     perche' non entra, con l'RSSI che il callback riceveva gia' e nessuno
//     leggeva. Si annota anche FUORI dalla finestra: contare non e' adottare.
// v46 (2026-09-03) — `wdt_armato` e `wdt_timeout_s` in /api/stato, piu' un
// avviso in /api/salute se non lo e'. Aggiunta subito dopo aver messo in
// servizio la v45, per una ragione che vale la riga in piu': un watchdog
// configurato male e uno giusto sono INDISTINGUIBILI da fuori finche' non
// serve, e quando serve e' tardi. Non prova che il riavvio funzioni -- per
// quello serve un blocco vero -- ma prova che il task loop e' iscritto, che e'
// la meta' che si puo' sbagliare in silenzio.
// v47 (2026-09-03) — la maschera degli idle task del watchdog si LEGGE dalle
// macro di sdkconfig invece di essere scritta a mano. Sull'S3 il risultato e'
// identico (l'idle di CPU0 e' iscritto, CPU1 no), quindi qui non cambia
// niente: la correzione serviva sul NODO, dove lo stesso codice gira su due
// chip configurati diversamente e un numero fisso sarebbe stato giusto sulla
// XIAO C3 e sbagliato sull'ESP32 classico -- disiscrivendogli l'idle di CPU0,
// cioe' togliendo in silenzio una protezione che c'era.
//
// La versione avanza lo stesso: il sorgente e' cambiato, e due sorgenti
// diversi non possono chiamarsi entrambi v46. FW_VERSION e' l'unico modo di
// sapere da remoto cosa sta girando davvero.
// v48 (2026-09-03) — anche il PRIMO delta dopo un riavvio di un nodo non e'
// un periodo, e va saltato come i buchi. E' il residuo della correzione della
// v44, trovato guardando i numeri veri subito dopo aver aggiornato il nodo a
// batteria: il suo primo DATA dopo il riavvio e' arrivato 671 s dopo il
// precedente invece di 300 — in mezzo c'era il boot piu' la finestra di veglia
// da 5 minuti — e siccome il seq era consecutivo (1->2) la media mobile se
// l'e' preso: cadenza appresa 393 s e soglia del muto 1012 invece di 780.
// v49 (2026-09-03) — `POST /api/prova/blocco`: fabbrica il guasto che il
// watchdog deve riprendere. `wdt_armato` diceva che il task e' iscritto, non
// che il riavvio funzioni — la meta' verificabile a costo zero, che resta
// meta'. Stessa disciplina di `prova-canale` e `prova-riallineo` sul nodo: una
// funzione che si attiva una volta all'anno, e mai sotto osservazione, e' una
// funzione che non si sa se esiste.
static const char FW_VERSION[] = "v55";

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
static uint32_t s_epdOrologioMs = 0;   // da v30 resta a zero: l'orologio non c'e' piu'

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
// ---------------------------------------------------------------------------
// Quanto e' durato il giro, e dove
// ---------------------------------------------------------------------------
// Trapiantato da projects/EnvNode_C3/ (v12), che pero' e' spenta dal 31/08:
// la misura stava sulla scheda che bloccava di meno, e non su questa, che di
// mestiere si ferma. Un refresh completo del pannello sono 2630 ms, il budget
// di invio di un file 20 s, una write su un client morto ~10 s, un OTA decine
// di secondi.
//
// Serve a distinguere DUE COSE CHE NEI CSV HANNO LO STESSO ASPETTO -- un buco:
// "si e' riavviata" (lo dicono gia' reset_reason e boot_count, da v13) e "e'
// rimasta ferma dentro una chiamata", che finora non lo diceva nessuno.
//
// IL PANNELLO NON ENTRA IN QUESTO MASSIMO, ed e' deliberato: 2,6 s li' sono
// normali, non un guasto, e coprirebbero per sempre tutto il resto -- che e'
// la parte interessante. Il tempo del disegno ha gia' due posti suoi,
// epd_ultimo_ms e il registro dei refresh sulla card. Qui si misura solo cio'
// che viene PRIMA di qualunque disegno, che e' anche tutto cio' che nel loop
// puo' bloccare senza doverlo.
//
// In RAM di proposito, come sull'altra scheda: il buco nel CSV si data gia' da
// se', e scrivere in NVS dentro il giro sarebbe un costo continuo per un
// evento raro.
static const uint32_t LOOP_LENTO_MS = 500;   // ~meta' del parziale piu' corto

static uint32_t s_loopMaxMs       = 0;
static char     s_loopMaxDove[12] = "";
static time_t   s_loopMaxTs       = 0;
static uint32_t s_loopLenti       = 0;

// Chiude la fase iniziata a "inizio" e apre la successiva: il ritorno e' il
// nuovo istante di partenza, cosi' nel loop() si incatenano.
static uint32_t faseFine(const char* nome, uint32_t inizio)
{
  const uint32_t durata = millis() - inizio;

  if (durata > s_loopMaxMs) {
    s_loopMaxMs = durata;
    strncpy(s_loopMaxDove, nome, sizeof(s_loopMaxDove) - 1);
    s_loopMaxDove[sizeof(s_loopMaxDove) - 1] = '\0';
    s_loopMaxTs = rtctime_now();
  }
  if (durata >= LOOP_LENTO_MS) {
    s_loopLenti++;
    Serial.printf("[lento] %s ha tenuto il giro per %lu ms\n",
                  nome, (unsigned long)durata);
  }
  return millis();
}

uint32_t    app_loop_max_ms()   { return s_loopMaxMs; }
const char* app_loop_max_dove() { return s_loopMaxDove; }
time_t      app_loop_max_ts()   { return s_loopMaxTs; }
uint32_t    app_loop_lenti()    { return s_loopLenti; }

// ---------------------------------------------------------------------------
// Watchdog del loop
// ---------------------------------------------------------------------------
// app_reset_reason() traduce WDT_TASK, WDT_INT e WDT dalla v13, ma in tutto lo
// sketch non c'era una sola chiamata a esp_task_wdt_add(): erano tre stringhe
// che NON POTEVANO COMPARIRE. Una diagnosi scritta per un meccanismo che non
// esisteva.
//
// Cosa lasciava scoperto: un loop() piantato per sempre. Il pannello e-ink e'
// BISTABILE, quindi resta li' nitido e plausibile anche a scheda ferma; il
// WebServer non risponde piu', e con lui sparisce tutta la diagnostica
// dell'hub; i nodi continuano a ricevere l'ACK di livello radio e contano i
// propri invii come riusciti. Un guasto totale con l'aspetto di un sistema
// perfetto.
//
// IL BARATTO DA CONOSCERE. Il core inizializza gia' il TWDT a 5 s con l'idle
// task di CPU0 iscritto (CONFIG_ESP_TASK_WDT_*), e il timeout e' UNO SOLO per
// tutto il TWDT: portarlo a 60 s allunga anche la protezione dell'idle di
// CPU0. Si accetta, perche' quella protezione copre uno scenario in cui la
// radio e' comunque morta e cambia solo quanto in fretta si riavvia, mentre
// qui si guadagna la protezione del loop, che oggi non ha niente ed e' dove
// sta il rischio vero. L'idle di CPU0 resta iscritto (idle_core_mask), non si
// toglie: si allunga, non si spegne.
//
// PERCHE' 60 s E NON MENO: il caso legittimo piu' lungo e' il budget di invio
// di un file (20 s) piu' una write bloccata dentro il core (~10 s), cioe' ~30.
// Un timeout stretto trasformerebbe un download lento in un riavvio, che e'
// molto peggio del guasto che si sta prevenendo.
static const uint32_t WDT_TIMEOUT_MS = 60000;

// Il watchdog e' davvero armato? Da fuori, un watchdog configurato male e uno
// configurato bene sono INDISTINGUIBILI finche' non serve -- e quando serve e'
// tardi. Questo campo trasforma "spero sia armato" in "la scheda dice che lo
// e'": non prova che il riavvio funzioni (per quello serve un blocco vero), ma
// prova che il task loop e' iscritto, che e' la meta' che si puo' sbagliare in
// silenzio.
static bool s_wdtArmato = false;

bool     app_wdt_armato()    { return s_wdtArmato; }
uint32_t app_wdt_timeout_s() { return WDT_TIMEOUT_MS / 1000; }

// --- prova-blocco: fabbricare il guasto invece di aspettarlo ---------------
// `wdt_armato` dice che il task e' iscritto, NON che il riavvio funzioni: e'
// la meta' che si puo' verificare a costo zero, e resta meta'. Questo comando
// blocca il loop() apposta, senza alimentare il watchdog, e la prova riesce se
// la scheda riparte da sola con reset_reason WDT_TASK -- una stringa che fino
// alla v45 non poteva comparire.
//
// E' la stessa disciplina di `prova-canale` e `prova-riallineo` sul nodo, e per
// la stessa ragione: una funzione che si attiva una volta all'anno, e mai sotto
// osservazione, e' una funzione che non si sa se esiste.
//
// SI ACCODA, non si esegue nell'handler HTTP: bloccando li' la risposta non
// partirebbe e il client resterebbe appeso senza sapere se il comando e'
// arrivato. Stessa regola di app_chiedi_pagina()/app_chiedi_refresh().
//
// IL TETTO SERVE AL CASO IN CUI LA PROVA FALLISCE. Se il watchdog NON scatta,
// il blocco dura tutto il tempo chiesto: senza limite, un numero sbagliato
// terrebbe ferma la scheda per ore. Con il tetto il caso peggiore e' due
// minuti, ed e' anche il risultato negativo che si voleva vedere.
static const uint32_t BLOCCO_MAX_S = 120;
static uint32_t s_bloccoChiesto = 0;

void app_chiedi_blocco(uint32_t secondi)
{
  if (secondi == 0) secondi = 1;
  if (secondi > BLOCCO_MAX_S) secondi = BLOCCO_MAX_S;
  s_bloccoChiesto = secondi;
}

// La maschera degli idle task si LEGGE da come il core e' configurato, non si
// scrive a mano: passarne una sbagliata li disiscriverebbe, togliendo in
// silenzio una protezione che c'era. Qui l'S3 ha l'idle di CPU0 iscritto, ma
// la stessa funzione sta anche su MeteoNode_C3 -- che gira su due chip
// configurati DIVERSAMENTE fra loro -- e un numero fisso sarebbe giusto su uno
// e sbagliato sull'altro.
static uint32_t wdtIdleMask()
{
  uint32_t m = 0;
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
  m |= (1 << 0);
#endif
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
  m |= (1 << 1);
#endif
  return m;
}

static void wdtBegin()
{
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms    = WDT_TIMEOUT_MS;
  cfg.idle_core_mask = wdtIdleMask();
  cfg.trigger_panic = true;        // riavvia, cosi' reset_reason lo dice

  // Il TWDT e' gia' inizializzato dal core (CONFIG_ESP_TASK_WDT_INIT), quindi
  // init() torna ESP_ERR_INVALID_STATE e si passa da reconfigure(). Si provano
  // entrambe per non dipendere da quella configurazione.
  esp_err_t e = esp_task_wdt_init(&cfg);
  if (e == ESP_ERR_INVALID_STATE) e = esp_task_wdt_reconfigure(&cfg);
  if (e != ESP_OK) {
    Serial.printf("[wdt] non configurato (%d): il loop resta senza rete\n", (int)e);
    return;
  }
  if (esp_task_wdt_add(NULL) != ESP_OK) {
    Serial.println("[wdt] il task loop non si e' iscritto");
    return;
  }
  s_wdtArmato = true;
  Serial.printf("[wdt] armato sul loop, timeout %lu s\n",
                (unsigned long)(WDT_TIMEOUT_MS / 1000));
}

// ---------------------------------------------------------------------------
// OTA in corso
// ---------------------------------------------------------------------------
// Serve a due cose insieme, ed e' il motivo per cui questa callback esiste
// adesso e prima no:
//
//  1. il tempo speso dentro handleClient() mentre si scrive la partizione sono
//     decine di secondi LEGITTIMI, e finirebbero nel massimo del giro
//     coprendo per sempre il guasto che quel contatore deve far vedere;
//  2. il watchdog va alimentato, o l'aggiornamento si riavvierebbe da solo a
//     meta'. Si alimenta qui e non sospendendo il watchdog, perche' cosi'
//     resta armato: un OTA che si pianta davvero viene comunque ripreso.
static bool s_otaAttivo = false;

static void onOtaProgress(int percent, const char* what)
{
  // Una riga sola all'inizio, non una per percentuale: e' il diario, non un
  // log. Che l'aggiornamento sia poi ANDATO A BUON FINE lo dice la riga di
  // boot successiva, con reset_reason SW e il firmware nuovo.
  if (!s_otaAttivo) evento("ota", what ? what : "aggiornamento iniziato");
  s_otaAttivo = true;
  esp_task_wdt_reset();
  static int ultimo = -1;
  if (percent == ultimo) return;
  ultimo = percent;
  if (percent < 0) Serial.printf("[OTA] %s: in corso...\n", what);
  else             Serial.printf("[OTA] %s: %d%%\n", what, percent);
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

// Ore di silenzio: dentro la fascia il pannello non si tocca. s_pagPrimaSil
// serve a rimettere le cose come stavano all'uscita, s_pagSilScelta a sapere
// SU QUALE pagina si e' finiti (con il sorteggio non e' quella configurata).
// Firma dell'ultima pagina disegnata, e quante volte si e' evitato di
// ridisegnare perche' non era cambiato niente. Il contatore serve a saperlo
// da remoto: senza, "il pannello non si aggiorna" e "non c'e' niente da
// aggiornare" sarebbero indistinguibili.
// Il ritardo di un nodo va CONFERMATO prima di mostrarlo. Senza, basta che la
// cadenza appresa sia piu' corta di quella vera perche' il nodo risulti in
// ritardo verso la fine di ogni ciclo e torni ok appena trasmette: lo stato
// oscilla, e siccome lo stato passa avanti alla cadenza ogni oscillazione
// costa DUE refresh.
//
// Succede davvero, e non solo per un errore: capita ad ogni cambio di
// cadenza di un nodo, mentre la media mobile dell'hub converge. Misurato il
// 2026-08-31 portando il nodo a muro da 60 a 300 s -- la cadenza appresa era
// a meta' strada (119 s) e il pannello si ridisegnava ogni 113 s invece che
// ogni 300.
//
// 60 secondi di conferma: piu' lunghi dell'oscillazione tipica e molto piu'
// corti della soglia del muto, che resta il segnale serio e non e' toccata.
static const uint32_t RITARDO_CONFERMA_MS = 60000UL;

// I timer si tengono per MAC, NON per posizione nel registro dei nodi.
//
// remote_forget() COMPATTA quel registro -- sposta i nodi successivi di un
// posto indietro -- e compatta insieme i suoi array paralleli, con un commento
// che dice perche': "o dopo un dimentica ogni nodo si ritrova lo storico di
// pressione del vicino". Questo array pero' sta FUORI dal modulo, quindi
// nessuno lo compattava: dopo un "dimentica" i timer restavano appiccicati
// alla posizione invece che al nodo.
//
// Il danno era piccolo e si riassorbiva da solo (un "!" mostrato o nascosto
// per meno di un minuto), ma la classe di errore no. Indicizzare per MAC la
// toglie invece di tapparne un caso, ed e' la stessa scelta, con la stessa
// motivazione, gia' presa da aggiornaDaLibreria(): "i peer si abbinano per
// MAC, non per indice". Con otto nodi al massimo la ricerca non si sente.
//
// `da == 0` significa "timer spento": uno slot con quel valore non porta
// informazione e si puo' riusare per un altro nodo.
struct RitardoTimer { uint8_t mac[6]; uint32_t da; };
static RitardoTimer s_ritardo[REMOTE_MAX_NODES] = {};

static bool nodoInRitardo(const RemoteNode& n)
{
  const bool grezzo = n.hasData && n.online &&
                      n.intervalloS > 0 && n.silenzioS > n.intervalloS * 2;

  int slot = -1, libero = -1;
  for (int i = 0; i < REMOTE_MAX_NODES; i++) {
    if (s_ritardo[i].da == 0) { if (libero < 0) libero = i; continue; }
    if (memcmp(s_ritardo[i].mac, n.mac, 6) == 0) { slot = i; break; }
  }

  if (!grezzo) {
    if (slot >= 0) s_ritardo[slot].da = 0;   // tornato in orario: timer spento
    return false;
  }
  if (slot < 0) {
    // Primo giro in ritardo: parte il timer.
    //
    // Un posto libero di norma c'e' -- al massimo un timer per nodo, e i nodi
    // sono al massimo REMOTE_MAX_NODES. L'unico modo di esaurirli e'
    // dimenticare un nodo MENTRE il suo timer corre: quella voce resta con un
    // MAC che non e' piu' in elenco e nessuno la spegne, perche' questo array
    // sta fuori dal modulo e non sa niente di remote_forget(). Ripetuto,
    // riempirebbe la tabella e da li' in poi nessun nodo mostrerebbe piu' il
    // "!" -- che e' un guasto silenzioso, cioe' proprio quello che indicizzare
    // per MAC serviva a togliere.
    //
    // Si ripulisce qui e solo qui: costa 8x8 confronti nel caso in cui la
    // tabella e' piena, cioe' quasi mai, e in cambio non c'e' nessuno stato
    // che possa marcire.
    if (libero < 0) {
      for (int i = 0; i < REMOTE_MAX_NODES && libero < 0; i++) {
        if (s_ritardo[i].da == 0) continue;
        bool ancoraInElenco = false;
        for (int k = 0; k < remote_count() && !ancoraInElenco; k++) {
          RemoteNode t;
          if (remote_get(k, &t) && memcmp(t.mac, s_ritardo[i].mac, 6) == 0)
            ancoraInElenco = true;
        }
        if (!ancoraInElenco) { s_ritardo[i].da = 0; libero = i; }
      }
    }
    if (libero < 0) return false;
    uint32_t ora = millis();
    if (ora == 0) ora = 1;                   // 0 e' la sentinella di "spento"
    memcpy(s_ritardo[libero].mac, n.mac, 6);
    s_ritardo[libero].da = ora;
    return false;
  }
  return (millis() - s_ritardo[slot].da) >= RITARDO_CONFERMA_MS;
}

static uint32_t s_firmaStato    = 0;
static uint32_t s_firmaValori   = 0;
static uint32_t s_nodiInvariati = 0;
static uint32_t s_ultimoCheckMs = 0;
static const uint32_t CHECK_MS  = 5000UL;   // ogni quanto si guarda se e' cambiato

static bool     s_inSilenzio    = false;
static uint8_t  s_pagPrimaSil   = 0;
static uint8_t  s_pagSilScelta  = PAG_SIL_NESSUNA;
// Modalita' "a caso fra tutte quelle sulla card": qui non c'e' uno slot, c'e'
// un file. Il nome resta finche' dura la fascia; l'ultimo sorteggiato si
// ricorda per non ripescarlo due notti di fila (in RAM: un riavvio che lo
// dimentica non e' un guasto, e' solo una notte che puo' ripetersi).
static char     s_silImgNome[IMG_NOME_MAX + 1]   = "";
static char     s_silImgUltima[IMG_NOME_MAX + 1] = "";
// Quante pagine ha disegnato showPage(): serve solo a sapere se, al mattino,
// sul vetro c'e' ancora l'immagine della notte o qualcosa che l'utente ha
// chiesto nel frattempo dal tasto BOOT o dal web. Un confronto fra due numeri
// invece di uno stato da tenere aggiornato in tre posti.
static uint32_t s_disegniPagina = 0;
static uint32_t s_silDisegnoAl  = 0;
static uint32_t s_nodiUltimoMs  = 0;
static uint8_t  s_nodiParziali  = 0;
static uint32_t s_fullUltimoMs  = 0;   // ultimo refresh COMPLETO, per l'alone

// ...e la stessa cosa in ora a muro, per poterla dire in rete. Un istante e
// non "da quanto tempo": vale la regola gia' scritta per l'ora dell'ultimo
// pacchetto, un istante resta vero anche quando nessuno lo rilegge da un
// pezzo. Vale 0 finche' un completo non c'e' stato.
static time_t   s_fullUltimoTs  = 0;

// I due si aggiornano SEMPRE insieme, da un punto solo: sono la stessa cosa
// contata con due orologi, e se si potessero muovere separatamente prima o poi
// divergerebbero. Stessa regola di drawOra(), che disegna l'ora da una
// funzione sola perche' due disegni dello stesso dato finirebbero per differire.
static void segnaFull()
{
  s_fullUltimoMs = millis();
  s_fullUltimoTs = time(nullptr);
}

// Quanti parziali si sono accumulati dall'ultimo completo, e quando il
// completo e' stato.
//
// PERCHE' SI ESPONGONO. Il ghosting e' l'unica proprieta' del pannello che
// l'anteprima NON puo' mostrare: lei restituisce la tela, cioe' cio' che l'hub
// ha disegnato, non i fotoni sul vetro. Questi due numeri non misurano
// l'alone -- non si puo' -- ma rendono leggibile da fuori la POLITICA che lo
// determina: si vede subito se il completo periodico sta scattando o se una
// configurazione (una pagina sola attiva, una fascia di silenzio lunga) lo sta
// rimandando.
//
// Da sapere leggendoli: il conteggio e' quello della PAGINA NODI, che e'
// l'unica a fare parziali. Ogni cambio pagina e' un completo e lo azzera.
uint32_t app_epd_parziali_da_full() { return s_nodiParziali; }
time_t   app_epd_ultimo_full_ts()   { return s_fullUltimoTs; }

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
// Registra un refresh: contatore in RAM e una riga sulla card. Sta in un
// punto solo, cosi' non si puo' aggiungere una pagina nuova e dimenticare di
// contarla.
//
// NIENTE TOTALE IN NVS, e la ragione e' che non serve: il totale e' gia'
// sulla card, una riga per refresh, e si conta quando lo si chiede
// (/api/epd/totale). Tenerne una copia in flash vorrebbe dire scrivere per
// sapere qualcosa che si sa gia' -- e la flash ha cicli di erase finiti,
// mentre la card no. Il costo si sposta dove non fa danno: dal consumo
// continuo di una memoria che si logora al conteggio occasionale di un file.
static void contaRefresh(const char* motivo, bool completo, uint32_t ms)
{
  s_epdRefresh++;
  sd_log_refresh(motivo, completo, ms);
}

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

static const int16_t NODI_TOP       = 2;    // da v36 non c'e' piu' intestazione
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

  // Un valore che ARROTONDA a zero non ha segno. `String(v, 1)` di -0,04 da'
  // "-0,0", cioe' "negativo ma nullo": non significa niente, e sul pannello si
  // legge come un guasto della formattazione. Visto sul vetro in v41, con la
  // pressione ferma. Si guarda il numero arrotondato, non quello vero, perche'
  // e' il primo quello che finisce sotto gli occhi.
  const float scala = (dec <= 0) ? 1.0f : (dec == 1 ? 10.0f : 100.0f);
  if (lroundf(v * scala) == 0) return fmtNum(0.0f, dec);

  String s = fmtNum(v, dec);
  if (v >= 0 && !s.startsWith("+")) s = "+" + s;
  return s;
}

// --- la testata col nome: grassetto e un filetto, non piu' la barra piena --
// Fino a v37 qui c'era un rettangolo nero a piena larghezza. Separava benissimo
// -- il nero pieno e' cio' che si vede da piu' lontano -- ma era anche il 15%
// della pagina acceso di continuo, ed e' esattamente il modo in cui un e-ink
// invecchia: l'alone si forma dove il nero non si muove. Le due barre da sole
// facevano la meta' del nero della pagina (22,2% misurato sull'anteprima del
// 2026-09-01).
//
// Il nome in grassetto con un filetto sotto separa quanto basta -- fra un
// blocco e l'altro ci sono comunque 130 px di bianco -- e libera il nero per
// l'unica cosa che deve gridare: il badge del nodo muto, che ora e' il solo
// negativo della pagina e per questo si vede molto piu' di prima.
static void drawTestataNodo(const RemoteNode& n, int16_t y, int16_t h)
{
  const int16_t W = tela.width();

  // Il testo si CENTRA nell'altezza misurandolo, invece di appoggiarlo a un
  // offset fisso: cosi' la testata puo' cambiare altezza o font senza che
  // nessuno debba rifare i conti a mano. by e' l'offset del bordo alto
  // rispetto alla baseline, ed e' negativo: sottrarlo e' cio' che porta il
  // testo dentro.
  tela.setFont(&FreeSansBold12pt7b);
  int16_t bx, by; uint16_t bw, bh;
  tela.getTextBounds(n.nome, 0, 0, &bx, &by, &bw, &bh);
  const int16_t base = y + (h - (int16_t)bh) / 2 - by;
  tela.setCursor(10, base);
  tela.print(n.nome);

  // Filetto doppio: su e-ink una linea da un pixel, vista da tre metri, non
  // c'e'. Due pixel sono ancora un ventesimo dell'inchiostro della barra.
  tela.drawFastHLine(0, y + h - 2, W, GxEPD_BLACK);
  tela.drawFastHLine(0, y + h - 1, W, GxEPD_BLACK);

  // L'ora dell'ultimo pacchetto NON si scrive piu' (da v31). Non e' per fare
  // spazio: e' che finche' il pannello mostra qualcosa che cambia da solo --
  // un orologio, un "ultimo alle" -- ogni refresh e' obbligato, anche quando i
  // numeri sono identici. Tolta anche quella, la pagina cambia SOLO quando
  // cambiano le misure, e un refresh che ridisegnerebbe gli stessi pixel si
  // puo' non fare. Il quando sta nella web UI, che quei dettagli li ha tutti.
  //
  // Resta il RITARDO, che e' l'unica cosa che il pannello deve dire da solo:
  // se un nodo tace da piu' della sua cadenza osservata, un punto esclamativo
  // in fondo alla barra. Non dice quanto -- dice "vai a guardare".
  // Un avviso solo, a destra. Fino a v36 il "MUTO" si scriveva DUE volte --
  // accanto al nome (residuo di quando l'ora stava a destra) e in fondo alla
  // barra -- e sul pannello si leggeva due volte la stessa parola.
  // Il MUTO torna a essere un badge in NEGATIVO (drawBadgeMuto, che c'era gia'
  // e non serviva piu' da quando la barra scriveva in bianco). Senza la barra
  // nera intorno e' il solo nero pieno della pagina: si vede prima dei numeri,
  // che e' il suo mestiere. Il "!" del ritardo resta un carattere nero -- e'
  // un avviso minore, e non deve somigliare a un guasto.
  if (n.hasData && !n.online) {
    drawBadgeMuto(W - 64, y + (h - 18) / 2);
  }
  else if (n.hasData && nodoInRitardo(n)) {
    tela.setFont(&FreeSansBold12pt7b);
    tela.getTextBounds("!", 0, 0, &bx, &by, &bw, &bh);
    drawRight("!", W - 12, y + (h - (int16_t)bh) / 2 - by);
  }
}

// --- la barra del giorno ---------------------------------------------------
// Dove sta la temperatura di ADESSO fra il minimo e il massimo delle ultime
// 24 ore. E' l'informazione che al pannello mancava: un numero da solo non
// dice se e' alto -- 26,5 gradi con minimo 12 e con minimo 24 sono due
// giornate diverse, e finora il pannello le mostrava identiche.
//
// Non e' una sparkline, ed e' voluto: la regola scritta in CLAUDE.md (il
// grafico sta a piena pagina, non compresso in un francobollo) resta valida,
// perche' una curva da 140x16 sarebbe un ornamento. Qui non si disegna
// l'andamento ma UNA posizione dentro un intervallo -- due tacche e un
// cursore, che a tre metri si legge, mentre una curva no.
//
// Quattro rettangoli in tutto: nessuna memoria nuova, l'anello dei 48 slot
// esiste gia' per la pagina grafico.
static void drawRangeGiorno(int16_t x, int16_t y, int16_t w,
                            float minC, float maxC, float ora)
{
  // Asta doppia, come il filetto e la freccia: un pixel solo non si vede.
  tela.drawFastHLine(x, y,     w, GxEPD_BLACK);
  tela.drawFastHLine(x, y + 1, w, GxEPD_BLACK);

  // Tacche agli estremi: dicono dove finisce la giornata, altrimenti l'asta
  // sembrerebbe continuare oltre.
  tela.fillRect(x,         y - 5, 2, 12, GxEPD_BLACK);
  tela.fillRect(x + w - 2, y - 5, 2, 12, GxEPD_BLACK);

  // Il cursore. Se l'escursione e' nulla (o quasi: un nodo appena acceso ha
  // un campione solo) si mette in mezzo, che e' la verita' -- non a un
  // estremo, che direbbe "minimo del giorno" senza che nessuno lo sappia.
  float frazione = 0.5f;
  if (isfinite(ora) && maxC - minC > 0.05f) {
    frazione = (ora - minC) / (maxC - minC);
    if (frazione < 0) frazione = 0;
    if (frazione > 1) frazione = 1;
  }
  const int16_t px = x + (int16_t)(frazione * (float)(w - 7));
  tela.fillRect(px, y - 7, 7, 16, GxEPD_BLACK);
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

// Quale dei due blocchi si usa. Sta in una funzione perche' la decisione
// serve in DUE posti: screenNodi() per disegnare, firmaValori() per sapere
// che cosa e' stato disegnato -- la rugiada c'e' solo nel compatto, min/max
// solo nel comodo. Due condizioni copiate divergerebbero al primo ritocco, e
// una firma che non corrisponde alla pagina si vede come refresh mancati:
// il pannello resta indietro e nessun contatore lo dice. E' la stessa regola
// per cui l'ora la disegna una funzione sola.
//
// Il blocco comodo vuole ~110 px: con la fascia del messaggio non ce ne sono,
// e disegnarlo lo stesso vorrebbe dire numeri sopra il separatore.
static bool nodiLayoutComodo(int quanti, const Message* fascia)
{
  return (quanti <= NODI_COMODI_FINO_A) && (fascia == nullptr);
}

// --- blocco COMODO: fino a due nodi ---------------------------------------
static void drawNodoComodo(const RemoteNode& n, int16_t y, int indice)
{
  const int16_t W = tela.width();
  drawTestataNodo(n, y, 26);

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
    tela.drawBitmap(10, y + 48, IC_TERMOMETRO, IC_TERMOMETRO_W, IC_TERMOMETRO_H,
                    GxEPD_BLACK);
    tela.setFont(&FreeSansBold24pt7b);
    tela.setCursor(36, y + 76);
    tela.print(fmtNum(n.value[0], 1));
    int16_t bx, by; uint16_t bw, bh;
    tela.getTextBounds(fmtNum(n.value[0], 1), 36, y + 76, &bx, &by, &bw, &bh);
    const int16_t xu = 36 + (int16_t)bw + 7;
    drawGrado(xu + 3, y + 52, 4);
    tela.setFont(&FreeSansBold12pt7b);
    tela.setCursor(xu + 10, y + 76);
    tela.print("C");
  }

  tela.setFont(&FreeSansBold12pt7b);
  if (isfinite(n.value[1])) {
    tela.drawBitmap(184, y + 50, IC_GOCCIA, IC_GOCCIA_W, IC_GOCCIA_H, GxEPD_BLACK);
    tela.setCursor(210, y + 67);
    tela.print(fmtNum(n.value[1], 0) + "%");
  }
  if (isfinite(n.value[2])) {
    tela.setFont(&FreeSansBold12pt7b);
    drawRight(fmtNum(n.value[2], 1), W - 46, y + 67);
    tela.setFont(&FreeSans9pt7b);
    tela.setCursor(W - 42, y + 67);
    tela.print("hPa");
  }

  // --- terza riga: la giornata a sinistra, il barometro a destra -----------
  //
  // Fino a v37 qui c'era la freccia del trend seguita dalla sua parola ("in
  // lieve salita"). Le due cose dicono lo stesso, e la parola costava una riga
  // intera su un pannello che di righe ne ha otto: ora resta la freccia --
  // l'inclinazione si vede da tre metri, una parola va letta -- e accanto va
  // il NUMERO, che dice anche quanto. La parola non e' persa: sta nella pagina
  // dettaglio del nodo e nella web UI, dove c'e' spazio per leggerla.
  //
  // Lo spazio liberato prende la barra del giorno, che e' il pezzo nuovo.
  float tMin, tMax, tDelta;
  const int campioni = statTemp(indice, &tMin, &tMax, &tDelta);

  // Le coordinate qui sotto NON sono a occhio: sono state verificate con le
  // metriche vere dei font (somma degli xAdvance in FreeSans9pt7b.h, cioe' lo
  // stesso conto che fa getTextBounds). Il caso peggiore non e' quello di oggi
  // ma l'INVERNO: "-10,5" sono 41 px contro i 28 di "21,4", e con le posizioni
  // stimate a occhio il minimo finiva sotto la barra. Un testo troppo largo su
  // questo pannello non da' errore -- si sovrappone e basta, e lo si scopre
  // guardando il vetro tre mesi dopo.
  tela.setFont(&FreeSans9pt7b);
  tela.setCursor(12, y + 113);
  tela.print("24h");                       // 30 px: arriva a 42

  if (campioni >= 2 && isfinite(tMin) && isfinite(tMax))
  {
    tela.setCursor(46, y + 113);           // fino a 87 con un minimo negativo
    tela.print(fmtNum(tMin, 1));
    drawRangeGiorno(92, y + 108, 124, tMin, tMax, n.value[0]);
    tela.setCursor(224, y + 113);          // fino a 265, la freccia parte a 274
    tela.print(fmtNum(tMax, 1));
  }
  else
  {
    // "Non lo so ancora" non deve somigliare a "escursione nulla": una barra
    // con il cursore in mezzo direbbe una cosa falsa. Meglio dirlo a parole,
    // che tanto e' una condizione che dura una mezz'ora dopo il riavvio.
    tela.setCursor(46, y + 113);
    tela.print("in raccolta");
  }

  // Il barometro: freccia piu' variazione a 3 ore. Il delta e' quello della
  // PRESSIONE (n.delta3h), coerente con la freccia che gli sta accanto -- non
  // quello della temperatura, che vive nella barra qui a sinistra.
  //
  // "/3h" e non " hPa/3h": con l'unita' per esteso il caso peggiore
  // ("+12,3 hPa/3h") e' 108 px e finisce SOTTO la freccia. L'unita' e' gia'
  // scritta sopra, nella stessa colonna, accanto alla pressione.
  drawFrecciaTrend(285, y + 108, n.trend);
  if (isfinite(n.delta3h)) {
    tela.setFont(&FreeSans9pt7b);
    drawRight(fmtDelta(n.delta3h, 1) + "/3h", 388, y + 113);   // max 71 px
  }
}

// Qui finiva, fino a v24, una riga con rugiada, min/max, delta a 3 ore,
// umidita' assoluta e percepiti. Era troppa roba: otto numeri per nodo in 116
// px si leggono uno per volta, cioe' non si leggono. Ora quei valori stanno
// nella pagina DETTAGLIO, che ne ha 300 di px e li puo' incolonnare.
//
// E' la regola gia' scritta per la fascia del messaggio e per il grafico: su
// e-ink il tempo e' la dimensione in piu', e per vedere tutto c'e' la
// rotazione -- pagine intere e leggibili invece di una compressa.

// --- blocco COMPATTO: da tre nodi in su -----------------------------------
static void drawNodoCompatto(const RemoteNode& n, int16_t y)
{
  const int16_t W = tela.width();
  drawTestataNodo(n, y, 23);

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
// L'ora in intestazione e' quella dell'ULTIMO AGGIORNAMENTO, non "adesso".
// Senza il refresh al minuto le due cose divergono fino a dieci minuti, e un
// orologio fermo che si spaccia per corrente e' peggio di nessun orologio:
// e' la stessa ragione per cui i nodi mostrano l'ora del loro ultimo
// pacchetto invece di "38 s fa".
static void screenNodi(bool full)
{
  const int16_t W = tela.width();
  const int16_t H = tela.height();

  {
    tela.fillScreen(GxEPD_WHITE);
    tela.setTextColor(GxEPD_BLACK);

    // Niente intestazione (da v36). "STAZIONE METEO" occupava 38 px su 300 --
    // il 13% della pagina -- per dire una cosa che non cambia mai e che chi
    // guarda il pannello sa gia'. L'ora dell'ultimo aggiornamento e' scesa nel
    // piede, dove c'era spazio avanzato. Quei 38 px vanno ai nodi, che sono il
    // motivo per cui la pagina esiste.

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
      const bool comodo = nodiLayoutComodo(quanti, msgFascia);
      const int16_t h   = (yBot - NODI_TOP) / quanti;

      for (int i = 0; i < quanti; i++)
      {
        RemoteNode nodo;
        if (!remote_get(i, &nodo)) continue;
        const int16_t y = NODI_TOP + (int16_t)i * h;

        // L'indice serve solo al blocco comodo, che con statTemp() legge
        // l'anello delle 24 h. E' una lettura immediata, consumata dentro
        // questo giro: non e' lo stesso caso di s_ritardo, che era stato che
        // SOPRAVVIVE fra una chiamata e l'altra e per questo va tenuto per MAC.
        if (comodo) drawNodoComodo(nodo, y, i);
        else        drawNodoCompatto(nodo, y);

        // Niente separatore fra un nodo e l'altro: ogni blocco si apre con la
        // sua testata, che porta gia' il filetto sotto il nome. Due separatori
        // a poche righe di distanza farebbero solo sporco. (Fino a v37 la
        // testata era una barra nera piena; da v38 e' nome piu' filetto -- vedi
        // drawTestataNodo per il perche'.)
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
      // hasData nel conto, da v38: `online` e' falso anche per un nodo che non
      // ha ANCORA parlato, quindi dopo ogni riavvio dell'hub il piede scriveva
      // "2 muto" mentre il corpo della pagina diceva correttamente "in attesa
      // del primo dato" -- la stessa pagina si contraddiceva, e l'unico allarme
      // che questa rete ha suonava a vuoto per qualche minuto ad ogni OTA.
      if (remote_get(i, &nodo) && nodo.hasData && !nodo.online) muti++;
    }
    // --- prima si decide COSA va a destra, poi quanto spazio resta a sinistra.
    //
    // L'ordine e' quello dell'urgenza: cosa sta succedendo adesso batte quanto
    // spazio c'e' sulla card.
    //
    // Fino a v38 la riga di sinistra si regolava su una riserva FISSA di 96 px,
    // che e' la larghezza esatta di "SD 14,6 GB" -- cioe' zero margine, e sul
    // vetro si leggeva "agg. ~11:09SD 14,6 GB", attaccati. Gli altri tre casi
    // stavano molto peggio: "SD NON MONTATA" e' 163 px, quindi l'avviso piu'
    // importante che questo pannello sappia dare finiva SOTTO il piede, di 65
    // px. Il guasto piu' silenzioso della scheda annunciato da una scritta
    // illeggibile: la riserva a occhio si paga sempre nel caso peggiore.
    String destra;
    bool   destraNegativo = false;
    if (remote_pairing_active()) {
      const uint32_t r = remote_pairing_remaining_s();
      char buf[24];
      snprintf(buf, sizeof(buf), "ASSOCIAZIONE %lu:%02lu",
               (unsigned long)(r / 60), (unsigned long)(r % 60));
      destra = buf;
    }
    else if (!remote_ready()) {
      destra = "ESP-NOW NON ATTIVO";
    }
    else if (!sd_mounted()) {
      // In negativo: senza card i DATA non li registra nessuno, ed e' il
      // guasto piu' silenzioso che questa scheda possa avere - tutto il resto
      // continua a funzionare come se niente fosse.
      destra         = "SD NON MONTATA";
      destraNegativo = true;
    }
    else {
      // In GB e non in MB: "14,9 GB" si legge meglio di "14900 MB" e occupa
      // meno, che in questo piede e' diventato il vincolo.
      // fmtNum() e non snprintf: mette la VIRGOLA decimale come tutto il
      // resto della pagina. Con "%.1f" usciva "14.6 GB" accanto a "27,4 C",
      // due convenzioni diverse a tre centimetri di distanza.
      destra = "SD " + fmtNum(sd_free_mb() / 1024.0f, 1) + " GB";
    }

    int16_t dbx, dby; uint16_t dbw, dbh;
    tela.getTextBounds(destra, 0, 0, &dbx, &dby, &dbw, &dbh);
    const int16_t xDestra = W - (destraNegativo ? 16 : 12);   // bordo destro
    // 12 px di respiro fra le due righe: sotto quella soglia si leggono come
    // una parola sola, che e' il difetto appena tolto.
    const int16_t limiteSx = xDestra - (int16_t)dbw - (destraNegativo ? 6 : 0) - 12;

    // --- la riga di sinistra, in tre versioni: si prende la prima che ci sta.
    // L'ordine E' la priorita', come in drawFila(): si sacrifica l'ornamento,
    // mai il dato. L'ora se ne va per prima (la si legge nella web UI), poi
    // l'IP -- che con la card smontata interessa molto meno del perche'.
    // Il conteggio si scrive solo quando DICE qualcosa. Con due nodi in
    // elenco e due blocchi disegnati qui sopra, "2 nodi" e' la stessa
    // ridondanza per cui in v36 e' sparita l'intestazione "STAZIONE METEO":
    // occupa spazio per confermare cio' che si sta gia' guardando. Se invece
    // qualcuno tace, o non c'e' stato posto per tutti, allora il numero e'
    // l'unico posto dove quell'informazione esiste e va scritto.
    //
    // I 44 px risparmiati sono esattamente quelli che servono all'ora
    // dell'ultimo aggiornamento, che altrimenti non ci sarebbe entrata.
    String base;
    if (muti > 0 || n > NODI_VISIBILI) {
      base = String(n) + (n == 1 ? " nodo" : " nodi");
      if (muti > 0)          base += String(", ") + muti + " muto";
      if (n > NODI_VISIBILI) base += String(" (+") + (n - NODI_VISIBILI) + " non mostrati)";
      base += "   ";
    }

    // La spaziatura la porta gia' `base` (che finisce con tre spazi quando c'e'
    // qualcosa): senza questo, con il conteggio taciuto la riga partirebbe con
    // tre spazi vuoti e l'IP risulterebbe scostato dal margine rispetto a tutte
    // le altre righe della pagina.
    const String conIp = base + (net_isConnected()
                                 ? WiFi.localIP().toString()
                                 : String("WiFi assente"));
    String conOra = conIp;
    {
      char ora[8] = "";
      if (rtctime_format(rtctime_now(), "%H:%M", ora, sizeof(ora)))
        conOra = conIp + "   agg. " + (rtctime_isSynced() ? "" : "~") + ora;
    }

    // L'ultimo gradino tiene SOLO l'allarme: con la card smontata e otto nodi
    // di cui due muti, la riga completa non entra accanto a "SD NON MONTATA"
    // (163 px) -- e in quel caso l'IP e il totale dei nodi valgono meno del
    // fatto che due tacciono. Si scende fin qui solo quando c'e' un allarme:
    // nel caso normale `base` e' vuota e la riga con IP e ora entra sempre.
    String corta;
    if (muti > 0)               corta = String(muti) + " muto";
    else if (n > NODI_VISIBILI) corta = String("+") + (n - NODI_VISIBILI) + " non mostrati";

    String baseSola = base;
    baseSola.trim();                           // via i tre spazi di giunzione

    String piede = corta;                      // l'ultima spiaggia
    const String candidati[] = { conOra, conIp, baseSola, corta };
    for (int i = 0; i < 4; i++) {
      int16_t bx, by; uint16_t bw, bh;
      tela.getTextBounds(candidati[i], 0, 0, &bx, &by, &bw, &bh);
      if (12 + (int16_t)bw <= limiteSx) { piede = candidati[i]; break; }
    }

    tela.setCursor(12, 288);
    tela.print(piede);

    if (destraNegativo) {
      // Il riquadro si dimensiona sul testo misurato, non su un 118 fisso: un
      // avviso piu' lungo sporgerebbe dal nero e si leggerebbe meta' bianco su
      // nero e meta' nero su bianco.
      tela.fillRect(xDestra - (int16_t)dbw - 6, 274, (int16_t)dbw + 12, 18, GxEPD_BLACK);
      tela.setTextColor(GxEPD_WHITE);
      drawRight(destra, xDestra, 288);
      tela.setTextColor(GxEPD_BLACK);
    }
    else {
      drawRight(destra, xDestra, 288);
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

// Il seeding di un nodo da un giorno di CSV. Il parser sta in sd_logger
// (sd_read_remote_day): era copiato qui e nel riepilogo, e una colonna
// aggiunta un domani avrebbe dovuto essere ricordata in due punti.
struct SeedCtx { const RemoteNode* n; time_t minTs; int* righe; };

static void seedRiga(time_t ts, uint32_t seq, const float v[3], void* arg)
{
  (void)seq;
  SeedCtx* c = (SeedCtx*)arg;
  if (ts < c->minTs) return;

  // Temperatura e pressione vanno in due storici diversi: 24 h a mezz'ora per
  // il disegno, 3 h a dieci minuti per il trend.
  if (isfinite(v[0])) remote_seed_temp(c->n->mac, ts, v[0]);
  if (!isfinite(v[2])) return;
  remote_seed_pressure(c->n->mac, ts, v[2]);
  (*c->righe)++;
}

static void seedNodoDaCsv(const RemoteNode* n, const char* giorno, time_t minTs, int* righe)
{
  // Solo la CODA del file. Da quando c'e' anche il grafico a 24 h la finestra
  // e' un giorno intero: 128 kB coprono ~26 h del nodo piu' veloce (60 s,
  // ~80 byte a riga). Oltre non si va - leggere due file interi per nodo
  // bloccherebbe loop(), e con lui web server, OTA e raccolta dei DATA.
  SeedCtx c = { n, minTs, righe };
  sd_read_remote_day(n->nome, giorno, seedRiga, &c, 131072);
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

// ---------------------------------------------------------------------------
// Riepilogo giornaliero - una riga per giorno chiuso, in /nodi/<NOME>/riepilogo.csv
// ---------------------------------------------------------------------------
// I CSV per giorno rispondono bene a "com'era ieri alle 15:40" e male a "che
// mese e' stato": ogni vista storica deve rileggerli interi, ed e' il motivo
// per cui il seeding legge solo la coda e il grafico si ferma a 24 h. Una riga
// per giorno sposta quel costo a fine giornata, una volta sola.
//
// NON E' UN TIMER A MEZZANOTTE, ed e' la scelta che rende la cosa affidabile:
// una riga prodotta da un timer sparisce per sempre se in quel minuto la
// scheda e' spenta, sta aggiornandosi o e' appena ripartita - e l'assenza di
// una riga non somiglia a un guasto, quindi nessuno se ne accorgerebbe. Qui
// invece si guarda quali giorni CHIUSI non hanno ancora la loro riga e si
// recuperano in ordine: idempotente per costruzione.
//
// UN GIORNO PER GIRO, e a turno fra i nodi. Leggere e aggregare un CSV sono
// ~23 kB dalla card; farne diciotto di fila (il backfill iniziale) terrebbe
// fermo il loop() per una decina di secondi, e con lui il WebServer, l'OTA e
// il prelievo dei DATA dal driver ESP-NOW, che ne tiene UNO solo per nodo. E'
// la stessa regola del WELCOME uno per giro della v44: il lavoro lungo si
// spalma, e spalmarlo non ritarda niente perche' i giorni chiusi non scappano.
static int      s_riepNodo    = 0;      // da quale nodo riprendere il giro
static bool     s_riepFatto   = false;  // niente piu' da recuperare, per ora
static uint32_t s_riepScritti = 0;
static char     s_riepGiornoUlt[11] = "";   // per accorgersi del cambio giorno

// Legge il CSV di un giorno e ne produce gli aggregati. Stesso parser di
// seedNodoDaCsv(), ma il file si legge TUTTO: qui non si ricostruisce una
// finestra recente, si chiude una giornata, e una coda troncata darebbe un
// minimo calcolato su mezzo pomeriggio senza dirlo.
static void riepRiga(time_t ts, uint32_t seq, const float v[3], void* arg)
{
  daily_add(*(daily_t*)arg, ts, seq, v[0], v[1], v[2]);
}

// Legge il CSV di un giorno e ne produce gli aggregati. Il file si legge
// TUTTO (coda 0): qui non si ricostruisce una finestra recente, si chiude una
// giornata, e una coda troncata darebbe un minimo calcolato su mezzo
// pomeriggio senza dirlo.
static bool riepilogoDaCsv(const RemoteNode* n, const char* giorno, daily_t& d)
{
  daily_reset(d);
  sd_read_remote_day(n->nome, giorno, riepRiga, &d, 0);
  return d.campioni > 0;
}

// Un float in un campo CSV: non finito diventa VUOTO, mai uno zero.
static void riepCampo(String& r, float v, int dec)
{
  r += ',';
  if (isfinite(v)) r += String(v, dec);
}

static void riepOra(String& r, time_t ts)
{
  r += ',';
  if (ts != 0) {
    char hhmm[6];
    rtctime_format(ts, "%H:%M", hhmm, sizeof(hhmm));
    r += hhmm;
  }
}

struct RiepScelta { char giorno[11]; };
static void riepPrimoGiorno(const char* iso, size_t bytes, void* arg)
{
  (void)bytes;
  RiepScelta* sc = (RiepScelta*)arg;
  if (sc->giorno[0] == '\0') strlcpy(sc->giorno, iso, sizeof(sc->giorno));
}

// Il prossimo giorno da chiudere per questo nodo. false se non ce n'e'.
static bool riepProssimoGiorno(const RemoteNode* n, const char* oggi,
                               char* out, size_t cap)
{
  char ultimo[11];
  if (sd_riep_ultimo_giorno(n->nome, ultimo, sizeof(ultimo)))
  {
    // Il giorno dopo l'ultimo gia' fatto, calcolato sull'epoch e non sulle
    // cifre: al 31 non segue il 32, e i mesi hanno lunghezze diverse.
    struct tm tmv; memset(&tmv, 0, sizeof(tmv));
    int aa = 0, mm = 0, gg = 0;
    if (sscanf(ultimo, "%4d-%2d-%2d", &aa, &mm, &gg) != 3) return false;
    tmv.tm_year = aa - 1900; tmv.tm_mon = mm - 1; tmv.tm_mday = gg;
    tmv.tm_hour = 12;              // mezzogiorno: immune ai salti dell'ora legale
    tmv.tm_isdst = -1;
    const time_t dopo = mktime(&tmv) + 24 * 3600;
    rtctime_format(dopo, "%Y-%m-%d", out, cap);
  }
  else
  {
    // Nessun riepilogo ancora: si parte dal primo CSV che il nodo ha.
    RiepScelta sc; sc.giorno[0] = '\0';
    sd_list_remote_days(n->nome, riepPrimoGiorno, &sc, 400);
    if (sc.giorno[0] == '\0') return false;
    strlcpy(out, sc.giorno, cap);
  }

  // Il giorno IN CORSO non si chiude: non e' finito, e una riga scritta
  // adesso resterebbe per sempre con i dati di mezza giornata.
  return strcmp(out, oggi) < 0;
}

static void riepilogoTick()
{
  if (!rtctime_isSynced() || !sd_mounted() || remote_count() == 0) return;

  char oggi[11];
  rtctime_format(rtctime_now(), "%Y-%m-%d", oggi, sizeof(oggi));

  // Cambiato giorno: c'e' una giornata nuova da chiudere, si riapre la caccia.
  // E' QUESTO il "dopo mezzanotte", senza nessun timer da azzeccare.
  if (strcmp(oggi, s_riepGiornoUlt) != 0)
  {
    strlcpy(s_riepGiornoUlt, oggi, sizeof(s_riepGiornoUlt));
    s_riepFatto = false;
  }
  if (s_riepFatto) return;

  // Un nodo per giro, ripartendo da dove si era rimasti: fermarsi sempre sul
  // primo lo farebbe avanzare da solo affamando gli altri.
  for (int giro = 0; giro < remote_count(); giro++)
  {
    const int i = (s_riepNodo + giro) % remote_count();
    RemoteNode n;
    if (!remote_get(i, &n)) continue;

    char giorno[11];
    if (!riepProssimoGiorno(&n, oggi, giorno, sizeof(giorno))) continue;

    s_riepNodo = (i + 1) % remote_count();

    daily_t d;
    if (!riepilogoDaCsv(&n, giorno, d))
    {
      // Nessun campione per quel giorno (il nodo taceva, o il file non c'e'):
      // si scrive lo stesso una riga a zero campioni, o quel giorno resterebbe
      // "da fare" per sempre e bloccherebbe tutti quelli dopo di lui.
      String vuota = String(giorno) + ",0,0,0,,0,,,,,,,,,,,,,,,";
      if (sd_riep_append(n.nome, vuota.c_str())) s_riepScritti++;
      else s_riepFatto = true;
      return;
    }

    // La cadenza si ricava DAL GIORNO STESSO, non da quella appresa dall'hub:
    // quella e' la cadenza di adesso, e il riepilogo di un giorno passato
    // dev'essere autosufficiente. Su questa stazione la differenza e' enorme
    // e non teorica — il nodo a muro stava a 60 s fino al 26/08 e a 300 s
    // dopo, quindi i 299 s appresi oggi darebbero al 28/08 una completezza
    // del 497 %. Se il giorno non ha abbastanza campioni per dirlo, `attesi`
    // resta 0 e la completezza esce VUOTA: e' la verita', non un 100 % finto.
    const uint32_t cadenza = daily_cadenza_s(d);
    const uint32_t attesi  = daily_attesi(cadenza, 24UL * 3600UL);
    const float complPct  = daily_completezza_pct(d.campioni, attesi);

    String r(giorno);
    r += ','; r += d.campioni;
    r += ','; r += attesi;
    r += ','; r += cadenza;      // la base del conto, o la completezza
                                 // sarebbe un numero non verificabile
    riepCampo(r, complPct, 1);
    r += ','; r += d.buchi;

    riepCampo(r, d.t.minimo, 2);  riepOra(r, d.t.oraMin);
    riepCampo(r, d.t.massimo, 2); riepOra(r, d.t.oraMax);
    riepCampo(r, daily_media(d.t), 2);

    riepCampo(r, d.h.minimo, 1);  riepCampo(r, d.h.massimo, 1);
    riepCampo(r, daily_media(d.h), 1);

    riepCampo(r, d.p.minimo, 2);  riepCampo(r, d.p.massimo, 2);
    riepCampo(r, daily_media(d.p), 2);
    riepCampo(r, daily_p_var(d), 2);

    riepCampo(r, d.td.minimo, 2); riepCampo(r, d.td.massimo, 2);
    riepCampo(r, daily_media(d.td), 2);

    if (sd_riep_append(n.nome, r.c_str()))
    {
      s_riepScritti++;
      Serial.printf("[riepilogo] %s %s: %lu campioni su %lu attesi, %lu buchi\n",
                    n.nome, giorno, (unsigned long)d.campioni,
                    (unsigned long)attesi, (unsigned long)d.buchi);
    }
    else
    {
      // Scrittura non arrivata sulla card: NON si segna il giorno come fatto
      // (non lo e'), e si smette di provare fino al prossimo cambio giorno -
      // ritentare subito su una card che rifiuta e' solo un modo per rifarlo
      // mille volte al secondo.
      evento("riepilogo", "scrittura fallita, riprovo piu' tardi");
      s_riepFatto = true;
    }
    return;   // UN giorno per giro
  }

  s_riepFatto = true;   // niente da recuperare fino al prossimo cambio giorno
}

uint32_t app_riepiloghi_scritti() { return s_riepScritti; }

// Rifa' da zero il riepilogo di un nodo (o di tutti, con nodo == nullptr).
// Cancella il file e riapre la caccia: i giorni si ricalcolano uno per giro,
// come il backfill iniziale.
bool app_riepilogo_ricalcola(const char* nodo)
{
  if (!sd_mounted()) return false;
  bool almenoUno = false;
  for (int i = 0; i < remote_count(); i++)
  {
    RemoteNode n;
    if (!remote_get(i, &n)) continue;
    if (nodo && *nodo && strcmp(nodo, n.nome) != 0) continue;
    if (sd_riep_azzera(n.nome)) almenoUno = true;
  }
  if (almenoUno) { s_riepFatto = false; s_riepNodo = 0; }
  return almenoUno;
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

// Il pannello e' fermo perche' SOSPESO o perche' bloccato? Da fuori le due
// cose si somigliano - il display non cambia - e senza questo campo l'unico
// modo di distinguerle sarebbe guardare l'ora e fidarsi.
bool app_pannello_sospeso()        { return s_inSilenzio; }
const char* app_silenzio_immagine() { return s_silImgNome; }
uint32_t app_refresh_evitati()     { return s_nodiInvariati; }

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

// ---------------------------------------------------------------------------
// Il diario degli eventi
// ---------------------------------------------------------------------------
// UNA RIGA PER TRANSIZIONE, mai una per campione: se ci finisse dentro ogni
// pacchetto diventerebbe illeggibile, e un diario che non si legge non serve a
// niente. Il perche' lungo sta in sd_logger.h.
//
// IL TETTO NON E' UN DI PIU'. Un nodo che oscilla fra online e muto
// scriverebbe due righe per oscillazione, e l'oscillazione capita davvero (e'
// il difetto che ha richiesto RITARDO_CONFERMA_MS). Oltre EV_MAX_ORA righe
// l'ora per tipo si smette di scrivere e si CONTA: alla fine della finestra
// esce una riga sola che dice quante ne sono state soppresse. Mai un silenzio.
static const uint8_t  EV_MAX_ORA   = 10;
static const uint32_t EV_FINESTRA_MS = 3600000UL;

struct EvTetto {
  const char* tipo;         // confronto per PUNTATORE: sono letterali costanti
  uint32_t    finestraDa;
  uint8_t     scritti;
  uint16_t    soppressi;
};
static EvTetto s_evTetto[8] = {};

// Il boot capita PRIMA del primo sync NTP, quindi non si puo' datare quando
// succede: si tiene da parte e si scrive appena l'orologio e' vero, portandosi
// dietro quanti secondi erano passati. Una riga datata con l'ora di
// compilazione -- identica ad ogni riavvio -- non e' un dato salvato, e' un
// dato falsificato.
static bool s_evBootDaScrivere = true;

// Il guasto della card e' gia' segnalato? Serve a scrivere una riga sola per
// serie, e a riarmarla quando torna a scrivere.
static bool s_sdKoSegnalato = false;

static void eventoScrivi(const char* tipo, const char* dettaglio)
{
  if (!sd_log_evento(tipo, dettaglio)) return;
  Serial.printf("[diario] %s: %s\n", tipo, dettaglio ? dettaglio : "");
}

static void evento(const char* tipo, const char* dettaglio)
{
  if (!orario_registrabile()) return;

  EvTetto* t = nullptr;
  for (size_t i = 0; i < sizeof(s_evTetto) / sizeof(s_evTetto[0]); i++) {
    if (s_evTetto[i].tipo == tipo) { t = &s_evTetto[i]; break; }
    if (s_evTetto[i].tipo == nullptr && t == nullptr) t = &s_evTetto[i];
  }
  if (t == nullptr) { eventoScrivi(tipo, dettaglio); return; }   // tabella piena: si scrive

  if (t->tipo != tipo) { t->tipo = tipo; t->finestraDa = millis(); t->scritti = 0; t->soppressi = 0; }

  if (millis() - t->finestraDa >= EV_FINESTRA_MS) {
    if (t->soppressi > 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%u righe soppresse nell'ora precedente", (unsigned)t->soppressi);
      eventoScrivi(tipo, buf);
    }
    t->finestraDa = millis();
    t->scritti = 0;
    t->soppressi = 0;
  }

  if (t->scritti < EV_MAX_ORA) { t->scritti++; eventoScrivi(tipo, dettaglio); }
  else if (t->soppressi < 0xFFFF) t->soppressi++;
}

// Nodi che tacciono e nodi che tornano. Lo stato precedente si tiene PER MAC,
// non per posizione: remote_forget() compatta il registro, e un array
// parallelo indicizzato per indice si disallineerebbe (stessa lezione di
// s_ritardo, e stessa ragione per cui remote_forget() compatta i suoi).
struct EvNodo { uint8_t mac[6]; bool usato; bool online; time_t muto_da; uint32_t persi_da; };
static EvNodo s_evNodi[REMOTE_MAX_NODES] = {};

static void diarioNodi()
{
  for (int i = 0; i < remote_count(); i++) {
    RemoteNode n;
    if (!remote_get(i, &n) || !n.hasData) continue;

    EvNodo* e = nullptr;
    for (int k = 0; k < REMOTE_MAX_NODES; k++) {
      if (s_evNodi[k].usato && memcmp(s_evNodi[k].mac, n.mac, 6) == 0) { e = &s_evNodi[k]; break; }
      if (!s_evNodi[k].usato && e == nullptr) e = &s_evNodi[k];
    }
    if (e == nullptr) continue;

    if (!e->usato) {
      // Primo incontro: si prende nota dello stato senza scrivere niente. Un
      // "e' online" all'avvio non e' una transizione, e riempirebbe il diario
      // di righe ad ogni riavvio.
      e->usato = true;
      memcpy(e->mac, n.mac, 6);
      e->online = n.online;
      continue;
    }
    if (e->online == n.online) continue;

    char buf[96];
    if (!n.online) {
      snprintf(buf, sizeof(buf), "%s muto da %lu s (soglia %lu)",
               n.nome, (unsigned long)n.silenzioS, (unsigned long)n.sogliaMutoS);
      e->muto_da  = rtctime_now();
      e->persi_da = n.persi;
      evento("nodo_muto", buf);
    } else {
      const uint32_t quanto = (e->muto_da > 0) ? (uint32_t)(rtctime_now() - e->muto_da) : 0;
      snprintf(buf, sizeof(buf), "%s torna dopo %lu s, %lu persi nel frattempo",
               n.nome, (unsigned long)quanto, (unsigned long)(n.persi - e->persi_da));
      evento("nodo_torna", buf);
    }
    e->online = n.online;
  }
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
    // Tornata a scrivere: si riarma la segnalazione, cosi' il prossimo guasto
    // si vede anche se questo si era risolto da solo.
    if (s_sdKoSegnalato) {
      s_sdKoSegnalato = false;
      evento("sd_ok", "la card ha ripreso a scrivere");
    }
  }
  else
  {
    // Da quando sd_log_remote() guarda il ritorno delle print(), questo ramo
    // distingue "card che ha rifiutato" da "riga scritta": prima la funzione
    // rispondeva sempre true e il contatore saliva comunque.
    s_scrittureKo++;

    // Solo la PRIMA di una serie: una card piena rifiuta OGNI riga, e una riga
    // di diario per ogni rifiuto sarebbe il log che questo file non vuole
    // essere. Il conto completo sta in /api/salute.
    if (!s_sdKoSegnalato) {
      s_sdKoSegnalato = true;
      evento("sd_errore", sd_last_error());
    }
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
// Pagina DETTAGLIO — tutto quello che si sa di UN nodo
// ---------------------------------------------------------------------------
// Nasce da un difetto della pagina nodi: i valori derivati (rugiada, min/max,
// percepiti, acqua nell'aria) erano finiti tutti li' dentro, e otto numeri in
// 116 px non si leggono da tre metri -- si decifrano da vicino, uno per volta.
//
// Qui invece c'e' una pagina intera per un nodo solo, quindi ogni valore ha la
// sua riga, l'etichetta a sinistra e il numero incolonnato a destra. Si legge
// come una tabella, che e' esattamente quello che e'.
//
// Il nodo si indica per NOME (il param della pagina) e non per indice: gli
// indici si spostano quando un nodo viene dimenticato, e la pagina finirebbe
// per mostrare un altro nodo senza dirlo.
// L'unita' viaggia separata dal valore perche' va disegnata in corpo piu'
// piccolo e, quando sono gradi, con il cerchietto vero al posto della lettera
// -- i font Adafruit_GFX sono ASCII puro e il grado non ce l'hanno. Scriverlo
// come " C" era l'unico punto della pagina dove un'unita' non somigliava a se
// stessa.
static void drawRigaDett(const String& etichetta, const String& valore,
                         const String& unita, int16_t y)
{
  const int16_t W = tela.width();
  tela.setFont(&FreeSans9pt7b);
  tela.setCursor(18, y);
  tela.print(etichetta);

  const bool gradi = (unita == "C");
  int16_t bx, by; uint16_t bw, bh;
  int16_t xFine = W - 18;

  if (gradi) {
    tela.setFont(&FreeSans9pt7b);
    tela.getTextBounds("C", 0, 0, &bx, &by, &bw, &bh);
    tela.setCursor(xFine - (int16_t)bw, y + 2);
    tela.print("C");
    xFine -= (int16_t)bw + 4;
    drawGrado(xFine, y - 6, 3);
    xFine -= 8;
  } else if (unita.length()) {
    tela.setFont(&FreeSans9pt7b);
    tela.getTextBounds(unita, 0, 0, &bx, &by, &bw, &bh);
    tela.setCursor(xFine - (int16_t)bw, y + 2);
    tela.print(unita);
    xFine -= (int16_t)bw + 6;
  }

  tela.setFont(&FreeSansBold12pt7b);
  drawRight(valore, xFine, y + 2);
}

static void screenDettaglio(const char* nomeNodo)
{
  const int16_t W = tela.width();
  tela.fillScreen(GxEPD_WHITE);
  tela.setTextColor(GxEPD_BLACK);

  RemoteNode n;
  int indice = -1;
  for (int i = 0; i < remote_count(); i++) {
    RemoteNode t;
    if (!remote_get(i, &t)) continue;
    if (strncmp(t.nome, nomeNodo, sizeof(t.nome)) == 0) { n = t; indice = i; break; }
  }

  if (indice < 0) {
    tela.setFont(&FreeSansBold18pt7b);
    drawCenter("NODO NON IN ELENCO", W / 2, 140);
    tela.setFont(&FreeSans9pt7b);
    drawCenter(String(nomeNodo), W / 2, 170);
    drawCenter("e' stato dimenticato, o non si e' mai presentato", W / 2, 192);
    telaSulPannello(true);
    return;
  }

  drawTestataNodo(n, 0, 26);

  if (!n.hasData) {
    tela.setFont(&FreeSans9pt7b);
    drawCenter("in attesa del primo dato", W / 2, 150);
    telaSulPannello(true);
    return;
  }

  // Le due misure principali, in grande: chi guarda da lontano deve poterle
  // leggere anche da questa pagina, non solo da quella dei nodi.
  if (isfinite(n.value[0])) {
    tela.drawBitmap(14, 44, IC_TERMOMETRO, IC_TERMOMETRO_W, IC_TERMOMETRO_H, GxEPD_BLACK);
    tela.setFont(&FreeSansBold24pt7b);
    tela.setCursor(40, 72);
    tela.print(fmtNum(n.value[0], 1));
    int16_t bx, by; uint16_t bw, bh;
    tela.getTextBounds(fmtNum(n.value[0], 1), 40, 72, &bx, &by, &bw, &bh);
    drawGrado(40 + (int16_t)bw + 10, 48, 4);
    tela.setFont(&FreeSansBold12pt7b);
    tela.setCursor(40 + (int16_t)bw + 17, 72);
    tela.print("C");
  }
  if (isfinite(n.value[1])) {
    tela.drawBitmap(228, 46, IC_GOCCIA, IC_GOCCIA_W, IC_GOCCIA_H, GxEPD_BLACK);
    tela.setFont(&FreeSansBold24pt7b);
    tela.setCursor(256, 72);
    tela.print(fmtNum(n.value[1], 0) + "%");
  }
  tela.drawFastHLine(14, 88, W - 28, GxEPD_BLACK);

  // La tabella. L'ordine e' quello dell'utilita': prima cosa si sente, poi
  // dove e' stata la temperatura, poi la pressione col suo trend.
  int16_t y = 116;
  const float td = meteo_dewpoint_c(n.value[0], n.value[1]);
  const float hx = meteo_humidex_c(n.value[0], n.value[1]);
  const float ah = meteo_umidita_assoluta_gm3(n.value[0], n.value[1]);

  if (isfinite(td)) { drawRigaDett("punto di rugiada", fmtNum(td, 1), "C", y); y += 26; }
  // Sotto i 20 gradi l'humidex non esiste: la riga non compare affatto,
  // invece di mostrare un trattino che occupa spazio per dire niente.
  if (isfinite(hx)) { drawRigaDett("temperatura percepita", fmtNum(hx, 0), "C", y); y += 26; }
  if (isfinite(ah)) { drawRigaDett("acqua nell'aria", fmtNum(ah, 1), "g/m3", y); y += 26; }

  float tMin, tMax, tD3;
  const int nCamp = statTemp(indice, &tMin, &tMax, &tD3);
  if (nCamp > 0) {
    drawRigaDett("ultime 24 ore", fmtNum(tMin, 1) + " / " + fmtNum(tMax, 1), "C", y);
    y += 26;
  }
  if (isfinite(tD3)) { drawRigaDett("ultime 3 ore", fmtDelta(tD3, 1), "C", y); y += 26; }

  // La pressione in fondo, con il trend accanto: sono la stessa informazione
  // letta in due modi, e separarle vorrebbe dire farle cercare due volte.
  tela.drawFastHLine(14, 246, W - 28, GxEPD_BLACK);
  if (isfinite(n.value[2])) {
    tela.setFont(&FreeSansBold12pt7b);
    tela.setCursor(18, 272);
    tela.print(fmtNum(n.value[2], 1));
    tela.setFont(&FreeSans9pt7b);
    tela.print(" hPa");
  }
  drawFrecciaTrend(232, 266, n.trend);
  tela.setFont(&FreeSans9pt7b);
  tela.setCursor(250, 272);
  tela.print(n.trend == TREND_IGNOTO ? "raccolgo dati" : remote_trend_label(n.trend));

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
// Una pagina immagine a caso fra quelle in elenco, o PAG_SIL_NESSUNA se non
// ce n'e' nessuna. Si sorteggia ad ogni ingresso nella fascia, quindi la
// scelta cambia ogni notte.
//
// esp_random() e non random(): quest'ultima senza randomSeed() darebbe la
// STESSA sequenza ad ogni riavvio, cioe' la stessa immagine tutte le notti
// dopo ogni power-cycle -- che e' esattamente il difetto che si vorrebbe
// evitare, ma silenzioso.
// Un'immagine a caso fra TUTTE quelle sulla card, non solo fra le pagine in
// elenco: l'archivio non ha il tetto dei sedici slot, e per disegnarla non
// serve che sia una pagina -- screenImmagine() lavora su un nome di file.
//
// Due passate sulla directory invece di un elenco di nomi in RAM: quante
// immagini ci siano lo decide la card (15.000 byte l'una su 14,9 GB), e
// tenerne in memoria un numero fisso sarebbe un tetto arbitrario e MUTO --
// esattamente il difetto che sd_img_page() ha gia' tolto alla pagina web. La
// scansione costa due volte al giorno.
struct SceltaImg { char* out; size_t cap; };
static void sceltaImgCb(const char* nome, size_t, void* arg)
{
  SceltaImg* s = (SceltaImg*)arg;
  // Un nome piu' lungo del buffer NON si tronca: troncato comporrebbe il path
  // di un file che non esiste, e la notte si passerebbe con "immagine non
  // disponibile" sul pannello. Meglio lasciarlo vuoto e sorteggiarne un altro.
  // Non capita alle immagini caricate dalla pagina (sd_img_name_safe le tiene
  // entro IMG_NOME_MAX), capita a un file copiato a mano sulla card.
  if (strlen(nome) >= s->cap) { s->out[0] = '\0'; return; }
  strncpy(s->out, nome, s->cap - 1);
  s->out[s->cap - 1] = '\0';
}

static bool immagineACasoDallaCard(char* out, size_t cap, const char* evita)
{
  if (!out || cap == 0) return false;
  out[0] = '\0';

  // quante=0: non consegna niente, conta e basta -- la callback non viene mai
  // chiamata, quindi l'arg nullo qui e' sicuro.
  int totale = 0;
  sd_img_page(sceltaImgCb, nullptr, 0, 0, "", &totale);
  if (totale <= 0) return false;

  SceltaImg s = { out, cap };
  // Due tentativi: con poche immagini la stessa uscirebbe due notti di fila
  // abbastanza spesso, e un pannello che non e' cambiato somiglia a un
  // pannello fermo. Con una sola immagine sulla card non c'e' scelta, e
  // rifarla e' giusto: non e' una ripetizione, e' l'unica che c'e'.
  for (int giro = 0; giro < 2; giro++)
  {
    out[0] = '\0';
    sd_img_page(sceltaImgCb, &s, (int)(esp_random() % (uint32_t)totale), 1, "", nullptr);
    if (out[0] == '\0') continue;      // nome troppo lungo, o file sparito nel frattempo
    if (totale < 2 || evita == nullptr || strcmp(out, evita) != 0) return true;
  }
  return out[0] != '\0';
}

static uint8_t immagineACaso()
{
  uint8_t cand[PAGES_MAX];
  uint8_t n = 0;
  for (uint8_t i = 0; i < pages_slots(); i++) {
    const PageCfg* p = pages_get(i);
    if (p && p->usato && p->tipo == PT_IMMAGINE) cand[n++] = i;
  }
  if (n == 0) return PAG_SIL_NESSUNA;
  return cand[esp_random() % n];
}

// Ogni quanto ridisegnare la pagina dei nodi, dedotto dalla cadenza OSSERVATA
// dei nodi invece che da una costante scelta a tavolino.
//
// Si prende il nodo piu' LENTO, non il piu' veloce: la pagina li mostra tutti,
// e ridisegnarla al ritmo del piu' veloce significa riscrivere i numeri degli
// altri identici a se stessi. Su un pannello che si guarda passando, cinque
// minuti di ritardo non si notano; 288 refresh in piu' al giorno si'.
//
// Gli estremi restano: mai piu' spesso di NODI_MIN_MS (un e-ink lampeggia, e
// due minuti sono gia' piu' di quanto serva a chi passa davanti) e mai piu'
// raro di CADENZA_MAX_MS, o un nodo configurato a un'ora renderebbe il
// pannello vecchio di un'ora.
static const uint32_t CADENZA_MAX_MS = 600000UL;   // 10 minuti

static uint32_t cadenzaNodiMs()
{
  uint32_t lento = 0;
  for (int i = 0; i < remote_count(); i++) {
    RemoteNode n;
    if (!remote_get(i, &n)) continue;
    // Un nodo muto o appena visto non ha ancora una cadenza: escluderlo
    // evita che un intervallo a zero faccia collassare il conto sul minimo.
    if (!n.hasData || n.intervalloS == 0) continue;
    const uint32_t ms = n.intervalloS * 1000UL;
    if (ms > lento) lento = ms;
  }
  if (lento < NODI_MIN_MS)     lento = NODI_MIN_MS;
  if (lento > CADENZA_MAX_MS)  lento = CADENZA_MAX_MS;
  return lento;
}

// Una firma di TUTTO cio' che finisce sulla pagina dei nodi. Se non cambia,
// ridisegnare significherebbe riscrivere pixel identici: 2,2 s di lampeggio e
// un pezzo di vita del pannello per niente.
//
// Ci entrano i valori mostrati (arrotondati COME si mostrano: 27,25 e 27,24
// sono lo stesso "27,2" sul pannello), lo stato online/ritardo, il trend e il
// numero di nodi. NON ci entra l'ora: e' proprio per questo che l'ora non si
// mostra piu'.
// DUE firme, e la distinzione e' il punto: i VALORI possono aspettare la
// cadenza dei nodi (una temperatura vecchia di cinque minuti non ha fatto male
// a nessuno), lo STATO no. Se un nodo smette di rispondere, il pannello deve
// dirlo appena lo sa - se aspettasse la cadenza, l'avviso arriverebbe fino a
// cinque minuti dopo, e un avviso in ritardo e' peggio di nessun avviso.
//
// Trovato provando sul serio, staccando un nodo: il pannello e' rimasto per
// minuti a mostrare dati vecchi senza dire niente, perche' il risparmio di
// refresh lo aveva reso lento a reagire.
static void firmaMescola(uint32_t& f, int32_t v)
{
  for (int b = 0; b < 4; b++) { f ^= (uint8_t)(v >> (b * 8)); f *= 16777619u; }
}

// Quello che DEVE comparire subito: chi c'e', chi tace, chi e' in ritardo.
static uint32_t firmaStato()
{
  uint32_t f = 2166136261u;                       // FNV-1a
  firmaMescola(f, remote_count());
  firmaMescola(f, pages_fascia() ? 1 : 0);
  for (int i = 0; i < remote_count(); i++) {
    RemoteNode n;
    if (!remote_get(i, &n)) continue;
    firmaMescola(f, n.online ? 1 : 0);
    firmaMescola(f, nodoInRitardo(n) ? 1 : 0);
    firmaMescola(f, (int32_t)n.hasData);
    for (const char* c = n.nome; *c; c++) firmaMescola(f, (int32_t)(uint8_t)*c);
  }
  return f;
}

// Quello che puo' aspettare: i numeri, arrotondati COME si mostrano (27,25 e
// 27,24 sono lo stesso "27,2" sul pannello, quindi non sono un cambiamento).
// Un valore com'e' SCRITTO sul pannello: `dec` cifre dopo la virgola, e la
// sentinella per il non-finito (che si disegna come un trattino, quindi due
// NAN diversi sono lo stesso disegno).
static int32_t firmaComeScritto(float v, int dec)
{
  if (!isfinite(v)) return INT32_MIN;
  return (int32_t)lroundf(v * (dec == 0 ? 1.0f : 10.0f));
}

static uint32_t firmaValori()
{
  uint32_t f = 2166136261u;

  // Il layout cambia COSA si disegna, quindi entra anche nella firma: nel
  // blocco compatto c'e' la rugiada, in quello comodo no. Deciso dalla stessa
  // funzione che usa screenNodi(), o le due condizioni potrebbero divergere e
  // la firma smetterebbe di corrispondere alla pagina.
  const Message* m = pages_fascia() ? msg_active(time(nullptr)) : nullptr;
  const int n      = remote_count();
  const int quanti = (n < NODI_VISIBILI) ? n : NODI_VISIBILI;
  const bool comodo = nodiLayoutComodo(quanti, m);

  for (int i = 0; i < remote_count(); i++) {
    RemoteNode n;
    if (!remote_get(i, &n)) continue;

    // Ogni grandezza con le cifre CON CUI SI MOSTRA, non tutte a un decimale.
    // L'umidita' sul pannello e' un intero: fino a v40 stava in firma a 0,1 e
    // un 41,2 -> 41,3 faceva un refresh completo che non cambiava un pixel.
    // Era la voce che cambiava di piu' -- 601 volte su 771 pacchetti nelle 24 h
    // misurate il 2026-09-01 -- proprio perche' contava cifre invisibili.
    firmaMescola(f, firmaComeScritto(n.value[0], 1));   // temperatura: 26,5
    firmaMescola(f, firmaComeScritto(n.value[1], 0));   // umidita': 41%
    firmaMescola(f, firmaComeScritto(n.value[2], 1));   // pressione: 1013,9
    firmaMescola(f, (int32_t)n.trend);

    // La rugiada la disegna SOLO il blocco compatto, e ha un decimale: li'
    // dipende dall'umidita' in modo continuo, quindi arrotondarla a intero
    // nella firma perderebbe dei cambiamenti veri (41,2 e 41,7 danno lo stesso
    // "41%" ma due rugiade diverse). Fuori dal compatto non si disegna, e
    // metterla in firma sarebbe l'errore opposto: refresh per un numero che
    // non c'e'.
    if (!comodo)
      firmaMescola(f, firmaComeScritto(meteo_dewpoint_c(n.value[0], n.value[1]), 1));

    // Anche min, max e variazione a 3 ore, da v38: sono disegnati, quindi se
    // cambiano la pagina e' cambiata. Senza, il pannello resterebbe fermo
    // proprio nel caso che conta -- il massimo di ieri che esce dalla finestra
    // mentre il nodo trasmette lo stesso identico valore -- e mostrerebbe
    // un'escursione vecchia accanto a un numero giusto.
    // Min, max e variazione a 3 ore li disegna solo il blocco comodo, con un
    // decimale. Nel compatto non ci sono: stessa regola della rugiada, al
    // contrario.
    if (comodo) {
      float mn, mx, dl;
      statTemp(i, &mn, &mx, &dl);
      firmaMescola(f, firmaComeScritto(mn, 1));
      firmaMescola(f, firmaComeScritto(mx, 1));
      firmaMescola(f, firmaComeScritto(n.delta3h, 1));
    }
  }
  if (m) for (const char* c = m->testo; *c; c++) firmaMescola(f, (int32_t)(uint8_t)*c);
  return f;
}

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

    case PT_DETTAGLIO:
      screenDettaglio(pg->param);
      break;

    case PT_BIANCA:
    default:
      screenBlank();
      break;
  }

  pannello.hibernate();
  contaRefresh("pagina", true, millis() - t0);
  s_epdUltimoMs   = millis() - t0;
  segnaFull();                  // il cambio pagina e' sempre un completo
  s_disegniPagina++;            // vedi s_silDisegnoAl: chi ha disegnato per ultimo
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

  // Subito dopo la diagnosi del boot e prima di qualunque cosa che possa
  // bloccare: da qui in poi un loop() piantato viene ripreso invece di
  // lasciare sul vetro una pagina nitida e non piu' vera.
  wdtBegin();

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
  // PRIMA di net_begin(): la callback e' anche quella che alimenta il watchdog
  // durante un aggiornamento, e un OTA puo' partire appena il server e' su.
  net_setOtaProgressCb(onOtaProgress);
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
  // Il giro e' vivo. Sta in cima e non in fondo perche' il loop() ha molti
  // return (ogni disegno del pannello ne fa uno): messo in fondo verrebbe
  // saltato proprio nei giri piu' lunghi.
  esp_task_wdt_reset();

  uint32_t t = millis();

  // net_loop() PRIMA di tutto e ad ogni giro: e' quella che serve il web server
  // e fa avanzare l'OTA. Se salta un giro, un aggiornamento via rete si pianta
  // a meta' — ed e' l'unico modo di aggiornare questa scheda una volta montata.
  net_loop();

  // Le fasi si misurano solo FINO AL PRIMO DISEGNO: da li' in poi il loop()
  // esce con un return e il tempo del pannello ha gia' i suoi posti
  // (epd_ultimo_ms e il registro dei refresh sulla card). Vedi faseFine().
  //
  // Durante un OTA il web server scrive la partizione dentro handleClient():
  // sono decine di secondi legittimi, e finirebbero nel massimo coprendo per
  // sempre il guasto che questo contatore deve far vedere.
  t = s_otaAttivo ? millis() : faseFine("web", t);

  // Prova del watchdog: si blocca QUI, dopo net_loop(), cosi' la risposta HTTP
  // e' gia' partita e chi ha dato il comando sa che e' arrivato. Da questo
  // punto in poi nessuno alimenta il watchdog: se e' armato davvero, la scheda
  // riparte da sola entro WDT_TIMEOUT_MS e reset_reason dira' WDT_TASK.
  if (s_bloccoChiesto) {
    const uint32_t secondi = s_bloccoChiesto;
    s_bloccoChiesto = 0;   // una volta sola: un riavvio non deve ripeterla
    evento("prova_blocco", "loop bloccato apposta per provare il watchdog");
    Serial.printf("[prova] blocco il loop per %lu s SENZA alimentare il "
                  "watchdog: se e' armato, riparto da solo\n",
                  (unsigned long)secondi);
    Serial.flush();
    const uint32_t fine = millis() + secondi * 1000UL;
    while ((int32_t)(millis() - fine) < 0) { /* apposta: niente wdt_reset */ }
    // Ci si arriva SOLO se il watchdog non ha fatto il suo lavoro. Non e' un
    // errore del comando: e' il risultato negativo, e va detto forte.
    Serial.println("[prova] FALLITA: il blocco e' finito e la scheda non e' "
                   "ripartita. Il watchdog NON sta funzionando.");
    evento("prova_blocco", "FALLITA: nessun riavvio, il watchdog non funziona");
    t = millis();
  }

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
  diarioNodi();
  t = faseFine("nodi", t);

  // L'orologio e' diventato vero. Una riga sola per accensione, e vale la pena
  // perche' DATA TUTTO IL RESTO: le righe del diario e dei CSV scritte prima
  // di questo istante portano un orario stimato.
  {
    static bool s_ntpPrec = false;
    const bool ora = rtctime_isSynced();
    if (ora && !s_ntpPrec) {
      s_ntpPrec = true;
      char buf[64];
      snprintf(buf, sizeof(buf), "orario sincronizzato dopo %lu s di accensione",
               (unsigned long)(millis() / 1000));
      evento("ntp", buf);
    }
  }

  // Il boot non si puo' datare quando succede: si scrive appena l'orologio e'
  // vero, portandosi dietro da quanti secondi la scheda e' su.
  if (s_evBootDaScrivere && orario_registrabile()) {
    s_evBootDaScrivere = false;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s n.%lu fw=%s, ripartita %lu s fa, ora %s",
             app_reset_reason(), (unsigned long)s_bootCount, FW_VERSION,
             (unsigned long)(millis() / 1000), rtctime_source());
    evento("boot", buf);
  }

  // Aspetta il primo sync NTP e poi gira una volta sola.
  seedForecastDaSD();
  t = faseFine("seed", t);

  // Il riepilogo del giorno chiuso: al massimo UN giorno per giro, quindi il
  // costo di una lettura di CSV (~23 kB) e mai piu' di quello. Sta qui, dopo
  // il seeding e prima del pannello, per la stessa ragione: e' lavoro su card
  // che deve stare lontano dai refresh e non deve mai accumularsi.
  riepilogoTick();
  t = faseFine("riep", t);

  const uint8_t ev = bootEvent();
  t = faseFine("bottone", t);
  (void)t;   // ultima fase prima dei disegni: da qui in poi si esce con return
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
    if (ora != s_pairingPrec) {
      s_pairingPrec = ora;
      s_nodiDirty = true;
      evento("pairing", ora ? "finestra di associazione aperta"
                            : "finestra di associazione chiusa");
    }
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

  // --- ore di silenzio: il pannello si ferma davvero ----------------------
  // Entrando si va una volta sola sulla pagina scelta, poi NON si aggiorna
  // piu' niente fino alla fine della fascia. I nodi intanto continuano a
  // essere ricevuti e scritti su card: qui si ferma il display, non l'hub.
  //
  // La pagina si forza SOLO all'ingresso, non ad ogni giro: se di notte si
  // preme BOOT per guardare i nodi, il pannello deve restare dove lo si e'
  // messo invece di tornare indietro da solo al giro dopo.
  {
    const uint8_t silPag = pages_silenzio_pagina();
    const bool silenzio  = (silPag != PAG_SIL_NESSUNA) &&
                           rtctime_isSynced() && pages_in_silenzio(time(nullptr));

    if (silenzio && !s_inSilenzio) {
      s_inSilenzio    = true;
      s_pagPrimaSil   = pages_current();
      s_silImgNome[0] = '\0';

      // Immagine dall'archivio: NON passa dal modello delle pagine. Non e' una
      // scorciatoia -- di notte non serve una pagina ma un file, e mettere in
      // elenco una pagina che esiste solo per il silenzio costerebbe uno dei
      // sedici slot e la si potrebbe attivare, spostare o togliere da una
      // schermata che della notte non parla.
      if (silPag == PAG_SIL_CARD) {
        s_pagSilScelta = PAG_SIL_NESSUNA;
        if (immagineACasoDallaCard(s_silImgNome, sizeof(s_silImgNome), s_silImgUltima)) {
          strncpy(s_silImgUltima, s_silImgNome, sizeof(s_silImgUltima) - 1);
          s_silImgUltima[sizeof(s_silImgUltima) - 1] = '\0';

          const uint32_t t0 = millis();
          screenImmagine(s_silImgNome);
          pannello.hibernate();
          contaRefresh("silenzio", true, millis() - t0);
          s_epdUltimoMs  = millis() - t0;
          segnaFull();
          pages_disegnata(millis());
          s_silDisegnoAl = s_disegniPagina;   // sul vetro c'e' un file, non una pagina
          Serial.printf("[epd] ore di silenzio: /images/%s.bin e mi fermo\n", s_silImgNome);
        } else {
          // Nessuna immagine sulla card (o card assente): ci si ferma com'era.
          // Disegnare l'avviso "immagine non disponibile" spegnerebbe la pagina
          // dei nodi per tutta la notte in cambio di un messaggio che nessuno
          // sta guardando, e la mattina si troverebbe un errore al posto dei
          // dati.
          Serial.println("[epd] ore di silenzio: nessuna immagine sulla card, resto com'ero");
          // Questo e' l'unico ramo del silenzio che vale una riga di diario:
          // gli altri sono prevedibili dalla fascia configurata, questo no --
          // il pannello resta com'era e da fuori non si distingue da un
          // pannello che non si aggiorna piu'.
          evento("silenzio_senza_immagine",
                 "nessuna immagine sulla card: il pannello resta com'era");
        }
        return;
      }

      s_pagSilScelta = (silPag == PAG_SIL_CASUALE) ? immagineACaso() : silPag;

      // pages_goto() sposta il modello, showPage() disegna: servono
      // entrambe, o la pagina resta "corrente" quella di prima e all'uscita
      // il confronto qui sotto guarderebbe il numero sbagliato.
      if (s_pagSilScelta != PAG_SIL_NESSUNA && s_pagSilScelta != s_pagPrimaSil &&
          pages_goto(s_pagSilScelta))
        showPage(s_pagSilScelta);
      Serial.printf("[epd] ore di silenzio: pagina %u e mi fermo\n",
                    (unsigned)s_pagSilScelta);
      return;
    }
    if (!silenzio && s_inSilenzio) {
      s_inSilenzio = false;
      if (s_silImgNome[0]) {
        // Sul vetro c'e' un file, che il modello non conosce: si ridisegna la
        // pagina corrente, ma solo se nessuno ha disegnato niente nel
        // frattempo. Se di notte si e' premuto BOOT o si e' chiesta una pagina
        // dal web, quella e' la pagina voluta, e riprendersela al mattino
        // sarebbe un refresh in piu' per togliere all'utente quel che ha
        // scelto.
        if (s_disegniPagina == s_silDisegnoAl) showPage(pages_current());
        s_silImgNome[0] = '\0';
      }
      // Si torna indietro solo se nessuno ha toccato niente nel frattempo:
      // altrimenti si porterebbe via la pagina che l'utente ha scelto a mano.
      // Il confronto e' con la pagina SORTEGGIATA, non con quella configurata:
      // con il caso le due cose non coincidono quasi mai.
      else if (pages_current() == s_pagSilScelta && pages_goto(s_pagPrimaSil))
        showPage(s_pagPrimaSil);
      s_pagSilScelta = PAG_SIL_NESSUNA;
      Serial.println("[epd] fine delle ore di silenzio");
      return;
    }
    if (s_inSilenzio) return;      // dentro la fascia: nessun refresh, mai
  }

  if (tipoCur == PT_NODI)
  {
    // L'orologio al minuto NON C'E' PIU' (da v30). Era meta' di tutti i
    // refresh della giornata -- 720 su 1440 -- per riscrivere sempre lo
    // stesso rettangolo, che e' anche il modo peggiore in cui un e-ink
    // invecchia. L'ora non e' sparita dal pannello: ogni nodo ha la sua nella
    // barra ("ultimo alle 15:23"), e quella e' un ISTANTE, che resta vero
    // anche se la pagina non si ridisegna da un pezzo. In intestazione ora
    // c'e' l'ora dell'ultimo aggiornamento, che e' un istante pure lei.

    // Il conto delle firme non si fa ad ogni giro di loop: scorrere i nodi
    // costa poco ma non e' gratis, e a nessuno serve saperlo mille volte al
    // secondo.
    if (millis() - s_ultimoCheckMs < CHECK_MS) return;
    s_ultimoCheckMs = millis();

    const uint32_t da = millis() - s_nodiUltimoMs;

    // Il minimo assoluto vale SEMPRE, anche per un allarme: due refresh a
    // meno di due minuti l'uno dall'altro sono due lampeggi, e un pannello che
    // sfarfalla non si guarda piu'.
    if (da < NODI_MIN_MS) return;

    const uint32_t fStato   = firmaStato();
    const uint32_t fValori  = firmaValori();
    const bool statoCambia  = (fStato  != s_firmaStato);
    const bool valoriCambia = (fValori != s_firmaValori);

    // Il completo periodico serve al GHOSTING, non a mostrare dati nuovi: va
    // fatto anche a pagina identica -- anzi soprattutto allora, perche' una
    // pagina ferma da ore e' quella che si imprime.
    const bool fullPerGhosting = (millis() - s_fullUltimoMs >= FULL_OGNI_MS);

    // Uno stato cambiato si mostra SUBITO. I valori aspettano la cadenza dei
    // nodi: ridisegnare piu' spesso di quanto arrivino i dati riscrive gli
    // stessi numeri.
    const uint32_t cadenza = cadenzaNodiMs();
    const bool perValori = valoriCambia &&
                           da >= (cadenza > NODI_MIN_MS ? cadenza : NODI_MIN_MS);

    if (!statoCambia && !perValori && !fullPerGhosting) {
      // Niente da mostrare. NON si riarma il timer del refresh: se lo si
      // facesse, un cambio di stato dieci secondi dopo dovrebbe aspettare
      // un'altra cadenza intera -- ed e' esattamente il difetto che ha fatto
      // arrivare un "nodo muto" con cinque minuti di ritardo.
      s_nodiDirty = false;
      s_nodiInvariati++;
      return;
    }

    s_firmaStato  = fStato;
    s_firmaValori = fValori;

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
      if (full) segnaFull();
    contaRefresh(statoCambia ? "stato" : (perValori ? "valori" : "ghosting"),
                 full, millis() - t0);
    s_epdUltimoMs  = millis() - t0;
    Serial.printf("[epd] nodi %s (%s): %lu ms\n",
                  full ? "COMPLETO" : "parziale",
                  statoCambia ? "stato" : (perValori ? "valori" : "ghosting"),
                  (unsigned long)(millis() - t0));
    return;
  }

}
