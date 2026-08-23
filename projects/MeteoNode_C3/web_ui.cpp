#include "web_ui.h"
#include "net_ota.h"
#include "forecast.h"
#include "rtc_time.h"
#include <WebServer.h>

// =====================================================================
//  Pagina di stato — HTML/CSS/JS inline, nessuna risorsa esterna.
//  Il nodo puo' finire su una rete senza uscita su Internet (o dentro una
//  scatola in giardino): un <script src> verso un CDN darebbe una pagina
//  bianca proprio nel momento in cui serve capire perche' non risponde.
//  Anche i grafici sono fatti a mano in SVG per lo stesso motivo: una
//  libreria di charting sarebbe stata un megabyte e una dipendenza di rete
//  per disegnare tre polilinee.
// =====================================================================
static const char PAGE_INDEX[] PROGMEM = R"HTML(<!doctype html>
<html lang="it"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeteoNode-C3</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1b1d21;--dim:#6b7280;--line:#e2e4e9;
      --ok:#1a7f47;--ko:#b42318;--warn:#a2600a;--accent:#2563eb;
      --t:#d1443c;--h:#2775c3;--p:#6b46c1;--grid:#e8eaee}
@media(prefers-color-scheme:dark){
:root{--bg:#15171b;--card:#1e2126;--fg:#e8eaed;--dim:#9aa0a8;--line:#2c3037;
      --ok:#4ade80;--ko:#f87171;--warn:#fbbf24;--accent:#60a5fa;
      --t:#f87171;--h:#60a5fa;--p:#a78bfa;--grid:#2a2e35}}
*{box-sizing:border-box}
body{margin:0;padding:16px;background:var(--bg);color:var(--fg);
     font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
.wrap{max-width:760px;margin:0 auto}
h1{font-size:19px;margin:0 0 2px}
.sub{color:var(--dim);font-size:13px;margin-bottom:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;
      padding:14px 16px;margin-bottom:12px}
.big{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:12px}
.big>div{flex:1 1 150px;background:var(--card);border:1px solid var(--line);
         border-radius:10px;padding:12px 14px}
.big .v{font-size:30px;font-weight:600;letter-spacing:-.5px}
.big .u{font-size:15px;font-weight:400;color:var(--dim)}
.big .k{font-size:12px;color:var(--dim);text-transform:uppercase;
        letter-spacing:.6px;margin-bottom:2px}
.big .mm{font-size:12px;color:var(--dim);margin-top:4px}
table{width:100%;border-collapse:collapse;font-size:14px}
td{padding:5px 0;border-bottom:1px solid var(--line)}
tr:last-child td{border-bottom:0}
td:first-child{color:var(--dim);width:52%}
td:last-child{text-align:right;font-variant-numeric:tabular-nums}
.ok{color:var(--ok)}.ko{color:var(--ko)}.warn{color:var(--warn)}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.6px;
   color:var(--dim);margin:0 0 8px}
button{font:inherit;padding:8px 14px;margin:0 6px 6px 0;cursor:pointer;
       border:1px solid var(--line);border-radius:8px;
       background:var(--card);color:var(--fg)}
button:hover{border-color:var(--accent);color:var(--accent)}
input{font:inherit;padding:7px 9px;border:1px solid var(--line);border-radius:8px;
      background:var(--bg);color:var(--fg);width:110px;
      font-variant-numeric:tabular-nums}
a{color:var(--accent)}
#msg,#cfgmsg{font-size:13px;color:var(--dim);min-height:18px;margin-top:6px}
#stale{display:none;color:var(--ko);font-size:13px;margin-bottom:10px}
.prev{font-size:20px;font-weight:600;line-height:1.3;margin-bottom:4px}
.prevsub{font-size:13px;color:var(--dim)}
.ch{margin-bottom:14px}
.ch:last-child{margin-bottom:0}
.chh{display:flex;justify-content:space-between;font-size:12px;
     color:var(--dim);margin-bottom:2px}
.chh b{font-weight:600}
svg{width:100%;height:110px;display:block;overflow:visible}
.riga{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:10px}
.riga label{font-size:14px;color:var(--dim);flex:1 1 210px}
.nota{font-size:12px;color:var(--dim);margin-top:-4px;margin-bottom:12px}
</style></head><body><div class="wrap">

<h1>MeteoNode-C3</h1>
<div class="sub">AHT20 + BMP280 &middot; <span id="fw">&mdash;</span> &middot;
  letto <span id="eta">&mdash;</span></div>
<div id="stale">La pagina non riesce piu' a leggere il nodo.</div>

<div class="big">
  <div><div class="k">Temperatura</div>
       <div><span class="v" id="t">--</span><span class="u"> &deg;C</span></div>
       <div class="mm" id="tmm">&nbsp;</div></div>
  <div><div class="k">Umidita'</div>
       <div><span class="v" id="h">--</span><span class="u"> %</span></div>
       <div class="mm" id="hmm">&nbsp;</div></div>
  <div><div class="k">Pressione</div>
       <div><span class="v" id="p">--</span><span class="u"> hPa</span></div>
       <div class="mm" id="pmm">&nbsp;</div></div>
</div>

<div class="card"><h2>Previsione</h2>
  <div class="prev" id="prev">&mdash;</div>
  <div class="prevsub" id="prevsub">&nbsp;</div>
</div>

<div class="card"><h2>Ultime 24 ore</h2>
  <div class="ch">
    <div class="chh"><b style="color:var(--t)">Temperatura &deg;C</b><span id="ct">&mdash;</span></div>
    <svg id="gt" viewBox="0 0 600 110" preserveAspectRatio="none"></svg>
  </div>
  <div class="ch">
    <div class="chh"><b style="color:var(--h)">Umidita' %</b><span id="chh">&mdash;</span></div>
    <svg id="gh" viewBox="0 0 600 110" preserveAspectRatio="none"></svg>
  </div>
  <div class="ch">
    <div class="chh"><b style="color:var(--p)">Pressione hPa (livello del mare)</b><span id="cp">&mdash;</span></div>
    <svg id="gp" viewBox="0 0 600 110" preserveAspectRatio="none"></svg>
  </div>
  <div class="chh" style="margin-top:6px"><span id="tda">&mdash;</span><span id="ta">&mdash;</span></div>
  <div class="nota" style="margin-top:8px">Lo storico sta solo in RAM: si azzera
  a ogni riavvio o stacco della batteria. Il nodo non ha microSD.</div>
</div>

<div class="card"><h2>Misure</h2><table>
<tr><td>Punto di rugiada</td><td id="dp">&mdash;</td></tr>
<tr><td>Pressione misurata (non corretta)</td><td id="pst">&mdash;</td></tr>
<tr><td>Temperatura dal BMP280</td><td id="tb">&mdash;</td></tr>
<tr><td>Scarto fra i due chip</td><td id="dt">&mdash;</td></tr>
<tr><td>Batteria</td><td id="bat">&mdash;</td></tr>
</table></div>

<div class="card"><h2>Impostazioni</h2>
  <div class="riga">
    <label for="iv">Intervallo di misurazione (2&ndash;3600 s)</label>
    <input type="number" id="iv" min="2" max="3600" step="1">
    <button onclick="salvaIntervallo()">Salva</button>
  </div>
  <div class="nota">Piu' lungo = meno consumo. Lo storico resta a passo fisso di
  2 minuti: se l'intervallo lo supera, i grafici mostrano dei buchi, che e'
  corretto.</div>

  <div class="riga">
    <label for="al">Altitudine del nodo (m)</label>
    <input type="number" id="al" min="-400" max="4000" step="1">
    <button onclick="salvaAltitudine()">Salva</button>
  </div>
  <div class="riga">
    <label for="cal">…oppure calibrala: pressione sul bollettino (hPa)</label>
    <input type="number" id="cal" min="900" max="1100" step="0.1">
    <button onclick="calibra()">Calcola</button>
  </div>
  <div class="nota">Cerca la pressione <i>al livello del mare</i> in un bollettino
  di Trieste e incollala qui: il nodo ricava l'altitudine dalla pressione che sta
  misurando adesso. Serve solo a rendere il numero confrontabile con i bollettini
  &mdash; la previsione si basa sul trend, che non dipende dall'altitudine.</div>
  <div id="cfgmsg">&nbsp;</div>
</div>

<div class="card"><h2>Sensore</h2><table>
<tr><td>Alimentazione (D3/GPIO5)</td><td id="pw">&mdash;</td></tr>
<tr><td>AHT20 &mdash; 0x38</td><td id="a">&mdash;</td></tr>
<tr><td>BMP280</td><td id="b">&mdash;</td></tr>
<tr><td>Letture riuscite / fallite</td><td id="rd">&mdash;</td></tr>
<tr><td>Power-cycle dal boot</td><td id="pc">&mdash;</td></tr>
</table></div>

<div class="card"><h2>Nodo</h2><table>
<tr><td>Orario</td><td id="ora">&mdash;</td></tr>
<tr><td>Indirizzo IP</td><td id="ip">&mdash;</td></tr>
<tr><td>Segnale WiFi</td><td id="rs">&mdash;</td></tr>
<tr><td>Canale (anche ESP-NOW)</td><td id="ch">&mdash;</td></tr>
<tr><td>ESP-NOW</td><td id="en">&mdash;</td></tr>
<tr><td>DATA inviati / falliti</td><td id="ent">&mdash;</td></tr>
<tr><td>Acceso da</td><td id="up">&mdash;</td></tr>
<tr><td>RAM libera</td><td id="hp">&mdash;</td></tr>
</table></div>

<div class="card"><h2>Comandi</h2>
<button onclick="cmd('scan')">Scansione I2C</button>
<button onclick="cmd('riavvia')">Power-cycle sensore</button>
<button onclick="cmd('alimentazione')">Accendi / spegni</button>
<div id="msg">L'esito della scansione esce sul monitor seriale; qui si vede
l'effetto sullo stato qui sopra.</div>
<div style="margin-top:10px;font-size:13px">
Aggiornamento firmware: <a href="/update">/update</a></div>
</div>

<script>
var n = function(x,d){ return (x===null||x===undefined) ? "—" : x.toFixed(d===undefined?1:d); };
var busy = false, cfgTocca = false;

function durata(s){
  var g=Math.floor(s/86400), o=Math.floor(s%86400/3600),
      m=Math.floor(s%3600/60), q=Math.floor(s%60);
  if(g) return g+" g "+o+" h";
  if(o) return o+" h "+m+" min";
  if(m) return m+" min "+q+" s";
  return q+" s";
}
function si(id,txt,cls){
  var e=document.getElementById(id);
  e.innerHTML=txt; e.className=cls||"";
}
function oreMin(ts){
  var d=new Date(ts*1000);
  return ("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2);
}

// ---- grafici ---------------------------------------------------------
// Una polilinea per serie, ridisegnata da zero a ogni aggiornamento. I buchi
// (valori null) spezzano la linea invece di essere interpolati: un salto
// dritto sopra un'ora senza dati sarebbe un'invenzione.
function disegna(svgId, etId, dati, colore, dec){
  var svg=document.getElementById(svgId), et=document.getElementById(etId);
  while(svg.firstChild) svg.removeChild(svg.firstChild);
  var v=dati.filter(function(x){return x!==null;});
  if(v.length<2){ et.textContent="in attesa di dati"; return; }

  var mn=Math.min.apply(null,v), mx=Math.max.apply(null,v);
  if(mx-mn<1e-6){ mn-=0.5; mx+=0.5; }
  var pad=(mx-mn)*0.12; mn-=pad; mx+=pad;
  var W=600, H=110;
  var x=function(i){ return dati.length<2 ? 0 : i*W/(dati.length-1); };
  var y=function(val){ return H - (val-mn)/(mx-mn)*H; };
  var NS="http://www.w3.org/2000/svg";

  // tre linee di griglia orizzontali, per dare la scala senza assi veri
  [0.25,0.5,0.75].forEach(function(f){
    var ln=document.createElementNS(NS,"line");
    ln.setAttribute("x1",0); ln.setAttribute("x2",W);
    ln.setAttribute("y1",H*f); ln.setAttribute("y2",H*f);
    ln.setAttribute("stroke","var(--grid)"); ln.setAttribute("stroke-width","1");
    svg.appendChild(ln);
  });

  var d="", su=false;
  for(var i=0;i<dati.length;i++){
    if(dati[i]===null){ su=false; continue; }
    d += (su?" L":" M") + x(i).toFixed(1) + " " + y(dati[i]).toFixed(1);
    su=true;
  }
  var path=document.createElementNS(NS,"path");
  path.setAttribute("d",d.trim());
  path.setAttribute("fill","none");
  path.setAttribute("stroke",colore);
  path.setAttribute("stroke-width","2");
  path.setAttribute("stroke-linejoin","round");
  path.setAttribute("vector-effect","non-scaling-stroke");
  svg.appendChild(path);

  et.textContent = "min "+v[v.indexOf(Math.min.apply(null,v))].toFixed(dec)+
                   "  max "+Math.max.apply(null,v).toFixed(dec)+
                   "  ora "+v[v.length-1].toFixed(dec);
}

function grafici(){
  fetch('/api/storico',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    var T=d.t.map(function(x){return x===null?null:x/10;});
    var H=d.h.map(function(x){return x===null?null:x/10;});
    var P=d.p.map(function(x){return x===null?null:(x+10000)/10;});
    disegna('gt','ct',T,'var(--t)',1);
    disegna('gh','chh',H,'var(--h)',1);
    disegna('gp','cp',P,'var(--p)',1);
    if(d.n>0){
      var fine=d.ultimo, inizio=d.ultimo-(d.n-1)*d.passo;
      document.getElementById('tda').textContent=oreMin(inizio);
      document.getElementById('ta').textContent=oreMin(fine);
    }
  }).catch(function(){});
}

// ---- stato -----------------------------------------------------------
function aggiorna(){
  if(busy) return;
  fetch('/api/stato',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    document.getElementById('stale').style.display='none';
    document.getElementById('fw').textContent=d.fw;
    document.getElementById('eta').textContent=d.eta_lettura+' s fa';

    document.getElementById('t').textContent = n(d.temp_aht);
    document.getElementById('h').textContent = n(d.hum);
    document.getElementById('p').textContent = n(d.press_sea,1);
    document.getElementById('tmm').innerHTML = d.temp_min===null ? '&nbsp;' :
      'min '+n(d.temp_min)+' &middot; max '+n(d.temp_max);
    document.getElementById('hmm').innerHTML = d.hum_min===null ? '&nbsp;' :
      'min '+n(d.hum_min)+' &middot; max '+n(d.hum_max);
    document.getElementById('pmm').innerHTML = d.press_min===null ? '&nbsp;' :
      'min '+n(d.press_min)+' &middot; max '+n(d.press_max);

    si('prev', d.previsione);
    document.getElementById('prevsub').innerHTML =
      d.delta_3h===null ? 'pressione '+d.trend
      : 'pressione '+d.trend+' &middot; '+(d.delta_3h>=0?'+':'')+
        n(d.delta_3h,1)+' hPa in 3 ore';

    si('dp',  n(d.dewpoint)+' &deg;C');
    si('pst', n(d.press,1)+' hPa');
    si('tb',  n(d.temp_bmp)+' &deg;C');
    if(d.temp_aht!==null && d.temp_bmp!==null){
      var dtv=Math.abs(d.temp_aht-d.temp_bmp);
      si('dt', n(dtv,2)+' &deg;C', dtv>2?'warn':'');
    } else si('dt','&mdash;');
    si('bat', d.battery===null ? 'partitore non cablato' : n(d.battery,2)+' V',
       d.battery===null ? '' : (d.battery<3.4?'ko':'ok'));

    si('pw', d.powered?'accesa':'SPENTA', d.powered?'ok':'warn');
    si('a',  d.aht_ok?'ok':'non risponde', d.aht_ok?'ok':'ko');
    var b = d.bmp_ok ? ('ok a 0x'+d.bmp_addr.toString(16).toUpperCase()+
                        ' &middot; chip 0x'+d.bmp_chip_id.toString(16).toUpperCase())
                     : 'non risponde';
    si('b', b, d.bmp_ok?'ok':'ko');
    si('rd', d.reads+' / '+d.errors, d.errors?'warn':'');
    si('pc', d.power_cycles);

    si('ora', d.ora+' <span style="color:var(--dim)">('+d.ora_fonte+')</span>');
    si('ip', d.ip);
    si('rs', d.rssi+' dBm', d.rssi<-80?'warn':'');
    si('ch', d.canale);
    // Il canale ESP-NOW deve combaciare con quello dell'AP: se diverge, il
    // pairing non parte e da fuori sembra un problema di portata.
    if(!d.espnow_ok)          si('en','non attivo','ko');
    else if(!d.espnow_paired) si('en',"in cerca dell'hub (canale "+d.espnow_canale+")",'warn');
    else                      si('en','associato a '+d.espnow_hub+' (canale '+d.espnow_canale+')','ok');
    si('ent', d.espnow_inviati+' / '+d.espnow_falliti, d.espnow_falliti?'warn':'');
    si('up', durata(d.uptime));
    si('hp', Math.round(d.heap/1024)+' kB');

    // I campi non si riscrivono se l'utente li sta modificando, o gli si
    // cancellerebbe quello che sta digitando sotto le dita.
    if(!cfgTocca){
      document.getElementById('iv').value = d.intervallo_s;
      document.getElementById('al').value = Math.round(d.altitudine_m);
    }
  }).catch(function(){
    document.getElementById('stale').style.display='block';
  });
}

['iv','al','cal'].forEach(function(id){
  document.getElementById(id).addEventListener('focus',function(){cfgTocca=true;});
  document.getElementById(id).addEventListener('blur', function(){cfgTocca=false;});
});

function cfg(q,ok){
  document.getElementById('cfgmsg').textContent='invio...';
  fetch('/api/config?'+q).then(function(r){return r.text();})
    .then(function(t){ document.getElementById('cfgmsg').textContent=t;
                       cfgTocca=false; setTimeout(aggiorna,300); })
    .catch(function(){ document.getElementById('cfgmsg').textContent='non riuscito'; });
}
function salvaIntervallo(){ cfg('intervallo='+document.getElementById('iv').value); }
function salvaAltitudine(){ cfg('altitudine='+document.getElementById('al').value); }
function calibra(){
  var v=document.getElementById('cal').value;
  if(!v){ document.getElementById('cfgmsg').textContent='inserisci la pressione del bollettino'; return; }
  cfg('calibra='+v);
}

function cmd(c){
  busy=true;
  document.getElementById('msg').textContent='comando "'+c+'" in corso...';
  fetch('/api/comando?c='+c).then(function(r){return r.text();})
    .then(function(t){ document.getElementById('msg').textContent=t; })
    .catch(function(){ document.getElementById('msg').textContent='comando non riuscito'; })
    // Il comando e' solo accodato: loop() lo esegue subito dopo aver
    // risposto, quindi si rilegge lo stato con un attimo di ritardo,
    // altrimenti si rivedrebbe ancora quello di prima.
    .then(function(){ busy=false; setTimeout(aggiorna, 900); });
}

aggiorna(); grafici();
setInterval(aggiorna, 5000);
setInterval(grafici, 60000);
</script>
</div></body></html>)HTML";

