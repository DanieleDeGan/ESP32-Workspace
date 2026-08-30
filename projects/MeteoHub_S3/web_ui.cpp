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

  uint32_t pacchetti = 0, persi = 0;
  int muti = 0;
  const int n = remote_count();
  for (int i = 0; i < n; i++) {
    RemoteNode nodo;
    if (!remote_get(i, &nodo)) continue;
    pacchetti += nodo.pacchetti;
    persi     += nodo.persi;
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
  json.reserve(160 + 340 * n);
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
    json += "\"batteria_mv\":"   + String(r.batteria_mv) + ",";
    json += "\"seq\":"           + String(r.seq)         + ",";
    json += "\"pacchetti\":"     + String(r.pacchetti)   + ",";
    json += "\"persi\":"         + String(r.persi)       + ",";
    json += "\"riavvii\":"       + String(r.riavvii)     + ",";

    // Previsione: calcolata qui sull'hub, perche' un nodo che dorme non puo'
    // tenere tre ore di storico (remote_nodes.h). I campi restano null finche'
    // lo storico non arriva a tre ore, e storico_slot dice quanto manca -
    // senza quel numero, "non ancora noto" e "guasto" si somigliano troppo.
    json += "\"press_sea\":";  appendJsonFloat(json, r.pressSeaHpa, 2); json += ',';
    json += "\"delta_3h\":";   appendJsonFloat(json, r.delta3h, 2);     json += ',';
    json += "\"trend\":";      appendJsonString(json, remote_trend_label(r.trend)); json += ',';
    json += "\"previsione\":"; appendJsonString(json, remote_forecast_text(&r));    json += ',';
    json += "\"storico_slot\":" + String(r.storicoSlot);
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
<p class="muted"><a href="/">nodi</a> &mdash; <a href="/pannello">pannello e messaggi</a> &mdash; <a href="/immagini">componi immagine</a> &mdash; <a href="/dashboard-upload">dashboard personalizzata</a> &mdash; <a href="/update">aggiornamento firmware</a></p>
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
  j += "\"invii_interrotti\":" + String(s_invii_interrotti)   + ",";
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

  String j = "{\"sd\":";
  j += sd_mounted() ? "true" : "false";
  j += ",\"byte_attesi\":"; j += (unsigned long)IMG_BYTES_ESATTI;
  j += ",\"immagini\":[";
  sd_img_list(imgListCb, &j, 32);
  j += "]}";
  net_server().send(200, "application/json", j);
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

  const String nome = srv.hasArg("param") ? srv.arg("param") : String("");
  if (nome.length() == 0) { srv.send(400, "text/plain", "manca param"); return; }
  if (!sd_img_exists(nome.c_str())) {
    srv.send(404, "text/plain", "immagine non presente sulla card");
    return;
  }
  if (pages_add(PT_IMMAGINE, nome.c_str()) < 0) {
    srv.send(507, "text/plain", "non c'e' piu' posto nell'elenco delle pagine");
    return;
  }
  pages_save();
  handleApiPannello();
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
 select,input[type=text],input[type=number],textarea{
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
 .fine{display:flex;gap:8px;margin-top:12px}
 .fine button{flex:1}
 .esito{font-size:.82rem;color:var(--ok);min-height:1.1em;margin-top:.5rem}
 .esito.err{color:#e08b86}

 /* --- immagini --- */
 .gal{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px}
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
 @media (max-width:420px){
  .azioni{grid-template-columns:1fr}
  .riga{flex-wrap:wrap}
 }
</style></head><body><div class="wrap">
<h1>Pannello e-ink</h1>

<div class="card" id="ora">
 <div class="muted" style="margin:0">A schermo adesso</div>
 <div style="font-size:1.35rem;font-weight:700;margin:.25rem 0 .8rem" id="oraNome">&mdash;</div>
 <button class="sec full" id="brf">Aggiorna il pannello adesso</button>
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
  <label>Non ruotare dalle</label>
  <div class="duo">
   <select id="sda"></select><span class="muted" style="margin:0">alle</span><select id="sa"></select>
  </div>
 </div>
 <div class="esito" id="ss"></div>
 <p class="muted">Con una sola pagina attiva non ruota: non c'&egrave; dove andare, e un
 cambio &egrave; sempre un refresh completo (~2,2 s, e lampeggia).</p>
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

<h2>Sulla card, non in elenco <span class="conta" id="contaCard"></span></h2>
<div class="gal" id="limg"></div>
<div class="card" id="cardVuota" style="display:none">
 <p class="muted" style="margin:0">Tutte le immagini della card sono gi&agrave; fra le
 pagine qui sopra.</p>
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
 <a href="/dashboard-upload">Dashboard</a><a href="/update">Aggiorna firmware</a>
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
for(let h=0;h<24;h++){const t=('0'+h).slice(-2)+':00';
 E('sda').insertAdjacentHTML('beforeend','<option value="'+h+'">'+t+'</option>');
 E('sa').insertAdjacentHTML('beforeend','<option value="'+h+'">'+t+'</option>');}

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

// I 15.000 byte di ogni immagine si scaricano UNA volta sola: la stessa
// figura puo' stare in piu' slot, e la pagina si ridisegna ogni 15 secondi.
const BIN={};
function anteprima(box,nome){
 const cv=document.createElement('canvas'); cv.width=400; cv.height=300;
 box.appendChild(cv);
 const dipingi=by=>{
  const ctx=cv.getContext('2d'),img=ctx.createImageData(400,300);
  for(let y=0;y<300;y++)for(let x=0;x<400;x++){
   const bit=(by[y*50+(x>>3)]>>(7-(x&7)))&1,o=(y*400+x)*4,v=bit?255:0;
   img.data[o]=img.data[o+1]=img.data[o+2]=v;img.data[o+3]=255;}
  ctx.putImageData(img,0,0);};
 if(BIN[nome]){dipingi(BIN[nome]);return;}
 fetch('/api/immagini/scarica?nome='+encodeURIComponent(nome))
  .then(r=>r.ok?r.arrayBuffer():Promise.reject())
  .then(b=>{BIN[nome]=new Uint8Array(b);dipingi(BIN[nome]);})
  .catch(()=>{box.className='mini gen';box.innerHTML='&#9888;';});
}

const GLIFO={nodi:'&#9925;',messaggio:'&#9993;',bianca:'&#9634;'};
var ULTIMO=null, SULLACARD=[];

function render(d){
 ULTIMO=d;
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
       (p.corrente?'<span class="badge">A SCHERMO</span>':'')+'</div>'+
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
     (p.tipo=='immagine'?'<button class="dan sol" data-x="'+p.i+'">Togli dall\'elenco</button>':'')+
   '</div>';
  box.appendChild(el);
  if(p.corrente) E('oraNome').textContent = p.tipo + (p.param? ' \u2014 '+p.param : '');
 });

 box.querySelectorAll('[data-mini]').forEach(m=>{
  if(m.dataset.tipo=='immagine'&&m.dataset.mini) anteprima(m,m.dataset.mini);});

 // Quanti posti restano: il numero che prima non compariva da nessuna parte,
 // e la cui assenza faceva sembrare rotto il pulsante che li riempiva.
 const tot=d.slot_totali;
 E('conta').textContent = tot? ('\u2014 '+d.pagine.length+' su '+tot+
   ((tot-d.pagine.length)?', '+(tot-d.pagine.length)+' liberi':', elenco pieno')) : '';

 if(!salvaTimer){
  E('rot').checked=d.rotazione; E('sda').value=d.silenzio_da; E('sa').value=d.silenzio_a;
 }
 E('fas').checked=d.fascia;

 box.querySelectorAll('[data-a]').forEach(c=>c.onchange=()=>
   postJson('/api/pannello/pagina?i='+c.dataset.a+'&attiva='+(c.checked?1:0)));
 box.querySelectorAll('[data-d]').forEach(c=>c.onchange=()=>
   postJson('/api/pannello/pagina?i='+c.dataset.d+'&durata='+c.value));
 box.querySelectorAll('[data-v]').forEach(b=>b.onclick=()=>{
   b.disabled=true;b.textContent='in coda...';
   post('/api/pannello/vai?i='+b.dataset.v).then(()=>setTimeout(carica,3500));});
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

// Le immagini della card che NON sono gia' una pagina. Un'immagine in uso sta
// in un posto solo — l'elenco — con la sua anteprima e i suoi comandi accanto.
function disegnaCard(){
 const box=E('limg'); if(!box) return;
 const inUso={};
 if(ULTIMO) ULTIMO.pagine.forEach(p=>{if(p.tipo=='immagine'&&p.param)inUso[p.param]=1;});
 const fuori=SULLACARD.filter(im=>!inUso[im.nome]);

 box.innerHTML='';
 E('contaCard').textContent = SULLACARD.length? ('\u2014 '+fuori.length+' di '+SULLACARD.length):'';
 E('cardVuota').style.display = (SULLACARD.length&&!fuori.length)?'block':'none';

 fuori.forEach(im=>{
  const el=document.createElement('div'); el.className='im';
  el.innerHTML='<div class="mini" style="width:100%" data-mini="'+esc(im.nome)+'"></div>'+
   '<div class="nm"><b>'+esc(im.nome)+'</b><span>'+(im.ok?'ok':im.byte+' byte, non valida')+'</span></div>'+
   '<div class="azioni"><button class="sec" data-add="'+esc(im.nome)+'">Mettila fra le pagine</button>'+
   '<button class="dan" data-del="'+esc(im.nome)+'">Elimina</button></div>';
  box.appendChild(el);
  if(im.ok) anteprima(el.querySelector('[data-mini]'),im.nome);
 });

 box.querySelectorAll('[data-add]').forEach(b=>b.onclick=()=>
   postJson('/api/pannello/aggiungi?param='+encodeURIComponent(b.dataset.add),E('simg'))
     .then(()=>window.scrollTo({top:0,behavior:'smooth'})));
 box.querySelectorAll('[data-del]').forEach(b=>b.onclick=()=>{
   if(!confirm('Eliminare '+b.dataset.del+' dalla card?'))return;
   delete BIN[b.dataset.del];
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

function caricaImmagini(){
 fetch('/api/immagini').then(r=>r.json()).then(d=>{
  SULLACARD = (d.sd && d.immagini) ? d.immagini : [];
  disegnaCard();
 });
}

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
  const q='/api/pannello?rotazione='+(E('rot').checked?1:0)+'&sil_da='+E('sda').value+'&sil_a='+E('sa').value;
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
E('sda').onchange=()=>salvaRotazione(800);
E('sa').onchange=()=>salvaRotazione(800);
E('brf').onclick=()=>post('/api/pannello/refresh').then(()=>flash(E('srf'),'Refresh in coda&hellip;'));
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
static void handleImmaginiPage() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().send_P(200, "text/html", DITHER_PAGE);
}

static void handlePannelloPage() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().send_P(200, "text/html", PANNELLO_PAGE);
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

static void handleApiPannello() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  const uint8_t corrente = pages_current();
  String j = "{\"slot_totali\":";
  j += pages_slots();
  j += ",\"rotazione\":";
  j += pages_rotazione() ? "true" : "false";
  j += ",\"silenzio_da\":"; j += pages_silenzio_da();
  j += ",\"silenzio_a\":";  j += pages_silenzio_a();
  j += ",\"fascia\":";
  j += pages_fascia() ? "true" : "false";
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

// POST /api/pannello?rotazione=0|1&sil_da=23&sil_a=7
static void handleApiPannelloSet() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (srv.hasArg("rotazione")) pages_set_rotazione(srv.arg("rotazione").toInt() != 0);
  if (srv.hasArg("sil_da") && srv.hasArg("sil_a"))
    pages_set_silenzio((uint8_t)srv.arg("sil_da").toInt(), (uint8_t)srv.arg("sil_a").toInt());
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
 <p class="muted"><a href="/">nodi</a> &mdash; <a href="/pannello">pannello e messaggi</a> &mdash; <a href="/immagini">componi immagine</a> &mdash; <a href="/dashboard-upload">dashboard personalizzata</a> &mdash; <a href="/update">aggiornamento firmware</a></p>
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
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  // Dashboard personalizzata sulla card, se c'è (vedi /dashboard-upload):
  // altrimenti quella incorporata nel firmware. Passa da
  // streamFileLimitato() come ogni file servito da qui: un client che se
  // ne va a metà non deve tenere fermo il loop(), che nel frattempo non
  // preleverebbe i DATA dei nodi.
  File custom = sd_open_dashboard();
  if (custom) {
    streamFileLimitato(net_server(), custom, "text/html");
    custom.close();
    return;
  }
  net_server().send_P(200, "text/html", HUB_PAGE);
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
  srv.on("/",                    HTTP_GET,  handleRoot);
  srv.on("/api/stato",           HTTP_GET,  handleApiStato);
  srv.on("/api/salute",          HTTP_GET,  handleApiSalute);
  srv.on("/api/nodi",            HTTP_GET,  handleApiNodi);
  srv.on("/api/pairing",         HTTP_POST, handleApiPairing);
  srv.on("/api/nodi/dimentica",  HTTP_POST, handleApiNodiDimentica);
  srv.on("/api/nodi/altitudine", HTTP_POST, handleApiNodiAltitudine);
  srv.on("/api/nodi/giorni",     HTTP_GET,  handleApiNodiGiorni);
  srv.on("/api/nodi/scarica",    HTTP_GET,  handleApiNodiScarica);

  // Dashboard personalizzata: la pagina di upload e il ripristino stanno
  // sempre nel firmware, mai sulla card che possono sostituire.
  srv.on("/dashboard-upload",     HTTP_GET,  handleDashboardUploadPage);
  srv.on("/dashboard-upload",     HTTP_POST, handleDashboardUploadDone, handleDashboardUploadChunk);
  srv.on("/dashboard-ripristina", HTTP_POST, handleDashboardRipristina);

  // Pannello: pagine, rotazione, messaggi. Gli handler che toccherebbero il
  // display si limitano ad accodare — il refresh lo fa il loop().
  srv.on("/pannello",                HTTP_GET,  handlePannelloPage);
  srv.on("/api/pannello",            HTTP_GET,  handleApiPannello);
  srv.on("/api/pannello",            HTTP_POST, handleApiPannelloSet);
  srv.on("/api/pannello/pagina",     HTTP_POST, handleApiPannelloPagina);
  srv.on("/api/pannello/vai",        HTTP_POST, handleApiPannelloVai);
  srv.on("/api/pannello/refresh",    HTTP_POST, handleApiPannelloRefresh);
  srv.on("/api/messaggio",           HTTP_GET,  handleApiMessaggio);
  srv.on("/api/messaggio",           HTTP_POST, handleApiMessaggioSet);
  srv.on("/api/messaggio/cancella",  HTTP_POST, handleApiMessaggioCancella);

  // Immagini sulla card e pagine che le mostrano.
  srv.on("/immagini",                HTTP_GET,  handleImmaginiPage);
  srv.on("/api/immagini",            HTTP_GET,  handleApiImmagini);
  srv.on("/api/immagini",            HTTP_POST, handleApiImmaginiDone, handleApiImmaginiChunk);
  srv.on("/api/immagini/elimina",    HTTP_POST, handleApiImmaginiElimina);
  srv.on("/api/immagini/scarica",    HTTP_GET,  handleApiImmaginiScarica);
  srv.on("/api/pannello/aggiungi",   HTTP_POST, handleApiPannelloAggiungi);
  srv.on("/api/pannello/rimuovi",    HTTP_POST, handleApiPannelloRimuovi);
  srv.on("/api/pannello/sposta",     HTTP_POST, handleApiPannelloSposta);
}
