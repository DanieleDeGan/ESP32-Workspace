/*
 * web_ui.cpp - pagina di stato e API HTTP dell'hub.
 * ---------------------------------------------------------------------------
 * Gli handler dei nodi, gli helper JSON e streamFileLimitato() vengono da
 * projects/EnvNode_C3/web_ui.cpp e sono presi tali e quali: l'hub espone gli
 * STESSI endpoint (/api/nodi, /api/pairing, /api/nodi/*), cosi' quello che si
 * sa fare con una scheda vale anche con l'altra. Non e' stato portato tutto
 * cio' che riguardava il sensore locale del C3 (/api/giorno, min/max): questa
 * scheda non misura niente di suo, riceve. La dashboard personalizzata su SD
 * invece c'è anche qui, ed è identica a quella del C3.
 *
 * Nuovo qui: /api/stato, che parla dell'hub e non di un sensore, e la card in
 * cima alla pagina che lo mostra. Se la microSD non e' montata i dati dei nodi
 * non li registra nessuno, e deve vedersi al primo colpo d'occhio - non dopo
 * aver aperto un log.
 */

#include "web_ui.h"
#include "net_ota.h"
#include "remote_nodes.h"
#include <EspNowLink.h>    // Link_Hub_Unknown(): l'ascolto di /api/pairing/ascolto
#include "forecast.h"
#include "sd_logger.h"
#include "rtc_time.h"
#include "pages.h"
#include "messages.h"
#include "dither_page.h"   // GENERATO da www/gen_page.py, servito su /immagini

#include <WiFi.h>
#include <WebServer.h>
#include <Middlewares.h>   // CorsMiddleware, bundled nella libreria WebServer del core
#include <SD.h>
#include <math.h>

// Definita piu' sotto, insieme alla whitelist delle pagine sostituibili: qui
// serve il prototipo perche' handleRoot() e le altre due pagine la chiamano
// prima che il compilatore l'abbia vista.
static void servePagina(const char* nome, const char* pm);
static bool paginaGiaPresente(uint8_t tipo, const char* param);

// ---------------------------------------------------------------------
//  Invii che si arrendono invece di trascinarsi dietro la scheda.
//  streamFile() del core ignora il valore di ritorno di write() e insiste
//  fino a fine file: un client che smette di dare ACK senza chiudere il
//  socket (telefono che si addormenta, coperchio del portatile) tiene
//  loop() dentro l'handler per minuti. Qui pesa quanto su EnvNode_C3: nel
//  frattempo nessuno preleva i DATA dei nodi dal driver, che tiene solo
//  l'ultimo, e nei log sembrerebbe un problema di radio.
// ---------------------------------------------------------------------
static constexpr uint32_t INVIO_BUDGET_MS = 20000;

static uint32_t s_invii_interrotti = 0;   // quante volte e' scattato il taglio

// Quanti byte occupa la sequenza UTF-8 che comincia con `c`, 0 se `c` non
// puo' iniziare una sequenza valida.
static uint8_t utf8Len(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 0;
}

// true se `s` e' UTF-8 ben formato (fino a `max` byte o al terminatore).
static bool utf8Valido(const char* s, size_t max) {
  if (s == nullptr) return true;
  for (size_t i = 0; i < max && s[i] != '\0'; ) {
    const uint8_t n = utf8Len((unsigned char)s[i]);
    if (n == 0) return false;
    for (uint8_t k = 1; k < n; k++) {
      if (s[i + k] == '\0') return false;
      if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
    }
    i += n;
  }
  return true;
}

static void appendJsonString(String& out, const char* s) {
  out += '"';
  if (s) {
    // Limite di lunghezza indipendente dalla terminazione: se mai un
    // chiamante passasse un buffer non terminato (vedi il commento su
    // rtctime_format in rtc_time.cpp), questo evita comunque una lettura
    // indefinita oltre il buffer invece di bloccare il web server.
    //
    // I byte che non compongono una sequenza UTF-8 valida diventano '?'.
    // NON e' pignoleria: un solo byte sbagliato rende non parsabile
    // l'INTERA risposta, quindi la pagina resta vuota per colpa di un
    // messaggio scritto male o del nome di un nodo arrivato storto dalla
    // radio. E' la stessa trappola del NAN emesso come "nan" invece che
    // come null, gia' costata su EnvNode_C3. Successo davvero il
    // 2026-08-28: un client che mandava CP1252 ha lasciato un 0xF9
    // nell'archivio dei messaggi, e /api/messaggio non si parsava piu'.
    for (size_t i = 0; i < 256 && s[i] != '\0'; ) {
      const char c = s[i];
      if (c == '"' || c == '\\') { out += '\\'; out += c; i++; continue; }
      if ((unsigned char)c < 0x20) { i++; continue; }

      const uint8_t n = utf8Len((unsigned char)c);
      if (n == 1) { out += c; i++; continue; }

      // Sequenza multibyte: si copia solo se e' completa e ben formata.
      bool ok = (n != 0);
      for (uint8_t k = 1; ok && k < n; k++) {
        if (s[i + k] == '\0' || ((unsigned char)s[i + k] & 0xC0) != 0x80) ok = false;
      }
      if (!ok) { out += '?'; i++; continue; }
      for (uint8_t k = 0; k < n; k++) out += s[i + k];
      i += n;
    }
  }
  out += '"';
}

static void appendJsonFloat(String& out, float v, int decimali) {
  if (isnan(v) || isinf(v)) { out += "null"; return; }
  out += String(v, decimali);
}

static void appendJsonMac(String& out, const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  appendJsonString(out, buf);
}

static void appendDayCb(const char* isoDate, size_t /*fileSizeBytes*/, void* arg) {
  String* out = (String*)arg;
  if (out->length() > 1) *out += ',';
  appendJsonString(*out, isoDate);
}

static void invioInterrotto(const char* perche) {
  s_invii_interrotti++;
  Serial.printf("[web] invio interrotto: %s\n", perche);
}

static bool streamFileLimitato(WebServer& srv, File& f, const char* contentType) {
  srv.setContentLength(f.size());
  srv.send(200, contentType, "");

  NetworkClient& cli = srv.client();
  uint8_t buf[1024];
  const uint32_t t0 = millis();

  while (f.available()) {
    if (!cli.connected()) { invioInterrotto("il client ha chiuso"); return false; }

    const int letti = f.read(buf, sizeof(buf));
    if (letti <= 0) break;

    // E' il controllo che manca al core, ed e' tutto qui: se il client non
    // ha preso l'intero chunk non ne prendera' altri, e ogni giro in piu'
    // costa dieci secondi.
    if (cli.write(buf, (size_t)letti) != (size_t)letti) {
      invioInterrotto("il client non accetta piu' dati");
      return false;
    }
    if (millis() - t0 > INVIO_BUDGET_MS) {
      invioInterrotto("oltre il budget di tempo");
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------
//  GET /api/salute — i controlli incrociati, fatti dalla scheda
//
//  Nasce da una verifica che finora si faceva a mano: leggere /api/stato e
//  /api/nodi e confrontare i contatori di moduli diversi. Il controllo che
//  vale davvero e' questo:
//
//      pacchetti ricevuti  ==  righe scritte + scartati + scritture fallite
//
//  e regge perche' remote_nodes incrementa `pacchetti` e chiama la callback
//  nello STESSO punto: ad ogni pacchetto contato corrisponde esattamente un
//  tentativo di scrittura, che finisce in uno dei tre contatori. Sono numeri
//  tenuti da moduli che non si conoscono fra loro — remote_nodes, sd_logger e
//  lo sketch — quindi se non tornano il guasto sta nel mezzo, fra la radio e
//  la card, che e' esattamente il tratto in cui nessun altro contatore guarda.
//
//  Tutti i valori sono "da questo avvio": vivono in RAM, come il resto della
//  diagnostica di questa scheda. Un riavvio li azzera, ed e' giusto cosi' —
//  dopo un riavvio il conto ripartirebbe comunque sfasato.
// ---------------------------------------------------------------------
static void handleApiSalute() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  const uint32_t righe    = app_righe_scritte();
  const uint32_t scartati = app_scartati_ora();
  const uint32_t fallite  = app_scritture_ko();

  uint32_t pacchetti = 0, persi = 0, seqAssurdi = 0;
  int muti = 0;
  const int n = remote_count();
  for (int i = 0; i < n; i++) {
    RemoteNode nodo;
    if (!remote_get(i, &nodo)) continue;
    pacchetti  += nodo.pacchetti;
    persi      += nodo.persi;
    seqAssurdi += nodo.seqAssurdi;
    if (!nodo.online && nodo.hasData) muti++;
  }

  const uint32_t contati = righe + scartati + fallite;
  const bool torna = (contati == pacchetti);

  // I problemi si elencano in ordine di gravita': chi legge si ferma al primo.
  String guai = "[";
  int gravi = 0, avvisi = 0;
  auto guaio = [&](const char* testo, bool grave) {
    if (guai.length() > 1) guai += ',';
    appendJsonString(guai, testo);
    if (grave) gravi++; else avvisi++;
  };

  if (!sd_mounted())  guaio("la microSD non e' montata: nessun dato viene registrato", true);
  if (fallite > 0)    guaio("la card ha rifiutato delle righe: piena o in errore", true);
  if (!torna)         guaio("il conto non torna: pacchetti ricevuti e righe scritte non corrispondono", true);
  if (n == 0)         guaio("nessun nodo in elenco", true);
  else if (muti == n) guaio("tutti i nodi sono muti", true);
  else if (muti > 0)  guaio("un nodo non parla da piu' del suo intervallo", false);

  if (!rtctime_isSynced())        guaio("orario mai sincronizzato via NTP", false);
  if (!net_isConnected())         guaio("WiFi non connesso", false);
  if (s_invii_interrotti > 0) guaio("qualche invio di file e' stato troncato: un client se n'e' andato a meta'", false);
  if (ESP.getFreeHeap() < 60000)  guaio("memoria libera sotto i 60 kB", false);
  // Un watchdog che non si e' armato non si vede in nessun altro modo: si
  // comporta esattamente come uno armato, fino al giorno in cui servirebbe.
  if (!app_wdt_armato()) guaio("il watchdog del loop NON e' armato: un blocco non verrebbe ripreso", false);
  // Non e' una perdita radio: e' un nodo che ha mandato un seq fuori scala,
  // cioe' quasi sempre un contatore sporco letto dalla RTC memory dopo un
  // risveglio. Vale la pena dirlo, o il contatore resta in /api/nodi e non lo
  // guarda nessuno.
  if (seqAssurdi > 0) guaio("un nodo ha mandato un seq fuori scala: numerazione sporca, non perdita radio", false);
  guai += ']';

  String j = "{\"stato\":\"";
  j += gravi ? "guasto" : (avvisi ? "attenzione" : "ok");
  j += "\",\"problemi\":" + guai;
  j += ",\"conto\":{\"pacchetti\":" + String(pacchetti);
  j += ",\"righe\":"             + String(righe);
  j += ",\"scartati_ora\":"      + String(scartati);
  j += ",\"scritture_fallite\":" + String(fallite);
  j += ",\"torna\":"             + String(torna ? "true" : "false") + "}";
  j += ",\"persi_radio\":"       + String(persi);
  j += ",\"seq_assurdi\":"       + String(seqAssurdi);
  j += ",\"nodi\":"              + String(n);
  j += ",\"nodi_muti\":"         + String(muti);
  j += ",\"sd\":"                + String(sd_mounted() ? "true" : "false");
  j += ",\"orario\":\""          + String(rtctime_source()) + "\"";
  j += ",\"invii_interrotti\":"  + String(s_invii_interrotti);
  j += ",\"heap\":"              + String((unsigned long)ESP.getFreeHeap());
  j += ",\"reset_reason\":\""  + String(app_reset_reason()) + "\"";
  j += ",\"boot_count\":"       + String(app_boot_count());
  j += ",\"uptime\":"            + String((unsigned long)(millis() / 1000)) + "}";

  net_server().send(200, "application/json", j);
}

static void handleApiNodi() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  const int n = remote_count();
  String json;
  json.reserve(160 + 400 * n);   // cresciuta con intervallo_campioni e seq_assurdi
  json += '{';
  json += "\"attivo\":";  json += (remote_ready() ? "true" : "false"); json += ',';
  // Il canale non e' una proprieta' di ESP-NOW ma dell'AP a cui siamo
  // connessi (vedi remote_nodes.h): si legge da qui perche' un nodo che
  // dorme, senza WiFi, dovra' impostarlo esplicitamente.
  json += "\"canale\":" + String(net_channel()) + ",";
  json += "\"pairing\":"; json += (remote_pairing_active() ? "true" : "false"); json += ',';
  json += "\"pairing_resta_s\":" + String(remote_pairing_remaining_s()) + ",";
  // L'altitudine sta qui e non fra le impostazioni del nodo locale: serve ai
  // nodi REMOTI, che trasmettono la pressione grezza (vedi remote_nodes.h).
  json += "\"altitudine_m\":" + String(remote_altitude_m(), 0) + ",";
  json += "\"nodi\":[";

  bool primo = true;
  for (int i = 0; i < n; i++) {
    RemoteNode r;
    if (!remote_get(i, &r)) continue;
    if (!primo) json += ',';
    primo = false;

    json += '{';
    json += "\"mac\":";       appendJsonMac(json, r.mac);                     json += ',';
    json += "\"nome\":";      appendJsonString(json, r.nome);                 json += ',';
    json += "\"tipo\":" + String(r.tipo) + ",";
    json += "\"tipo_nome\":"; appendJsonString(json, remote_tipo_nome(r.tipo)); json += ',';
    json += "\"dati\":";      json += (r.hasData ? "true" : "false");         json += ',';

    if (r.hasData) {
      json += "\"valori\":[";
      appendJsonFloat(json, r.value[0], 2); json += ',';
      appendJsonFloat(json, r.value[1], 2); json += ',';
      appendJsonFloat(json, r.value[2], 2);
      json += "],";
      char buf[24] = "--";
      rtctime_format(r.ultimoTs, "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
      json += "\"ultimo\":"; appendJsonString(json, buf); json += ',';
    } else {
      json += "\"valori\":null,\"ultimo\":null,";
    }

    json += "\"online\":"; json += (r.online ? "true" : "false"); json += ',';
    json += "\"silenzio_s\":"    + String(r.silenzioS)   + ",";
    json += "\"soglia_muto_s\":" + String(r.sogliaMutoS) + ",";
    json += "\"intervallo_s\":"  + String(r.intervalloS) + ",";
    // Quanti delta CONSECUTIVI hanno formato quell'intervallo. Serve a
    // distinguere "cadenza vecchia" da "cadenza sbagliata": se questo sta
    // fermo mentre `persi` sale, il numero mostrato e' l'ultimo buono -- la
    // cadenza si impara solo dai pacchetti in sequenza, perche' un delta
    // misurato a cavallo di un buco non e' un periodo.
    json += "\"intervallo_campioni\":" + String(r.intervalloCampioni) + ",";
    json += "\"batteria_mv\":"   + String(r.batteria_mv) + ",";
    json += "\"seq\":"           + String(r.seq)         + ",";
    json += "\"pacchetti\":"     + String(r.pacchetti)   + ",";
    json += "\"persi\":"         + String(r.persi)       + ",";
    json += "\"riavvii\":"       + String(r.riavvii)     + ",";
    // Salti di seq troppo grandi per essere pacchetti persi: un contatore
    // sporco (il seq attraversa il deep sleep dalla RTC memory). Contati qui
    // e non in `persi`, che altrimenti resterebbe avvelenato per sempre.
    json += "\"seq_assurdi\":"   + String(r.seqAssurdi)  + ",";

    // Previsione: calcolata qui sull'hub, perche' un nodo che dorme non puo'
    // tenere tre ore di storico (remote_nodes.h). I campi restano null finche'
    // lo storico non arriva a tre ore, e storico_slot dice quanto manca -
    // senza quel numero, "non ancora noto" e "guasto" si somigliano troppo.
    json += "\"press_sea\":";  appendJsonFloat(json, r.pressSeaHpa, 2); json += ',';
    json += "\"delta_3h\":";   appendJsonFloat(json, r.delta3h, 2);     json += ',';
    json += "\"trend\":";      appendJsonString(json, remote_trend_label(r.trend)); json += ',';
    json += "\"previsione\":"; appendJsonString(json, remote_forecast_text(&r));    json += ',';
    json += "\"storico_slot\":" + String(r.storicoSlot);
    json += ",\"temp_campioni\":" + String(remote_temp_campioni(i));
    json += '}';
  }

  json += "]}";
  net_server().send(200, "application/json", json);
}

static void handleApiNodiGiorni() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("nodo")) { srv.send(400, "text/plain", "manca il parametro nodo"); return; }

  String json = "[";
  sd_list_remote_days(srv.arg("nodo").c_str(), appendDayCb, &json, 400);
  json += ']';
  srv.send(200, "application/json", json);
}

static void handleApiNodiScarica() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("nodo") || !srv.hasArg("d")) {
    srv.send(400, "text/plain", "servono i parametri nodo e d");
    return;
  }

  // sd_open_remote_day() valida sia la data sia il nome (che diventa un
  // pezzo di path): qui non si compone niente a mano.
  File f = sd_open_remote_day(srv.arg("nodo").c_str(), srv.arg("d").c_str());
  if (!f) { srv.send(404, "text/plain", "file inesistente"); return; }

  char disp[80];
  snprintf(disp, sizeof(disp), "attachment; filename=\"%s_%s.csv\"",
           srv.arg("nodo").c_str(), srv.arg("d").c_str());
  srv.sendHeader("Content-Disposition", disp);
  streamFileLimitato(srv, f, "text/csv");
  f.close();
}

static void handleApiNodiAltitudine() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!srv.hasArg("m")) { srv.send(400, "text/plain", "manca il parametro m"); return; }
  if (!remote_set_altitude_m(srv.arg("m").toFloat())) {
    srv.send(400, "text/plain", "fuori range (-400..4000 m)");
    return;
  }
  srv.send(200, "text/plain", String("altitudine: ") + String(remote_altitude_m(), 0) + " m");
}