// ---------------------------------------------------------------------
//  Helper JSON
// ---------------------------------------------------------------------
// JSON non ha un modo di scrivere NaN: un valore mancante diventa null, e
// la pagina lo mostra come "—" invece di stampare "nan" o uno zero finto.
static String numOrNull(float v, unsigned int decimali = 2) {
  if (isnan(v)) return F("null");
  return String(v, decimali);
}

// ---------------------------------------------------------------------
//  Rotte
// ---------------------------------------------------------------------
static bool authOk() {
  if (net_webAuthOk()) return true;
  net_server().requestAuthentication();
  return false;
}

static void handleIndex() {
  if (!authOk()) return;
  net_server().send_P(200, "text/html; charset=utf-8", PAGE_INDEX);
}

static void handleStato() {
  if (!authOk()) return;

  app_snapshot_t s;
  app_get_snapshot(s);

  const forecast_trend_t tr = (forecast_trend_t)s.trend;

  char ora[24] = "--";
  rtctime_format(rtctime_now(), "%Y-%m-%d %H:%M:%S", ora, sizeof(ora));

  String j;
  j.reserve(1050);
  j += F("{\"fw\":\"");          j += app_fw_version();      j += F("\"");
  j += F(",\"powered\":");       j += s.powered ? F("true") : F("false");
  j += F(",\"aht_ok\":");        j += s.aht_ok ? F("true") : F("false");
  j += F(",\"bmp_ok\":");        j += s.bmp_ok ? F("true") : F("false");
  j += F(",\"bmp_addr\":");      j += s.bmp_addr;
  j += F(",\"bmp_chip_id\":");   j += s.bmp_chip_id;
  j += F(",\"temp_aht\":");      j += numOrNull(s.temp_aht);
  j += F(",\"hum\":");           j += numOrNull(s.hum);
  j += F(",\"temp_bmp\":");      j += numOrNull(s.temp_bmp);
  j += F(",\"press\":");         j += numOrNull(s.press_hpa);
  j += F(",\"press_sea\":");     j += numOrNull(s.press_sea);
  j += F(",\"dewpoint\":");      j += numOrNull(s.dewpoint);
  j += F(",\"delta_3h\":");      j += numOrNull(s.delta_3h);
  j += F(",\"trend\":\"");       j += forecast_trend_label(tr);          j += F("\"");
  j += F(",\"previsione\":\"");  j += forecast_text(tr, s.press_sea);    j += F("\"");
  j += F(",\"temp_min\":");      j += numOrNull(s.temp_min);
  j += F(",\"temp_max\":");      j += numOrNull(s.temp_max);
  j += F(",\"hum_min\":");       j += numOrNull(s.hum_min);
  j += F(",\"hum_max\":");       j += numOrNull(s.hum_max);
  j += F(",\"press_min\":");     j += numOrNull(s.press_min);
  j += F(",\"press_max\":");     j += numOrNull(s.press_max);
  j += F(",\"battery\":");       j += numOrNull(s.battery_v);
  j += F(",\"reads\":");         j += s.reads;
  j += F(",\"errors\":");        j += s.read_errors;
  j += F(",\"power_cycles\":");  j += s.power_cycles;
  j += F(",\"intervallo_s\":");  j += s.intervallo_s;
  j += F(",\"altitudine_m\":");  j += String(s.altitudine_m, 1);
  j += F(",\"eta_lettura\":");   j += app_eta_ultima_lettura_s();
  j += F(",\"ora\":\"");         j += ora;                   j += F("\"");
  j += F(",\"ora_fonte\":\"");   j += rtctime_source();      j += F("\"");
  j += F(",\"ip\":\"");          j += net_ip();              j += F("\"");
  j += F(",\"rssi\":");          j += net_rssi();
  j += F(",\"canale\":");        j += net_channel();
  j += F(",\"espnow_ok\":");     j += s.espnow_ok ? F("true") : F("false");
  j += F(",\"espnow_paired\":"); j += s.espnow_paired ? F("true") : F("false");
  j += F(",\"espnow_canale\":"); j += s.espnow_channel;
  j += F(",\"espnow_inviati\":");j += s.espnow_sent;
  j += F(",\"espnow_falliti\":");j += s.espnow_failed;
  j += F(",\"espnow_hub\":\"");  j += s.espnow_hub_mac;    j += F("\"");
  j += F(",\"uptime\":");        j += millis() / 1000;
  j += F(",\"heap\":");          j += ESP.getFreeHeap();
  j += F("}");

  net_server().sendHeader(F("Cache-Control"), F("no-store"));
  net_server().send(200, F("application/json"), j);
}

