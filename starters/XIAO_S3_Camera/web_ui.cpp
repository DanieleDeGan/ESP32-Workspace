#include "web_ui.h"
#include "net_ota.h"
#include "camera.h"
#include "storage.h"
#include "hub_link.h"

#define STREAM_BOUNDARY "camframe"

// ---------------------------------------------------------------------
//  Pagina di controllo (PROGMEM: sta in flash, non in RAM)
// ---------------------------------------------------------------------
static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Nodo camera XIAO</title><style>
 :root{color-scheme:dark}
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:1rem;display:flex;justify-content:center}
 .wrap{max-width:760px;width:100%}
 h1{font-size:1.1rem;margin:0 0 .8rem}
 .card{background:#1c1c1c;border:1px solid #333;border-radius:12px;padding:1rem;margin-bottom:1rem}
 .card h2{font-size:.85rem;text-transform:uppercase;letter-spacing:.05em;color:#9aa;margin:0 0 .7rem}
 button{padding:.55rem .9rem;border:0;border-radius:8px;background:#3b82f6;color:#fff;font-size:.9rem;cursor:pointer;margin:0 .4rem .4rem 0}
 button.sec{background:#374151}
 button.dan{background:#b91c1c}
 img#live{width:100%;background:#000;border-radius:8px;display:block;min-height:120px}
 table{width:100%;border-collapse:collapse;font-size:.85rem}
 td,th{text-align:left;padding:.35rem .3rem;border-bottom:1px solid #2a2a2a}
 th{color:#9aa;font-weight:600}
 .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:.5rem .9rem;font-size:.85rem}
 .grid div span{color:#9aa}
 label{display:block;font-size:.8rem;color:#9aa;margin:.5rem 0 .2rem}
 select,input{background:#111;color:#eee;border:1px solid #444;border-radius:6px;padding:.4rem;width:100%}
 .row{display:flex;gap:.8rem;flex-wrap:wrap}
 .row>div{flex:1 1 130px}
 .ok{color:#4ade80}.no{color:#f87171}.muted{color:#8a8a8a;font-size:.78rem;line-height:1.4}
 a{color:#3b82f6}
</style></head><body><div class="wrap">
<h1>Nodo camera XIAO <span id="fw" class="muted"></span></h1>

<div class="card"><h2>Vista</h2>
 <img id="live" alt="">
 <div style="margin-top:.7rem">
  <button id="bstream">Avvia video</button>
  <button id="bshot" class="sec">Anteprima</button>
  <button id="bsave">Scatta e salva</button>
 </div>
 <p class="muted">Mentre il video e' in corso la scheda non risponde ad altre
 richieste (nemmeno all'OTA): fermalo prima di aggiornare il firmware. Si
 interrompe comunque da solo dopo 5 minuti.</p>
 <p class="muted" id="msg"></p>
</div>

<div class="card"><h2>Stato</h2>
 <div class="grid" id="stato"></div>
</div>

<div class="card"><h2>Impostazioni</h2>
 <div class="row">
  <div><label>Sorveglianza PIR</label>
   <select id="armato"><option value="1">Armata</option><option value="0">Disarmata</option></select></div>
  <div><label>Pausa tra scatti (s)</label><input id="cooldown" type="number" min="1" max="3600"></div>
 </div>
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
 <div style="margin-top:.8rem"><button id="bcfg">Applica</button>
  <a href="/update" style="margin-left:.6rem">Aggiorna firmware</a></div>
</div>

<div class="card"><h2>Foto su microSD</h2>
 <button id="brefresh" class="sec">Aggiorna elenco</button>
 <table><thead><tr><th>File</th><th>Dimensione</th><th></th></tr></thead><tbody id="foto"></tbody></table>
</div>

<script>
const $=id=>document.getElementById(id);
let streaming=false, timer=null;

function msg(t){$('msg').textContent=t;}

function stato(){
 if(streaming) return;                       // il server e' occupato a trasmettere
 fetch('/api/stato').then(r=>r.json()).then(s=>{
  $('fw').textContent='— '+s.nodo+' · fw '+s.fw;
  const si=(c,t)=>'<span class="'+(c?'ok':'no')+'">'+t+'</span>';
  $('stato').innerHTML=
   '<div><span>Rete</span><br>'+(s.wifi?si(1,s.ip+' ('+s.rssi+' dBm)'):si(0,'non connesso'))+'</div>'+
   '<div><span>Canale ESP-NOW</span><br>'+s.canale+'</div>'+
   '<div><span>Hub</span><br>'+(s.hub?si(1,'associato '+s.hub_mac):si(0,'in attesa'))+'</div>'+
   '<div><span>microSD</span><br>'+(s.sd?si(1,s.sd_usati+'/'+s.sd_totali+' MB · '+s.foto+' foto'):si(0,s.sd_errore))+'</div>'+
   '<div><span>PIR</span><br>'+(s.armato?si(s.caldo,s.caldo?'armato':'riscaldamento...'):si(0,'disarmato'))+'</div>'+
   '<div><span>Rilevamenti</span><br>'+s.rilevamenti+(s.ultimo>=0?' (ultimo '+s.ultimo+' s fa)':'')+'</div>'+
   '<div><span>Camera</span><br>'+s.sensore+' · '+s.size_nome+'</div>'+
   '<div><span>Acceso da</span><br>'+s.uptime+' s · heap '+Math.round(s.heap/1024)+' KB</div>';
  if($('size').options.length!=s.sizes.length){
   $('size').innerHTML=s.sizes.map((n,i)=>'<option value="'+i+'">'+n+'</option>').join('');
  }
  $('armato').value=s.armato?'1':'0'; $('cooldown').value=s.cooldown;
  $('size').value=s.size; $('qualita').value=s.qualita;
  $('vflip').value=s.vflip?'1':'0'; $('hmirror').value=s.hmirror?'1':'0';
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
  msg(r.ok?('Salvata: '+r.file):('Errore: '+r.errore));
  if(r.ok){$('live').src='/foto?f='+r.file+'&'+Date.now(); foto(); stato();}
 }).catch(()=>msg('Errore di rete'));
};
$('bcfg').onclick=()=>{
 const q=new URLSearchParams({armato:$('armato').value,cooldown:$('cooldown').value,
  size:$('size').value,qualita:$('qualita').value,vflip:$('vflip').value,hmirror:$('hmirror').value});
 fetch('/api/config?'+q,{method:'POST'}).then(r=>r.json()).then(r=>{msg(r.ok?'Impostazioni applicate.':'Errore');stato();});
};
function foto(){
 fetch('/api/foto').then(r=>r.json()).then(l=>{
  $('foto').innerHTML=l.sort((a,b)=>b.n.localeCompare(a.n)).map(f=>
   '<tr><td><a href="/foto?f='+f.n+'" target="_blank">'+f.n+'</a></td><td>'+Math.round(f.b/1024)+' KB</td>'+
   '<td><button class="dan" onclick="elimina(\''+f.n+'\')">Elimina</button></td></tr>').join('')
   ||'<tr><td colspan="3" class="muted">Nessuna foto.</td></tr>';
 }).catch(()=>{});
}
function elimina(n){ if(!confirm('Eliminare '+n+'?')) return;
 fetch('/api/elimina?f='+encodeURIComponent(n),{method:'POST'}).then(()=>{foto();stato();}); }
$('brefresh').onclick=foto;

stato(); foto(); timer=setInterval(stato,3000);
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

    // Il web server e' fermo qui dentro: senza questa chiamata il nodo
    // smetterebbe di sorvegliare per tutta la durata dello stream.
    app_pump();
  }
  client.print("--" STREAM_BOUNDARY "--\r\n");
  client.stop();
}

static void handleCapture() {
  if (!authed()) return;
  WebServer& server = net_server();
  char name[40];
  char json[96];
  if (app_capture_and_store("WEB", name, sizeof(name))) {
    snprintf(json, sizeof(json), "{\"ok\":true,\"file\":\"%s\"}", name);
    server.send(200, "application/json", json);
  } else {
    snprintf(json, sizeof(json), "{\"ok\":false,\"errore\":\"%s\"}",
             camera_ready() ? sd_last_error() : "camera non disponibile");
    server.send(500, "application/json", json);
  }
}

static void handleStato() {
  if (!authed()) return;
  WebServer& server = net_server();

  String sizes = "[";
  for (int i = 0; i < camera_size_count(); i++) {
    if (i) sizes += ',';
    sizes += '"';
    sizes += camera_size_name(i);
    sizes += '"';
  }
  sizes += ']';

  char buf[1024];
  snprintf(buf, sizeof(buf),
           "{\"nodo\":\"%s\",\"fw\":\"%s\","
           "\"wifi\":%s,\"ip\":\"%s\",\"rssi\":%d,\"canale\":%u,"
           "\"hub\":%s,\"hub_mac\":\"%s\","
           "\"sd\":%s,\"sd_errore\":\"%s\",\"sd_totali\":%llu,\"sd_usati\":%llu,\"foto\":%lu,"
           "\"armato\":%s,\"caldo\":%s,\"cooldown\":%lu,\"rilevamenti\":%lu,\"ultimo\":%ld,"
           "\"sensore\":\"%s\",\"size\":%d,\"size_nome\":\"%s\",\"sizes\":%s,"
           "\"qualita\":%d,\"vflip\":%s,\"hmirror\":%s,"
           "\"invii_interrotti\":%lu,"
           "\"uptime\":%lu,\"heap\":%lu}",
           app_node_name(), app_fw_version(),
           net_isConnected() ? "true" : "false", net_ip().c_str(), net_rssi(), (unsigned)hub_channel(),
           hub_paired() ? "true" : "false", hub_mac_str(),
           sd_mounted() ? "true" : "false", sd_last_error(),
           sd_total_mb(), sd_used_mb(), (unsigned long)sd_photo_count(),
           app_armed() ? "true" : "false", app_pir_warm() ? "true" : "false",
           (unsigned long)app_cooldown_s(), (unsigned long)app_motion_count(), app_seconds_since_motion(),
           camera_sensor_name(), camera_size_index(), camera_size_name(camera_size_index()), sizes.c_str(),
           camera_quality(), camera_vflip() ? "true" : "false", camera_hmirror() ? "true" : "false",
           (unsigned long)web_invii_interrotti(),
           (unsigned long)(millis() / 1000), (unsigned long)ESP.getFreeHeap());
  server.send(200, "application/json", buf);
}

static void handleConfig() {
  if (!authed()) return;
  WebServer& server = net_server();

  if (server.hasArg("armato"))   app_set_armed(server.arg("armato").toInt() != 0);
  if (server.hasArg("cooldown")) app_set_cooldown_s((uint32_t)server.arg("cooldown").toInt());
  if (server.hasArg("size"))     camera_set_size_index(server.arg("size").toInt());
  if (server.hasArg("qualita"))  camera_set_quality(server.arg("qualita").toInt());
  if (server.hasArg("vflip") || server.hasArg("hmirror")) {
    camera_set_flip(server.arg("vflip").toInt() != 0, server.arg("hmirror").toInt() != 0);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// Elenco foto: la callback accumula direttamente il JSON.
static void listCb(const char* name, size_t size, void* arg) {
  String* out = (String*)arg;
  if (out->length() > 1) *out += ',';
  *out += "{\"n\":\"";
  *out += name;
  *out += "\",\"b\":";
  *out += (uint32_t)size;
  *out += '}';
}

static void handleFotoList() {
  if (!authed()) return;
  String out = "[";
  sd_list_photos(listCb, &out, 200);
  out += ']';
  net_server().send(200, "application/json", out);
}

// ---------------------------------------------------------------------
//  Invio limitato: un client morto non deve poter fermare la scheda
//
//  WebServer::streamFile() finisce in NetworkClient::write(Stream&), che
//  IGNORA il valore di ritorno della write() e prosegue fino a fine file
//  anche quando il client non accetta piu' un byte. E ogni write() aspetta
//  che il socket torni scrivibile con dieci select() da un secondo l'uno.
//  Un client che smette di dare ACK senza chiudere il socket - telefono che
//  si addormenta, WiFi che cade, coperchio del portatile - tiene quindi
//  loop() dentro l'handler finche' non e' lo stack TCP a rinunciare al peer.
//  Sono minuti, e non e' un caso limite: e' il modo normale in cui muore una
//  pagina lasciata aperta.
//
//  QUI PESA PIU' CHE ALTROVE. Sulla scheda ambientale il guasto e' stato
//  misurato in 456 s di blocco; qui la scheda ferma dentro un handler non
//  guarda il PIR e non preleva i comandi dell'hub dalla coda ESP-NOW. Un
//  cliente andato via a meta' scaricamento puo' quindi far perdere uno
//  scatto di allarme - cioe' proprio l'evento per cui il nodo esiste.
//  E le foto sono il caso peggiore per costruzione: un CSV di un giorno sta
//  in poche decine di kB, una foto da 300 kB sono ~220 chunk, e la galleria
//  ne carica decine per volta.
//
//  Rimedio: ci si ferma al primo chunk che il client non accetta per intero
//  - il controllo che manca al core - e comunque a fine budget. Il costo
//  residuo e' UNA write bloccata (~10 s): quel numero sta dentro il core e
//  da qui non si abbassa.
// ---------------------------------------------------------------------
static constexpr uint32_t INVIO_BUDGET_MS = 20000;   // su una LAN una foto vola: 20 s e' larghissimo

static uint32_t s_invii_interrotti = 0;   // quante volte e' scattato il taglio, da questo avvio

uint32_t web_invii_interrotti() { return s_invii_interrotti; }

static void invioInterrotto(const char* perche) {
  s_invii_interrotti++;
  Serial.printf("[web] invio interrotto: %s\n", perche);
}

// Come streamFile(), ma si arrende invece di trascinarsi dietro la scheda.
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

static void handleFoto() {
  if (!authed()) return;
  WebServer& server = net_server();
  String name = server.arg("f");
  File f = sd_open_photo(name.c_str());
  if (!f) {
    server.send(404, "text/plain", "foto non trovata");
    return;
  }
  streamFileLimitato(server, f, "image/jpeg");
  f.close();
}

static void handleElimina() {
  if (!authed()) return;
  WebServer& server = net_server();
  bool ok = sd_delete_photo(server.arg("f").c_str());
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
  server.on("/api/foto", HTTP_GET, handleFotoList);
  server.on("/foto", HTTP_GET, handleFoto);
  server.on("/api/elimina", HTTP_POST, handleElimina);
}