static void handleApiNodiDimentica() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!srv.hasArg("mac")) { srv.send(400, "text/plain", "manca il parametro mac"); return; }

  uint8_t mac[6];
  if (!remote_parse_mac(srv.arg("mac").c_str(), mac)) {
    srv.send(400, "text/plain", "MAC non valido");
    return;
  }
  if (!remote_forget(mac)) {
    srv.send(404, "text/plain", "nodo non trovato");
    return;
  }
  srv.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiPairing() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!remote_ready()) {
    srv.send(503, "text/plain", "ESP-NOW non attivo su questa scheda");
    return;
  }

  const bool on = srv.hasArg("on") ? (srv.arg("on") == "1" || srv.arg("on") == "true") : true;
  if (on) {
    remote_pairing_open(srv.hasArg("s") ? (uint32_t)srv.arg("s").toInt() : 0);
  } else {
    remote_pairing_close();
  }

  String json = "{\"pairing\":";
  json += (remote_pairing_active() ? "true" : "false");
  json += ",\"pairing_resta_s\":" + String(remote_pairing_remaining_s()) + "}";
  srv.send(200, "application/json", json);
}

// POST /api/prova/blocco?s=90 — fabbrica il guasto che il watchdog deve
// riprendere. La scheda smette di rispondere per qualche decina di secondi e
// riparte da sola: e' voluto, ed e' l'unico modo di sapere se il watchdog
// funziona davvero invece di essere solo configurato.
static void handleApiProvaBlocco() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  const uint32_t s = srv.hasArg("s") ? (uint32_t)srv.arg("s").toInt() : 90;
  app_chiedi_blocco(s);

  // La risposta parte ADESSO, prima che il loop si blocchi: il comando si
  // accoda e basta. Dice anche cosa aspettarsi, perche' da fuori un blocco
  // voluto e una scheda morta si somigliano parecchio.
  String j = "{\"ok\":true,\"blocco_s\":" + String(s);
  j += ",\"wdt_armato\":"; j += (app_wdt_armato() ? "true" : "false");
  j += ",\"wdt_timeout_s\":" + String(app_wdt_timeout_s());
  j += ",\"atteso\":\"la scheda smette di rispondere e riparte da sola entro il "
       "timeout; poi /api/stato deve dire reset_reason WDT_TASK\"}";
  srv.send(200, "application/json", j);
}

// GET /api/eventi?m=AAAA-MM — il diario di un mese, in CSV.
//
// Si serve il file grezzo e non un JSON: sono poche righe testuali al giorno,
// il formato e' gia' leggibile a occhio, e passare da streamFileLimitato()
// evita di costruire in RAM una risposta la cui dimensione non si conosce.
static void handleApiEventi() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  // Senza mese si intende quello corrente: e' la richiesta che si fa il 99%
  // delle volte, e farla scrivere a mano ogni volta e' solo attrito.
  char meseOra[8];
  if (!srv.hasArg("m")) {
    time_t t = rtctime_now();
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(meseOra, sizeof(meseOra), "%04d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1);
  }
  const String mese = srv.hasArg("m") ? srv.arg("m") : String(meseOra);

  File f = sd_open_eventi(mese.c_str());
  if (!f) { srv.send(404, "text/plain", "nessun diario per quel mese"); return; }

  char disp[64];
  snprintf(disp, sizeof(disp), "inline; filename=\"eventi_%s.csv\"", mese.c_str());
  srv.sendHeader("Content-Disposition", disp);
  streamFileLimitato(srv, f, "text/csv");
  f.close();
}

// GET /api/pairing/ascolto — chi bussa, e perche' non entra.
//
// Prima, quando un nodo non si associava, questa scheda non mostrava NIENTE:
// nessun tentativo, nessun contatore, nessun log. Le cause sono almeno cinque
// e da fuori si presentavano tutte come una lista di nodi che non cresce.
static void handleApiPairingAscolto() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  String j = "{\"finestra\":";
  j += (remote_pairing_active() ? "true" : "false");
  j += ",\"resta_s\":" + String(remote_pairing_remaining_s());
  j += ",\"sconosciuti\":[";

  bool primo = true;
  for (int i = 0; i < LINK_UNKNOWN_MAX; i++) {
    uint8_t  mac[6];
    int8_t   rssi;
    uint32_t lastMs;
    uint16_t quante;
    uint8_t  esito;
    if (!Link_Hub_Unknown(i, mac, &rssi, &lastMs, &quante, &esito)) continue;

    if (!primo) j += ',';
    primo = false;
    j += "{\"mac\":";   appendJsonMac(j, mac);
    j += ",\"rssi\":"   + String((int)rssi);
    j += ",\"visto_s\":" + String((millis() - lastMs) / 1000);
    j += ",\"quante\":" + String(quante);
    j += ",\"esito\":";  appendJsonString(j, Link_Hub_UnknownEsito(esito));
    j += '}';
  }
  j += "],";

  // Il limite va detto QUI e non lasciato dedurre: un elenco vuoto non
  // significa "non c'e' nessun nodo acceso". Un nodo che si crede gia'
  // associato a un altro hub non manda HELLO, e i suoi DATA vanno in unicast
  // a quell'altro MAC: questa scheda non li riceve nemmeno a livello radio.
  j += "\"nota\":\"un elenco vuoto significa che nessun nodo sta CERCANDO un hub, "
       "non che non ce ne siano di accesi: un nodo gia' associato altrove non manda HELLO\"}";
  net_server().send(200, "application/json", j);
}


