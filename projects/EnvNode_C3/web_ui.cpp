#include "web_ui.h"
#include "net_ota.h"
#include "settings.h"
#include "sd_logger.h"
#include "rtc_time.h"
#include "comfort.h"
#include "remote_nodes.h"
#include <Middlewares.h>   // CorsMiddleware, bundled nella libreria WebServer del core

// ---------------------------------------------------------------------
//  Dashboard (PROGMEM: sta in flash, non in RAM). Niente CDN/librerie
//  esterne: grafici canvas fatti a mano in vanilla JS, come da convenzione
//  del resto del repo (vedi net_ota.cpp/web_ui.cpp di XIAO_S3_Camera).
//
//  I dati grezzi (ts,T,H) arrivano da /api/giorno; comfort score/etichetta/
//  in-range si ricalcolano NEL BROWSER contro /api/config corrente (stessa
//  formula di comfort.h, duplicata qui in JS): cambiare le soglie da web
//  ricolora anche lo storico gia' scaricato, senza un nuovo giro di rete e
//  senza costo aggiuntivo sul device.
// ---------------------------------------------------------------------
static const char DASHBOARD_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EnvNode-C3</title><style>
 :root{color-scheme:dark}
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:1rem;display:flex;justify-content:center}
 .wrap{max-width:820px;width:100%}
 h1{font-size:1.1rem;margin:0 0 .8rem}
 .card{background:#1c1c1c;border:1px solid #333;border-radius:12px;padding:1rem;margin-bottom:1rem}
 .card h2{font-size:.8rem;text-transform:uppercase;letter-spacing:.05em;color:#9aa;margin:0 0 .7rem}
 .tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:.7rem}
 .tile{background:#191919;border-radius:10px;padding:.7rem .9rem}
 .tile .lbl{font-size:.72rem;color:#9aa;text-transform:uppercase;letter-spacing:.04em}
 .tile .val{font-size:1.5rem;margin-top:.2rem}
 .tile .sub{font-size:.72rem;color:#8a8a8a;margin-top:.15rem}
 canvas{width:100%;height:180px;display:block;background:#161615;border-radius:8px}
 select,input{background:#111;color:#eee;border:1px solid #444;border-radius:6px;padding:.4rem;width:100%}
 label{display:block;font-size:.78rem;color:#9aa;margin:.5rem 0 .2rem}
 .row{display:flex;gap:.8rem;flex-wrap:wrap}
 .row>div{flex:1 1 110px}
 button{padding:.55rem .9rem;border:0;border-radius:8px;background:#3987e5;color:#fff;font-size:.9rem;cursor:pointer;margin:.6rem .4rem 0 0}
 button.sec{background:#374151}
 button.dan{background:#b91c1c}
 .muted{color:#8a8a8a;font-size:.78rem;line-height:1.4}
 .legend{font-size:.75rem;color:#9aa;margin-top:.4rem}
 .dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:.3rem;vertical-align:middle}
 a{color:#3987e5}
 #tooltip{position:fixed;pointer-events:none;background:#000;border:1px solid #444;border-radius:6px;padding:.3rem .5rem;font-size:.72rem;color:#eee;display:none;white-space:nowrap;z-index:5}
</style></head><body><div class="wrap">
<h1 id="titolo">EnvNode-C3</h1>

<div class="card"><h2>Stato</h2>
 <div class="tiles" id="tiles"></div>
</div>

<div class="card">
 <h2>Giorno</h2>
 <select id="giorno"></select>
 <button id="baggiorna" class="sec">Aggiorna</button>
 <button id="bscarica" class="sec">Scarica CSV</button>
 <button id="belimina" class="dan">Elimina giorno</button>
 <p class="muted" id="eliminaMsg"></p>
</div>

<div class="card"><h2>Temperatura</h2><canvas id="cTemp" width="760" height="180"></canvas></div>
<div class="card"><h2>Umidita'</h2><canvas id="cHum" width="760" height="180"></canvas></div>
<div class="card"><h2>Comfort (scatter temp/umidita')</h2><canvas id="cScatter" width="760" height="220"></canvas>
 <div class="legend"><span class="dot" style="background:#0ca30c"></span>dentro range
  <span class="dot" style="background:#d03b3b;margin-left:.8rem"></span>fuori range</div>
</div>
<div class="card"><h2>Comfort score nel tempo</h2><canvas id="cScore" width="760" height="180"></canvas></div>

<div class="card"><h2>Impostazioni</h2>
 <div class="row">
  <div><label>Nome nodo</label><input id="cfgNodo" maxlength="23"></div>
  <div><label>Intervallo log (s)</label><input id="cfgLogInt" type="number" min="5" max="3600"></div>
  <div><label>Rotazione pagine OLED (s)</label><input id="cfgPageSec" type="number" min="2" max="30"></div>
 </div>
 <div class="row">
  <div><label>Temp min comfort (C)</label><input id="cfgTMin" type="number" step="0.5"></div>
  <div><label>Temp max comfort (C)</label><input id="cfgTMax" type="number" step="0.5"></div>
  <div><label>Umid. min comfort (%)</label><input id="cfgHMin" type="number" step="1"></div>
  <div><label>Umid. max comfort (%)</label><input id="cfgHMax" type="number" step="1"></div>
 </div>
 <div class="row"><div><label>Fuso orario (stringa POSIX TZ)</label><input id="cfgTz"></div></div>
 <button id="bsalva">Applica</button>
 <a href="/update" style="margin-left:.6rem">Aggiorna firmware</a>
 <a href="/dashboard-upload" style="margin-left:.6rem">Dashboard personalizzata</a>
 <p class="muted" id="cfgMsg"></p>
</div>

</div><div id="tooltip"></div>
<script>
const $=id=>document.getElementById(id);
const tip=$('tooltip');
let cfg=null;

function fmtTime(ts){
  const d=new Date(ts*1000);
  return d.toLocaleString('it-IT',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'});
}

// Stessa formula di comfort.h (vedi il commento in testa a questo file):
// duplicata qui per ricalcolare lo storico nel browser senza round-trip.
function comfortEval(t,h,c){
  const distT = t<c.t_min ? c.t_min-t : (t>c.t_max ? t-c.t_max : 0);
  const distH = h<c.h_min ? c.h_min-h : (h>c.h_max ? h-c.h_max : 0);
  const pT = Math.min(distT/6*50, 50);
  const pH = Math.min(distH/30*50, 50);
  const score = Math.max(0, Math.min(100, Math.round(100-pT-pH)));
  const inRange = t>=c.t_min && t<=c.t_max && h>=c.h_min && h<=c.h_max;
  let label;
  if (score>=80) label='Confortevole';
  else if (score>=60) label='Accettabile';
  else {
    const tOff=pT>0, hOff=pH>0;
    if (tOff && hOff) label='Scomodo';
    else if (tOff) label = t<c.t_min ? 'Troppo freddo' : 'Troppo caldo';
    else label = h<c.h_min ? 'Troppo secco' : 'Troppo umido';
  }
  return {score,label,inRange};
}

function tile(lbl,val,sub){
  return '<div class="tile"><div class="lbl">'+lbl+'</div><div class="val">'+val+'</div>'+
         (sub?('<div class="sub">'+sub+'</div>'):'')+'</div>';
}

function refreshStato(){
  fetch('/api/stato').then(r=>r.json()).then(s=>{
    $('titolo').textContent = s.nodo+' · fw '+s.fw;
    const t = s.temp!=null ? s.temp.toFixed(1)+'°C' : '--';
    const h = s.hum!=null ? s.hum.toFixed(0)+'%' : '--';
    const comfort = s.comfort_label!=null ? (s.comfort_label+' ('+s.comfort_score+')') : '--';
    const sd = s.sd ? (s.sd_liberi_mb+'/'+s.sd_totali_mb+' MB') : (s.sd_errore||'assente');
    $('tiles').innerHTML =
      tile('Temperatura', t, s.temp_max!=null?('min '+s.temp_min.toFixed(1)+' · max '+s.temp_max.toFixed(1)):'') +
      tile('Umidita\'', h) +
      tile('Comfort', comfort) +
      tile('SD', sd, s.record_oggi+' oggi · '+s.record_totali+' tot') +
      tile('Ora', s.ora, s.ora_fonte) +
      tile('Rete', s.wifi ? (s.ip+' ('+s.rssi+' dBm)') : 'non connesso');
  }).catch(()=>{});
}

function refreshConfig(){
  return fetch('/api/config').then(r=>r.json()).then(c=>{
    cfg=c;
    $('cfgNodo').value=c.nodo; $('cfgLogInt').value=c.log_int_s; $('cfgPageSec').value=c.page_sec;
    $('cfgTMin').value=c.t_min; $('cfgTMax').value=c.t_max;
    $('cfgHMin').value=c.h_min; $('cfgHMax').value=c.h_max;
    $('cfgTz').value=c.tz;
  });
}

function niceMinMax(vals){
  let mn=Math.min(...vals), mx=Math.max(...vals);
  if (mn===mx){ mn-=1; mx+=1; }
  const pad=(mx-mn)*0.12;
  return [mn-pad, mx+pad];
}

function drawLineChart(canvas, points, color, unit){
  const ctx=canvas.getContext('2d');
  const W=canvas.width, H=canvas.height;
  ctx.clearRect(0,0,W,H);
  const pad={l:38,r:12,t:10,b:10};
  if (!points.length){
    ctx.fillStyle='#8a8785'; ctx.font='12px system-ui';
    ctx.fillText('nessun dato per questo giorno', pad.l, H/2);
    canvas.onmousemove=null; canvas.onmouseleave=null;
    return;
  }
  const xs=points.map(p=>p.x), ys=points.map(p=>p.y);
  const xMin=Math.min(...xs), xMax=Math.max(...xs);
  const [y0,y1]=niceMinMax(ys);
  const xToPx=x=> pad.l + (W-pad.l-pad.r) * (xMax>xMin ? (x-xMin)/(xMax-xMin) : 0.5);
  const yToPx=y=> H-pad.b - (H-pad.t-pad.b) * ((y-y0)/(y1-y0));

  ctx.strokeStyle='#2c2c2a'; ctx.fillStyle='#898781'; ctx.font='10px system-ui'; ctx.lineWidth=1;
  for(let i=0;i<=3;i++){
    const gy=pad.t+(H-pad.t-pad.b)*i/3;
    ctx.beginPath(); ctx.moveTo(pad.l,gy); ctx.lineTo(W-pad.r,gy); ctx.stroke();
    ctx.fillText((y1-(y1-y0)*i/3).toFixed(1), 2, gy+3);
  }

  ctx.strokeStyle=color; ctx.lineWidth=2; ctx.beginPath();
  points.forEach((p,i)=>{ const px=xToPx(p.x), py=yToPx(p.y); if(i===0) ctx.moveTo(px,py); else ctx.lineTo(px,py); });
  ctx.stroke();

  canvas.onmousemove=(ev)=>{
    const rect=canvas.getBoundingClientRect();
    const mx=(ev.clientX-rect.left)*(canvas.width/rect.width);
    let best=points[0], bestD=Infinity;
    points.forEach(p=>{ const d=Math.abs(xToPx(p.x)-mx); if(d<bestD){bestD=d;best=p;} });
    tip.style.display='block';
    tip.style.left=(ev.clientX+12)+'px'; tip.style.top=(ev.clientY-10)+'px';
    tip.textContent = fmtTime(best.x)+'  '+best.y.toFixed(1)+unit;
  };
  canvas.onmouseleave=()=>{ tip.style.display='none'; };
}

function drawScatter(canvas, points, band){
  const ctx=canvas.getContext('2d');
  const W=canvas.width, H=canvas.height;
  ctx.clearRect(0,0,W,H);
  const pad={l:38,r:12,t:10,b:22};
  if (!points.length){
    ctx.fillStyle='#8a8785'; ctx.font='12px system-ui';
    ctx.fillText('nessun dato per questo giorno', pad.l, H/2);
    canvas.onmousemove=null; canvas.onmouseleave=null;
    return;
  }
  const xs=points.map(p=>p.t), ys=points.map(p=>p.h);
  const [x0,x1]=niceMinMax(xs.concat([band.t_min,band.t_max]));
  const [y0,y1]=niceMinMax(ys.concat([band.h_min,band.h_max]));
  const xToPx=x=> pad.l + (W-pad.l-pad.r) * ((x-x0)/(x1-x0));
  const yToPx=y=> H-pad.b - (H-pad.t-pad.b) * ((y-y0)/(y1-y0));

  ctx.strokeStyle='#2c2c2a'; ctx.fillStyle='#898781'; ctx.font='10px system-ui'; ctx.lineWidth=1;
  for(let i=0;i<=3;i++){
    const gy=pad.t+(H-pad.t-pad.b)*i/3;
    ctx.beginPath(); ctx.moveTo(pad.l,gy); ctx.lineTo(W-pad.r,gy); ctx.stroke();
    ctx.fillText((y1-(y1-y0)*i/3).toFixed(0), 2, gy+3);
  }

  ctx.setLineDash([4,3]); ctx.strokeStyle='#9aa'; ctx.lineWidth=1;
  ctx.strokeRect(xToPx(band.t_min), yToPx(band.h_max),
                 xToPx(band.t_max)-xToPx(band.t_min), yToPx(band.h_min)-yToPx(band.h_max));
  ctx.setLineDash([]);

  points.forEach(p=>{
    const inRange = p.t>=band.t_min && p.t<=band.t_max && p.h>=band.h_min && p.h<=band.h_max;
    ctx.fillStyle = inRange ? '#0ca30c' : '#d03b3b';
    ctx.beginPath(); ctx.arc(xToPx(p.t), yToPx(p.h), 4, 0, 2*Math.PI); ctx.fill();
  });

  canvas.onmousemove=(ev)=>{
    const rect=canvas.getBoundingClientRect();
    const mx=(ev.clientX-rect.left)*(canvas.width/rect.width);
    const my=(ev.clientY-rect.top)*(canvas.height/rect.height);
    let best=points[0], bestD=Infinity;
    points.forEach(p=>{ const dx=xToPx(p.t)-mx, dy=yToPx(p.h)-my, d=dx*dx+dy*dy; if(d<bestD){bestD=d;best=p;} });
    tip.style.display='block';
    tip.style.left=(ev.clientX+12)+'px'; tip.style.top=(ev.clientY-10)+'px';
    tip.textContent = fmtTime(best.ts)+'  '+best.t.toFixed(1)+'°C, '+best.h.toFixed(0)+'%';
  };
  canvas.onmouseleave=()=>{ tip.style.display='none'; };
}

function loadGiorno(date){
  if(!date || !cfg) return;
  fetch('/api/giorno?d='+encodeURIComponent(date)).then(r=>r.json()).then(rows=>{
    // Il CSV puo' avere righe fuori ordine cronologico (es. un riavvio che
    // riparte da una stima di orario prima che l'NTP la corregga): un grafico
    // a linee deve comunque avanzare nel tempo, altrimenti la linea "torna
    // indietro" ogni volta che incontra una riga con timestamp minore della
    // precedente. Si ordina qui, una volta, invece di fidarsi dell'ordine del file.
    rows = rows.slice().sort((a,b)=>a[0]-b[0]);
    const tempPts    = rows.map(r=>({x:r[0],y:r[1]}));
    const humPts     = rows.map(r=>({x:r[0],y:r[2]}));
    const scatterPts = rows.map(r=>({ts:r[0],t:r[1],h:r[2]}));
    const scorePts   = rows.map(r=>{ const c=comfortEval(r[1],r[2],cfg); return {x:r[0],y:c.score}; });
    drawLineChart($('cTemp'), tempPts, '#d95926', '°C');
    drawLineChart($('cHum'), humPts, '#3987e5', '%');
    drawScatter($('cScatter'), scatterPts, cfg);
    drawLineChart($('cScore'), scorePts, '#199e70', '');
  });
}

function loadGiorni(){
  return fetch('/api/giorni').then(r=>r.json()).then(list=>{
    const sel=$('giorno');
    sel.innerHTML = list.map(d=>'<option value="'+d+'">'+d+'</option>').join('');
    if (list.length){ sel.value=list[list.length-1]; loadGiorno(sel.value); }
  });
}

$('giorno').addEventListener('change', ()=>loadGiorno($('giorno').value));
$('baggiorna').addEventListener('click', ()=>loadGiorno($('giorno').value));

$('bscarica').addEventListener('click', ()=>{
  const d = $('giorno').value;
  if (!d) return;
  window.location = '/api/scarica?d='+encodeURIComponent(d);
});

$('belimina').addEventListener('click', ()=>{
  const d = $('giorno').value;
  if (!d) return;
  if (!confirm('Eliminare definitivamente il log di '+d+'? Non si puo\' annullare.')) return;
  fetch('/api/elimina?d='+encodeURIComponent(d), {method:'POST'}).then(r=>r.json()).then(res=>{
    $('eliminaMsg').textContent = res.ok ? (d+' eliminato.') : ('Errore: '+res.errore);
    if (res.ok) { refreshStato(); loadGiorni(); }
  });
});

$('bsalva').addEventListener('click', ()=>{
  const p = new URLSearchParams({
    nodo: $('cfgNodo').value, log_int_s: $('cfgLogInt').value, page_sec: $('cfgPageSec').value,
    t_min: $('cfgTMin').value, t_max: $('cfgTMax').value,
    h_min: $('cfgHMin').value, h_max: $('cfgHMax').value, tz: $('cfgTz').value
  });
  fetch('/api/config?'+p.toString(), {method:'POST'}).then(r=>r.json()).then(res=>{
    $('cfgMsg').textContent = res.ok ? 'Salvato.' : ('Errore: '+res.errore);
    if (res.ok) refreshConfig().then(()=>{ if ($('giorno').value) loadGiorno($('giorno').value); });
  });
});

refreshStato();
setInterval(refreshStato, 5000);
refreshConfig().then(loadGiorni);
</script>
</body></html>
)HTML";

// ---------------------------------------------------------------------
//  Pagina di upload/ripristino della dashboard (PROGMEM, SEMPRE servita da
//  qui indipendentemente da cosa c'e' sulla SD): e' la via di recupero se
//  una dashboard.html caricata a mano risulta rotta. Stessa tecnica di
//  upload della pagina /update in net_ota.cpp.
// ---------------------------------------------------------------------
static const char DASH_UPLOAD_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EnvNode-C3 &mdash; Dashboard personalizzata</title><style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:2rem;display:flex;justify-content:center}
 .card{max-width:420px;width:100%;background:#1c1c1c;border:1px solid #333;border-radius:12px;padding:1.5rem}
 h1{font-size:1.05rem;margin:0 0 1rem}
 input[type=file]{width:100%;margin:.5rem 0 1rem;color:#ccc}
 button{width:100%;padding:.7rem;border:0;border-radius:8px;background:#3987e5;color:#fff;font-size:1rem;cursor:pointer;margin-top:.5rem}
 button.dan{background:#b91c1c}
 button:disabled{background:#555}
 progress{width:100%;height:1rem;margin-top:1rem}
 .muted{color:#8a8a8a;font-size:.8rem;margin-top:1rem;line-height:1.4}
 a{color:#3987e5}
</style></head><body><div class="card">
 <h1>Dashboard personalizzata</h1>
 <p class="muted">Carica un file .html self-contained (CSS/JS inline, nessuna
 richiesta esterna) per sostituire la dashboard di default. Viene salvato
 sulla microSD in <code>/www/dashboard.html</code>. Questa pagina resta
 SEMPRE raggiungibile qui, anche se la dashboard personalizzata non funziona
 o la SD viene rimossa (in quel caso torna in uso quella di default).</p>
 <form id="f"><input type="file" name="dashboard" accept=".html,.htm" required>
 <button type="submit" id="b">Carica</button>
 <progress id="p" value="0" max="100" hidden></progress></form>
 <p class="muted" id="s"></p>
 <button id="br" class="dan">Ripristina dashboard di default</button>
 <p class="muted"><a href="/">&larr; torna alla dashboard</a></p>
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

// =======================================================================
//  Helper: JSON minimale a mano (stesso stile "niente librerie extra" del
//  resto del repo). Solo escaping di virgolette/backslash/controlli: le
//  stringhe che ci finiscono sono nomi nodo/etichette/errori, mai input
//  binario.
// =======================================================================
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

// ---------------------------------------------------------------------
//  Invio limitato: un client morto non deve poter fermare la scheda
//
//  WebServer::streamFile() finisce in NetworkClient::write(Stream&), che
//  IGNORA il valore di ritorno della write() e prosegue fino a fine file
//  anche quando il client non accetta piu' un byte. E ogni write() aspetta
//  che il socket torni scrivibile con dieci select() da un secondo l'uno.
//  Quindi un client che smette di dare ACK senza chiudere il socket -
//  telefono che si addormenta, WiFi che cade, coperchio del portatile - ci
//  tiene dentro l'handler finche' non e' lo stack TCP a rinunciare al peer,
//  che sono minuti. Non e' un caso limite: e' come muore una pagina lasciata
//  aperta.
//
//  Misurato su questa scheda il 2026-08-24 alle 21:00:46: ferma 456 s. In
//  quella finestra non ha campionato il proprio DHT11 (buco nel CSV) e
//  soprattutto non ha chiamato remote_loop(), quindi i DATA dei nodi
//  ESP-NOW arrivavano alla radio e nessuno li prelevava dal driver, che
//  tiene solo l'ultimo: sei pacchetti di un nodo e uno dell'altro persi per
//  sempre. Un client andato via a meta' scaricamento fa un buco nei dati di
//  TUTTA la rete, e nei log sembra un problema di radio: e' un guasto che
//  punta lontano da se stesso.
//
//  Rimedio: ci si ferma al primo chunk che il client non accetta per
//  intero, e comunque a fine budget. Il costo residuo e' UNA write bloccata
//  (~10 s): quel numero sta dentro il core e da qui non si abbassa.
// ---------------------------------------------------------------------
static constexpr uint32_t INVIO_BUDGET_MS = 20000;   // su una LAN un giorno intero di CSV vola: 20 s e' gia' larghissimo

static uint32_t s_invii_interrotti = 0;   // quante volte e' scattato il taglio, da questo avvio

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

// ---------------------------------------------------------------------
//  GET /
// ---------------------------------------------------------------------
static void handleRoot() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  // Dashboard personalizzata su SD, se presente (vedi /dashboard-upload):
  // altrimenti quella di default incorporata nel firmware.
  File custom = sd_open_dashboard();
  if (custom) {
    streamFileLimitato(net_server(), custom, "text/html");
    custom.close();
    return;
  }
  net_server().send_P(200, "text/html", DASHBOARD_PAGE);
}

// ---------------------------------------------------------------------
//  GET/POST /dashboard-upload, POST /dashboard-ripristina
// ---------------------------------------------------------------------
static File s_dashUploadFile;
static bool s_dashUploadOk = false;

static void handleDashboardUploadPage() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().send_P(200, "text/html", DASH_UPLOAD_PAGE);
}

// Fine dell'upload: risposta in base a come e' andata la scrittura.
static void handleDashboardUploadDone() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().sendHeader("Connection", "close");
  net_server().send(s_dashUploadOk ? 200 : 500, "text/plain",
                     s_dashUploadOk ? "OK" : "Caricamento fallito (SD non disponibile o scrittura fallita)");
}

// Ricezione del file a blocchi -> scrittura su /www/dashboard.html.
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

    default:
      break;
  }
}

static void handleDashboardRipristina() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  bool ok = sd_delete_dashboard();
  net_server().send(ok ? 200 : 500, "text/plain",
                     ok ? "Ripristinata la dashboard di default." : "Cancellazione fallita.");
}

// ---------------------------------------------------------------------
//  GET /api/stato
// ---------------------------------------------------------------------
static void handleApiStato() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }

  const AppSettings& cfg = settings_get();
  char oraBuf[24] = "--";
  rtctime_format(rtctime_now(), "%Y-%m-%d %H:%M:%S", oraBuf, sizeof(oraBuf));

  String json;
  json.reserve(700);
  json += '{';
  json += "\"nodo\":";     appendJsonString(json, cfg.nodeName);       json += ',';
  json += "\"fw\":";       appendJsonString(json, app_fw_version());  json += ',';
  json += "\"wifi\":";     json += (net_isConnected() ? "true" : "false"); json += ',';
  json += "\"ip\":";       appendJsonString(json, net_isConnected() ? net_ip().c_str() : ""); json += ',';
  json += "\"rssi\":" + String(net_isConnected() ? net_rssi() : 0) + ",";
  json += "\"ora\":";      appendJsonString(json, oraBuf);            json += ',';
  json += "\"ora_fonte\":"; appendJsonString(json, rtctime_source()); json += ',';

  if (app_has_reading()) {
    float t = app_temp_now(), h = app_hum_now();
    ComfortResult c = comfort_eval(t, h, cfg.comfort);
    char buf[24];

    json += "\"temp\":" + String(t, 1) + ",";
    json += "\"hum\":"  + String(h, 1) + ",";

    json += "\"temp_min\":" + String(app_temp_min(), 1) + ",";
    rtctime_format(app_temp_min_ts(), "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
    json += "\"temp_min_ora\":"; appendJsonString(json, buf); json += ',';

    json += "\"temp_max\":" + String(app_temp_max(), 1) + ",";
    rtctime_format(app_temp_max_ts(), "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
    json += "\"temp_max_ora\":"; appendJsonString(json, buf); json += ',';

    json += "\"hum_min\":" + String(app_hum_min(), 1) + ",";
    rtctime_format(app_hum_min_ts(), "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
    json += "\"hum_min_ora\":"; appendJsonString(json, buf); json += ',';

    json += "\"hum_max\":" + String(app_hum_max(), 1) + ",";
    rtctime_format(app_hum_max_ts(), "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
    json += "\"hum_max_ora\":"; appendJsonString(json, buf); json += ',';

    json += "\"comfort_score\":" + String(c.score) + ",";
    json += "\"comfort_label\":"; appendJsonString(json, c.label); json += ',';
    json += "\"comfort_in_range\":"; json += (c.inRange ? "true" : "false"); json += ',';
  } else {
    json += "\"temp\":null,\"hum\":null,"
            "\"temp_min\":null,\"temp_min_ora\":null,"
            "\"temp_max\":null,\"temp_max_ora\":null,"
            "\"hum_min\":null,\"hum_min_ora\":null,"
            "\"hum_max\":null,\"hum_max_ora\":null,"
            "\"comfort_score\":null,\"comfort_label\":null,\"comfort_in_range\":null,";
  }

  json += "\"sd\":"; json += (sd_mounted() ? "true" : "false"); json += ',';
  json += "\"sd_errore\":"; appendJsonString(json, sd_mounted() ? "" : sd_last_error()); json += ',';
  json += "\"sd_liberi_mb\":"  + String((unsigned long)sd_free_mb()) + ",";
  json += "\"sd_totali_mb\":"  + String((unsigned long)sd_total_mb()) + ",";
  json += "\"record_oggi\":"   + String(sd_record_count_today()) + ",";
  json += "\"record_totali\":" + String(sd_record_count_total()) + ",";
  json += "\"dht_errori\":"    + String(app_dht_errors()) + ",";

  // Nodi ESP-NOW: qui solo i due contatori, cosi' una dashboard puo'
  // mostrare "1 nodo muto" senza una seconda chiamata. Il dettaglio sta in
  // /api/nodi.
  json += "\"nodi\":"          + String(remote_count()) + ",";
  json += "\"nodi_online\":"   + String(remote_count_online()) + ",";
  json += "\"pairing\":"; json += (remote_pairing_active() ? "true" : "false"); json += ',';

  // Diagnostica del giro e degli invii tagliati: e' cio' che permette di
  // spiegare un buco nei dati senza rifare l'indagine sui CSV.
  json += "\"loop_max_ms\":"   + String(app_loop_max_ms()) + ",";
  json += "\"loop_max_dove\":"; appendJsonString(json, app_loop_max_dove()); json += ',';
  if (app_loop_max_ts() > 0) {
    char lbuf[24];
    rtctime_format(app_loop_max_ts(), "%Y-%m-%d %H:%M:%S", lbuf, sizeof(lbuf));
    json += "\"loop_max_ora\":"; appendJsonString(json, lbuf); json += ',';
  } else {
    json += "\"loop_max_ora\":null,";
  }
  json += "\"loop_lenti\":"      + String(app_loop_lenti()) + ",";
  json += "\"invii_interrotti\":" + String(s_invii_interrotti) + ",";

  json += "\"uptime\":"        + String(millis() / 1000) + ",";
  json += "\"heap\":"          + String(ESP.getFreeHeap());
  json += '}';

  net_server().send(200, "application/json", json);
}

// ---------------------------------------------------------------------
//  GET /api/giorni
// ---------------------------------------------------------------------
static void appendDayCb(const char* isoDate, size_t /*fileSizeBytes*/, void* arg) {
  String* out = (String*)arg;
  if (out->length() > 1) *out += ',';
  appendJsonString(*out, isoDate);
}

static void handleApiGiorni() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  String json = "[";
  sd_list_days(appendDayCb, &json, 0);
  json += ']';
  net_server().send(200, "application/json", json);
}

// ---------------------------------------------------------------------
//  GET /api/giorno?d=YYYY-MM-DD — risposta in streaming, mai l'intero
//  file in RAM (vedi sd_read_day in sd_logger.cpp).
// ---------------------------------------------------------------------
struct GiornoStreamCtx {
  WebServer* server;
  bool       first;
  bool       interrotto;   // client morto o budget scaduto: si smette di spedire
  uint32_t   t0;
  String     buf;          // le righe si accumulano invece di partire una per una
};

// Una sendContent() per riga erano TRE write() sul socket per ~25 byte di
// dati (in chunked encoding: la dimensione, il corpo, il terminatore), cioe'
// ~4200 write per un giorno. Accumulando in un buffer da 1 kB diventano una
// cinquantina - e ogni write in meno e' un'attesa in meno da dieci secondi
// quando il client dall'altra parte e' morto (vedi streamFileLimitato).
static void giornoFlush(GiornoStreamCtx* ctx) {
  if (ctx->interrotto || ctx->buf.length() == 0) return;

  if (!ctx->server->client().connected()) {
    ctx->interrotto = true;
    invioInterrotto("il client ha chiuso");
    return;
  }
  ctx->server->sendContent(ctx->buf);
  ctx->buf = "";

  // sendContent() non dice quanto ha scritto, quindi qui il client morto non
  // si riconosce dal chunk rifiutato come nello streaming dei file: si
  // riconosce dal tempo. Una write bloccata costa ~10 s, quindi bastano due
  // giri sopra il budget per accorgersene.
  if (millis() - ctx->t0 > INVIO_BUDGET_MS) {
    ctx->interrotto = true;
    invioInterrotto("oltre il budget di tempo");
  }
}

static void streamRowCb(time_t ts, float t, float h, void* arg) {
  GiornoStreamCtx* ctx = (GiornoStreamCtx*)arg;

  // sd_read_day() non si puo' fermare a meta': si smette di spedire e si
  // lascia scorrere la lettura, che sulla card e' questione di un secondo.
  if (ctx->interrotto) return;

  char chunk[48];
  snprintf(chunk, sizeof(chunk), "%s[%lu,%.1f,%.1f]",
           ctx->first ? "" : ",", (unsigned long)ts, t, h);
  ctx->first = false;
  ctx->buf += chunk;
  if (ctx->buf.length() >= 1024) giornoFlush(ctx);
}

static void handleApiGiorno() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!srv.hasArg("d") || !sd_name_is_safe(srv.arg("d").c_str())) {
    srv.send(400, "application/json", "[]");
    return;
  }
  String date = srv.arg("d");

  srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv.send(200, "application/json", "");
  srv.sendContent("[");
  GiornoStreamCtx ctx{ &srv, true, false, millis(), String() };
  ctx.buf.reserve(1200);
  sd_read_day(date.c_str(), streamRowCb, &ctx);
  giornoFlush(&ctx);

  // Se si e' interrotto, il JSON resta tronco E NON SI CHIUDE: un array
  // chiuso a meta' verrebbe letto come un giorno con meno dati, cioe' un
  // grafico sbagliato che sembra giusto. Cosi' invece il parse fallisce e
  // la dashboard mostra un errore, che e' la verita'. Il chunked lo chiude
  // comunque WebServer::_finalizeResponse().
  if (!ctx.interrotto) srv.sendContent("]");
}

// ---------------------------------------------------------------------
//  GET /api/scarica?d=YYYY-MM-DD — scarica il CSV grezzo di un giorno.
// ---------------------------------------------------------------------
static void handleApiScarica() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!srv.hasArg("d") || !sd_name_is_safe(srv.arg("d").c_str())) {
    srv.send(400, "text/plain", "data non valida");
    return;
  }
  String date = srv.arg("d");

  File f = sd_open_day(date.c_str());
  if (!f) {
    srv.send(404, "text/plain", "file non trovato");
    return;
  }

  char header[40];
  snprintf(header, sizeof(header), "attachment; filename=\"%s.csv\"", date.c_str());
  srv.sendHeader("Content-Disposition", header);
  streamFileLimitato(srv, f, "text/csv");
  f.close();
}

// ---------------------------------------------------------------------
//  POST /api/elimina?d=YYYY-MM-DD — elimina il file di log di un giorno.
// ---------------------------------------------------------------------
static void handleApiElimina() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();

  if (!srv.hasArg("d") || !sd_name_is_safe(srv.arg("d").c_str())) {
    srv.send(400, "application/json", "{\"ok\":false,\"errore\":\"data non valida\"}");
    return;
  }

  bool ok = sd_delete_day(srv.arg("d").c_str());
  String json = "{\"ok\":";
  json += ok ? "true" : "false";
  if (!ok) { json += ",\"errore\":"; appendJsonString(json, sd_last_error()); }
  json += '}';
  srv.send(ok ? 200 : 400, "application/json", json);
}

// ---------------------------------------------------------------------
//  GET /api/config
// ---------------------------------------------------------------------
static void handleApiConfigGet() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  const AppSettings& cfg = settings_get();

  String json = "{";
  json += "\"nodo\":"; appendJsonString(json, cfg.nodeName); json += ',';
  json += "\"log_int_s\":" + String(cfg.logIntervalS) + ",";
  json += "\"page_sec\":"  + String(cfg.pageSeconds) + ",";
  json += "\"t_min\":" + String(cfg.comfort.tMin, 1) + ",";
  json += "\"t_max\":" + String(cfg.comfort.tMax, 1) + ",";
  json += "\"h_min\":" + String(cfg.comfort.hMin, 1) + ",";
  json += "\"h_max\":" + String(cfg.comfort.hMax, 1) + ",";
  json += "\"tz\":"; appendJsonString(json, cfg.tz);
  json += '}';

  net_server().send(200, "application/json", json);
}

// ---------------------------------------------------------------------
//  POST /api/config — ogni campo e' opzionale/indipendente (stesso
//  pattern di handleConfig() in XIAO_S3_Camera/web_ui.cpp).
// ---------------------------------------------------------------------
static void handleApiConfigPost() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  String err;

  if (srv.hasArg("nodo") && !settings_set_node_name(srv.arg("nodo").c_str())) {
    err = "nome nodo non valido";
  }
  if (err.length() == 0 && srv.hasArg("log_int_s") &&
      !settings_set_log_interval_s((uint32_t)srv.arg("log_int_s").toInt())) {
    err = "intervallo di log fuori range (5-3600s)";
  }
  if (err.length() == 0 && srv.hasArg("page_sec") &&
      !settings_set_page_seconds((uint32_t)srv.arg("page_sec").toInt())) {
    err = "rotazione pagine fuori range (2-30s)";
  }
  if (err.length() == 0 &&
      (srv.hasArg("t_min") || srv.hasArg("t_max") || srv.hasArg("h_min") || srv.hasArg("h_max"))) {
    ComfortConfig band = settings_get().comfort;
    if (srv.hasArg("t_min")) band.tMin = srv.arg("t_min").toFloat();
    if (srv.hasArg("t_max")) band.tMax = srv.arg("t_max").toFloat();
    if (srv.hasArg("h_min")) band.hMin = srv.arg("h_min").toFloat();
    if (srv.hasArg("h_max")) band.hMax = srv.arg("h_max").toFloat();
    if (!settings_set_comfort(band)) err = "banda di comfort non valida";
  }
  if (err.length() == 0 && srv.hasArg("tz")) {
    if (!settings_set_tz(srv.arg("tz").c_str())) {
      err = "fuso orario non valido";
    } else {
      rtctime_begin(settings_get().tz);   // riapplica subito, non solo al prossimo riavvio
    }
  }

  String json = "{\"ok\":";
  json += (err.length() == 0) ? "true" : "false";
  if (err.length() > 0) { json += ",\"errore\":"; appendJsonString(json, err.c_str()); }
  json += '}';

  srv.send(err.length() == 0 ? 200 : 400, "application/json", json);
}

// ---------------------------------------------------------------------
//  Nodi ESP-NOW: pagina /nodi + /api/nodi + /api/pairing
// ---------------------------------------------------------------------
//  Pagina a se' stante, deliberatamente NON dentro la dashboard: quella
//  vera sta sulla microSD (vedi /dashboard-upload) e va ricaricata a mano
//  dopo ogni modifica, quindi una funzione nuova che vive solo in PROGMEM
//  e' raggiungibile SEMPRE, anche con una dashboard personalizzata vecchia
//  o rotta. Stessa logica di /dashboard-upload.
// ---------------------------------------------------------------------
static const char NODI_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EnvNode-C3 &mdash; Nodi ESP-NOW</title><style>
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
<h1>Nodi ESP-NOW</h1>
<div class="card">
 <div class="row">
  <button id="bp">Apri pairing 5 min</button>
  <button id="bc" class="off">Chiudi</button>
  <span class="muted" id="st"></span>
 </div>
 <p class="muted">Un nodo si associa solo mentre la finestra e' aperta. Il
 registro dei nodi vive in RAM: dopo un riavvio di questa scheda la finestra
 si riapre da sola per 5 minuti, cosi' i nodi gia' noti rientrano senza che
 nessuno debba premere niente.</p>
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
<p class="muted"><a href="/">&larr; dashboard</a></p>
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
function tick(){fetch('/api/nodi').then(r=>r.json()).then(d=>{render(d);altAggiorna(d);}).catch(()=>{});}
E('bp').onclick=()=>fetch('/api/pairing?on=1&s=300',{method:'POST'}).then(tick);
E('bc').onclick=()=>fetch('/api/pairing?on=0',{method:'POST'}).then(tick);
tick();setInterval(tick,2000);
</script></div></body></html>
)HTML";

// Un float non finito va emesso come null, MAI come numero: String(NAN, 2)
// produce "nan", che non e' JSON valido e farebbe fallire il parse dell'INTERA
// risposta nel browser — la pagina resterebbe vuota per colpa di un solo
// sensore guasto su un solo nodo. E i NaN arrivano davvero: un nodo che non
// riesce a leggere il proprio sensore trasmette lo stesso, con i valori a NAN.
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

static void handleNodiPage() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  net_server().send_P(200, "text/html", NODI_PAGE);
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

// GET /api/nodi/giorni?nodo=<nome>   elenco dei CSV di quel nodo su SD
static void handleApiNodiGiorni() {
  if (!net_webAuthOk()) { net_server().requestAuthentication(); return; }
  WebServer& srv = net_server();
  if (!srv.hasArg("nodo")) { srv.send(400, "text/plain", "manca il parametro nodo"); return; }

  String json = "[";
  sd_list_remote_days(srv.arg("nodo").c_str(), appendDayCb, &json, 400);
  json += ']';
  srv.send(200, "application/json", json);
}

// GET /api/nodi/scarica?nodo=<nome>&d=YYYY-MM-DD   CSV grezzo
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

// POST /api/nodi/altitudine?m=29
// Quota usata per riportare al livello del mare la pressione dei nodi, che la
// trasmettono grezza. Non tocca il trend, che e' una differenza: sposta solo i
// valori assoluti, cioe' le soglie con cui forecast_text() distingue "bel
// tempo stabile" da "perturbato che non si sblocca".
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

// POST /api/nodi/dimentica?mac=AA:BB:CC:DD:EE:FF
// Toglie il nodo dal registro (libreria + RAM + NVS). Serve quando una scheda
// viene sostituita: l'identita' di un nodo e' il suo MAC, quindi quella
// vecchia resterebbe in elenco per sempre come nodo muto, cioe' un allarme
// falso permanente.
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

// POST /api/pairing?on=1[&s=300] | ?on=0
// Parametri in query string anche sulla POST, come /api/config e
// /api/elimina: e' la convenzione gia' in uso in questo file.
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

// ---------------------------------------------------------------------
// CORS permissivo sulle /api/*: serve per sviluppare una dashboard.html in
// locale (aperta come file o da un dev server sul PC) e chiamare comunque
// le API dell'IP della scheda in fetch() cross-origin. Non serve per l'uso
// normale (la dashboard servita dalla scheda e' same-origin): e' solo
// comodita' di sviluppo, non una necessita' di sicurezza per un dispositivo
// su LAN privata. setAllowCredentials(false) perche' l'autenticazione qui
// passa da un header Authorization impostato a mano nel fetch(), non da
// cookie/credenziali gestite dal browser — combinare origin "*" con
// Access-Control-Allow-Credentials:true sarebbe comunque non valido per lo
// standard fetch. Il middleware gestisce da solo anche il preflight OPTIONS
// (nessuna autenticazione richiesta li', come da specifica CORS).
static CorsMiddleware s_cors;

void web_ui_begin() {
  WebServer& srv = net_server();

  // WebServer non tiene traccia degli header della richiesta (nemmeno
  // "Origin") a meno di dirglielo esplicitamente: senza questa riga
  // CorsMiddleware::run() vedrebbe sempre hasHeader("Origin")==false, non
  // aggiungerebbe mai gli header CORS e (peggio) non intercetterebbe il
  // preflight OPTIONS, che finirebbe sul 404 di default (bug reale
  // riscontrato in test: la richiesta funzionava ma senza intestazioni
  // CORS, il preflight falliva con 404).
  srv.collectAllHeaders();

  s_cors.setOrigin("*").setAllowCredentials(false);
  srv.addMiddleware(&s_cors);

  srv.on("/", HTTP_GET, handleRoot);
  srv.on("/dashboard-upload", HTTP_GET, handleDashboardUploadPage);
  srv.on("/dashboard-upload", HTTP_POST, handleDashboardUploadDone, handleDashboardUploadChunk);
  srv.on("/dashboard-ripristina", HTTP_POST, handleDashboardRipristina);
  srv.on("/api/stato", HTTP_GET, handleApiStato);
  srv.on("/api/giorni", HTTP_GET, handleApiGiorni);
  srv.on("/api/giorno", HTTP_GET, handleApiGiorno);
  srv.on("/api/scarica", HTTP_GET, handleApiScarica);
  srv.on("/api/elimina", HTTP_POST, handleApiElimina);
  srv.on("/api/config", HTTP_GET, handleApiConfigGet);
  srv.on("/api/config", HTTP_POST, handleApiConfigPost);
  srv.on("/nodi", HTTP_GET, handleNodiPage);
  srv.on("/api/nodi", HTTP_GET, handleApiNodi);
  srv.on("/api/pairing", HTTP_POST, handleApiPairing);
  srv.on("/api/nodi/dimentica", HTTP_POST, handleApiNodiDimentica);
  srv.on("/api/nodi/altitudine", HTTP_POST, handleApiNodiAltitudine);
  srv.on("/api/nodi/giorni", HTTP_GET, handleApiNodiGiorni);
  srv.on("/api/nodi/scarica", HTTP_GET, handleApiNodiScarica);
}