// Lo storico va servito A BLOCCHI, non costruendo una String unica: 720
// campioni per tre serie sono una decina di kB, e allocarli tutti insieme su
// un heap da 180 kB mentre il WiFi lavora e' il modo migliore per trovarsi un
// giorno con una frammentazione che non si spiega. Stesso approccio dello
// streaming di /api/giorno in EnvNode_C3.
//
// Valori come interi scalati (x10, e la pressione anche traslata di 1000 hPa):
// dimezza il testo da mandare rispetto ai decimali, e la risoluzione che serve
// a un grafico e' comunque molto minore.
static void handleStorico() {
  if (!authOk()) return;

  WebServer &sv = net_server();
  const uint16_t n = app_hist_count();

  sv.sendHeader(F("Cache-Control"), F("no-store"));
  sv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  sv.send(200, F("application/json"), F(""));

  String head;
  head.reserve(96);
  head += F("{\"passo\":");   head += app_hist_period_s();
  head += F(",\"ultimo\":");  head += (uint32_t)app_hist_last_ts();
  head += F(",\"n\":");       head += n;
  sv.sendContent(head);

  // Tre passate sullo stesso buffer, una per serie: costa un po' di CPU ma
  // tiene il JSON in un formato che la pagina consuma senza rigirarlo.
  const char* nomi[3] = { ",\"t\":[", ",\"h\":[", ",\"p\":[" };
  for (uint8_t serie = 0; serie < 3; serie++) {
    String buf;
    buf.reserve(1200);
    buf += nomi[serie];

    for (uint16_t i = 0; i < n; i++) {
      // dal piu' VECCHIO al piu' recente, cosi' la pagina disegna da
      // sinistra a destra senza doverli rivoltare
      const uint16_t back = (uint16_t)(n - 1 - i);
      float t, h, p;
      const bool ok = app_hist_at(back, &t, &h, &p);
      const float v = !ok ? NAN : (serie == 0 ? t : (serie == 1 ? h : p));

      if (i) buf += ',';
      if (isnan(v)) {
        buf += F("null");
      } else if (serie == 2) {
        buf += (int)lroundf(v * 10.0f - 10000.0f);
      } else {
        buf += (int)lroundf(v * 10.0f);
      }

      if (buf.length() > 1000) { sv.sendContent(buf); buf = ""; }
    }
    buf += ']';
    sv.sendContent(buf);
  }

  sv.sendContent(F("}"));
  sv.sendContent(F(""));   // chiude il chunked
}