static const char HUB_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeteoHub-S3 &mdash; Stazione meteo</title><style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:1rem;display:flex;justify-content:center}
 .wrap{max-width:820px;width:100%}
 h1{font-size:1.05rem;margin:0 0 .8rem}
 .card{background:#1c1c1c;border:1px solid #333;border-radius:12px;padding:1rem;margin-bottom:1rem}
 .row{display:flex;flex-wrap:wrap;gap:.6rem;align-items:center}
 button{padding:.55rem .9rem;border:0;border-radius:8px;background:#3987e5;color:#fff;font-size:.9rem;cursor:pointer}
 button.off{background:#444}
 h3{margin:0;font-size:1rem;display:flex;align-items:center;gap:.5rem}
 .dot{width:.6rem;height:.6rem;border-radius:50%;display:inline-block;flex:none}
 .ok{background:#3fb950}.ko{background:#f85149}
 .vals{font-size:1.3rem;margin:.6rem 0 .4rem}
 .muted{color:#8a8a8a;font-size:.8rem;line-height:1.6;margin:.2rem 0}
 .warn{color:#f0a020}
 .prev{margin:.35rem 0 0;font-size:.88rem;color:#cfe3ff}
 .prev b{color:#8ab4e8;font-weight:600}
 input[type=number]{width:5.5rem;padding:.45rem;border-radius:6px;border:1px solid #444;background:#161616;color:#eee}
 button.dim{background:#3a2020;border:1px solid #5a2a2a;color:#e0888a;font-size:.75rem;padding:.35rem .6rem;margin-top:.6rem}
 button.reg{background:#1f2a38;border:1px solid #2e3f55;color:#8ab4e8;font-size:.75rem;padding:.35rem .6rem;margin:.6rem .5rem 0 0}
 .giorni{margin-top:.5rem;font-size:.78rem;line-height:1.9}
 .giorni a{margin-right:.7rem;white-space:nowrap}
 a{color:#3987e5}
 code{color:#9aa}
</style></head><body><div class="wrap">
<h1>MeteoHub-S3 &mdash; nodi della stazione</h1>
<div class="card">
 <h3><span class="dot" id="hd"></span> <span id="hnome">hub</span></h3>
 <p class="muted" id="hinfo">lettura dello stato...</p>
 <p class="muted" id="hsd"></p>
</div>
<div class="card">
 <div class="row">
  <button id="bp">Apri pairing 5 min</button>
  <button id="bc" class="off">Chiudi</button>
  <span class="muted" id="st"></span>
 </div>
 <p class="muted">Un nodo si associa solo mentre la finestra e' aperta, e su
 questo hub la finestra <b>non si apre da sola all'avvio</b>, al contrario di
 EnvNode-C3: i nodi gia' noti stanno in memoria permanente e rientrano
 comunque, mentre un hub lasciato in ascolto si porterebbe via il primo nodo
 che si riavvia in casa &mdash; e con lui il suo storico su SD. Si apre da qui,
 oppure tenendo premuto il tasto BOOT sulla scheda (2 minuti).</p>
</div>
<div class="card">
 <div class="row">
  <span class="muted">Altitudine dei nodi</span>
  <input type="number" id="alt" step="1" min="-400" max="4000">
  <span class="muted">m</span>
  <button id="ba">Salva</button>
  <span class="muted" id="sa"></span>
 </div>
 <p class="muted">Serve solo a riportare al livello del mare la pressione, che i
 nodi trasmettono GREZZA. Il trend a tre ore non ne dipende (e' una differenza,
 l'offset si cancella): cambiarla sposta i valori assoluti, non la previsione.
 Un valore per tutti i nodi &mdash; 8 m valgono 1 hPa, e i nodi di una casa
 stanno molto piu' vicini di cosi'.</p>
</div>
<div id="lista"></div>
<p class="muted"><a href="/">nodi</a> &mdash; <a href="/pannello">pannello e messaggi</a> &mdash; <a href="/immagini">componi immagine</a> &mdash; <a href="/pagine">pagine</a> &mdash; <a href="/api">API</a> &mdash; <a href="/update">aggiornamento firmware</a></p>
<p class="muted">I registri dei nodi stanno su microSD, un file per giorno per nodo.</p>
<script>
const E=document.getElementById.bind(document);
// Il nome del nodo arriva dalla radio: nel DOM va come testo, mai come
// markup. Sedici caratteri scelti da chiunque sia a tiro d'antenna non sono
// un posto dove fidarsi.
function esc(x){return String(x==null?'':x).replace(/[&<>"]/g,
 c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function dur(s){if(s<60)return s+' s';if(s<3600)return Math.round(s/60)+' min';return (s/3600).toFixed(1)+' h';}
// null = valore non finito lato nodo (sensore che non ha risposto): si
// mostra come tale invece di stampare uno zero che sembrerebbe una misura.
function num(x,d){return x===null?'--':x.toFixed(d);}
function vals(n){
 if(!n.dati)return '<span class="muted">associato, nessun DATA ancora</span>';
 const v=n.valori;
 // Le unita' si mostrano solo per i tipi di cui conosciamo il contratto dei
 // tre float; per gli altri si stampano i numeri nudi invece di inventare.
 if(n.tipo==2)return num(v[0],1)+' &deg;C &middot; '+num(v[1],1)+' % &middot; '+num(v[2],1)+' hPa';
 return v.map(x=>num(x,2)).join(' &middot; ');
}
// La previsione la calcola l'HUB, non il nodo: un nodo in deep sleep perde la
// RAM ad ogni risveglio e non puo' tenere le tre ore di storico che servono.
// Finche' lo storico non arriva a tre ore si dice quanti slot ci sono, non
// "non disponibile": la differenza fra "sto ancora raccogliendo" e "qualcosa
// non va" deve restare leggibile.
function meteo(n){
 if(!n.dati || n.press_sea===null) return '';
 let r='<p class="muted">livello mare: '+num(n.press_sea,1)+' hPa';
 if(n.delta_3h===null){
  return r+' &middot; storico '+n.storico_slot+'/19 slot da 10 min</p>';
 }
 r+=' &middot; variazione 3 h: '+(n.delta_3h>0?'+':'')+num(n.delta_3h,1)+' hPa</p>';
 return r+'<p class="prev"><b>'+esc(n.trend)+'</b> &mdash; '+esc(n.previsione)+'</p>';
}
function stato(n){
 if(!n.dati)return '<p class="muted">in attesa del primo DATA</p>';
 return '<p class="muted">ultimo: '+n.ultimo+' &middot; silenzio '+dur(n.silenzio_s)+
  (n.online?'':' <span class="warn">&mdash; MUTO (soglia '+dur(n.soglia_muto_s)+')</span>')+'</p>';
}
function render(d){
 E('st').textContent=d.attivo
  ?(d.pairing?('pairing aperto, ancora '+dur(d.pairing_resta_s)):'pairing chiuso')+' — canale '+d.canale
  :'ESP-NOW non attivo';
 if(!d.nodi.length){E('lista').innerHTML='<div class="card"><p class="muted">Nessun nodo associato. Apri il pairing, poi accendi (o riavvia) il nodo.</p></div>';return;}
 E('lista').innerHTML=d.nodi.map(n=>`<div class="card">
  <h3><span class="dot ${n.online?'ok':'ko'}"></span>${esc(n.nome||'(senza nome)')}
   <span class="muted">${esc(n.tipo_nome)}</span></h3>
  <div class="vals">${vals(n)}</div>
  ${meteo(n)}
  ${stato(n)}
  <p class="muted">cadenza osservata: ${n.intervallo_s?dur(n.intervallo_s):'in apprendimento'}
   &middot; batteria: ${n.batteria_mv?(n.batteria_mv/1000).toFixed(2)+' V':'non misurata'}</p>
  <p class="muted">pacchetti ${n.pacchetti} &middot; persi ${n.persi} &middot; riavvii ${n.riavvii}
   &middot; seq ${n.seq} &middot; <code>${n.mac}</code></p>
  <button class="reg" data-nodo="${esc(n.nome)}" data-box="g-${n.mac.replace(/:/g,'')}">Registri su SD</button>
  <button class="dim" data-mac="${n.mac}">Dimentica questo nodo</button>
  <div class="giorni" id="g-${n.mac.replace(/:/g,'')}"></div>
  </div>`).join('');
 // I registri si caricano SOLO su richiesta: elencare i file della SD e' una
 // scansione della card, e farla ad ogni giro di polling (2 s) toglierebbe
 // tempo a campionamento, scrittura e OTA su un WebServer che e' sincrono.
 document.querySelectorAll('button.reg').forEach(b=>
   b.onclick=()=>registri(b.dataset.nodo, E(b.dataset.box)));
 // I pulsanti si ricreano ad ogni render, quindi il gestore si riaggancia qui
 // invece di una volta sola all'avvio.
 document.querySelectorAll('button.dim').forEach(b=>b.onclick=()=>dimentica(b.dataset.mac));
}
function registri(nodo, box){
 box.textContent='lettura della card...';
 fetch('/api/nodi/giorni?nodo='+encodeURIComponent(nodo)).then(r=>r.json()).then(g=>{
  if(!g.length){box.textContent='nessun registro ancora (il primo file nasce al prossimo DATA)';return;}
  box.innerHTML = g.slice().reverse().map(d =>
    '<a href="/api/nodi/scarica?nodo='+encodeURIComponent(nodo)+'&d='+d+'">'+d+'</a>').join('');
 }).catch(()=>{box.textContent='errore di lettura';});
}
function dimentica(mac){
 if(!confirm('Dimenticare il nodo '+mac+'? Viene tolto dal registro e dalla memoria '
  +"permanente. Se e' ancora acceso e si crede associato non tornera' da solo: "
  +'serve una finestra di associazione aperta, oppure un suo riavvio.')) return;
 fetch('/api/nodi/dimentica?mac='+encodeURIComponent(mac),{method:'POST'})
  .then(r=>r.text()).then(()=>tick()).catch(()=>{});
}
// Il campo non si riscrive ad ogni polling (2 s), o cancellerebbe quello che
// l'utente sta digitando proprio mentre lo digita: si riempie una volta sola,
// e poi solo se non e' stato toccato.
let altTocca=false;
function altAggiorna(d){
 const i=E('alt');
 if(altTocca||document.activeElement===i)return;
 i.value=d.altitudine_m;
}
E('alt').addEventListener('input',()=>{altTocca=true;});
E('ba').onclick=()=>{
 E('sa').textContent='salvo...';
 fetch('/api/nodi/altitudine?m='+encodeURIComponent(E('alt').value),{method:'POST'})
  .then(r=>r.text()).then(t=>{E('sa').textContent=t;altTocca=false;tick();})
  .catch(()=>{E('sa').textContent='errore';});
};
function tickHub(){fetch('/api/stato').then(r=>r.json()).then(d=>{
  E('hd').className='dot '+(d.wifi?'ok':'ko');
  E('hnome').textContent=d.nodo+'  '+d.fw;
  E('hinfo').textContent=d.ora+' ('+d.ora_fonte+')  \u00b7  '+d.ip+'  '+d.rssi
   +' dBm  canale '+d.canale+'  \u00b7  acceso da '+dur(d.uptime)+'  \u00b7  heap '
   +Math.round(d.heap/1024)+' kB';
  E('hsd').textContent = d.sd
   ? ('microSD: '+d.sd_liberi_mb+' MB liberi su '+d.sd_totali_mb+'  \u00b7  '
      +d.righe_scritte+' righe scritte  \u00b7  pannello: '+d.epd_refresh
      +' refresh, ultimo '+d.epd_ultimo_ms+' ms')
   : ('microSD NON montata: '+(d.sd_errore||'?')+'  \u2014 i dati dei nodi non '
      +'vengono registrati da nessuna parte');
  E('hsd').className = d.sd ? 'muted' : 'muted warn';
 }).catch(()=>{});}
// Un giro su tre: il WebServer e' sincrono, e mentre serve questa risposta non
// preleva i DATA dei nodi ne' fa avanzare l'OTA.
let giro=0;
function tick(){
 if(giro++%3===0)tickHub();
 fetch('/api/nodi').then(r=>r.json()).then(d=>{render(d);altAggiorna(d);}).catch(()=>{});
}
E('bp').onclick=()=>fetch('/api/pairing?on=1&s=300',{method:'POST'}).then(tick);
E('bc').onclick=()=>fetch('/api/pairing?on=0',{method:'POST'}).then(tick);
tick();setInterval(tick,2000);
</script></div></body></html>
)HTML";

// ---------------------------------------------------------------------
//  GET /api/stato - l'hub, non i nodi
// ---------------------------------------------------------------------
static void handleApiStato() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  char ora[24] = "--";
  rtctime_format(rtctime_now(), "%Y-%m-%d %H:%M:%S", ora, sizeof(ora));

  String j;
  j.reserve(600);
  j += '{';
  j += "\"nodo\":";      appendJsonString(j, app_hub_nome());     j += ',';
  j += "\"fw\":";        appendJsonString(j, app_fw_version());   j += ',';
  j += "\"ora\":";       appendJsonString(j, ora);                j += ',';
  j += "\"ora_fonte\":"; appendJsonString(j, rtctime_source());   j += ',';
  j += "\"wifi\":";      j += (net_isConnected() ? "true" : "false"); j += ',';
  j += "\"ip\":";        appendJsonString(j, WiFi.localIP().toString().c_str()); j += ',';
  j += "\"rssi\":"   + String(net_rssi())    + ",";
  j += "\"canale\":" + String(net_channel()) + ",";

  // La microSD e' la differenza fra "l'hub mostra i nodi" e "l'hub conserva
  // quello che i nodi dicono": il CSV su questa card e' l'unico posto dove
  // quelle letture esistono, perche' un nodo che dorme non se le tiene.
  j += "\"sd\":";        j += (sd_mounted() ? "true" : "false"); j += ',';
  j += "\"sd_errore\":"; appendJsonString(j, sd_mounted() ? "" : sd_last_error()); j += ',';
  j += "\"sd_liberi_mb\":"  + String((uint32_t)sd_free_mb())  + ",";
  j += "\"sd_totali_mb\":"  + String((uint32_t)sd_total_mb()) + ",";
  j += "\"righe_scritte\":" + String(app_righe_scritte())     + ",";

  j += "\"nodi\":"        + String(remote_count())        + ",";
  j += "\"nodi_online\":" + String(remote_count_online()) + ",";
  j += "\"pairing\":";    j += (remote_pairing_active() ? "true" : "false"); j += ',';
  j += "\"pairing_resta_s\":" + String(remote_pairing_remaining_s()) + ",";

  j += "\"epd_refresh\":"      + String(app_epd_refresh())    + ",";
  j += "\"epd_ultimo_ms\":"    + String(app_epd_ultimo_ms())  + ",";
  j += "\"epd_orologio_ms\":"  + String(app_epd_orologio_ms()) + ",";
  j += "\"refresh_evitati\":" + String(app_refresh_evitati()) + ",";

  // Lo stato della politica antighosting, che da fuori non si vedeva.
  // L'anteprima del pannello mostra cio' che l'hub ha DISEGNATO, non i fotoni
  // sul vetro: l'alone e' l'unica cosa che non puo' verificare. Questi due non
  // lo misurano -- non si puo' da remoto -- ma dicono se il completo periodico
  // sta scattando o se una configurazione lo sta rimandando.
  j += "\"epd_parziali_da_full\":" + String(app_epd_parziali_da_full()) + ",";
  {
    // L'ORA, non "da quanto tempo": un istante resta vero anche quando nessuno
    // rilegge la pagina da un pezzo. Stessa regola dell'ora dell'ultimo
    // pacchetto di un nodo. null finche' un completo non c'e' stato.
    const time_t tf = app_epd_ultimo_full_ts();
    char buf[24];
    if (tf > 0 && rtctime_format(tf, "%Y-%m-%d %H:%M:%S", buf, sizeof(buf))) {
      j += "\"epd_ultimo_full\":"; appendJsonString(j, buf); j += ',';
    } else {
      j += "\"epd_ultimo_full\":null,";
    }
  }
  j += "\"invii_interrotti\":" + String(s_invii_interrotti)   + ",";
  // Il giro piu' lungo, e dove. Serve a distinguere due cose che nei CSV
  // hanno lo stesso aspetto -- un buco: "si e' riavviata" (lo dicono
  // reset_reason e boot_count) e "e' rimasta ferma dentro una chiamata".
  // Il disegno del pannello NON e' qui dentro: 2,6 s sono normali e
  // coprirebbero per sempre tutto il resto.
  j += "\"loop_max_ms\":"   + String(app_loop_max_ms()) + ",";
  j += "\"loop_max_dove\":"; appendJsonString(j, app_loop_max_dove()); j += ',';
  {
    const time_t lt = app_loop_max_ts();
    char lbuf[24];
    if (lt > 0 && rtctime_format(lt, "%Y-%m-%d %H:%M:%S", lbuf, sizeof(lbuf))) {
      j += "\"loop_max_ora\":"; appendJsonString(j, lbuf); j += ',';
    } else {
      j += "\"loop_max_ora\":null,";
    }
  }
  j += "\"loop_lenti\":"    + String(app_loop_lenti()) + ",";
  j += "\"wdt_armato\":";   j += (app_wdt_armato() ? "true" : "false"); j += ',';
  j += "\"wdt_timeout_s\":" + String(app_wdt_timeout_s()) + ",";

  j += "\"reset_reason\":\"" + String(app_reset_reason())    + "\",";
  j += "\"boot_count\":"      + String(app_boot_count())      + ",";
  j += "\"uptime\":" + String(millis() / 1000) + ",";
  j += "\"heap\":"   + String(ESP.getFreeHeap());
  j += '}';
  net_server().send(200, "application/json", j);
}

// Definita piu' sotto insieme agli altri handler del pannello: qui serve
// perche' due endpoint delle immagini rispondono con l'elenco aggiornato
// delle pagine, invece di far fare al client una seconda richiesta.
static void handleApiPannello();

// ---------------------------------------------------------------------
//  Pagine immagine — /images/<nome>.bin
//
//  Il contratto e' in sd_logger.h: 15.000 byte esatti, gia' impacchettati
//  da www/dither.html. La dimensione si controlla QUI, quando si carica:
//  un file storto scoperto al momento di disegnarlo si vedrebbe come una
//  pagina sbilenca, che somiglia a un guasto del pannello invece che a un
//  upload sbagliato.
// ---------------------------------------------------------------------
static void imgListCb(const char* nome, size_t bytes, void* arg) {
  String* j = (String*)arg;
  if (!j->endsWith("[")) *j += ',';
  *j += "{\"nome\":"; appendJsonString(*j, nome);
  *j += ",\"byte\":"; *j += (unsigned long)bytes;
  *j += ",\"ok\":"; *j += (bytes == IMG_BYTES_ESATTI) ? "true" : "false";
  *j += '}';
}

static void handleApiImmagini() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  WebServer& srv = net_server();
  const int  da     = srv.hasArg("da")     ? srv.arg("da").toInt()     : 0;
  int        quante = srv.hasArg("quante") ? srv.arg("quante").toInt() : 12;
  const String cerca = srv.hasArg("cerca") ? srv.arg("cerca") : String("");

  // Un tetto c'e' comunque, ma ora e' dichiarato e la pagina sa quante ne
  // restano: il vecchio 32 fisso faceva sparire la trentatreesima immagine
  // senza dirlo a nessuno.
  if (quante < 1)  quante = 1;
  if (quante > 48) quante = 48;

  int totale = 0;
  String j = "{\"sd\":";
  j += sd_mounted() ? "true" : "false";
  j += ",\"byte_attesi\":"; j += (unsigned long)IMG_BYTES_ESATTI;
  j += ",\"da\":";     j += da;
  j += ",\"quante\":"; j += quante;
  j += ",\"immagini\":[";
  sd_img_page(imgListCb, &j, da, quante, cerca.c_str(), &totale);
  j += "],\"totale\":"; j += totale;
  j += "}";
  srv.send(200, "application/json", j);
}

// POST /api/immagini?nome=xxx  (multipart, campo "img")
static File   s_imgFile;
static bool   s_imgOk    = false;
static size_t s_imgBytes = 0;
static char   s_imgNome[IMG_NOME_MAX + 1] = "";

static void handleApiImmaginiDone() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!s_imgOk) {
    srv.send(500, "text/plain", "scrittura fallita (microSD assente?)");
    return;
  }
  // Il controllo che vale: 15.000 byte esatti, o non e' un'immagine per
  // questo pannello. Il file storto si cancella subito, invece di restare
  // sulla card ad aspettare di essere scelto per sbaglio.
  if (s_imgBytes != IMG_BYTES_ESATTI) {
    sd_img_delete(s_imgNome);
    String m = "servono 15000 byte esatti, ne sono arrivati ";
    m += (unsigned long)s_imgBytes;
    srv.send(400, "text/plain", m);
    return;
  }
  srv.send(200, "text/plain", "ok");
}

static void handleApiImmaginiChunk() {
  WebServer& srv = net_server();
  HTTPUpload& up = srv.upload();

  switch (up.status) {
    case UPLOAD_FILE_START: {
      s_imgOk    = false;
      s_imgBytes = 0;
      s_imgNome[0] = '\0';
      if (!net_webAuthOk()) return;

      // Il nome puo' arrivare dalla query string o, in mancanza, dal nome
      // del file caricato: sanificato in entrambi i casi (lista bianca),
      // perche' finisce dentro un path.
      String n = srv.hasArg("nome") ? srv.arg("nome") : up.filename;
      if (n.endsWith(".bin")) n.remove(n.length() - 4);
      if (!sd_img_name_safe(n.c_str(), s_imgNome, sizeof(s_imgNome))) return;

      s_imgFile = sd_img_open_for_write(s_imgNome);
      s_imgOk   = (bool)s_imgFile;
      break;
    }

    case UPLOAD_FILE_WRITE:
      if (s_imgOk) {
        s_imgOk = (s_imgFile.write(up.buf, up.currentSize) == up.currentSize);
        s_imgBytes += up.currentSize;
      }
      break;

    case UPLOAD_FILE_END:
      if (s_imgFile) s_imgFile.close();
      break;

    case UPLOAD_FILE_ABORTED:
      // Come per la dashboard e per Update.abort(): un trasferimento caduto
      // a meta' non deve lasciare un handle appeso ne' un file mezzo scritto
      // che poi verrebbe disegnato.
      if (s_imgFile) s_imgFile.close();
      if (s_imgNome[0]) sd_img_delete(s_imgNome);
      s_imgOk = false;
      break;

    default:
      break;
  }
}

static void handleApiImmaginiElimina() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("nome")) { srv.send(400, "text/plain", "manca nome"); return; }

  const String nome = srv.arg("nome");
  const bool ok = sd_img_delete(nome.c_str());

  // Le pagine che puntavano a quell'immagine restano: si vedrebbero come
  // "immagine non disponibile" sul pannello. Toglierle d'ufficio sarebbe
  // peggio — l'utente potrebbe ricaricare lo stesso nome fra un minuto, e
  // si ritroverebbe la pagina sparita senza averlo chiesto.
  srv.send(ok ? 200 : 404, "text/plain", ok ? "ok" : "non trovata");
}

// GET /api/epd/totale — quanti refresh ha fatto il pannello, davvero
//
// Si contano le righe dei file in /epd/, ora, invece di tenere un totale in
// NVS aggiornato ad ogni refresh: la flash ha cicli di erase finiti e li'
// dentro ci vivono le pagine e il registro dei nodi, mentre la card no. Il
// costo si sposta dal consumo continuo di una memoria che si logora al
// conteggio occasionale di un file -- e questa rotta la chiama una persona
// ogni tanto, non il firmware ogni cinque minuti.
//
// Si contano i '\n' a blocchi da 512 byte e si toglie l'intestazione: sono
// ~4700 righe al mese, cioe' ~200 kB da leggere per un anno intero.
static void handleApiEpdTotale() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!sd_mounted()) { srv.send(503, "text/plain", "microSD non montata"); return; }

  File dir = SD.open("/epd");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    srv.send(200, "application/json", "{\"totale\":0,\"mesi\":[]}");
    return;
  }

  String j = "{\"mesi\":[";
  uint32_t totale = 0;
  bool primo = true;
  uint8_t buf[512];

  for (;;) {
    File f = dir.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      String nome = f.name();
      const int barra = nome.lastIndexOf('/');
      if (barra >= 0) nome = nome.substring(barra + 1);
      if (nome.endsWith(".csv")) {
        uint32_t righe = 0;
        int letti;
        while ((letti = f.read(buf, sizeof(buf))) > 0)
          for (int i = 0; i < letti; i++) if (buf[i] == '\n') righe++;
        if (righe > 0) righe--;                 // l'intestazione non e' un refresh
        totale += righe;
        nome.remove(nome.length() - 4);
        if (!primo) j += ',';
        primo = false;
        j += "{\"mese\":"; appendJsonString(j, nome.c_str());
        j += ",\"refresh\":"; j += righe; j += '}';
      }
    }
    f.close();
  }
  dir.close();

  j += "],\"totale\":"; j += totale;
  j += ",\"da_questo_avvio\":"; j += app_epd_refresh();
  j += '}';
  srv.send(200, "application/json", j);
}

// GET /api/epd/registro?m=AAAA-MM — il registro dei refresh del pannello
//
// Senza questa rotta il registro sarebbe un file scritto e mai letto: la card
// si legge solo smontandola, ed e' esattamente il modo in cui un contatore
// diventa inutile. Con m si sceglie il mese; senza, quello corrente.
static void handleApiEpdRegistro() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  char nomeFile[40];
  if (srv.hasArg("m")) {
    const String m = srv.arg("m");
    // Il nome non arriva mai dalla rete come pezzo di path: si accetta solo
    // AAAA-MM e lo si ricompone qui. Stessa regola dei nomi delle immagini.
    if (m.length() != 7 || m[4] != '-') { srv.send(400, "text/plain", "mese non valido"); return; }
    for (size_t i = 0; i < m.length(); i++)
      if (i != 4 && !isdigit((unsigned char)m[i])) { srv.send(400, "text/plain", "mese non valido"); return; }
    snprintf(nomeFile, sizeof(nomeFile), "/epd/%s.csv", m.c_str());
  } else {
    time_t ora = time(nullptr);
    struct tm tmv;
    localtime_r(&ora, &tmv);
    snprintf(nomeFile, sizeof(nomeFile), "/epd/%04d-%02d.csv", tmv.tm_year + 1900, tmv.tm_mon + 1);
  }

  File f = SD.open(nomeFile, FILE_READ);
  if (!f) { srv.send(404, "text/plain", "nessun registro per quel mese"); return; }
  streamFileLimitato(srv, f, "text/csv; charset=utf-8");
  f.close();
}

// GET /api/immagini/mini?nome=xxx — 600 byte invece di 15.000
//
// La stessa immagine sottocampionata 5x. Serve per la galleria: dodici
// anteprime piene sono 180 kB su un web server SINCRONO, cioe' altrettanto
// tempo in cui l'hub non preleva i DATA dei nodi dal driver ESP-NOW, che
// tiene solo l'ultimo. Con le miniature sono 7,2 kB.
//
// Si calcola ad ogni richiesta invece di tenerla sulla card: sono pochi ms di
// lettura, e una miniatura salvata sarebbe un secondo file da creare,
// cancellare e tenere allineato all'originale -- tre modi in piu' di andare
// fuori sincrono per risparmiare una cosa che non costa.
static void handleApiImmaginiMini() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("nome")) { srv.send(400, "text/plain", "manca nome"); return; }

  uint8_t mini[MINI_BYTES];
  if (!sd_img_mini(srv.arg("nome").c_str(), mini)) {
    srv.send(404, "text/plain", "non trovata o non valida");
    return;
  }
  srv.setContentLength(MINI_BYTES);
  srv.send(200, "application/octet-stream", "");
  srv.sendContent((const char*)mini, MINI_BYTES);
}

// GET /api/pannello/anteprima — quello che il pannello sta mostrando.
//
// Serve perche' il pannello, da remoto, era l'unica cosa di questa scheda che
// non si poteva guardare: /api/pannello dice QUALE pagina e' in mostra, non
// che cosa c'e' sopra, e le due cose divergono per qualunque motivo -- una
// immagine mancante, un nodo senza dati, un refresh a meta'.
//
// Sono i byte della tela, quindi lo stesso formato dei .bin: il browser li
// disegna con l'unpack() che ha gia', e chi verifica da fuori puo' anche
// confrontarli bit a bit con l'immagine che si aspetta.
//
// Una write sola da 15 kB (piu' l'header): il taglio a budget dei file su SD
// qui non serve, perche' non c'e' un ciclo che possa restare appeso -- al
// massimo si paga la singola write bloccata, che e' il costo minimo che il
// core impone comunque.
static void handleApiPannelloAnteprima() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  srv.setContentLength(app_tela_bytes());
  srv.send(200, "application/octet-stream", "");
  srv.sendContent((const char*)app_tela(), app_tela_bytes());
}

// GET /api/immagini/scarica?nome=xxx — i 15.000 byte, per l'anteprima nel
// browser (unpack/paint sono gia' scritti in www/dither.html).
static void handleApiImmaginiScarica() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("nome")) { srv.send(400, "text/plain", "manca nome"); return; }

  File f = sd_img_open(srv.arg("nome").c_str());
  if (!f) { srv.send(404, "text/plain", "non trovata"); return; }

  streamFileLimitato(srv, f, "application/octet-stream");
  f.close();
}

// POST /api/pannello/aggiungi?param=nome — crea una pagina immagine
static void handleApiPannelloAggiungi() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  // Senza `param` si aggiunge una pagina che non ha parametri: oggi solo il
  // grafico. Con `param` si intende un'immagine, che e' il caso storico e
  // resta quello di default per non cambiare le chiamate gia' scritte.
  const String tipo = srv.hasArg("tipo") ? srv.arg("tipo") : String("");

  // Una pagina di dettaglio per nodo. Il nodo si indica per NOME e non per
  // indice: gli indici si spostano quando un nodo viene dimenticato, e la
  // pagina finirebbe per mostrarne un altro senza dirlo.
  if (tipo == "dettaglio") {
    const String nodo = srv.hasArg("param") ? srv.arg("param") : String("");
    if (!nodo.length()) { srv.send(400, "text/plain", "manca param"); return; }
    if (paginaGiaPresente(PT_DETTAGLIO, nodo.c_str())) {
      srv.send(409, "text/plain", "il dettaglio di quel nodo c'e' gia'");
      return;
    }
    if (pages_add(PT_DETTAGLIO, nodo.c_str()) < 0) {
      srv.send(507, "text/plain", "non c'e' piu' posto nell'elenco delle pagine");
      return;
    }
    pages_save();
    handleApiPannello();
    return;
  }

  if (tipo == "grafico") {
    if (paginaGiaPresente(PT_GRAFICO, nullptr)) {
      srv.send(409, "text/plain", "la pagina del grafico c'e' gia'");
      return;
    }
    if (pages_add(PT_GRAFICO, "") < 0) {
      srv.send(507, "text/plain", "non c'e' piu' posto nell'elenco delle pagine");
      return;
    }
    pages_save();
    handleApiPannello();
    return;
  }

  const String nome = srv.hasArg("param") ? srv.arg("param") : String("");
  if (nome.length() == 0) { srv.send(400, "text/plain", "manca param"); return; }
  if (!sd_img_exists(nome.c_str())) {
    srv.send(404, "text/plain", "immagine non presente sulla card");
    return;
  }
  if (paginaGiaPresente(PT_IMMAGINE, nome.c_str())) {
    srv.send(409, "text/plain", "quell'immagine e' gia' fra le pagine");
    return;
  }
  if (pages_add(PT_IMMAGINE, nome.c_str()) < 0) {
    srv.send(507, "text/plain", "non c'e' piu' posto nell'elenco delle pagine");
    return;
  }
  pages_save();
  handleApiPannello();
}

