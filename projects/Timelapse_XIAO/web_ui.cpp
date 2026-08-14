#include "web_ui.h"
#include "net_ota.h"
#include "camera.h"
#include "storage.h"
#include "rtc_time.h"

#define STREAM_BOUNDARY "camframe"

// ---------------------------------------------------------------------
//  Pagina di controllo (PROGMEM: sta in flash, non in RAM)
// ---------------------------------------------------------------------
static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Timelapse XIAO</title><style>
 :root{color-scheme:dark}
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:1rem;display:flex;justify-content:center}
 .wrap{max-width:820px;width:100%}
 h1{font-size:1.1rem;margin:0 0 .8rem}
 .card{background:#1c1c1c;border:1px solid #333;border-radius:12px;padding:1rem;margin-bottom:1rem}
 .card h2{font-size:.85rem;text-transform:uppercase;letter-spacing:.05em;color:#9aa;margin:0 0 .7rem}
 button{padding:.55rem .9rem;border:0;border-radius:8px;background:#3b82f6;color:#fff;font-size:.9rem;cursor:pointer;margin:0 .4rem .4rem 0}
 button.sec{background:#374151}
 button.dan{background:#b91c1c}
 img#live,img#play{width:100%;background:#000;border-radius:8px;display:block;min-height:120px}
 .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:.5rem .9rem;font-size:.85rem}
 .grid div span{color:#9aa}
 label{display:block;font-size:.8rem;color:#9aa;margin:.5rem 0 .2rem}
 select,input{background:#111;color:#eee;border:1px solid #444;border-radius:6px;padding:.4rem;width:100%}
 .row{display:flex;gap:.8rem;flex-wrap:wrap}
 .row>div{flex:1 1 130px}
 .ok{color:#4ade80}.no{color:#f87171}.muted{color:#8a8a8a;font-size:.78rem;line-height:1.4}
 a{color:#3b82f6}
 .gal{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:4px;margin-top:.7rem;max-height:340px;overflow-y:auto}
 .gal figure{margin:0;position:relative}
 .gal img{width:100%;height:78px;object-fit:cover;border-radius:4px;display:block;cursor:pointer;background:#000}
 .gal figcaption{font-size:.65rem;color:#9aa;text-align:center}
 .gal .x{position:absolute;top:2px;right:2px;background:#000a;border:0;color:#f87171;border-radius:4px;padding:0 .3rem;margin:0;cursor:pointer;font-size:.8rem}
 .pl{display:flex;align-items:center;gap:.5rem;flex-wrap:wrap;margin-top:.6rem}
 .pl input[type=range]{flex:1 1 160px}
 .pl select{width:auto}
</style></head><body><div class="wrap">
<h1>Timelapse XIAO <span id="fw" class="muted"></span></h1>

<div class="card"><h2>Inquadratura</h2>
 <img id="live" alt="">
 <div style="margin-top:.7rem">
  <button id="bstream">Avvia video</button>
  <button id="bshot" class="sec">Anteprima</button>
  <button id="bsave">Scatta e salva</button>
 </div>
 <p class="muted">Il video serve a puntare la camera: mentre e' in corso la
 scheda non risponde ad altro (nemmeno all'OTA) e gli scatti automatici
 vengono saltati. Si ferma da solo dopo 5 minuti.</p>
 <p class="muted" id="msg"></p>
</div>

<div class="card"><h2>Stato</h2><div class="grid" id="stato"></div></div>

<div class="card"><h2>Timelapse</h2>
 <div class="row">
  <div><label>Scatti automatici</label>
   <select id="attivo"><option value="1">Attivi</option><option value="0">Fermi</option></select></div>
  <div><label>Intervallo (s)</label><input id="intervallo" type="number" min="5" max="86400"></div>
 </div>
 <div class="row">
  <div><label>Dalle ore</label><input id="oraInizio" type="number" min="0" max="23"></div>
  <div><label>Alle ore</label><input id="oraFine" type="number" min="0" max="23"></div>
 </div>
 <p class="muted">Stessa ora in entrambi i campi = scatta tutto il giorno.
 Inizio maggiore della fine = finestra che scavalca la mezzanotte (es. 22 &rarr; 6).</p>
 <div class="row">
  <div><label>Quando la card e' piena</label>
   <select id="politica"><option value="0">Ferma gli scatti</option><option value="1">Elimina il giorno piu' vecchio</option></select></div>
  <div><label>Spazio minimo libero (MB)</label><input id="minliberi" type="number" min="10" max="60000"></div>
 </div>
 <div style="margin-top:.8rem"><button id="bcfg">Applica</button></div>
</div>

<div class="card"><h2>Camera</h2>
 <div class="row">
  <div><label>Risoluzione</label><select id="size"></select></div>
  <div><label>Qualita' JPEG (10 = migliore)</label><input id="qualita" type="number" min="10" max="40"></div>
 </div>
 <div class="row">
  <div><label>Capovolgi verticale</label>
   <select id="vflip"><option value="0">No</option><option value="1">Si</option></select></div>
  <div><label>Specchia orizzontale</label>
   <select id="hmirror"><option value="0">No</option><option value="1">Si</option></select></div>
 </div>
 <div style="margin-top:.8rem"><button id="bcam">Applica</button>
  <a href="/update" style="margin-left:.6rem">Aggiorna firmware</a></div>
 <p class="muted">Risoluzione e qualita' non sono persistenti: dopo un
 riavvio la camera riparte dai valori di fabbrica dello sketch.</p>
</div>

<div class="card"><h2>Archivio</h2>
 <div class="row">
  <div><label>Giorno</label><select id="giorno"></select></div>
  <div style="flex:0 0 auto;display:flex;align-items:flex-end">
   <button id="brefresh" class="sec">Aggiorna</button>
   <a id="csv" href="#" class="muted" style="margin:0 .6rem .6rem 0">CSV</a>
   <button id="bdelday" class="dan">Elimina giorno</button>
  </div>
 </div>
 <img id="play" alt="">
 <div class="pl">
  <button id="bprev" class="sec">&#9664;</button>
  <button id="bplay">&#9654; Riproduci</button>
  <button id="bnext" class="sec">&#9654;|</button>
  <input id="pos" type="range" min="0" max="0" value="0">
  <select id="fps"><option value="2">2 fps</option><option value="5" selected>5 fps</option><option value="10">10 fps</option><option value="20">20 fps</option></select>
  <span class="muted" id="pinfo"></span>
 </div>
 <div class="gal" id="gal"></div>
 <p class="muted">La riproduzione scarica una foto alla volta dalla scheda:
 a 20 fps il WiFi non ce la fa e il filmato va a scatti. Per il montaggio
 vero conviene copiare la cartella del giorno dalla microSD.</p>
</div>

<script>
const $=id=>document.getElementById(id);
let streaming=false, foto=[], idx=0, playing=false, ptimer=null;

function msg(t){$('msg').textContent=t;}
function giornoSel(){return $('giorno').value;}
function url(i){return '/foto?g='+giornoSel()+'&f='+encodeURIComponent(foto[i].n);}

// Non sovrascrivere il campo che l'utente sta modificando in quel momento.
function set(id,v){const e=$(id); if(document.activeElement!==e) e.value=v;}

function stato(){
 if(streaming) return;                       // il server e' occupato a trasmettere
 fetch('/api/stato').then(r=>r.json()).then(s=>{
  $('fw').textContent='— '+s.nodo+' · fw '+s.fw;
  const si=(c,t)=>'<span class="'+(c?'ok':'no')+'">'+t+'</span>';
  $('stato').innerHTML=
   '<div><span>Rete</span><br>'+(s.wifi?si(1,s.ip+' ('+s.rssi+' dBm)'):si(0,'non connesso'))+'</div>'+
   '<div><span>Orario</span><br>'+si(s.fonte_ora=='NTP',s.ora+' · '+s.fonte_ora)+'</div>'+
   '<div><span>Scatti automatici</span><br>'+(s.attivo?si(s.in_finestra,s.in_finestra?('ogni '+s.intervallo+' s'):'fuori orario'):si(0,'fermi'))+'</div>'+
   '<div><span>Prossimo scatto</span><br>'+(s.prossimo>=0?('tra '+s.prossimo+' s'):'—')+'</div>'+
   '<div><span>Oggi</span><br>'+s.foto_oggi+' foto'+(s.giorno_oggi?' in '+s.giorno_oggi:'')+'</div>'+
   '<div><span>Scatti</span><br>'+s.scatti_sessione+' da accensione · '+s.scatti_totali+' totali</div>'+
   '<div><span>microSD</span><br>'+(s.sd?si(s.sd_liberi>s.min_liberi,s.sd_liberi+' MB liberi su '+s.sd_totali):si(0,s.sd_errore))+'</div>'+
   '<div><span>Ultimo scatto</span><br>'+(s.ultimo?s.ultimo:'—')+(s.ultimo_errore?si(0,' · '+s.ultimo_errore):'')+'</div>'+
   '<div><span>Camera</span><br>'+s.sensore+' · '+s.size_nome+'</div>'+
   '<div><span>Acceso da</span><br>'+s.uptime+' s · heap '+Math.round(s.heap/1024)+' KB</div>';
  if($('size').options.length!=s.sizes.length){
   $('size').innerHTML=s.sizes.map((n,i)=>'<option value="'+i+'">'+n+'</option>').join('');
  }
  set('attivo',s.attivo?'1':'0'); set('intervallo',s.intervallo);
  set('oraInizio',s.ora_inizio); set('oraFine',s.ora_fine);
  set('politica',String(s.politica)); set('minliberi',s.min_liberi);
  set('size',s.size); set('qualita',s.qualita);
  set('vflip',s.vflip?'1':'0'); set('hmirror',s.hmirror?'1':'0');
 }).catch(()=>{});
}

$('bstream').onclick=()=>{
 streaming=!streaming;
 $('bstream').textContent=streaming?'Ferma video':'Avvia video';
 $('live').src=streaming?('/stream?'+Date.now()):'';
 if(!streaming) setTimeout(stato,400);
};
$('bshot').onclick=()=>{ if(streaming) return msg('Ferma prima il video.');
 $('live').src='/snapshot.jpg?'+Date.now(); msg('Anteprima (non salvata).'); };
$('bsave').onclick=()=>{ if(streaming) return msg('Ferma prima il video.');
 msg('Scatto in corso...');
 fetch('/api/scatta',{method:'POST'}).then(r=>r.json()).then(r=>{
  msg(r.ok?('Salvata: '+r.giorno+'/'+r.file):('Errore: '+r.errore));
  if(r.ok){$('live').src='/foto?g='+r.giorno+'&f='+r.file+'&'+Date.now(); giorni(); stato();}
 }).catch(()=>msg('Errore di rete'));
};

function config(q){
 fetch('/api/config?'+new URLSearchParams(q),{method:'POST'})
  .then(r=>r.json()).then(r=>{msg(r.ok?'Impostazioni applicate.':'Errore');stato();})
  .catch(()=>msg('Errore di rete'));
}
$('bcfg').onclick=()=>config({attivo:$('attivo').value,intervallo:$('intervallo').value,
 ora_inizio:$('oraInizio').value,ora_fine:$('oraFine').value,
 politica:$('politica').value,min_liberi:$('minliberi').value});
$('bcam').onclick=()=>config({size:$('size').value,qualita:$('qualita').value,
 vflip:$('vflip').value,hmirror:$('hmirror').value});

// ---- archivio -------------------------------------------------------
function giorni(){
 fetch('/api/giorni').then(r=>r.json()).then(l=>{
  l.sort().reverse();
  const prima=giornoSel();
  $('giorno').innerHTML=l.map(g=>'<option>'+g+'</option>').join('')||'<option value="">nessun giorno</option>';
  if(l.includes(prima)) $('giorno').value=prima;
  elenco();
 }).catch(()=>{});
}
function elenco(){
 const g=giornoSel();
 $('csv').href='/log?g='+g;
 if(!g){foto=[];$('gal').innerHTML='<p class="muted">Nessuna foto sulla card.</p>';return;}
 fetch('/api/foto?g='+g).then(r=>r.json()).then(l=>{
  foto=l.sort((a,b)=>a.n.localeCompare(b.n));
  $('pos').max=Math.max(0,foto.length-1);
  $('gal').innerHTML=foto.map((f,i)=>
   '<figure><img loading="lazy" src="'+url(i)+'" onclick="vai('+i+')" alt="'+f.n+'">'+
   '<button class="x" onclick="elimina('+i+')">&times;</button>'+
   '<figcaption>'+f.n.substr(0,2)+':'+f.n.substr(2,2)+':'+f.n.substr(4,2)+'</figcaption></figure>').join('')
   ||'<p class="muted">Nessuna foto in questo giorno.</p>';
  vai(0);
 }).catch(()=>{});
}
function vai(i){
 if(!foto.length){$('play').src='';$('pinfo').textContent='';return;}
 idx=(i+foto.length)%foto.length;
 $('play').src=url(idx);
 $('pos').value=idx;
 $('pinfo').textContent=(idx+1)+' / '+foto.length+' · '+Math.round(foto[idx].b/1024)+' KB';
 if(foto.length>1) new Image().src=url((idx+1)%foto.length);   // precarica il prossimo
}
function stop(){playing=false;clearInterval(ptimer);$('bplay').innerHTML='&#9654; Riproduci';}
$('bplay').onclick=()=>{
 if(playing) return stop();
 if(foto.length<2) return;
 playing=true;$('bplay').innerHTML='&#10073;&#10073; Pausa';
 ptimer=setInterval(()=>{ if(idx+1>=foto.length){vai(0);stop();} else vai(idx+1); },1000/+$('fps').value);
};
$('bprev').onclick=()=>{stop();vai(idx-1);};
$('bnext').onclick=()=>{stop();vai(idx+1);};
$('pos').oninput=e=>{stop();vai(+e.target.value);};
$('fps').onchange=()=>{ if(playing){stop();$('bplay').click();} };
$('giorno').onchange=()=>{stop();elenco();};
$('brefresh').onclick=giorni;
function elimina(i){
 const f=foto[i].n;
 if(!confirm('Eliminare '+f+'?')) return;
 fetch('/api/elimina?g='+giornoSel()+'&f='+encodeURIComponent(f),{method:'POST'}).then(()=>{elenco();stato();});
}
$('bdelday').onclick=()=>{
 const g=giornoSel(); if(!g) return;
 if(!confirm('Eliminare TUTTE le foto del '+g+'? Il CSV del giorno resta.')) return;
 fetch('/api/elimina-giorno?g='+g,{method:'POST'}).then(()=>{giorni();stato();});
};

stato(); giorni(); setInterval(stato,3000);
</script></div></body></html>
)HTML";

// ---------------------------------------------------------------------
//  Handler
// ---------------------------------------------------------------------
static bool authed() {
  if (net_webAuthOk()) return true;
  net_server().requestAuthentication();
  return false;
}

static void handleRoot() {
  if (!authed()) return;
  net_server().send_P(200, "text/html", PAGE);
}

// Un fotogramma al volo, senza toccare la microSD.
static void handleSnapshot() {
  if (!authed()) return;
  WebServer& server = net_server();
  camera_fb_t* fb = camera_grab_fresh();
  if (!fb) {
    server.send(503, "text/plain", "camera non disponibile");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.client().write(fb->buf, fb->len);   // il JPEG sta in PSRAM: si scrive diretto sul socket
  camera_release(fb);
}

// Stream MJPEG. La risposta viene scritta a mano sul socket invece che con
// server.send(): a lunghezza sconosciuta il WebServer userebbe il chunked
// encoding, che qui corromperebbe il multipart.
static void handleStream() {
  if (!authed()) return;
  WebServer& server = net_server();
  if (!camera_ready()) {
    server.send(503, "text/plain", "camera non disponibile");
    return;
  }

  NetworkClient& client = server.client();
  client.printf("HTTP/1.1 200 OK\r\n"
                "Content-Type: multipart/x-mixed-replace; boundary=" STREAM_BOUNDARY "\r\n"
                "Cache-Control: no-store\r\n"
                "Connection: close\r\n\r\n");

  const uint32_t t0 = millis();
  while (client.connected() && (millis() - t0) < WEB_STREAM_MAX_MS) {
    camera_fb_t* fb = camera_grab();
    if (!fb) break;
    const size_t len = fb->len;
    client.printf("--" STREAM_BOUNDARY "\r\nContent-Type: image/jpeg\r\n"
                  "Content-Length: %u\r\n\r\n", (unsigned)len);
    const size_t sent = client.write(fb->buf, len);
    client.print("\r\n");
    camera_release(fb);
    if (sent != len) break;   // client scollegato a meta' frame

    // Il web server e' fermo qui dentro: senza questa chiamata il timer del
    // timelapse non avanzerebbe per tutta la durata dello stream.
    app_pump();
  }
  client.print("--" STREAM_BOUNDARY "--\r\n");
  client.stop();
}

static void handleCapture() {
  if (!authed()) return;
  WebServer& server = net_server();
  char day[16], name[24], json[128];
  if (app_capture_now(day, sizeof(day), name, sizeof(name))) {
    snprintf(json, sizeof(json), "{\"ok\":true,\"giorno\":\"%s\",\"file\":\"%s\"}", day, name);
    server.send(200, "application/json", json);
  } else {
    snprintf(json, sizeof(json), "{\"ok\":false,\"errore\":\"%s\"}",
             camera_ready() ? sd_last_error() : "camera non disponibile");
    server.send(500, "application/json", json);
  }
}

static void handleStato() {
  if (!authed()) return;

  String sizes = "[";
  for (int i = 0; i < camera_size_count(); i++) {
    if (i) sizes += ',';
    sizes += '"';
    sizes += camera_size_name(i);
    sizes += '"';
  }
  sizes += ']';

  char ora[24];
  if (!rtctime_format(rtctime_now(), "%Y-%m-%d %H:%M:%S", ora, sizeof(ora))) ora[0] = '\0';

  // static: 1.4 KB sono troppi per lo stack del task che serve le richieste,
  // e comunque questo handler non e' rientrante (web server sincrono).
  static char buf[1400];
  snprintf(buf, sizeof(buf),
           "{\"nodo\":\"%s\",\"fw\":\"%s\","
           "\"wifi\":%s,\"ip\":\"%s\",\"rssi\":%d,"
           "\"ora\":\"%s\",\"fonte_ora\":\"%s\","
           "\"attivo\":%s,\"intervallo\":%lu,\"ora_inizio\":%d,\"ora_fine\":%d,"
           "\"in_finestra\":%s,\"prossimo\":%ld,"
           "\"scatti_sessione\":%lu,\"scatti_totali\":%lu,"
           "\"foto_oggi\":%lu,\"giorno_oggi\":\"%s\","
           "\"ultimo\":\"%s\",\"ultimo_errore\":\"%s\","
           "\"sd\":%s,\"sd_errore\":\"%s\",\"sd_totali\":%llu,\"sd_usati\":%llu,"
           "\"sd_liberi\":%llu,\"min_liberi\":%lu,\"politica\":%d,"
           "\"sensore\":\"%s\",\"size\":%d,\"size_nome\":\"%s\",\"sizes\":%s,"
           "\"qualita\":%d,\"vflip\":%s,\"hmirror\":%s,"
           "\"uptime\":%lu,\"heap\":%lu}",
           app_node_name(), app_fw_version(),
           net_isConnected() ? "true" : "false", net_ip().c_str(), net_rssi(),
           ora, rtctime_source(),
           app_enabled() ? "true" : "false", (unsigned long)app_interval_s(),
           app_window_start(), app_window_end(),
           app_in_window() ? "true" : "false", app_next_shot_s(),
           (unsigned long)app_shots_session(), (unsigned long)sd_shot_total(),
           (unsigned long)sd_photos_today(), sd_today_dir(),
           app_last_shot_iso(), app_last_error(),
           sd_mounted() ? "true" : "false", sd_last_error(),
           sd_total_mb(), sd_used_mb(), sd_free_mb(),
           (unsigned long)app_min_free_mb(), app_full_policy(),
           camera_sensor_name(), camera_size_index(), camera_size_name(camera_size_index()),
           sizes.c_str(), camera_quality(),
           camera_vflip() ? "true" : "false", camera_hmirror() ? "true" : "false",
           (unsigned long)(millis() / 1000), (unsigned long)ESP.getFreeHeap());
  net_server().send(200, "application/json", buf);
}

static void handleConfig() {
  if (!authed()) return;
  WebServer& server = net_server();

  if (server.hasArg("attivo"))     app_set_enabled(server.arg("attivo").toInt() != 0);
  if (server.hasArg("intervallo")) app_set_interval_s((uint32_t)server.arg("intervallo").toInt());
  if (server.hasArg("ora_inizio") && server.hasArg("ora_fine")) {
    app_set_window(server.arg("ora_inizio").toInt(), server.arg("ora_fine").toInt());
  }
  if (server.hasArg("politica"))   app_set_full_policy(server.arg("politica").toInt());
  if (server.hasArg("min_liberi")) app_set_min_free_mb((uint32_t)server.arg("min_liberi").toInt());

  if (server.hasArg("size"))       camera_set_size_index(server.arg("size").toInt());
  if (server.hasArg("qualita"))    camera_set_quality(server.arg("qualita").toInt());
  if (server.hasArg("vflip") || server.hasArg("hmirror")) {
    camera_set_flip(server.arg("vflip").toInt() != 0, server.arg("hmirror").toInt() != 0);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// Gli elenchi si accumulano direttamente come JSON dentro la callback.
static void dayCb(const char* day, void* arg) {
  String* out = (String*)arg;
  if (out->length() > 1) *out += ',';
  *out += '"';
  *out += day;
  *out += '"';
}

// L'elenco delle foto di un giorno puo' essere lunghissimo (a 5 s di
// intervallo sono 17280 voci): accumularlo tutto in una String prima di
// spedirlo esaurirebbe l'heap. Si spedisce a blocchi (chunked), tenendo in
// RAM solo il pezzo in costruzione.
struct ChunkedList {
  String buf;
  bool   first = true;
};

static void photoCb(const char* name, size_t size, void* arg) {
  ChunkedList* o = (ChunkedList*)arg;
  if (!o->first) o->buf += ',';
  o->first = false;
  o->buf += "{\"n\":\"";
  o->buf += name;
  o->buf += "\",\"b\":";
  o->buf += (uint32_t)size;
  o->buf += '}';
  if (o->buf.length() >= 1024) {
    net_server().sendContent(o->buf);
    o->buf = "";
  }
}

static void handleGiorni() {
  if (!authed()) return;
  String out = "[";
  sd_list_days(dayCb, &out, 400);
  out += ']';
  net_server().send(200, "application/json", out);
}

static void handleFotoList() {
  if (!authed()) return;
  WebServer& server = net_server();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);   // -> risposta chunked
  server.send(200, "application/json", "");
  server.sendContent("[");

  ChunkedList out;
  sd_list_photos(server.arg("g").c_str(), photoCb, &out, 20000);
  if (out.buf.length()) server.sendContent(out.buf);

  server.sendContent("]");
  server.sendContent("");   // chunk vuoto: chiude il trasferimento
}

static void handleFoto() {
  if (!authed()) return;
  WebServer& server = net_server();
  File f = sd_open_photo(server.arg("g").c_str(), server.arg("f").c_str());
  if (!f) {
    server.send(404, "text/plain", "foto non trovata");
    return;
  }
  server.sendHeader("Cache-Control", "max-age=86400");   // i file non cambiano mai
  server.streamFile(f, "image/jpeg");
  f.close();
}

static void handleLog() {
  if (!authed()) return;
  WebServer& server = net_server();
  String day = server.arg("g");
  File f = sd_open_log(day.c_str());
  if (!f) {
    server.send(404, "text/plain", "nessun log per quel giorno");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=" + day + ".csv");
  server.streamFile(f, "text/csv");
  f.close();
}

static void handleElimina() {
  if (!authed()) return;
  WebServer& server = net_server();
  const bool ok = sd_delete_photo(server.arg("g").c_str(), server.arg("f").c_str());
  server.send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleEliminaGiorno() {
  if (!authed()) return;
  WebServer& server = net_server();
  const bool ok = sd_delete_day(server.arg("g").c_str());
  server.send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ---------------------------------------------------------------------
void web_ui_begin() {
  WebServer& server = net_server();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/snapshot.jpg", HTTP_GET, handleSnapshot);
  server.on("/api/scatta", HTTP_POST, handleCapture);
  server.on("/api/stato", HTTP_GET, handleStato);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/giorni", HTTP_GET, handleGiorni);
  server.on("/api/foto", HTTP_GET, handleFotoList);
  server.on("/foto", HTTP_GET, handleFoto);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/api/elimina", HTTP_POST, handleElimina);
  server.on("/api/elimina-giorno", HTTP_POST, handleEliminaGiorno);
}