static void handleConfig() {
  if (!authOk()) return;
  WebServer &sv = net_server();

  if (sv.hasArg("intervallo")) {
    const uint32_t v = (uint32_t)sv.arg("intervallo").toInt();
    if (app_set_intervallo_s(v)) {
      sv.send(200, F("text/plain; charset=utf-8"), String(F("intervallo: ")) + v + F(" s"));
    } else {
      sv.send(400, F("text/plain; charset=utf-8"), F("intervallo fuori range (2-3600 s)"));
    }
    return;
  }
  if (sv.hasArg("altitudine")) {
    const float v = sv.arg("altitudine").toFloat();
    if (app_set_altitudine_m(v)) {
      sv.send(200, F("text/plain; charset=utf-8"), String(F("altitudine: ")) + String(v, 0) + F(" m"));
    } else {
      sv.send(400, F("text/plain; charset=utf-8"), F("altitudine fuori range (-400..4000 m)"));
    }
    return;
  }
  if (sv.hasArg("calibra")) {
    const float v = sv.arg("calibra").toFloat();
    if (app_calibra_altitudine(v)) {
      app_snapshot_t s;
      app_get_snapshot(s);
      sv.send(200, F("text/plain; charset=utf-8"),
              String(F("altitudine calcolata: ")) + String(s.altitudine_m, 0) + F(" m"));
    } else {
      sv.send(400, F("text/plain; charset=utf-8"),
              F("calibrazione non riuscita: serve una lettura valida del BMP280"));
    }
    return;
  }
  sv.send(400, F("text/plain; charset=utf-8"), F("nessun parametro riconosciuto"));
}