// C'e' gia' una pagina di questo tipo (e, per le immagini, con questo
// parametro)? Serve a non accettare doppioni: due pagine identiche nella
// rotazione mostrerebbero la stessa cosa due volte di fila, e ogni cambio
// pagina costa un refresh completo da 2,2 s. Sul pannello un doppione non si
// vede come un errore — si vede come una rotazione che si inceppa.
static bool paginaGiaPresente(uint8_t tipo, const char* param) {
  for (uint8_t i = 0; i < pages_slots(); i++) {
    const PageCfg* pg = pages_get(i);
    if (pg == nullptr || !pg->usato || pg->tipo != tipo) continue;
    if (param == nullptr || *param == '\0') return true;      // tipo senza parametro
    if (strcmp(pg->param, param) == 0) return true;
  }
  return false;
}

static void handleApiPannelloSposta() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("i") || !srv.hasArg("dir")) { srv.send(400, "text/plain", "manca i o dir"); return; }

  const int i   = srv.arg("i").toInt();
  const int dir = srv.arg("dir").toInt();
  if (i < 0 || i >= pages_slots()) { srv.send(400, "text/plain", "indice fuori range"); return; }

  if (!pages_move((uint8_t)i, dir)) {
    // I "no" hanno ragioni diverse (gia' agli estremi, slot 0, slot libero)
    // ma per chi guarda la pagina sono la stessa cosa: la freccia non aveva
    // dove portare. Agli estremi le frecce sono gia' disabilitate — questo
    // resta per il caso di due schede aperte sulla stessa scheda.
    srv.send(409, "text/plain", "la pagina non si puo' spostare li'");
    return;
  }
  pages_save();
  handleApiPannello();
}

static void handleApiPannelloRimuovi() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("i")) { srv.send(400, "text/plain", "manca i"); return; }

  if (!pages_remove((uint8_t)srv.arg("i").toInt())) {
    srv.send(400, "text/plain", "slot non rimovibile");
    return;
  }
  pages_save();
  handleApiPannello();
}

