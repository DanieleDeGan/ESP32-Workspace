/*
 * web_ui.cpp - pagina di stato e API HTTP dell'hub.
 * ---------------------------------------------------------------------------
 * Gli handler dei nodi, gli helper JSON e streamFileLimitato() vengono da
 * projects/EnvNode_C3/web_ui.cpp e sono presi tali e quali: l'hub espone gli
 * STESSI endpoint (/api/nodi, /api/pairing, /api/nodi/*), cosi' quello che si
 * sa fare con una scheda vale anche con l'altra. Non e' stato portato tutto
 * cio' che riguardava il sensore locale del C3 (dashboard su SD, /api/giorno,
 * min/max): questa scheda non misura niente di suo, riceve.
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

#include <WiFi.h>
#include <WebServer.h>
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

static void appendJsonString(String& out, const char* s) {
  out += '"';
  if (s) {
    // Limite di lunghezza indipendente dalla terminazione: se mai un
    // chiamante passasse un buffer non terminato (vedi il commento su
    // rtctime_format in rtc_time.cpp), questo evita comunque una lettura
    // indefinita oltre il buffer invece di bloccare il web server.
    for (size_t i = 0; i < 256 && s[i] != '\0'; i++) {
      char c = s[i];
      if (c == '"' || c == '\\') { out += '\\'; out += c; }
      else if ((unsigned char)c >= 0x20) out += c;
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
<p class="muted"><a href="/update">aggiornamento firmware (OTA)</a> &mdash;
 i registri dei nodi stanno su microSD, un file per giorno per nodo.</p>
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
  j += "\"uptime\":" + String(millis() / 1000) + ",";
  j += "\"heap\":"   + String(ESP.getFreeHeap());
  j += '}';
  net_server().send(200, "application/json", j);
}

static void handleRoot() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().send_P(200, "text/html", HUB_PAGE);
}

void web_ui_begin() {
  WebServer& srv = net_server();
  srv.on("/",                    HTTP_GET,  handleRoot);
  srv.on("/api/stato",           HTTP_GET,  handleApiStato);
  srv.on("/api/nodi",            HTTP_GET,  handleApiNodi);
  srv.on("/api/pairing",         HTTP_POST, handleApiPairing);
  srv.on("/api/nodi/dimentica",  HTTP_POST, handleApiNodiDimentica);
  srv.on("/api/nodi/altitudine", HTTP_POST, handleApiNodiAltitudine);
  srv.on("/api/nodi/giorni",     HTTP_GET,  handleApiNodiGiorni);
  srv.on("/api/nodi/scarica",    HTTP_GET,  handleApiNodiScarica);
}