static void handleComando() {
  if (!authOk()) return;

  // Si risponde SUBITO e si lascia il lavoro a loop(): vedi la nota in
  // testa a web_ui.h sul perche' l'handler non deve bloccare.
  const String c = net_server().arg("c");
  if (c == "scan") {
    app_cmd_scan();
    net_server().send(200, F("text/plain; charset=utf-8"),
                      F("scansione I2C accodata: l'elenco degli indirizzi esce sul monitor seriale"));
  } else if (c == "riavvia") {
    app_cmd_restart_sensor();
    net_server().send(200, F("text/plain; charset=utf-8"),
                      F("power-cycle accodato: fra un attimo il sensore risulta reinizializzato"));
  } else if (c == "alimentazione") {
    app_cmd_toggle_power();
    net_server().send(200, F("text/plain; charset=utf-8"),
                      F("accensione/spegnimento accodato: guarda la riga Alimentazione qui sopra"));
  } else {
    net_server().send(400, F("text/plain; charset=utf-8"), F("comando sconosciuto"));
  }
}

void web_ui_begin() {
  net_server().on("/", HTTP_GET, handleIndex);
  net_server().on("/api/stato", HTTP_GET, handleStato);
  net_server().on("/api/storico", HTTP_GET, handleStorico);
  net_server().on("/api/config", HTTP_GET, handleConfig);
  // GET, non POST. Sembra scorretto per un comando, ed e' deliberato: il
  // WebServer del core, davanti a una POST senza corpo, resta ad aspettare
  // dati fino a HTTP_MAX_POST_WAIT (5 s) prima di passare la richiesta
  // all'handler, e in quei secondi il server e' fermo per tutti. Con la
  // pagina che fa polling ogni 5 s le richieste si accavallano e da fuori
  // sembra che il nodo sia morto. Misurato il 2026-08-22: la stessa
  // chiamata rispondeva in 8-10 s da POST e in 10 ms da GET. Gli argomenti
  // di query si leggono dall'URL, senza aspettare nessun corpo.
  net_server().on("/api/comando", HTTP_GET, handleComando);
}