// ---------------------------------------------------------------------
//  Pagina /pannello — il telecomando del display.
//
//  In PROGMEM come la pagina dei nodi e /dashboard-upload, e per la stessa
//  ragione: la pagina principale puo' essere sostituita da una copia sulla
//  card, e una funzione che vive nel firmware resta raggiungibile comunque.
// ---------------------------------------------------------------------
static const char PANNELLO_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<title>MeteoHub-S3 &mdash; Pannello</title><style>
 :root{
  --bg:#0e0e10; --card:#1a1a1d; --card2:#212125; --bordo:#2e2e33;
  --txt:#ececee; --dim:#8e8e96; --acc:#3987e5; --ok:#3fb950; --dan:#c9342d;
  --r:14px;
 }
 *{box-sizing:border-box}
 body{font-family:system-ui,-apple-system,Segoe UI,Arial,sans-serif;background:var(--bg);
  color:var(--txt);margin:0;padding:12px 12px calc(20px + env(safe-area-inset-bottom));
  -webkit-text-size-adjust:100%}
 .wrap{max-width:760px;margin:0 auto}
 h1{font-size:1.15rem;margin:.2rem 0 1rem;display:flex;align-items:center;gap:.5rem}
 h2{font-size:.8rem;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);
  margin:1.4rem 0 .6rem;font-weight:600}
 .card{background:var(--card);border:1px solid var(--bordo);border-radius:var(--r);
  padding:14px;margin-bottom:10px}
 .muted{color:var(--dim);font-size:.82rem;line-height:1.5;margin:.5rem 0 0}

 /* --- tocco: nulla sotto i 44 px, e 16px sugli input o iOS zooma da solo --- */
 button,select,input,textarea{font-family:inherit;font-size:16px}
 button{min-height:44px;padding:0 16px;border:0;border-radius:10px;background:var(--acc);
  color:#fff;font-weight:600;cursor:pointer;-webkit-tap-highlight-color:transparent}
 button:active{transform:scale(.98)}
 button.sec{background:var(--card2);border:1px solid var(--bordo);color:var(--txt);font-weight:500}
 button.dan{background:transparent;border:1px solid #5a2a28;color:#e08b86;font-weight:500}
 button.full{width:100%}
 select,input[type=text],input[type=number],input[type=time],textarea{
  background:#141417;color:var(--txt);border:1px solid var(--bordo);border-radius:10px;
  padding:11px 12px;min-height:44px;width:100%}
 textarea{min-height:92px;resize:vertical;line-height:1.45}
 input[type=file]{width:100%;color:var(--dim);font-size:.85rem}

 /* --- interruttore --- */
 .sw{display:flex;align-items:center;justify-content:space-between;gap:12px;
  min-height:44px;cursor:pointer;user-select:none}
 .sw input{position:absolute;opacity:0;pointer-events:none}
 .sw .track{flex:none;width:50px;height:30px;border-radius:15px;background:#3a3a41;
  position:relative;transition:background .15s}
 .sw .track::after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;
  border-radius:50%;background:#fff;transition:transform .15s}
 .sw input:checked + .track{background:var(--ok)}
 .sw input:checked + .track::after{transform:translateX(20px)}

 /* --- una pagina del pannello --- */
 .pg{background:var(--card);border:1px solid var(--bordo);border-radius:var(--r);
  padding:14px;margin-bottom:10px}
 .pg.now{border-color:var(--ok);background:linear-gradient(180deg,rgba(63,185,80,.07),transparent 60%)}
 .pg .top{display:flex;align-items:center;gap:.5rem;margin-bottom:.2rem}
 .pg .nome{font-size:1.05rem;font-weight:600;text-transform:capitalize}
 .pg .par{color:var(--dim);font-size:.85rem;font-weight:400;text-transform:none}
 .badge{margin-left:auto;flex:none;font-size:.7rem;font-weight:700;letter-spacing:.04em;
  padding:4px 9px;border-radius:99px;background:rgba(63,185,80,.15);color:var(--ok)}
 .riga{display:flex;align-items:center;justify-content:space-between;gap:12px;
  padding:8px 0;border-top:1px solid var(--bordo);margin-top:10px}
 .riga:first-of-type{margin-top:4px}
 .riga label{color:var(--dim);font-size:.85rem}
 .riga select{width:auto;min-width:118px}
 .azioni{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:12px}
 .azioni button{padding:0 8px;font-size:.88rem}
 .azioni .sol{grid-column:1/-1}

 .duo{display:flex;gap:8px;align-items:center}
 .duo select{width:auto;min-width:90px}
 /* Il campo dell'ora si dimensiona da se' sul contenuto: su iOS un width:100%
    lo stira per tutta la riga e le due meta' di "dalle ... alle" si accavallano. */
 .duo input[type=time]{width:auto;min-width:104px}
 .fine{display:flex;gap:8px;margin-top:12px}
 .fine button{flex:1}
 .esito{font-size:.82rem;color:var(--ok);min-height:1.1em;margin-top:.5rem}
 .esito.err{color:#e08b86}

 /* --- immagini --- */
 .im{background:var(--card2);border:1px solid var(--bordo);border-radius:12px;
  padding:10px;overflow:hidden}
 .im canvas{width:100%;height:auto;display:block;border-radius:8px;background:#fff;
  image-rendering:pixelated}
 .im .nm{display:flex;align-items:baseline;gap:.5rem;margin:8px 2px}
 .im .nm b{font-size:.95rem}
 .im .nm span{color:var(--dim);font-size:.75rem;margin-left:auto}
 .im .azioni{margin-top:8px}

 /* --- una riga dell'elenco: miniatura a sinistra, comandi a destra ---
    L'anteprima sta NELL'elenco, non in una galleria a parte: prima ogni
    immagine compariva due volte, e quella che mostrava la figura era
    proprio quella da cui non si governava la pagina. --- */
 .cap{display:flex;gap:12px;align-items:flex-start}
 .mini{flex:none;width:116px;border-radius:8px;overflow:hidden;background:#fff;
  border:1px solid var(--bordo)}
 .mini canvas{width:100%;height:auto;display:block;image-rendering:pixelated}
 .mini.gen{background:var(--card2);color:var(--dim);height:87px;font-size:1.9rem;
  display:flex;align-items:center;justify-content:center}
 .testa{flex:1;min-width:0}
 .ord{flex:none;display:flex;flex-direction:column;gap:5px}
 .ord button{min-height:0;height:34px;width:40px;padding:0;font-size:.95rem;
  background:var(--card2);border:1px solid var(--bordo);color:var(--txt)}
 .ord button:disabled{opacity:.25}
 .conta{font-weight:400;text-transform:none;letter-spacing:0}
 @media (max-width:420px){ .mini{width:92px} .mini.gen{height:69px} }

 .arch p{background:var(--card2);border-left:3px solid var(--bordo);border-radius:0 8px 8px 0;
  padding:10px 12px;margin:8px 0;font-size:.9rem;cursor:pointer;min-height:44px;
  display:flex;align-items:center}
 .arch p:active{border-left-color:var(--acc)}
 nav{margin:1.8rem 0 .5rem;display:flex;flex-wrap:wrap;gap:.4rem 1rem;font-size:.85rem}
 a{color:var(--acc);text-decoration:none}
 table.img{width:100%;border-collapse:collapse}
 table.img td{padding:.45rem .3rem;border-top:1px solid var(--bordo);vertical-align:middle}
 table.img tr:first-child td{border-top:0}
 table.img .nome{font-weight:600;word-break:break-all}
 table.img .stato{font-size:.78rem;color:var(--dim)}
 table.img .cmd{text-align:right;white-space:nowrap}
 table.img .cmd button{margin-left:.3rem}
 table.img .vista{background:#fff;border-radius:6px;padding:6px;margin:.2rem 0}
 table.img .vista canvas{width:100%;max-width:400px;height:auto;display:block;
   margin:0 auto;image-rendering:pixelated}
 .ante{background:#fff;border-radius:8px;overflow:hidden;margin:.1rem 0 .45rem}
 .ante canvas{width:100%;height:auto;display:block;image-rendering:pixelated}
 @media (max-width:420px){
  .azioni{grid-template-columns:1fr}
  .riga{flex-wrap:wrap}
 }
</style></head><body><div class="wrap">
<h1>Pannello e-ink</h1>

<div class="card" id="ora">
 <div class="muted" style="margin:0">A schermo adesso</div>
 <div style="font-size:1.35rem;font-weight:700;margin:.25rem 0 .45rem" id="oraNome">&mdash;</div>
 <div class="ante"><canvas id="ante" width="400" height="300"></canvas></div>
 <div class="muted" id="sante" style="margin:0 0 .55rem">&mdash;</div>
 <button class="sec full" id="bant">Rileggi l'anteprima</button>
 <button class="sec full" id="brf" style="margin-top:.4rem">Aggiorna il pannello adesso</button>
 <div class="esito" id="srf"></div>
</div>

<h2>Pagine del pannello <span class="conta" id="conta"></span></h2>
<div id="lista"></div>
<div class="esito" id="sord"></div>

<div class="card">
 <label class="sw"><span>Messaggio anche sulla pagina nodi</span>
  <input type="checkbox" id="fas"><span class="track"></span></label>
 <p class="muted">La fascia compare solo quando c'&egrave; un messaggio attivo,
 e si prende 70 px: con due nodi la temperatura passa da 24 a 18 pt. Si
 guadagna il messaggio sempre sotto gli occhi, si perde corpo sui numeri.</p>
 <div class="esito" id="sf"></div>
</div>

<h2>Cambio automatico</h2>
<div class="card">
 <label class="sw"><span>Ruota fra le pagine attive</span>
  <input type="checkbox" id="rot"><span class="track"></span></label>
 <div class="riga">
  <label>Non ruotare dalle<br><span style="font-size:.78rem">a quarti d'ora</span></label>
  <div class="duo">
   <input type="time" id="sda" step="900"><span class="muted" style="margin:0">alle</span><input type="time" id="sa" step="900">
  </div>
 </div>
 <div class="riga">
  <label>In quelle ore mostra</label>
  <select id="spag" style="flex:1"></select>
 </div>
 <div class="esito" id="ss"></div>
 <p class="muted">Con una sola pagina attiva non ruota: non c'&egrave; dove andare, e un
 cambio &egrave; sempre un refresh completo (~2,2 s, e lampeggia).</p>
 <p class="muted">&laquo;Fra tutte quelle sulla card&raquo; pesca nell'archivio
 completo delle immagini, non solo fra le pagine in elenco: non consuma uno
 slot, e la stessa non esce due notti di fila. Quale sia toccata a stanotte lo
 dice l'intestazione qui sopra.</p>
 <p class="muted">Scegliendo una pagina, in quelle ore il pannello ci va e
 <b>smette di aggiornarsi</b> del tutto: due refresh invece di ~640. Con
 &laquo;nessuna&raquo; si ferma solo la rotazione, come prima. I nodi
 continuano a essere ricevuti e registrati: si ferma il display, non l'hub.</p>
</div>

<h2>Messaggio</h2>
<div class="card">
 <textarea id="txt" maxlength="200" placeholder="Il bigliettino sul frigo&hellip;"></textarea>
 <div class="riga">
  <label>Scade fra</label>
  <select id="min">
   <option value="0">mai</option><option value="60">1 ora</option>
   <option value="240">4 ore</option><option value="720">12 ore</option>
   <option value="1440">1 giorno</option><option value="4320">3 giorni</option>
  </select>
 </div>
 <label class="sw"><span>Urgente <span class="muted">&mdash; va subito sul pannello</span></span>
  <input type="checkbox" id="urg"><span class="track"></span></label>
 <div class="fine">
  <button id="bm">Manda al pannello</button>
  <button class="dan" id="bx" style="flex:0 0 auto">Togli</button>
 </div>
 <div class="esito" id="sm"></div>
 <p class="muted" id="att"></p>
 <div class="arch" id="arch"></div>
</div>

<h2>Immagini sulla card <span class="conta" id="contaCard"></span></h2>
<div class="card">
 <div class="riga">
  <input type="search" id="cerca" placeholder="cerca per nome&hellip;"
         autocomplete="off" style="flex:1">
  <button class="sec" id="cprev" title="pagina precedente">&larr;</button>
  <button class="sec" id="cnext" title="pagina successiva">&rarr;</button>
 </div>
 <p class="muted" id="cardNota" style="margin:.2rem 0 0">&mdash;</p>
</div>
<div class="card" id="limgBox">
 <table class="img"><tbody id="limg"></tbody></table>
</div>
<div class="card" id="cardVuota" style="display:none">
 <p class="muted" style="margin:0">Nessuna immagine con questo nome.</p>
</div>

<h2>Altre pagine</h2>
<div class="card">
 <button class="sec full" id="bgraf">Aggiungi la pagina del grafico</button>
 <p class="muted">Temperatura dei nodi nelle ultime 24 ore, a piena pagina. Lo
 storico si ricostruisce dai CSV sulla card, quindi il grafico e&grave; pieno
 subito dopo un riavvio invece di impiegare un giorno a formarsi.</p>
 <div class="esito" id="sgraf"></div>
</div>
<div class="card">
 <div id="bdett"></div>
 <p class="muted">Una pagina per nodo con tutto quello che si sa di lui:
 rugiada, percepiti, acqua nell'aria, minimi e massimi del giorno, variazione
 a tre ore, pressione e trend. Sono gli stessi valori che stavano schiacciati
 nella pagina dei nodi fino alla v24: qui hanno una riga per uno.</p>
 <div class="esito" id="sdett"></div>
</div>

<h2>Aggiungere un'immagine</h2>
<div class="card">
 <button class="full" onclick="location.href='/immagini'">Componi un'immagine</button>
 <p class="muted">Ritaglio, luminosit&agrave;, gamma, dithering e il testo sopra la
 foto, tutto nel browser. Qui sotto si carica un <code>.bin</code> gi&agrave;
 pronto (15.000 byte esatti).</p>
 <div class="riga" style="border:0;padding-top:0"><input type="file" id="fimg" accept=".bin"></div>
 <div class="duo"><input type="text" id="nimg" placeholder="nome" maxlength="20">
  <button class="sec" id="bimg" style="flex:0 0 auto">Carica</button></div>
 <div class="esito" id="simg"></div>
</div>

<nav>
 <a href="/">Nodi</a><a href="/pannello">Pannello</a><a href="/immagini">Componi immagine</a>
 <a href="/pagine">Pagine</a><a href="/api">API</a><a href="/update">Aggiorna firmware</a>
</nav>
<script>
const E=document.getElementById.bind(document);
function esc(x){return String(x==null?'':x).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function ora(u){return u?new Date(u*1000).toLocaleString('it-IT',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'}):'';}
function post(u){return fetch(u,{method:'POST'});}
function flash(el,t,err){el.textContent=t;el.className='esito'+(err?' err':'');setTimeout(()=>{el.textContent='';},3000);}

// Durate proposte, non un campo libero: sotto il minuto non ha senso (un cambio
// pagina e' un refresh completo), e su un telefono una tendina si tocca meglio
// di un campo numerico.
const DUR=[[60,'1 min'],[120,'2 min'],[300,'5 min'],[600,'10 min'],[900,'15 min'],
           [1800,'30 min'],[3600,'1 ora'],[10800,'3 ore']];
function optDur(v){
 let o='',visto=false;
 DUR.forEach(([s,t])=>{const sel=(s==v);if(sel)visto=true;o+='<option value="'+s+'"'+(sel?' selected':'')+'>'+t+'</option>';});
 if(!visto)o='<option value="'+v+'" selected>'+Math.round(v/60)+' min</option>'+o;
 return o;
}
// Le due tendine da 24 voci non ci sono piu': sono campi orario, che il
// server manda e riceve gia' come "HH:MM". step=900 li fa muovere di un
// quarto d'ora per volta, che e' la granularita' vera della fascia -- un
// campo libero al minuto accetterebbe le 21:07 e il firmware lo
// arrotonderebbe alle 21:00, cioe' mostrerebbe un valore mai chiesto.

// Ogni POST puo' fallire con un testo che dice perche'. Prima si faceva
// .then(r=>r.json()) senza guardare r.ok: su una risposta d'errore in
// text/plain la promise andava in eccezione e il pulsante NON FACEVA NIENTE,
// in silenzio. E' cosi' che il 507 "non c'e' piu' posto nell'elenco" e'
// rimasto invisibile per giorni, con gli slot esauriti.
function postJson(u,esito){
 return post(u).then(r=>{
  if(!r.ok) return r.text().then(t=>{throw new Error(t||('errore '+r.status));});
  return r.json();
 }).then(d=>{render(d);return d;})
  .catch(e=>{flash(esito||E('sord'),e.message||'hub non raggiungibile',1);});
}

// Disegna 15.000 byte su un canvas: 400x300 a 1 bit, MSB per primo, 1 =
// bianco. Una funzione sola per le anteprime delle immagini E per quella del
// pannello, perche' e' lo stesso formato -- due copie divergerebbero, ed e'
// la stessa regola per cui a bordo l'ora la disegna una funzione sola.
function dipingiBit(cv,by,w,h){
 const stride=w>>3, ctx=cv.getContext('2d'), img=ctx.createImageData(w,h);
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){
  const bit=(by[y*stride+(x>>3)]>>(7-(x&7)))&1,o=(y*w+x)*4,v=bit?255:0;
  img.data[o]=img.data[o+1]=img.data[o+2]=v;img.data[o+3]=255;}
 cv.width=w; cv.height=h;
 ctx.putImageData(img,0,0);}
function dipingi15k(cv,by){ dipingiBit(cv,by,400,300); }

// L'anteprima di cio' che il pannello mostra ADESSO. Non si aggiorna da sola
// col resto della pagina (ogni 15 s): sono 15 kB su un server sincrono, e
// chiederli in continuazione toglierebbe tempo ai nodi e all'OTA. Si rilegge
// a mano, e da sola dopo le azioni che ridisegnano il pannello.
function leggiAnte(){
 const b=E('bant'); b.disabled=true; E('sante').textContent='lettura\u2026';
 fetch('/api/pannello/anteprima',{cache:'no-store'})
  .then(r=>r.ok?r.arrayBuffer():Promise.reject('HTTP '+r.status))
  .then(x=>{const by=new Uint8Array(x);
   if(by.length!=15000){E('sante').textContent=by.length+' byte invece di 15000';return;}
   dipingi15k(E('ante'),by);
   let neri=0; for(let i=0;i<by.length;i++){let v=by[i]; for(let k=0;k<8;k++){if(!(v&1))neri++; v>>=1;}}
   E('sante').textContent='letta alle '+new Date().toLocaleTimeString()+
     ' \u2014 '+(neri/1200).toFixed(1)+'% di nero';})
  .catch(e=>{E('sante').textContent='anteprima non disponibile ('+e+')';})
  .finally(()=>{b.disabled=false;});
}

// Due cache separate, e la separazione NON e' pignoleria: una miniatura e
// un'immagine piena hanno la stessa chiave (il nome) ma dimensioni diverse,
// e mescolarle vorrebbe dire disegnare 600 byte come se fossero 15.000.
// Si scaricano una volta sola: la stessa figura puo' stare in piu' slot, e la
// pagina si ridisegna ogni 15 secondi.
const MINI={}, PIENA={};
// Miniatura, non immagine piena: 600 byte contro 15.000. Con una dozzina di
// riquadri la differenza e' fra 7 kB e 180 kB chiesti a un server SINCRONO --
// cioe' fra un'attesa impercettibile e un pezzo di minuto in cui l'hub non
// preleva i DATA dei nodi.
function anteprima(box,nome){
 const cv=document.createElement('canvas'); cv.width=80; cv.height=60;
 box.appendChild(cv);
 if(MINI[nome]){dipingiBit(cv,MINI[nome],80,60);return;}
 fetch('/api/immagini/mini?nome='+encodeURIComponent(nome),{cache:'no-store'})
  .then(r=>r.ok?r.arrayBuffer():Promise.reject())
  .then(b=>{MINI[nome]=new Uint8Array(b);dipingiBit(cv,MINI[nome],80,60);})
  .catch(()=>{box.className='mini gen';box.innerHTML='&#9888;';});
}

// Si scaricano solo le miniature che entrano davvero nello schermo: con
// cinquanta immagini, quelle sotto il bordo non costano niente finche' non le
// si va a cercare. Senza observer si torna al comportamento di prima, che e'
// corretto e solo piu' avido.
const VISTA = ('IntersectionObserver' in window)
 ? new IntersectionObserver((voci,obs)=>{voci.forEach(v=>{
     if(!v.isIntersecting) return;
     obs.unobserve(v.target);
     anteprima(v.target, v.target.dataset.mini);
   });},{rootMargin:'200px'})
 : null;
function anteprimaPigra(box,nome){
 box.dataset.mini=nome;
 if(VISTA) VISTA.observe(box); else anteprima(box,nome);
}

const GLIFO={nodi:'&#9925;',messaggio:'&#9993;',bianca:'&#9634;',grafico:'&#128200;'};
var ULTIMO=null, SULLACARD=[];

function render(d){
 ULTIMO=d;
 // Il file della notte non e' una pagina dell'elenco: finche' c'e', la badge
 // "A SCHERMO" sulla pagina corrente direbbe il falso, perche' quella pagina
 // sul vetro non c'e'.
 const NOTT=d.silenzio_immagine||'';
 const box=E('lista'); box.innerHTML='';
 const usate=d.pagine.map(p=>p.i);
 const primoMobile=usate.length>1?usate[1]:-1, ultimo=usate[usate.length-1];

 d.pagine.forEach(p=>{
  const el=document.createElement('div');
  el.className='pg'+(p.corrente?' now':'');
  const fisso=(p.i===usate[0]);
  el.innerHTML=
   '<div class="cap">'+
    '<div class="mini'+(p.tipo=='immagine'?'':' gen')+'" data-mini="'+esc(p.param)+
      '" data-tipo="'+esc(p.tipo)+'">'+(p.tipo=='immagine'?'':(GLIFO[p.tipo]||'?'))+'</div>'+
    '<div class="testa">'+
     '<div class="top"><span class="nome">'+esc(p.tipo)+
       (p.param?' <span class="par">'+esc(p.param)+'</span>':'')+'</span>'+
       (p.corrente&&!NOTT?'<span class="badge">A SCHERMO</span>':'')+'</div>'+
     '<label class="sw"><span>Nel cambio automatico</span>'+
       '<input type="checkbox" data-a="'+p.i+'"'+(p.attiva?' checked':'')+'><span class="track"></span></label>'+
     '<div class="riga"><label>Resta a schermo</label>'+
       '<select data-d="'+p.i+'">'+optDur(p.durata_s)+'</select></div>'+
    '</div>'+
    '<div class="ord">'+
     '<button data-su="'+p.i+'"'+(fisso||p.i===primoMobile?' disabled':'')+' title="Su">&#9650;</button>'+
     '<button data-giu="'+p.i+'"'+(fisso||p.i===ultimo?' disabled':'')+' title="Gi&ugrave;">&#9660;</button>'+
    '</div>'+
   '</div>'+
   '<div class="azioni">'+
     (p.corrente?'':'<button class="sec" data-v="'+p.i+'">Mostra ora</button>')+
     '<button class="sec' + (p.corrente?' sol':'') + '" data-f="'+p.i+'">Solo questa</button>'+
     (p.i===usate[0]?'':'<button class="dan sol" data-x="'+p.i+'">Togli dall\'elenco</button>')+
   '</div>';
  box.appendChild(el);
  if(p.corrente&&!NOTT) E('oraNome').textContent = p.tipo + (p.param? ' \u2014 '+p.param : '');
 });
 if(NOTT) E('oraNome').textContent = 'immagine della notte \u2014 ' + NOTT;

 box.querySelectorAll('[data-mini]').forEach(m=>{
  if(m.dataset.tipo=='immagine'&&m.dataset.mini) anteprimaPigra(m,m.dataset.mini);});

 // Quanti posti restano: il numero che prima non compariva da nessuna parte,
 // e la cui assenza faceva sembrare rotto il pulsante che li riempiva.
 // Il pulsante del grafico si spegne se la pagina c'e' gia': il server la
 // rifiuta comunque (409), ma un pulsante che si puo' premere e non fa
 // niente e' il difetto che si e' appena tolto altrove.
 const bg=E('bgraf');
 if(bg){
  const c=d.pagine.some(p=>p.tipo=='grafico');
  bg.disabled=c;
  bg.textContent=c?'Il grafico è già fra le pagine':'Aggiungi la pagina del grafico';
 }

 const tot=d.slot_totali;
 E('conta').textContent = tot? ('\u2014 '+d.pagine.length+' su '+tot+
   ((tot-d.pagine.length)?', '+(tot-d.pagine.length)+' liberi':', elenco pieno')) : '';

 if(!salvaTimer){
  E('rot').checked=d.rotazione; E('sda').value=d.silenzio_da; E('sa').value=d.silenzio_a;

 // La tendina si costruisce dalle pagine VERE ad ogni giro: una pagina tolta
 // deve sparire da qui, o si sceglierebbe uno slot che non esiste piu'.
 const sp=E('spag'); const scelta=d.silenzio_pagina;
 sp.innerHTML='<option value="-1">nessuna (ferma solo la rotazione)</option>'+
  '<option value="254"'+(scelta==254?' selected':'')+'>un\'immagine a caso fra le pagine qui sotto</option>'+
  '<option value="253"'+(scelta==253?' selected':'')+'>un\'immagine a caso fra TUTTE quelle sulla card</option>'+
  d.pagine.map(p=>'<option value="'+p.i+'"'+(p.i==scelta?' selected':'')+'>'+
   esc(p.tipo+(p.param?(' — '+p.param):''))+'</option>').join('');
 if(scelta==null||scelta<0||scelta>=255) sp.value='-1';
 }
 E('fas').checked=d.fascia;

 box.querySelectorAll('[data-a]').forEach(c=>c.onchange=()=>
   postJson('/api/pannello/pagina?i='+c.dataset.a+'&attiva='+(c.checked?1:0)));
 box.querySelectorAll('[data-d]').forEach(c=>c.onchange=()=>
   postJson('/api/pannello/pagina?i='+c.dataset.d+'&durata='+c.value));
 box.querySelectorAll('[data-v]').forEach(b=>b.onclick=()=>{
   b.disabled=true;b.textContent='in coda...';
   post('/api/pannello/vai?i='+b.dataset.v).then(()=>setTimeout(()=>{carica();leggiAnte();},3500));});
 box.querySelectorAll('[data-f]').forEach(b=>b.onclick=()=>
   postJson('/api/pannello/pagina?i='+b.dataset.f+'&fissa=1'));
 box.querySelectorAll('[data-su]').forEach(b=>b.onclick=()=>
   postJson('/api/pannello/sposta?dir=-1&i='+b.dataset.su));
 box.querySelectorAll('[data-giu]').forEach(b=>b.onclick=()=>
   postJson('/api/pannello/sposta?dir=1&i='+b.dataset.giu));
 box.querySelectorAll('[data-x]').forEach(b=>b.onclick=()=>{
   if(!confirm('Togliere questa pagina? L\'immagine resta sulla card.'))return;
   postJson('/api/pannello/rimuovi?i='+b.dataset.x);});

 disegnaCard();
}

// Una TABELLA e non piu' una griglia di miniature, ed e' una scelta di scala:
// con molte immagini contano il numero di righe che stanno in uno schermo e
// il costo di aprire la pagina. La tabella non chiede NIENTE alla scheda
// finche' non si preme "vedi" -- la griglia chiedeva dodici miniature, cioe'
// circa un secondo di hub occupato ad ogni caricamento.
//
// Il prezzo e' il riconoscimento a colpo d'occhio: "GigiFeligi" dice qualcosa,
// "IMG_0473" no. Per quello c'e' l'anteprima a richiesta, che si apre a piena
// risoluzione sotto la riga -- una alla volta, perche' sono 15 kB l'una.
//
// Mostra anche le immagini gia' in elenco, marcate e col pulsante spento:
// nasconderle faceva ballare i conti ("20 di 87" e poi diciassette righe), e
// un conteggio che non torna e' peggio di una riga in piu'.
function disegnaCard(){
 const box=E('limg'); if(!box) return;
 const inUso={};
 if(ULTIMO) ULTIMO.pagine.forEach(p=>{if(p.tipo=='immagine'&&p.param)inUso[p.param]=1;});

 box.innerHTML='';
 const fine=CARD_DA+SULLACARD.length;
 E('contaCard').textContent = CARD_TOT
   ? ('\u2014 '+(CARD_DA+1)+'-'+fine+' di '+CARD_TOT) : '';
 E('cardNota').textContent = CARD_TOT
   ? (CARD_CERCA ? ('filtrate per "'+CARD_CERCA+'"') : 'in ordine di come stanno sulla card')
   : (CARD_CERCA ? '' : 'nessuna immagine sulla card');
 E('cardVuota').style.display = (CARD_TOT===0 && CARD_CERCA)?'block':'none';
 E('limgBox').style.display = SULLACARD.length?'block':'none';
 E('cprev').disabled = (CARD_DA<=0);
 E('cnext').disabled = (fine>=CARD_TOT);

 SULLACARD.forEach(im=>{
  const usata=!!inUso[im.nome];
  const tr=document.createElement('tr');
  tr.innerHTML='<td><div class="nome">'+esc(im.nome)+'</div>'+
   '<div class="stato">'+(im.ok?(usata?'gi&agrave; fra le pagine':'sulla card')
                              :im.byte+' byte, non valida')+'</div></td>'+
   '<td class="cmd">'+
   (im.ok?'<button class="sec" data-see="'+esc(im.nome)+'">vedi</button>':'')+
   '<button class="sec" data-add="'+esc(im.nome)+'"'+(usata||!im.ok?' disabled':'')+
   '>usa</button>'+
   '<button class="dan" data-del="'+esc(im.nome)+'">elimina</button></td>';
  box.appendChild(tr);
 });

 // "vedi" apre l'anteprima piena sotto la riga, e la richiude. Una alla
 // volta: sono 15 kB e un server sincrono, e due aperte insieme sarebbero
 // due richieste in coda per guardarne una.
 box.querySelectorAll('[data-see]').forEach(b=>b.onclick=()=>{
  const nome=b.dataset.see, tr=b.closest('tr');
  const gia=tr.nextElementSibling;
  if(gia && gia.dataset.vista){ gia.remove(); b.textContent='vedi'; return; }
  box.querySelectorAll('tr[data-vista]').forEach(x=>x.remove());
  box.querySelectorAll('[data-see]').forEach(x=>x.textContent='vedi');
  const riga=document.createElement('tr');
  riga.dataset.vista='1';
  riga.innerHTML='<td colspan="2"><div class="vista"></div></td>';
  tr.after(riga);
  b.textContent='chiudi';
  const cv=document.createElement('canvas'); cv.width=400; cv.height=300;
  riga.querySelector('.vista').appendChild(cv);
  if(PIENA[nome]){ dipingi15k(cv,PIENA[nome]); return; }
  fetch('/api/immagini/scarica?nome='+encodeURIComponent(nome),{cache:'no-store'})
   .then(r=>r.ok?r.arrayBuffer():Promise.reject())
   .then(x=>{PIENA[nome]=new Uint8Array(x);dipingi15k(cv,PIENA[nome]);})
   .catch(()=>{riga.querySelector('.vista').textContent='anteprima non disponibile';});
 });

 box.querySelectorAll('[data-add]').forEach(b=>b.onclick=()=>
   postJson('/api/pannello/aggiungi?param='+encodeURIComponent(b.dataset.add),E('simg'))
     .then(()=>window.scrollTo({top:0,behavior:'smooth'})));
 box.querySelectorAll('[data-del]').forEach(b=>b.onclick=()=>{
   if(!confirm('Eliminare '+b.dataset.del+' dalla card?'))return;
   delete MINI[b.dataset.del]; delete PIENA[b.dataset.del];
   post('/api/immagini/elimina?nome='+encodeURIComponent(b.dataset.del)).then(caricaImmagini);});
}

function carica(){
 fetch('/api/pannello').then(r=>r.json()).then(render);
 fetch('/api/messaggio').then(r=>r.json()).then(d=>{
  E('att').innerHTML = d.attivo
   ? 'Sul pannello: <b>'+esc(d.attivo.testo)+'</b>'+(d.attivo.scadenza?'<br>fino al '+esc(ora(d.attivo.scadenza)):'')
   : 'Nessun messaggio attivo.';
  const a=E('arch'); a.innerHTML='';
  if(d.archivio.length){
   a.innerHTML='<p class="muted" style="background:0;border:0;padding:0;min-height:0;display:block">Gi&agrave; scritti &mdash; tocca per riusare:</p>';
   d.archivio.forEach(m=>{const p=document.createElement('p');
    p.textContent=m.testo; p.title=ora(m.creato);
    p.onclick=()=>{E('txt').value=m.testo;E('txt').focus();}; a.appendChild(p);});}
 });
}

var CARD_DA=0, CARD_TOT=0, CARD_CERCA='';
const CARD_PER_PAGINA=20;

function caricaImmagini(){
 const q='/api/immagini?da='+CARD_DA+'&quante='+CARD_PER_PAGINA+
         (CARD_CERCA?('&cerca='+encodeURIComponent(CARD_CERCA)):'');
 fetch(q,{cache:'no-store'}).then(r=>r.json()).then(d=>{
  SULLACARD = (d.sd && d.immagini) ? d.immagini : [];
  CARD_TOT  = d.totale||0;
  // Se si cancella l'ultima immagine di una pagina, quella pagina non esiste
  // piu': senza questo si resterebbe a guardare un elenco vuoto con il
  // contatore che dice che ce ne sono.
  if(CARD_DA>0 && !SULLACARD.length){ CARD_DA=Math.max(0,CARD_DA-CARD_PER_PAGINA); caricaImmagini(); return; }
  disegnaCard();
 });
}

// La ricerca aspetta che si smetta di digitare: ogni battuta e' una scansione
// della directory sulla card, e il server e' sincrono.
var cercaTimer=null;
E('cerca').addEventListener('input',()=>{
 clearTimeout(cercaTimer);
 cercaTimer=setTimeout(()=>{CARD_CERCA=E('cerca').value.trim();CARD_DA=0;caricaImmagini();},350);
});
E('cprev').onclick=()=>{ if(CARD_DA>0){CARD_DA-=CARD_PER_PAGINA;caricaImmagini();} };
E('cnext').onclick=()=>{ if(CARD_DA+CARD_PER_PAGINA<CARD_TOT){CARD_DA+=CARD_PER_PAGINA;caricaImmagini();} };

// Rotazione e ore di silenzio si salvano al tocco, come tutto il resto della
// pagina. Prima stavano dietro un pulsante "Salva", e non era solo
// un'incoerenza: la pagina si rilegge da sola ogni 15 s e render() riscrive i
// campi con i valori del server, quindi una modifica non salvata in tempo
// tornava indietro DA SOLA, senza dire niente. Da fuori sembrava che la
// scheda avesse rifiutato il comando.
//
// Le due tendine delle ore aspettano 800 ms prima di partire: cosi' spostare
// "dalle 23 alle 7" costa UNA scrittura in NVS invece di due.
var salvaTimer = null;
function salvaRotazione(ritardo){
 clearTimeout(salvaTimer);
 salvaTimer = setTimeout(()=>{
  const q='/api/pannello?rotazione='+(E('rot').checked?1:0)+
          '&sil_da='+encodeURIComponent(E('sda').value)+
          '&sil_a='+encodeURIComponent(E('sa').value)+'&sil_pagina='+E('spag').value;
  salvaTimer=null;
  post(q).then(r=>r.json()).then(d=>{render(d);flash(E('ss'),'Salvato');})
   .catch(()=>flash(E('ss'),'Non salvato: hub non raggiungibile',1));
 }, ritardo);
}
E('rot').onchange=()=>salvaRotazione(0);
// La fascia cambia il layout della pagina nodi: dopo averla salvata il
// pannello va ridisegnato, o resterebbe con la disposizione di prima fino al
// refresh di cadenza.
E('fas').onchange=()=>post('/api/pannello?fascia='+(E('fas').checked?1:0))
 .then(r=>r.json()).then(d=>{render(d);flash(E('sf'),'Salvato');post('/api/pannello/refresh');});
E('spag').onchange=()=>salvaRotazione(0);
E('sda').onchange=()=>salvaRotazione(800);
E('sa').onchange=()=>salvaRotazione(800);
E('brf').onclick=()=>post('/api/pannello/refresh')
 .then(()=>{flash(E('srf'),'Refresh in coda&hellip;');setTimeout(leggiAnte,3500);});
E('bant').onclick=leggiAnte;
leggiAnte();
E('bgraf').onclick=()=>postJson('/api/pannello/aggiungi?tipo=grafico',E('sgraf'))
 .then(()=>{flash(E('sgraf'),'Aggiunta in fondo all\'elenco');window.scrollTo({top:0,behavior:'smooth'});});
// I pulsanti del dettaglio si costruiscono dai nodi VERI, non da un elenco
// scritto a mano: un nodo dimenticato deve sparire da qui da solo, o si
// aggiungerebbe la pagina di uno che non c'e' piu'.
fetch('/api/nodi',{cache:'no-store'}).then(r=>r.json()).then(d=>{
 const box=E('bdett'); if(!box) return;
 if(!d.nodi.length){box.innerHTML='<p class="muted" style="margin:0">Nessun nodo associato.</p>';return;}
 d.nodi.forEach(n=>{
  const b=document.createElement('button');
  b.className='sec full'; b.style.marginBottom='.4rem';
  b.textContent='Aggiungi il dettaglio di '+n.nome;
  b.onclick=()=>postJson('/api/pannello/aggiungi?tipo=dettaglio&param='+encodeURIComponent(n.nome),E('sdett'))
   .then(()=>{flash(E('sdett'),'Aggiunta in fondo');window.scrollTo({top:0,behavior:'smooth'});});
  box.appendChild(b);
 });
}).catch(()=>{});

E('bm').onclick=()=>{
 if(!E('txt').value.trim()){flash(E('sm'),'Scrivi qualcosa',1);return;}
 const b=new URLSearchParams();b.set('t',E('txt').value);b.set('min',E('min').value);b.set('urg',E('urg').checked?1:0);
 fetch('/api/messaggio',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})
  .then(r=>r.text().then(t=>{flash(E('sm'),r.status==200?'Mandato':t,r.status!=200);setTimeout(carica,1200);}));};
E('bx').onclick=()=>{if(!confirm('Togliere il messaggio dal pannello?'))return;
 post('/api/messaggio/cancella').then(()=>{E('txt').value='';setTimeout(carica,1200);});};
E('bimg').onclick=()=>{
 const f=E('fimg').files[0];
 if(!f){flash(E('simg'),'Scegli un file .bin',1);return;}
 const nome=E('nimg').value||f.name.replace(/\.bin$/,'');
 const fd=new FormData(); fd.append('img',f);
 E('simg').textContent='Invio…';
 fetch('/api/immagini?nome='+encodeURIComponent(nome),{method:'POST',body:fd})
  .then(r=>r.text().then(t=>{flash(E('simg'),r.status==200?'Caricata':t,r.status!=200);caricaImmagini();}));};

carica();caricaImmagini();setInterval(carica,15000);
</script></div></body></html>
)HTML";

// ---------------------------------------------------------------------
//  /immagini — composizione di un'immagine per il pannello.
//
//  E' www/dither.html servita dalla scheda: ritaglio, zoom, rotazione,
//  luminosita', gamma e quattro algoritmi di dithering, con l'anteprima di
//  come verra' davvero. Il lavoro pesante resta nel browser — la scheda
//  riceve 15.000 byte gia' impacchettati e non converte niente, che e' la
//  decisione su cui poggia tutta la catena delle immagini.
//
//  La pagina NON e' una seconda copia: dither.html resta il sorgente unico
//  e dither_page.h si rigenera da li' con www/gen_page.py. Due copie a mano
//  divergerebbero al primo ritocco, e la differenza si vedrebbe solo
//  confrontando la pagina della scheda con quella sul PC.
// ---------------------------------------------------------------------
static void handleImmaginiPage() { servePagina("immagini", DITHER_PAGE); }

static void handlePannelloPage() {
  servePagina("pannello", PANNELLO_PAGE);
}


// ---------------------------------------------------------------------
//  Pannello: elenco pagine, rotazione, messaggi.
//
//  Tutto quello che tocca il display si LIMITA a mettere una richiesta in
//  coda (app_chiedi_pagina/app_chiedi_refresh): un refresh sono ~2,2 s, e
//  farlo dentro un handler HTTP terrebbe fermo il server, l'OTA e il
//  prelievo dei DATA dal driver ESP-NOW, che tiene solo l'ultimo. E' la
//  stessa regola dei callback della radio: la richiesta accoda, il loop
//  lavora.
// ---------------------------------------------------------------------
static void appendPagina(String& j, uint8_t i, const PageCfg* p, uint8_t corrente)
{
  j += "{\"i\":"; j += i;
  j += ",\"tipo\":\"";  j += pages_tipo_nome(p->tipo); j += '"';
  j += ",\"attiva\":";  j += p->attiva ? "true" : "false";
  j += ",\"durata_s\":"; j += p->durata_s;
  j += ",\"corrente\":"; j += (i == corrente) ? "true" : "false";
  j += ",\"param\":"; appendJsonString(j, p->param);
  j += '}';
}

// La fascia di silenzio si misura in quarti d'ora dalla mezzanotte (0..95).
// Verso la rete si scrive e si legge "HH:MM": e' cio' che l'utente vede sul
// campo orario, ed e' l'unica forma che non perde meta' dei valori possibili.
static String quartoHM(uint8_t q) {
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", (unsigned)(q / 4), (unsigned)((q % 4) * 15));
  return String(b);
}

// "21:15" -> 85. Un intero senza ':' resta l'ORA PIENA di prima (0..23), cosi'
// i comandi gia' scritti a mano continuano a valere invece di diventare
// quarti d'ora e spostare la fascia di colpo alle cinque del mattino.
//
// Fuori range non si arrotonda e non si tronca: si torna false e il chiamante
// non tocca niente. Una fascia messa a caso e' peggio di una non cambiata,
// perche' nessuno sta guardando il pannello nel momento in cui si sposta.
static bool parseQuarto(const String& s, uint8_t& q) {
  const int c = s.indexOf(':');
  if (c < 0) {
    const long h = s.toInt();
    if (h < 0 || h > 23) return false;
    q = (uint8_t)(h * 4);
    return true;
  }
  const long h = s.substring(0, c).toInt();
  const long m = s.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  // Al quarto piu' vicino; il % 96 serve solo a 23:53 e oltre, che risalgono
  // a mezzanotte invece di sfondare l'intervallo.
  q = (uint8_t)(((((h * 60 + m) + 7) / 15) % 96));
  return true;
}

static void handleApiPannello() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  const uint8_t corrente = pages_current();
  String j = "{\"slot_totali\":";
  j += pages_slots();
  j += ",\"rotazione\":";
  j += pages_rotazione() ? "true" : "false";
  j += ",\"silenzio_pagina\":"; j += (int)pages_silenzio_pagina();
  // Che cosa c'e' DAVVERO sul vetro quando la notte mostra un file
  // dell'archivio: "corrente" li' descrive il modello delle pagine, che quel
  // file non lo conosce. Senza questo campo la risposta resterebbe vera e
  // fuorviante insieme.
  j += ",\"silenzio_immagine\":"; appendJsonString(j, app_silenzio_immagine());
  // "21:15", non piu' il numero dell'ora: da v43 la fascia si muove a quarti,
  // e un intero non potrebbe che mentire su tre valori su quattro. Il quarto
  // grezzo resta accanto, per chi preferisce contare invece di leggere.
  j += ",\"silenzio_da\":"; appendJsonString(j, quartoHM(pages_silenzio_da_q()).c_str());
  j += ",\"silenzio_a\":";  appendJsonString(j, quartoHM(pages_silenzio_a_q()).c_str());
  j += ",\"silenzio_da_q\":"; j += (int)pages_silenzio_da_q();
  j += ",\"silenzio_a_q\":";  j += (int)pages_silenzio_a_q();
  j += ",\"fascia\":";
  j += pages_fascia() ? "true" : "false";
  j += ",\"sospeso\":";
  j += app_pannello_sospeso() ? "true" : "false";
  j += ",\"in_silenzio\":";
  j += pages_in_silenzio(time(nullptr)) ? "true" : "false";
  j += ",\"corrente\":"; j += corrente;
  j += ",\"pagine\":[";

  bool primo = true;
  for (uint8_t i = 0; i < pages_slots(); i++) {
    const PageCfg* p = pages_get(i);
    if (!p || !p->usato) continue;
    if (!primo) j += ',';
    primo = false;
    appendPagina(j, i, p, corrente);
  }
  j += "]}";
  net_server().send(200, "application/json", j);
}

// GET /api/pannello/slot — lo stato GREZZO dell'array delle pagine.
//
// Diagnostica, e nata da un caso preciso: le pagine immagine non si potevano
// piu' selezionare (pages_goto falliva) mentre l'elenco normale le mostrava
// tutte. L'elenco filtra su `usato` e quindi non poteva dire dove fosse la
// differenza; questo stampa tutti i 16 slot come stanno in memoria.
static void handleApiPannelloSlot() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  String j = "{\"slots\":"; j += (int)pages_slots();
  j += ",\"corrente\":"; j += (int)pages_current();
  j += ",\"pag\":[";
  for (uint8_t i = 0; i < pages_slots(); i++) {
    const PageCfg* p = pages_get(i);
    if (i) j += ',';
    j += "{\"i\":"; j += (int)i;
    j += ",\"usato\":"; j += (p && p->usato) ? "true" : "false";
    j += ",\"tipo\":"; j += (int)(p ? p->tipo : 255);
    j += ",\"attiva\":"; j += (p && p->attiva) ? "true" : "false";
    j += ",\"param\":"; appendJsonString(j, p ? p->param : "");
    j += ",\"goto_ok\":"; j += (p && p->usato && i < pages_slots()) ? "true" : "false";
    j += '}';
  }
  j += "]}";
  net_server().send(200, "application/json", j);
}

// POST /api/pannello?rotazione=0|1&sil_da=23:00&sil_a=07:30&sil_pagina=<slot|-1>
static void handleApiPannelloSet() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (srv.hasArg("rotazione")) pages_set_rotazione(srv.arg("rotazione").toInt() != 0);
  // -1 spegne la sospensione: nella fascia si ferma solo la rotazione, com'era
  // fino a v27. Qualunque slot valido invece FERMA i refresh e mostra quello.
  if (srv.hasArg("sil_pagina")) {
    const int v = srv.arg("sil_pagina").toInt();
    uint8_t slot = PAG_SIL_NESSUNA;
    if (v == PAG_SIL_CASUALE)          slot = PAG_SIL_CASUALE;
    else if (v == PAG_SIL_CARD)        slot = PAG_SIL_CARD;
    else if (v >= 0 && v < PAGES_MAX)  slot = (uint8_t)v;
    pages_set_silenzio_pagina(slot);
  }

  // Le due estremita' si muovono INSIEME o non si muovono: applicarne una
  // sola perche' l'altra e' scritta male darebbe una fascia che nessuno ha
  // chiesto, e per meta' della giornata.
  if (srv.hasArg("sil_da") && srv.hasArg("sil_a")) {
    uint8_t qDa, qA;
    if (parseQuarto(srv.arg("sil_da"), qDa) && parseQuarto(srv.arg("sil_a"), qA))
      pages_set_silenzio_q(qDa, qA);
  }
  // La forma in quarti grezzi, per chi legge /api/pannello e rimanda indietro
  // quello che ha letto senza riformattarlo.
  if (srv.hasArg("sil_da_q") && srv.hasArg("sil_a_q"))
    pages_set_silenzio_q((uint8_t)srv.arg("sil_da_q").toInt(),
                         (uint8_t)srv.arg("sil_a_q").toInt());
  if (srv.hasArg("fascia")) pages_set_fascia(srv.arg("fascia").toInt() != 0);

  // In NVS solo qui, alla conferma dell'utente: mai a ogni cambio pagina.
  pages_save();
  handleApiPannello();
}

// POST /api/pannello/pagina?i=2&attiva=1&durata=300
static void handleApiPannelloPagina() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("i")) { srv.send(400, "text/plain", "manca i"); return; }

  const uint8_t i = (uint8_t)srv.arg("i").toInt();
  if (srv.hasArg("attiva")) pages_set_attiva(i, srv.arg("attiva").toInt() != 0);
  if (srv.hasArg("durata")) pages_set_durata(i, (uint16_t)srv.arg("durata").toInt());
  if (srv.hasArg("fissa") && srv.arg("fissa").toInt() != 0) {
    // "Fissa questa pagina" non e' una modalita' a parte: e' tutte le altre
    // disattivate. Cosi' non esiste uno stato "fissato" che possa andare
    // fuori sincrono con l'elenco.
    pages_fissa(i);
    app_chiedi_pagina(i);
  }
  pages_save();
  handleApiPannello();
}

// POST /api/pannello/vai?i=1  — cambio pagina immediato (accodato)
static void handleApiPannelloVai() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("i")) { srv.send(400, "text/plain", "manca i"); return; }
  app_chiedi_pagina((uint8_t)srv.arg("i").toInt());
  srv.send(200, "text/plain", "ok");
}

// POST /api/pannello/refresh — completo sulla pagina corrente, per togliere
// il ghosting senza aspettare il ciclo.
static void handleApiPannelloRefresh() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  app_chiedi_refresh();
  net_server().send(200, "text/plain", "ok");
}

// ---------------------------------------------------------------------
//  Messaggi
// ---------------------------------------------------------------------
static void archivioCb(const Message& m, void* arg) {
  String* j = (String*)arg;
  if (j->endsWith("[")) { } else { *j += ','; }
  *j += "{\"testo\":"; appendJsonString(*j, m.testo);
  *j += ",\"creato\":";   *j += (long)m.creato;
  *j += ",\"scadenza\":"; *j += (long)m.scadenza;
  *j += ",\"urgente\":";  *j += (m.priorita == MSG_URGENTE) ? "true" : "false";
  *j += '}';
}

static void handleApiMessaggio() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  const time_t ora = time(nullptr);
  const Message* m = msg_active(ora);

  String j = "{\"attivo\":";
  if (m) {
    j += "{\"testo\":"; appendJsonString(j, m->testo);
    j += ",\"creato\":";   j += (long)m->creato;
    j += ",\"scadenza\":"; j += (long)m->scadenza;
    j += ",\"urgente\":";  j += (m->priorita == MSG_URGENTE) ? "true" : "false";
    j += '}';
  } else {
    j += "null";
  }
  j += ",\"archivio\":[";
  msg_archive_list(archivioCb, &j, 10);
  j += "]}";
  net_server().send(200, "application/json", j);
}

// POST /api/messaggio  (form-urlencoded: t=testo&min=durata&urg=0|1)
static void handleApiMessaggioSet() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  const String testo = srv.arg("t");
  if (testo.length() == 0) { srv.send(400, "text/plain", "testo vuoto"); return; }
  if (testo.length() > MSG_TESTO_MAX) {
    srv.send(400, "text/plain", "testo troppo lungo");
    return;
  }
  // Si rifiuta all'ingresso quello che non e' UTF-8: cosi' non entra in NVS
  // ne' nell'archivio sulla card, dove resterebbe per sempre. Il filtro in
  // appendJsonString() sotto e' la seconda rete, per le righe gia' scritte.
  if (!utf8Valido(testo.c_str(), testo.length())) {
    srv.send(400, "text/plain", "testo non UTF-8 (il client ha mandato un'altra codifica)");
    return;
  }

  const time_t ora = time(nullptr);
  const long   min = srv.hasArg("min") ? srv.arg("min").toInt() : 0;
  const time_t scad = (min > 0) ? (ora + (time_t)min * 60) : 0;
  const uint8_t prio = (srv.hasArg("urg") && srv.arg("urg").toInt() != 0)
                       ? MSG_URGENTE : MSG_NORMALE;

  if (!msg_set(testo.c_str(), ora, scad, prio)) {
    srv.send(500, "text/plain", "salvataggio fallito");
    return;
  }
  // Un messaggio NORMALE non scavalca la pagina corrente: lo si vedra' alla
  // prossima rotazione, o andandoci a mano. Se scavalcasse, ogni bigliettino
  // toglierebbe dallo schermo l'unica pagina per cui l'hub esiste.
  srv.send(200, "text/plain", "ok");
}

static void handleApiMessaggioCancella() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  msg_clear();
  net_server().send(200, "text/plain", "ok");
}

// ---------------------------------------------------------------------
//  Dashboard personalizzata su microSD — stesso meccanismo di
//  projects/EnvNode_C3/, e le funzioni sd_* che servono erano gia' qui
//  (sd_logger.cpp e' una copia di quello del C3): si carica un .html
//  self-contained, finisce in /www/dashboard.html e da li' in poi e' lui
//  a rispondere su "/".
//
//  Questa pagina di upload sta in PROGMEM ed e' servita SEMPRE da qui,
//  qualunque cosa ci sia sulla card: e' la via di recupero se la
//  dashboard caricata a mano risulta rotta. Se dipendesse dalla SD, una
//  pagina sbagliata chiuderebbe fuori proprio chi deve sostituirla — e
//  su questa scheda il rientro sarebbe andare a staccare la card.
// ---------------------------------------------------------------------
static const char DASH_UPLOAD_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeteoHub-S3 &mdash; Dashboard personalizzata</title><style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:2rem;display:flex;justify-content:center}
 .card{max-width:460px;width:100%;background:#1c1c1c;border:1px solid #333;border-radius:12px;padding:1.5rem}
 h1{font-size:1.05rem;margin:0 0 1rem}
 input[type=file]{width:100%;margin:.5rem 0 1rem;color:#ccc}
 button{width:100%;padding:.7rem;border:0;border-radius:8px;background:#3987e5;color:#fff;font-size:1rem;cursor:pointer;margin-top:.5rem}
 button.dan{background:#b91c1c}
 button:disabled{background:#555}
 progress{width:100%;height:1rem;margin-top:1rem}
 .muted{color:#8a8a8a;font-size:.8rem;margin-top:1rem;line-height:1.5}
 code{color:#9aa}
 a{color:#3987e5}
</style></head><body><div class="card">
 <h1>Dashboard personalizzata</h1>
 <p class="muted">Carica un file <code>.html</code> self-contained (CSS e JS
 inline, nessuna richiesta esterna: la scheda non ha internet da offrire) per
 sostituire la pagina di default dell'hub. Viene salvato sulla microSD in
 <code>/www/dashboard.html</code> e servito su <code>/</code>.</p>
 <p class="muted">I dati si leggono dalle stesse API che usa la pagina di
 default: <code>/api/stato</code> (hub, pannello, SD),
 <code>/api/nodi</code> (nodi con valori, trend e previsione),
 <code>/api/nodi/giorni</code> e <code>/api/nodi/scarica</code> (CSV).</p>
 <p class="muted">Questa pagina resta SEMPRE raggiungibile qui, anche se la
 dashboard caricata non funziona o la card viene tolta &mdash; in quel caso
 torna in uso quella di default.</p>
 <form id="f"><input type="file" name="dashboard" accept=".html,.htm" required>
 <button type="submit" id="b">Carica</button>
 <progress id="p" value="0" max="100" hidden></progress></form>
 <p class="muted" id="s"></p>
 <button id="br" class="dan">Ripristina dashboard di default</button>
 <p class="muted"><a href="/">nodi</a> &mdash; <a href="/pannello">pannello e messaggi</a> &mdash; <a href="/immagini">componi immagine</a> &mdash; <a href="/pagine">pagine</a> &mdash; <a href="/api">API</a> &mdash; <a href="/update">aggiornamento firmware</a></p>
<script>
const f=document.getElementById('f'),b=document.getElementById('b'),p=document.getElementById('p'),s=document.getElementById('s'),br=document.getElementById('br');
f.addEventListener('submit',e=>{e.preventDefault();const fd=new FormData(f),x=new XMLHttpRequest();
 x.open('POST','/dashboard-upload');p.hidden=false;b.disabled=true;
 x.upload.onprogress=ev=>{if(ev.lengthComputable){const pc=Math.round(ev.loaded/ev.total*100);p.value=pc;s.textContent='Caricamento '+pc+'%';}};
 x.onload=()=>{s.textContent=(x.status==200)?'OK! Vai alla dashboard per vederla.':('Errore: '+x.responseText);b.disabled=false;};
 x.onerror=()=>{s.textContent='Errore di rete';b.disabled=false;};
 x.send(fd);});
br.addEventListener('click',()=>{
 if(!confirm("Ripristinare la dashboard di default? La versione personalizzata verra' eliminata dalla SD."))return;
 fetch('/dashboard-ripristina',{method:'POST'}).then(r=>r.text()).then(t=>{s.textContent=t;});
});
</script></div></body></html>
)HTML";

// Il file arriva a blocchi dal WebServer: si scrive man mano, senza tenerlo
// in RAM. Stessa forma dell'upload di /update in net_ota.cpp.
static File s_dashUploadFile;
static bool s_dashUploadOk = false;

static void handleDashboardUploadPage() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().send_P(200, "text/html", DASH_UPLOAD_PAGE);
}

static void handleDashboardUploadDone() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().sendHeader("Connection", "close");
  net_server().send(s_dashUploadOk ? 200 : 500, "text/plain",
                    s_dashUploadOk ? "OK"
                                   : "Caricamento fallito (SD non disponibile o scrittura fallita)");
}

static void handleDashboardUploadChunk() {
  HTTPUpload& up = net_server().upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      if (!net_webAuthOk()) { s_dashUploadOk = false; return; }
      s_dashUploadFile = sd_open_dashboard_for_write();
      s_dashUploadOk   = (bool)s_dashUploadFile;
      break;

    case UPLOAD_FILE_WRITE:
      if (s_dashUploadOk) {
        s_dashUploadOk = (s_dashUploadFile.write(up.buf, up.currentSize) == up.currentSize);
      }
      break;

    case UPLOAD_FILE_END:
      if (s_dashUploadFile) s_dashUploadFile.close();
      break;

    // Un upload interrotto a meta' lascia il file aperto e mezzo scritto:
    // si chiude e si dichiara fallito, cosi' la prossima volta si riparte
    // da un troncamento pulito invece che da un handle appeso. E' la stessa
    // disciplina che in net_ota.cpp vuole Update.abort() su questo caso.
    case UPLOAD_FILE_ABORTED:
      if (s_dashUploadFile) s_dashUploadFile.close();
      s_dashUploadOk = false;
      break;

    default:
      break;
  }
}

static void handleDashboardRipristina() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  const bool ok = sd_delete_dashboard();
  net_server().send(ok ? 200 : 500, "text/plain",
                    ok ? "Ripristinata la dashboard di default." : "Cancellazione fallita.");
}

static void handleRoot() {
  // La versione sulla card se c'è, altrimenti quella del firmware: da v18
  // vale per tutte le pagine sostituibili, non più solo per questa.
  servePagina("dashboard", HUB_PAGE);
}

// ---------------------------------------------------------------------
//  La tabella delle rotte — una sola, usata due volte
// ---------------------------------------------------------------------
//  Da qui si registrano gli handler sul WebServer E si genera
//  /api/elenco, che documenta l'interfaccia a chi si scrive le proprie
//  pagine. Due usi della stessa riga: una rotta nuova compare nella
//  documentazione perche' e' stata REGISTRATA, non perche' qualcuno si e'
//  ricordato di scriverla da qualche parte.
//
//  E' la ragione per cui la pagina /api puo' stare sulla card senza
//  diventare bugiarda: sulla card c'e' solo l'impaginazione, i fatti
//  arrivano dal firmware ad ogni richiesta.
//
//  `handler == nullptr` significa "documentata qui, registrata a mano piu'
//  sotto": sono le rotte con upload multipart, che vogliono due callback e
//  non entrano in questa forma.
struct Rotta {
  HTTPMethod  metodo;
  const char* path;
  void      (*handler)();
  const char* cosa;
  const char* parametri;
};

static const Rotta ROTTE[] = {
  { HTTP_GET,  "/",                     handleRoot,                  "la pagina dei nodi (sostituibile dalla card)", "" },
  { HTTP_GET,  "/pannello",             handlePannelloPage,          "pagine del pannello, messaggi, immagini (sostituibile)", "" },
  { HTTP_GET,  "/immagini",             handleImmaginiPage,          "composizione di un'immagine per il pannello (sostituibile)", "" },

  { HTTP_GET,  "/api/stato",            handleApiStato,              "stato generale: firmware, rete, ora, card, contatori", "" },
  { HTTP_GET,  "/api/salute",           handleApiSalute,             "controlli incrociati: pacchetti == righe + scartati + fallite", "" },
  { HTTP_GET,  "/api/elenco",           nullptr,                     "QUESTO elenco, in JSON", "" },

  { HTTP_GET,  "/api/nodi",             handleApiNodi,               "i nodi: valori, cadenza, trend, previsione, pacchetti persi", "" },
  { HTTP_POST, "/api/pairing",          handleApiPairing,            "apre o chiude la finestra di associazione", "on=0|1, s=secondi" },
  { HTTP_GET,  "/api/pairing/ascolto",  handleApiPairingAscolto,     "chi bussa e non entra: MAC sconosciuti con RSSI e motivo dello scarto, anche fuori dalla finestra", "" },
  { HTTP_POST, "/api/nodi/dimentica",   handleApiNodiDimentica,      "toglie un nodo dal registro (RAM e NVS)", "mac=AA:BB:..." },
  { HTTP_POST, "/api/nodi/altitudine",  handleApiNodiAltitudine,     "quota per riportare la pressione al livello del mare", "m=metri" },
  { HTTP_GET,  "/api/nodi/giorni",      handleApiNodiGiorni,         "i giorni di CSV presenti sulla card per un nodo", "nodo=NOME" },
  { HTTP_GET,  "/api/nodi/scarica",     handleApiNodiScarica,        "il CSV di un giorno (ts_iso,ts_unix,fonte_ora,mac,seq,temp_c,hum_pct,press_hpa,batt_mv)", "nodo=NOME, d=AAAA-MM-GG" },

  { HTTP_GET,  "/api/pannello",         handleApiPannello,           "elenco delle pagine, rotazione, ore di silenzio", "" },
  { HTTP_POST, "/api/pannello",         handleApiPannelloSet,        "rotazione, ore di silenzio (a quarti d'ora), fascia del messaggio", "rotazione=0|1, sil_da=HH:MM (o 0..23 = ora piena), sil_a=HH:MM, sil_da_q/sil_a_q=0..95, sil_pagina=<slot|-1|254>, fascia=0|1" },
  { HTTP_POST, "/api/pannello/pagina",  handleApiPannelloPagina,     "una pagina: attiva, durata, oppure 'solo questa'", "i=slot, attiva=0|1, durata=secondi, fissa=1" },
  { HTTP_POST, "/api/pannello/vai",     handleApiPannelloVai,        "manda subito una pagina sul pannello (accoda: la disegna il loop)", "i=slot" },
  { HTTP_GET,  "/api/pannello/slot",    handleApiPannelloSlot,       "stato grezzo dei 16 slot: usato, tipo, attiva, param", "" },
  { HTTP_GET,  "/api/pannello/anteprima", handleApiPannelloAnteprima,  "i 15.000 byte che il pannello sta mostrando (formato .bin)", "" },
  { HTTP_POST, "/api/pannello/refresh", handleApiPannelloRefresh,    "ridisegna la pagina corrente (completo, ~2,2 s)", "" },
  { HTTP_POST, "/api/pannello/aggiungi",handleApiPannelloAggiungi,   "aggiunge una pagina: immagine, grafico o dettaglio di un nodo", "param=NOME | tipo=grafico | tipo=dettaglio&param=NODO" },
  { HTTP_POST, "/api/pannello/rimuovi", handleApiPannelloRimuovi,    "toglie una pagina dall'elenco (lo slot 0 non si tocca)", "i=slot" },
  { HTTP_POST, "/api/pannello/sposta",  handleApiPannelloSposta,     "sposta una pagina di un posto nell'elenco", "i=slot, dir=-1|1" },

  { HTTP_GET,  "/api/messaggio",        handleApiMessaggio,          "il messaggio attivo e l'archivio", "" },
  { HTTP_POST, "/api/messaggio",        handleApiMessaggioSet,       "scrive il messaggio sul pannello (form-urlencoded)", "t=testo, min=minuti, urg=0|1" },
  { HTTP_POST, "/api/messaggio/cancella",handleApiMessaggioCancella, "toglie il messaggio dal pannello", "" },

  { HTTP_GET,  "/api/immagini",         handleApiImmagini,           "le immagini sulla card, a pagine, col totale filtrato", "da=0&quante=12&cerca=TESTO" },
  { HTTP_GET,  "/api/eventi",           handleApiEventi,             "il diario degli eventi di un mese (boot, NTP, nodo muto, card, OTA, associazione)", "m=AAAA-MM (default: il mese corrente)" },
  { HTTP_POST, "/api/prova/blocco",     handleApiProvaBlocco,        "PROVA: blocca il loop apposta per verificare che il watchdog riavvii davvero. La scheda smette di rispondere e riparte", "s=secondi (default 90, max 120)" },
  { HTTP_GET,  "/api/epd/totale",       handleApiEpdTotale,          "quanti refresh ha fatto il pannello: si contano dalla card", "" },
  { HTTP_GET,  "/api/epd/registro",     handleApiEpdRegistro,        "il registro dei refresh del pannello, un file al mese", "m=AAAA-MM" },
  { HTTP_GET,  "/api/immagini/mini",    handleApiImmaginiMini,       "la miniatura 80x60 di un'immagine (600 byte)", "nome=NOME" },
  { HTTP_POST, "/api/immagini",         nullptr,                     "carica un'immagine da 15.000 byte esatti (multipart, campo 'img')", "nome=NOME" },
  { HTTP_POST, "/api/immagini/elimina", handleApiImmaginiElimina,    "elimina un'immagine dalla card", "nome=NOME" },
  { HTTP_GET,  "/api/immagini/scarica", handleApiImmaginiScarica,    "i 15.000 byte di un'immagine (per l'anteprima)", "nome=NOME" },

  { HTTP_GET,  "/api",                  nullptr,                     "questo elenco, impaginato (sostituibile)", "" },
  { HTTP_GET,  "/pagine",               nullptr,                     "gestione delle pagine sostituibili (sempre nel firmware)", "" },
  { HTTP_POST, "/api/pagine/carica",    nullptr,                     "carica una pagina sulla card (multipart, campo 'pagina')", "nome=dashboard|pannello|immagini|api" },
  { HTTP_POST, "/api/pagine/ripristina",nullptr,                     "toglie la pagina dalla card: torna quella del firmware", "nome=..." },
  { HTTP_GET,  "/update",               nullptr,                     "aggiornamento del firmware (in net_ota.cpp, sempre nel firmware)", "" },
};
static const int ROTTE_N = sizeof(ROTTE) / sizeof(ROTTE[0]);

// GET /api/elenco — l'interfaccia raccontata da se stessa.
static void handleApiElenco() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  String j = "{\"fw\":";
  appendJsonString(j, app_fw_version());
  j += ",\"rotte\":[";
  for (int i = 0; i < ROTTE_N; i++) {
    if (i) j += ',';
    j += "{\"metodo\":\"";
    j += (ROTTE[i].metodo == HTTP_GET) ? "GET" : "POST";
    j += "\",\"path\":";  appendJsonString(j, ROTTE[i].path);
    j += ",\"cosa\":";    appendJsonString(j, ROTTE[i].cosa);
    j += ",\"parametri\":"; appendJsonString(j, ROTTE[i].parametri);
    j += '}';
  }
  j += "]}";
  net_server().send(200, "application/json", j);
}

// ---------------------------------------------------------------------
//  /api — l'elenco impaginato
// ---------------------------------------------------------------------
//  Volutamente MINIMA: non contiene un solo fatto sulle rotte, li chiede a
//  /api/elenco ad ogni caricamento. E' cio' che le permette di stare sulla
//  card senza diventare bugiarda il giorno che il firmware cambia — e di
//  costare poche centinaia di byte invece di qualche kB.
static const char API_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark"><title>MeteoHub-S3 &mdash; API</title><style>
 :root{--bg:#0e0e10;--card:#1a1a1d;--bordo:#2e2e33;--txt:#ececee;--dim:#8e8e96;--acc:#3987e5;--ok:#3fb950}
 *{box-sizing:border-box}
 body{font-family:system-ui,-apple-system,Segoe UI,Arial,sans-serif;background:var(--bg);
  color:var(--txt);margin:0;padding:14px}
 .wrap{max-width:860px;margin:0 auto}
 h1{font-size:1.15rem;margin:.2rem 0 .3rem}
 .sub{color:var(--dim);font-size:.85rem;line-height:1.5;margin-bottom:1.2rem}
 .r{background:var(--card);border:1px solid var(--bordo);border-radius:12px;
  padding:11px 13px;margin-bottom:8px}
 .top{display:flex;align-items:center;gap:.6rem;flex-wrap:wrap}
 .m{font-size:.68rem;font-weight:700;letter-spacing:.05em;padding:2px 7px;border-radius:5px;
  background:#20343f;color:#79c0ff;flex:none}
 .m.post{background:#3a2a1e;color:#e5a13a}
 code{font-family:ui-monospace,Consolas,monospace;font-size:.9rem}
 .c{color:var(--dim);font-size:.85rem;margin-top:.35rem;line-height:1.45}
 .p{margin-top:.35rem;font-size:.8rem}
 .p b{color:var(--ok);font-weight:600}
 nav{margin:1.6rem 0 .5rem;display:flex;flex-wrap:wrap;gap:.4rem 1rem;font-size:.85rem}
 a{color:var(--acc);text-decoration:none}
</style></head><body><div class="wrap">
<h1>API dell&rsquo;hub <span id="fw" class="sub"></span></h1>
<p class="sub">Questo elenco lo genera il firmware da s&eacute;: ogni riga esiste perch&eacute;
la rotta &egrave; <b>registrata</b>, non perch&eacute; qualcuno si &egrave; ricordato di
scriverla. Serve a chi si costruisce le proprie pagine &mdash; vedi
<a href="/pagine">Pagine</a>. Tutte le rotte vogliono la stessa autenticazione
del resto dell&rsquo;interfaccia.</p>
<div id="l">lettura&hellip;</div>
<nav>
 <a href="/">Nodi</a><a href="/pannello">Pannello</a><a href="/immagini">Componi immagine</a>
 <a href="/pagine">Pagine</a><a href="/api">API</a><a href="/update">Aggiorna firmware</a>
</nav>
<script>
const esc=x=>String(x==null?'':x).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
fetch('/api/elenco').then(r=>{if(!r.ok)throw new Error(r.status);return r.json();}).then(d=>{
 document.getElementById('fw').textContent='firmware '+d.fw;
 document.getElementById('l').innerHTML=d.rotte.map(r=>
  '<div class="r"><div class="top"><span class="m'+(r.metodo=='POST'?' post':'')+'">'+r.metodo+
  '</span><code>'+esc(r.path)+'</code></div><div class="c">'+esc(r.cosa)+'</div>'+
  (r.parametri?'<div class="p">parametri: <b>'+esc(r.parametri)+'</b></div>':'')+'</div>').join('');
}).catch(e=>{document.getElementById('l').textContent='elenco non leggibile: '+e.message;});
</script></div></body></html>
)HTML";

// ---------------------------------------------------------------------
//  Pagine sostituibili: la whitelist
// ---------------------------------------------------------------------
//  Il nome NON arriva mai dalla rete come pezzo di path: si cerca in questa
//  tabella e si usa la voce trovata. Cosi' non c'e' path traversal da
//  parare, e sulla card non finiscono file che nessuno serve.
//
//  /pagine, /update e i due upload NON sono qui, ed e' la regola che vale da
//  quando esiste la dashboard personalizzata: la via di rientro non puo'
//  dipendere da cio' da cui si sta rientrando. Se la card manca, si corrompe
//  o contiene una pagina rotta, quelle continuano ad arrivare dal firmware.
struct PaginaSost {
  const char* nome;
  const char* path;
  const char* titolo;
  const char* pm;      // il fallback nel firmware
};
static const PaginaSost PAGINE_SOST[] = {
  { "dashboard", "/",         "Nodi (home)",            HUB_PAGE      },
  { "pannello",  "/pannello", "Pannello e messaggi",    PANNELLO_PAGE },
  { "immagini",  "/immagini", "Composizione immagini",  DITHER_PAGE   },
  { "api",       "/api",      "Elenco delle API",       API_PAGE      },
};
static const int PAGINE_SOST_N = sizeof(PAGINE_SOST) / sizeof(PAGINE_SOST[0]);

static const PaginaSost* paginaSost(const String& nome) {
  for (int i = 0; i < PAGINE_SOST_N; i++) {
    if (nome == PAGINE_SOST[i].nome) return &PAGINE_SOST[i];
  }
  return nullptr;
}

// Serve la versione sulla card se c'e', altrimenti quella del firmware.
// streamFileLimitato() come per ogni file: un client che se ne va a meta' non
// deve tenere fermo il loop(), che nel frattempo non preleva i DATA dei nodi.
static void servePagina(const char* nome, const char* pm) {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  File f = sd_open_www(nome);
  if (f) {
    streamFileLimitato(net_server(), f, "text/html");
    f.close();
    return;
  }
  net_server().send_P(200, "text/html", pm);
}

static void handleApiPage() { servePagina("api", API_PAGE); }

// GET /api/pagine/elenco — quali pagine sono sostituite, e con quale firmware
// erano state caricate. Il confronto fra quella versione e quella che gira
// adesso e' l'unico modo di accorgersi di una pagina rimasta indietro: e' il
// rischio che ci si prende spostandole sulla card.
static void handleApiPagineElenco() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  String j = "{\"fw\":";
  appendJsonString(j, app_fw_version());
  j += ",\"sd\":";
  j += sd_mounted() ? "true" : "false";
  j += ",\"pagine\":[";
  for (int i = 0; i < PAGINE_SOST_N; i++) {
    if (i) j += ',';
    const bool suCard = sd_www_exists(PAGINE_SOST[i].nome);
    char fw[12] = "";
    time_t quando = 0;
    const bool info = suCard && sd_www_info(PAGINE_SOST[i].nome, fw, sizeof(fw), &quando);

    j += "{\"nome\":";   appendJsonString(j, PAGINE_SOST[i].nome);
    j += ",\"path\":";   appendJsonString(j, PAGINE_SOST[i].path);
    j += ",\"titolo\":"; appendJsonString(j, PAGINE_SOST[i].titolo);
    j += ",\"su_card\":"; j += suCard ? "true" : "false";
    j += ",\"fw_caricata\":"; if (info) appendJsonString(j, fw); else j += "null";
    j += ",\"quando\":";  j += (long)(info ? quando : 0);
    j += '}';
  }
  j += "]}";
  net_server().send(200, "application/json", j);
}

// POST /api/pagine/carica?nome=... (multipart, campo "pagina")
static File s_pagUploadFile;
static bool s_pagUploadOk = false;
static String s_pagUploadNome;

static void handlePagineCaricaChunk() {
  HTTPUpload& up = net_server().upload();
  switch (up.status) {
    case UPLOAD_FILE_START: {
      if (!net_webAuthOk()) { s_pagUploadOk = false; return; }
      s_pagUploadNome = net_server().hasArg("nome") ? net_server().arg("nome") : String("");
      const PaginaSost* p = paginaSost(s_pagUploadNome);
      if (p == nullptr) { s_pagUploadOk = false; return; }
      s_pagUploadFile = sd_open_www_for_write(p->nome);
      s_pagUploadOk   = (bool)s_pagUploadFile;
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (s_pagUploadOk) {
        s_pagUploadOk = (s_pagUploadFile.write(up.buf, up.currentSize) == up.currentSize);
      }
      break;

    case UPLOAD_FILE_END:
      if (s_pagUploadFile) s_pagUploadFile.close();
      break;

    // Stessa disciplina di Update.abort() in net_ota.cpp: un upload caduto a
    // meta' non deve lasciare un handle appeso ne' passare per riuscito.
    case UPLOAD_FILE_ABORTED:
      if (s_pagUploadFile) s_pagUploadFile.close();
      s_pagUploadOk = false;
      break;

    default:
      break;
  }
}

static void handlePagineCaricaDone() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  if (!s_pagUploadOk) {
    net_server().send(500, "text/plain",
                      "caricamento fallito (nome non valido, card assente o piena)");
    return;
  }
  // Si registra CON QUALE firmware e' stata caricata: e' il dato che permette
  // di vedere, piu' avanti, una pagina rimasta indietro.
  sd_www_registra(s_pagUploadNome.c_str(), app_fw_version(), rtctime_now());
  net_server().send(200, "text/plain", "OK");
}

// POST /api/pagine/ripristina?nome=... — toglie il file dalla card
static void handlePagineRipristina() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  const String nome = net_server().hasArg("nome") ? net_server().arg("nome") : String("");
  const PaginaSost* p = paginaSost(nome);
  if (p == nullptr) { net_server().send(400, "text/plain", "pagina sconosciuta"); return; }
  if (!sd_delete_www(p->nome)) {
    net_server().send(500, "text/plain", "non si e' potuto eliminare il file dalla card");
    return;
  }
  net_server().send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------------
//  /pagine — gestione delle pagine sostituibili
// ---------------------------------------------------------------------
//  Sta SEMPRE nel firmware, come /update e come la vecchia
//  /dashboard-upload: e' la via da cui si rimette a posto una pagina rotta,
//  e non puo' dipendere dalla card che sta sostituendo.
static const char PAGINE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark"><title>MeteoHub-S3 &mdash; Pagine</title><style>
 :root{--bg:#0e0e10;--card:#1a1a1d;--card2:#212125;--bordo:#2e2e33;--txt:#ececee;
  --dim:#8e8e96;--acc:#3987e5;--ok:#3fb950;--warn:#d29922;--dan:#c9342d}
 *{box-sizing:border-box}
 body{font-family:system-ui,-apple-system,Segoe UI,Arial,sans-serif;background:var(--bg);
  color:var(--txt);margin:0;padding:14px}
 .wrap{max-width:760px;margin:0 auto}
 h1{font-size:1.15rem;margin:.2rem 0 .3rem}
 .sub{color:var(--dim);font-size:.85rem;line-height:1.55;margin-bottom:1.2rem}
 .p{background:var(--card);border:1px solid var(--bordo);border-radius:12px;
  padding:13px;margin-bottom:10px}
 .top{display:flex;align-items:center;gap:.6rem;flex-wrap:wrap;margin-bottom:.3rem}
 .top b{font-size:1rem}
 code{font-family:ui-monospace,Consolas,monospace;font-size:.85rem;color:var(--dim)}
 .tag{margin-left:auto;font-size:.68rem;font-weight:700;letter-spacing:.04em;
  padding:3px 8px;border-radius:99px}
 .tag.fw{background:rgba(142,142,150,.16);color:var(--dim)}
 .tag.sd{background:rgba(63,185,80,.15);color:var(--ok)}
 .tag.old{background:rgba(210,153,34,.15);color:var(--warn)}
 .info{color:var(--dim);font-size:.82rem;line-height:1.5}
 .az{display:flex;gap:8px;margin-top:10px;flex-wrap:wrap;align-items:center}
 input[type=file]{color:var(--dim);font-size:.8rem;flex:1;min-width:180px}
 button{min-height:40px;padding:0 14px;border:0;border-radius:9px;background:var(--acc);
  color:#fff;font-weight:600;font-size:15px;cursor:pointer}
 button.sec{background:var(--card2);border:1px solid var(--bordo);color:var(--txt);font-weight:500}
 button:disabled{opacity:.4}
 .esito{font-size:.8rem;margin-top:.5rem;min-height:1.1em;color:var(--ok)}
 .esito.err{color:#e08b86}
 nav{margin:1.6rem 0 .5rem;display:flex;flex-wrap:wrap;gap:.4rem 1rem;font-size:.85rem}
 a{color:var(--acc);text-decoration:none}
</style></head><body><div class="wrap">
<h1>Pagine dell&rsquo;interfaccia</h1>
<p class="sub">Ogni pagina qui sotto pu&ograve; essere sostituita da un file sulla
microSD: si carica, e da quel momento l&rsquo;hub serve la tua invece della sua &mdash;
senza ricompilare e senza riavviare. <b>Ripristina</b> toglie il file dalla card e
fa tornare quella del firmware, che resta sempre l&igrave; sotto: se la card manca o
la tua pagina &egrave; rotta, l&rsquo;interfaccia continua a funzionare.<br>
Le rotte disponibili sono elencate in <a href="/api">API</a>.</p>
<div id="l">lettura&hellip;</div>
<nav>
 <a href="/">Nodi</a><a href="/pannello">Pannello</a><a href="/immagini">Componi immagine</a>
 <a href="/pagine">Pagine</a><a href="/api">API</a><a href="/update">Aggiorna firmware</a>
</nav>
<script>
const E=document.getElementById.bind(document);
const esc=x=>String(x==null?'':x).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const dt=u=>u?new Date(u*1000).toLocaleString('it-IT',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'}):'';
let FW='';

function carica(){
 fetch('/api/pagine/elenco').then(r=>r.json()).then(d=>{
  FW=d.fw;
  E('l').innerHTML=d.pagine.map(p=>{
   const vecchia = p.su_card && p.fw_caricata && p.fw_caricata!=d.fw;
   const tag = !p.su_card ? '<span class="tag fw">dal firmware</span>'
             : (vecchia ? '<span class="tag old">caricata con '+esc(p.fw_caricata)+'</span>'
                        : '<span class="tag sd">dalla card</span>');
   let info;
   if(!p.su_card) info='Nessun file sulla card: l&rsquo;hub serve la sua.';
   else if(vecchia) info='Caricata con <b>'+esc(p.fw_caricata)+'</b>, adesso gira <b>'+esc(d.fw)+
     '</b>. Se nel frattempo &egrave; cambiata un&rsquo;API, questa pagina pu&ograve; chiamare '+
     'rotte che non esistono pi&ugrave;: controlla in <a href="/api">API</a>.';
   else info='Sulla card dal '+esc(dt(p.quando))+', caricata con questo firmware.';
   return '<div class="p"><div class="top"><b>'+esc(p.titolo)+'</b><code>'+esc(p.path)+'</code>'+tag+'</div>'+
    '<div class="info">'+info+'</div>'+
    '<div class="az"><input type="file" accept=".html,.htm" data-f="'+esc(p.nome)+'">'+
    '<button data-u="'+esc(p.nome)+'">Carica</button>'+
    (p.su_card?'<button class="sec" data-r="'+esc(p.nome)+'">Ripristina</button>':'')+
    '</div><div class="esito" data-e="'+esc(p.nome)+'"></div></div>';
  }).join('');
  if(!d.sd) E('l').insertAdjacentHTML('afterbegin',
    '<div class="p" style="border-color:var(--dan)"><div class="info">La microSD non &egrave; '+
    'montata: le pagine arrivano tutte dal firmware e non si pu&ograve; caricarne nessuna. '+
    'Tutto il resto continua a funzionare.</div></div>');
  aggancia();
 }).catch(()=>{E('l').textContent='elenco non leggibile';});
}

function esito(n,t,err){const e=document.querySelector('[data-e="'+n+'"]');
 if(!e)return; e.textContent=t; e.className='esito'+(err?' err':'');}

function aggancia(){
 document.querySelectorAll('[data-u]').forEach(b=>b.onclick=()=>{
  const n=b.dataset.u;
  const f=document.querySelector('[data-f="'+n+'"]').files[0];
  if(!f){esito(n,'Scegli prima un file .html',1);return;}
  esito(n,'Invio\u2026');
  const fd=new FormData(); fd.append('pagina',f);
  fetch('/api/pagine/carica?nome='+encodeURIComponent(n),{method:'POST',body:fd})
   .then(r=>r.text().then(t=>{esito(n,r.ok?'Caricata':t,!r.ok);if(r.ok)setTimeout(carica,600);}))
   .catch(e=>esito(n,'hub non raggiungibile',1));
 });
 document.querySelectorAll('[data-r]').forEach(b=>b.onclick=()=>{
  const n=b.dataset.r;
  if(!confirm('Togliere la tua pagina dalla card? Torna quella del firmware.'))return;
  fetch('/api/pagine/ripristina?nome='+encodeURIComponent(n),{method:'POST'})
   .then(r=>r.text().then(t=>{esito(n,r.ok?'Ripristinata':t,!r.ok);if(r.ok)setTimeout(carica,600);}));
 });
}
carica();
</script></div></body></html>
)HTML";

static void handlePaginePage() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().send_P(200, "text/html", PAGINE_PAGE);
}

static CorsMiddleware s_cors;

void web_ui_begin() {
  WebServer& srv = net_server();

  // Come su EnvNode_C3: senza collectAllHeaders() il middleware non vedrebbe
  // mai l'header Origin, non aggiungerebbe gli header CORS e (peggio) non
  // intercetterebbe il preflight OPTIONS, che finirebbe sul 404 di default.
  // Serve a www/dither.html, che gira come file locale nel browser e manda il
  // .bin direttamente qui: senza CORS quel POST sarebbe cross-origin e basta.
  srv.collectAllHeaders();
  s_cors.setOrigin("*").setAllowCredentials(false);
  srv.addMiddleware(&s_cors);
  // Tutte le rotte con un handler semplice si registrano dalla tabella: e'
  // l'unico posto dove esistono, e da li' esce anche /api/elenco.
  for (int i = 0; i < ROTTE_N; i++) {
    if (ROTTE[i].handler) srv.on(ROTTE[i].path, ROTTE[i].metodo, ROTTE[i].handler);
  }
  srv.on("/api/elenco", HTTP_GET, handleApiElenco);

  // Le rotte con upload multipart vogliono due callback e non entrano nella
  // forma della tabella: sono documentate li' con handler nullptr.
  srv.on("/api/immagini",     HTTP_POST, handleApiImmaginiDone,  handleApiImmaginiChunk);
  srv.on("/api/pagine/carica",HTTP_POST, handlePagineCaricaDone, handlePagineCaricaChunk);

  // Pagine sostituibili: gestione, upload e ripristino stanno SEMPRE nel
  // firmware, mai sulla card che possono sostituire. Una via di rientro che
  // dipende da cio' da cui si sta rientrando non e' una via di rientro.
  srv.on("/api",                   HTTP_GET,  handleApiPage);
  srv.on("/pagine",                HTTP_GET,  handlePaginePage);
  srv.on("/api/pagine/ripristina", HTTP_POST, handlePagineRipristina);
  srv.on("/api/pagine/elenco",     HTTP_GET,  handleApiPagineElenco);

  // I vecchi indirizzi della sola dashboard restano: erano scritti in
  // CLAUDE.md e nei segnalibri, e romperli non guadagnerebbe niente.
  srv.on("/dashboard-upload",     HTTP_GET,  handleDashboardUploadPage);
  srv.on("/dashboard-upload",     HTTP_POST, handleDashboardUploadDone, handleDashboardUploadChunk);
  srv.on("/dashboard-ripristina", HTTP_POST, handleDashboardRipristina);
}
