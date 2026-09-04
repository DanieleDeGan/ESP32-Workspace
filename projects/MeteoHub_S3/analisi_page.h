#pragma once

// ============================================================
//  GENERATO DA www/gen_page.py - NON MODIFICARE A MANO.
//  La sorgente e' www/analisi.html: si modifica quella e si
//  rilancia  python www/gen_page.py analisi  prima di ricompilare.
//  (38314 byte di pagina, serviti su /analisi)
// ============================================================

static const char ANALISI_PAGE[] PROGMEM = R"ANALISIPAGE(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark"><title>MeteoHub-S3 &mdash; Analisi</title><style>
 :root{--bg:#0e0e10;--card:#1a1a1d;--bordo:#2e2e33;--txt:#ececee;--dim:#8e8e96;
  --acc:#3987e5;--ok:#3fb950;--warn:#e5a13a;--bad:#e05252;--cold:#58a6ff;--hot:#f0883e}
 *{box-sizing:border-box}
 /* SERVE: [hidden]{display:none} lo mette il foglio di stile del BROWSER, che
    ha priorita' minore di qualunque regola scritta qui. Con .barra{display:flex}
    le barre marcate `hidden` restavano visibili, con i menu ancora vuoti. */
 [hidden]{display:none!important}
 body{font-family:system-ui,-apple-system,Segoe UI,Arial,sans-serif;background:var(--bg);
  color:var(--txt);margin:0;padding:14px}
 .wrap{max-width:1100px;margin:0 auto}
 h1{font-size:1.15rem;margin:.2rem 0 .3rem}
 h2{font-size:.95rem;margin:0 0 .6rem;font-weight:600}
 .sub{color:var(--dim);font-size:.85rem;line-height:1.5;margin-bottom:1rem}
 .card{background:var(--card);border:1px solid var(--bordo);border-radius:12px;
  padding:13px 15px;margin-bottom:10px}
 .barra{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center;margin-bottom:1rem}
 select,button,input{background:#22222a;color:var(--txt);border:1px solid var(--bordo);
  border-radius:8px;padding:6px 10px;font-size:.85rem;font-family:inherit}
 button{cursor:pointer}
 button:hover{border-color:var(--acc)}
 label{color:var(--dim);font-size:.85rem;display:flex;align-items:center;gap:.35rem}
 .tabs{display:flex;gap:.4rem;margin-bottom:.9rem;flex-wrap:wrap}
 .tab{background:#1a1a1d;border:1px solid var(--bordo);color:var(--dim);
  border-radius:9px;padding:6px 13px;font-size:.85rem;cursor:pointer;font-family:inherit}
 .tab.on{background:#20343f;border-color:#2f5470;color:#79c0ff}
 .rec{display:flex;gap:.8rem;flex-wrap:wrap}
 .rq{flex:1 1 140px;background:#15151a;border:1px solid var(--bordo);border-radius:10px;padding:9px 11px}
 .rq .k{color:var(--dim);font-size:.72rem;text-transform:uppercase;letter-spacing:.04em}
 .rq .v{font-size:1.25rem;font-weight:650;margin-top:2px}
 .rq .d{color:var(--dim);font-size:.75rem;margin-top:1px}
 table{border-collapse:collapse;width:100%;font-size:.8rem}
 th,td{padding:4px 7px;text-align:right;border-bottom:1px solid #26262b;white-space:nowrap}
 th{color:var(--dim);font-weight:600;text-align:right;position:sticky;top:0;background:var(--card)}
 td:first-child,th:first-child{text-align:left}
 .tw{overflow-x:auto;max-height:420px;overflow-y:auto}
 tr.parz td{color:var(--warn)}
 .avv{background:#3a2a1e;border:1px solid #5a4128;color:#e5a13a;border-radius:10px;
  padding:9px 12px;font-size:.82rem;line-height:1.5;margin-bottom:10px}
 .err{background:#3a1e1e;border-color:#5a2828;color:#e08a8a}
 .info{background:#1a2a33;border-color:#284a5a;color:#79c0ff}
 svg{display:block;width:100%;height:auto}
 .g{width:100%;height:340px}
 .g.alto{height:420px}
 .lg{color:var(--dim);font-size:.75rem;margin-top:.4rem;line-height:1.5}
 .lg i{font-style:normal;padding:0 .3rem}
 nav{margin:1.6rem 0 .5rem;display:flex;flex-wrap:wrap;gap:.4rem 1rem;font-size:.85rem}
 a{color:var(--acc);text-decoration:none}
</style></head><body><div class="wrap">
<h1>Analisi <span id="fw" class="sub"></span></h1>
<p class="sub">I dati vengono dall&rsquo;hub in due forme: <b>una riga per giorno chiuso</b>
(<code>/api/nodi/riepilogo</code>) e la <b>serie oraria decimata a bordo</b>
(<code>/api/nodi/serie</code>). I giorni <b>incompleti restano visibili e segnati</b>:
un minimo calcolato sul 40&nbsp;% dei campioni ha lo stesso aspetto di un minimo vero.</p>

<div class="tabs">
 <button class="tab on" data-v="giorni">Per giorno</button>
 <button class="tab" data-v="serie">Andamento</button>
 <button class="tab" data-v="conf">Confronto giorni</button>
 <button class="tab" data-v="nodi">Confronto nodi</button>
 <button class="tab" data-v="mappa">Mappa oraria</button>
 <button class="tab" data-v="distr">Distribuzione</button>
</div>

<div class="barra">
 <label>nodo <select id="nodo"></select></label>
 <span id="cPer"><label>periodo <select id="per">
  <option value="0">tutti i giorni</option>
  <option value="7">ultimi 7</option>
  <option value="30">ultimi 30</option>
 </select></label></span>
 <span id="cSog"><label>segna sotto <select id="sog">
  <option value="90">90%</option><option value="99">99%</option>
  <option value="0">mai</option>
 </select></label></span>
 <button id="ric">ricarica</button>
</div>

<div class="barra" id="barraSerie" hidden>
 <label>dal <select id="sDa"></select></label>
 <label>al <select id="sA"></select></label>
 <label>grandezza <select id="sV"></select></label>
 <label>dettaglio <select id="sP">
  <option value="300">normale</option><option value="600">fine</option>
  <option value="1000">massimo</option><option value="150">grossolano</option>
 </select></label>
 <button id="sGo">disegna</button>
</div>

<div class="barra" id="barraConf" hidden>
 <label>giorno A <select id="cA"></select></label>
 <label>giorno B <select id="cB"></select></label>
 <label>giorno C <select id="cC"></select></label>
 <label>grandezza <select id="cV"></select></label>
 <button id="cGo">confronta</button>
</div>

<div class="barra" id="barraNodi" hidden>
 <label>dal <select id="nDa"></select></label>
 <label>al <select id="nA"></select></label>
 <label>grandezza <select id="nV"></select></label>
 <button id="nGo">confronta i nodi</button>
</div>

<div class="barra" id="barraMappa" hidden>
 <label>dal <select id="mDa"></select></label>
 <label>al <select id="mA"></select></label>
 <label>grandezza <select id="mV"></select></label>
 <button id="mGo">disegna</button>
</div>

<div class="barra" id="barraDistr" hidden>
 <label>dal <select id="dDa"></select></label>
 <label>al <select id="dA"></select></label>
 <label>grandezza <select id="dV"></select></label>
 <button id="dGo">calcola</button>
</div>

<div id="msg"></div>
<div id="cont"></div>

<nav>
 <a href="/">Nodi</a><a href="/pannello">Pannello</a><a href="/analisi">Analisi</a>
 <a href="/immagini">Componi immagine</a><a href="/pagine">Pagine</a>
 <a href="/api">API</a><a href="/update">Aggiorna firmware</a>
</nav>

<script>
const $=x=>document.getElementById(x);
const esc=x=>String(x==null?'':x).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const num=(v,d)=>v==null||!isFinite(v)?'&mdash;':v.toFixed(d);
let DATI={};        // nodo -> righe di riepilogo
let NODI=[];        // elenco nomi
let EC=null;        // ECharts, se e' arrivata
let VISTA='giorni';
const GRAFICI=[];   // istanze da ridimensionare

const NOMI_V=['temperatura','umidità','pressione'];
const UNI_V=['°C','%','hPa'];
const DEC_V=[1,0,1];
const COL_V=['#f0883e','#58a6ff','#3987e5'];

// =====================================================================
//  La libreria dei grafici la scarica IL BROWSER, non la scheda: l'hub
//  serve solo HTML e dati. Quindi funziona finche' il dispositivo che
//  guarda la pagina ha internet — e se non ce l'ha, si torna ai grafici
//  SVG disegnati a mano, che sono rimasti apposta qui sotto.
//
//  Il timeout serve perche' il caso cattivo non e' "niente rete" (li'
//  l'errore arriva subito) ma "rete che c'e' e non risponde": senza, la
//  pagina resterebbe a fissare il vuoto senza dire niente.
// =====================================================================
const CDN='https://cdn.jsdelivr.net/npm/echarts@5.5.0/dist/echarts.min.js';
function caricaLibreria(){
 return new Promise(res=>{
  const s=document.createElement('script');
  let fatto=false;
  const finito=ok=>{ if(fatto)return; fatto=true; res(ok?window.echarts:null); };
  s.src=CDN; s.async=true;
  s.onload=()=>finito(true);
  s.onerror=()=>finito(false);
  setTimeout(()=>finito(!!window.echarts),7000);
  document.head.appendChild(s);
 });
}

function tema(){
 return {backgroundColor:'transparent',
  textStyle:{color:'#ececee',fontFamily:'system-ui,Segoe UI,Arial,sans-serif'},
  grid:{left:52,right:18,top:28,bottom:56},
  tooltip:{trigger:'axis',backgroundColor:'#15151a',borderColor:'#2e2e33',
   textStyle:{color:'#ececee',fontSize:12}},
  legend:{textStyle:{color:'#8e8e96'},top:0},
  xAxis:{axisLine:{lineStyle:{color:'#2e2e33'}},axisLabel:{color:'#8e8e96'},
   splitLine:{show:false}},
  yAxis:{axisLine:{show:false},axisLabel:{color:'#8e8e96'},
   splitLine:{lineStyle:{color:'#26262b'}}}};
}

function nuovoGrafico(el,opt){
 const g=EC.init(el,null,{renderer:'canvas'});
 g.setOption(Object.assign(tema(),opt));
 GRAFICI.push(g);
 return g;
}
function pulisciGrafici(){ while(GRAFICI.length){ try{GRAFICI.pop().dispose();}catch(e){} } }
window.addEventListener('resize',()=>GRAFICI.forEach(g=>{try{g.resize();}catch(e){}}));

// --- dati ------------------------------------------------------------
async function chiedi(u,tipo){
 const r=await fetch(u);
 if(!r.ok) throw new Error(r.status+' '+(await r.text()).slice(0,120));
 return tipo=='json'?r.json():r.text();
}
function avviso(t,cls){ $('msg').innerHTML='<div class="avv '+(cls||'')+'">'+t+'</div>'; }

// Il CSV del riepilogo -> oggetti. Un campo vuoto resta null e NON diventa
// zero: nel grafico dev'essere un buco, non una misura che nessuno ha fatto.
function parse(txt){
 const rr=txt.trim().split('\n');
 if(rr.length<2) return [];
 const idx={}; rr[0].split(',').forEach((c,i)=>idx[c.trim()]=i);
 const out=[];
 for(let i=1;i<rr.length;i++){
  const c=rr[i].split(',');
  if(c.length<5) continue;
  const g=v=>{const s=c[idx[v]];return s===undefined||s===''?null:parseFloat(s);};
  const s=v=>{const x=c[idx[v]];return x===undefined||x===''?null:x;};
  out.push({giorno:c[0],campioni:g('campioni'),attesi:g('attesi'),cadenza:g('cadenza_s'),
   compl:g('completezza_pct'),buchi:g('buchi'),
   tmin:g('t_min'),tminOra:s('t_min_ora'),tmax:g('t_max'),tmaxOra:s('t_max_ora'),tmed:g('t_med'),
   hmin:g('h_min'),hmax:g('h_max'),hmed:g('h_med'),
   pmin:g('p_min'),pmax:g('p_max'),pmed:g('p_med'),pvar:g('p_var24'),
   tdmin:g('td_min'),tdmax:g('td_max'),tdmed:g('td_med')});
 }
 return out;
}
const serie=(nodo,da,a,v,punti)=>chiedi('/api/nodi/serie?nodo='+encodeURIComponent(nodo)+
  '&da='+da+'&a='+a+'&v='+v+'&punti='+punti,'json');

const giorniDelNodo=n=>(DATI[n||$('nodo').value]||[]).map(r=>r.giorno);
function riempiSelect(id,vals,def){
 const e=$(id); const prima=e.value;
 e.innerHTML=vals.map(g=>'<option>'+g+'</option>').join('');
 if(prima&&vals.includes(prima)) e.value=prima; else if(def!=null) e.value=def;
}
function riempiGrandezze(id){
 const e=$(id); if(e.options.length) return;
 e.innerHTML=NOMI_V.map((n,i)=>'<option value="'+i+'">'+n+'</option>').join('');
}
// Un elenco vuoto ha due cause diverse, e vanno DETTE: i riepiloghi non sono
// ancora arrivati, oppure quel nodo non ha giorni chiusi.
function giorniPronti(){
 const g=giorniDelNodo();
 if(g.length) return g;
 avviso(Object.keys(DATI).length
  ? 'Per <b>'+esc($('nodo').value)+'</b> non c&rsquo;&egrave; ancora nessun giorno chiuso.'
  : 'Riepiloghi non ancora caricati: premi <b>ricarica</b>.','err');
 return null;
}
// L'istante del cesto i, come Date: la serie porta t0 e passo, non i timestamp.
const istante=(S,i)=>new Date((S.t0+i*S.passo)*1000);
const hhmm=d=>String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0');

// =====================================================================
//  VISTE
// =====================================================================
function vistaGiorni(){
 const per=parseInt($('per').value,10), sog=parseInt($('sog').value,10);
 let righe=(DATI[$('nodo').value]||[]).slice();
 if(per>0) righe=righe.slice(-per);
 if(!righe.length){ $('cont').innerHTML='<div class="card">nessun giorno chiuso.</div>'; return; }
 const parz=righe.filter(r=>sog>0&&r.compl!=null&&r.compl<sog);
 let avv='';
 if(parz.length) avv='<div class="avv"><b>'+parz.length+' giorn'+(parz.length==1?'o':'i')+
  ' sotto il '+sog+'% dei campioni attesi</b> ('+parz.map(r=>r.giorno).join(', ')+
  '). Restano nei grafici e in tabella, segnati: toglierli farebbe sparire anche '+
  'l&rsquo;informazione che sono mancati.</div>';

 $('cont').innerHTML=avv+
  '<div class="card"><h2>Il periodo in sintesi</h2>'+records(righe)+'</div>'+
  '<div class="card"><h2>Temperatura</h2><div id="g1" class="g"></div>'+
   '<div class="lg">banda = escursione del giorno, linea = media. '+
   'I punti cerchiati sono giorni con meno campioni del previsto.</div></div>'+
  '<div class="card"><h2>Completezza dei dati</h2><div id="g2" class="g"></div>'+
   '<div class="lg">campioni ricevuti sul totale atteso alla cadenza di quel giorno. '+
   'Sopra il 100&nbsp;% si pu&ograve; andare: la cadenza &egrave; stimata.</div></div>'+
  '<div class="card"><h2>Pressione e variazione sulle 24 h</h2><div id="g3" class="g"></div></div>'+
  '<div class="card"><h2>Umidit&agrave; e punto di rugiada</h2><div id="g4" class="g"></div></div>'+
  '<div class="card"><h2>Tutti i giorni</h2>'+tabella(righe,sog)+'</div>';

 if(!EC){ disegnaGiorniSvg(righe,sog); return; }
 const gg=righe.map(r=>r.giorno);
 const banda=(min,max,med,col,unita,dec)=>({
  tooltip:{trigger:'axis',valueFormatter:v=>v==null?'—':v.toFixed(dec)+' '+unita},
  legend:{data:['minima','massima','media']},
  xAxis:{type:'category',data:gg,axisLabel:{color:'#8e8e96',rotate:gg.length>12?45:0}},
  yAxis:{type:'value',scale:true,axisLabel:{formatter:v=>v.toFixed(dec)+unita}},
  series:[
   {name:'minima',type:'line',data:righe.map(r=>r[min]),lineStyle:{opacity:0},
    stack:'b',symbol:'none',areaStyle:{color:'transparent'}},
   {name:'massima',type:'line',data:righe.map(r=>r[max]!=null&&r[min]!=null?+(r[max]-r[min]).toFixed(3):null),
    lineStyle:{opacity:0},stack:'b',symbol:'none',
    areaStyle:{color:col,opacity:.18},tooltip:{show:false}},
   {name:'media',type:'line',data:righe.map(r=>r[med]),smooth:true,
    lineStyle:{color:col,width:2},itemStyle:{color:col},
    symbol:(val,p)=>{const r=righe[p.dataIndex];
      return (sog>0&&r&&r.compl!=null&&r.compl<sog)?'circle':'none';},
    symbolSize:9}]});
 nuovoGrafico($('g1'),banda('tmin','tmax','tmed',COL_V[0],'°C',1));
 nuovoGrafico($('g2'),{
  tooltip:{trigger:'axis',valueFormatter:v=>v==null?'—':v.toFixed(1)+' %'},
  xAxis:{type:'category',data:gg,axisLabel:{color:'#8e8e96',rotate:gg.length>12?45:0}},
  yAxis:{type:'value',max:v=>Math.max(110,v.max)},
  series:[{type:'bar',data:righe.map(r=>({value:r.compl,
    itemStyle:{color:r.compl==null?'#444':(r.compl<50?'#e05252':(r.compl<sog?'#e5a13a':'#3fb950'))}})),
   markLine:{silent:true,symbol:'none',lineStyle:{color:'#3fb950',type:'dashed'},
    data:[{yAxis:100}]}}]});
 nuovoGrafico($('g3'),{
  tooltip:{trigger:'axis'},legend:{data:['media','variazione 24 h']},
  xAxis:{type:'category',data:gg,axisLabel:{color:'#8e8e96',rotate:gg.length>12?45:0}},
  yAxis:[{type:'value',scale:true,axisLabel:{formatter:v=>v.toFixed(0)}},
         {type:'value',scale:true,axisLabel:{formatter:v=>(v>0?'+':'')+v.toFixed(1)},splitLine:{show:false}}],
  series:[{name:'media',type:'line',data:righe.map(r=>r.pmed),smooth:true,
    lineStyle:{color:COL_V[2],width:2},itemStyle:{color:COL_V[2]},symbol:'none'},
   {name:'variazione 24 h',type:'bar',yAxisIndex:1,data:righe.map(r=>({value:r.pvar,
     itemStyle:{color:(r.pvar||0)>=0?'#3fb950':'#e05252'}}))}]});
 nuovoGrafico($('g4'),{
  tooltip:{trigger:'axis'},legend:{data:['umidità media','rugiada media']},
  xAxis:{type:'category',data:gg,axisLabel:{color:'#8e8e96',rotate:gg.length>12?45:0}},
  yAxis:[{type:'value',scale:true,axisLabel:{formatter:v=>v.toFixed(0)+'%'}},
         {type:'value',scale:true,axisLabel:{formatter:v=>v.toFixed(0)+'°'},splitLine:{show:false}}],
  series:[{name:'umidità media',type:'line',data:righe.map(r=>r.hmed),smooth:true,
    lineStyle:{color:COL_V[1],width:2},itemStyle:{color:COL_V[1]},symbol:'none'},
   {name:'rugiada media',type:'line',yAxisIndex:1,data:righe.map(r=>r.tdmed),smooth:true,
    lineStyle:{color:'#8b949e',width:2,type:'dashed'},itemStyle:{color:'#8b949e'},symbol:'none'}]});
}

async function vistaSerie(){
 const v=parseInt($('sV').value,10);
 avviso('lettura dei CSV sulla card&hellip;','info');
 try{
  const S=await serie($('nodo').value,$('sDa').value,$('sA').value,v,parseInt($('sP').value,10));
  $('msg').innerHTML='';
  const vuoti=S.s.filter(c=>!c).length;
  $('cont').innerHTML='<div class="card"><h2>'+NOMI_V[v]+' &mdash; dal '+S.da+' al '+S.a+
   '</h2><div id="g1" class="g alto"></div><div class="lg">'+S.righe_lette+
   ' campioni letti dai CSV, aggregati a bordo in '+S.punti+' punti da '+
   Math.round(S.passo/60)+' min'+(vuoti?' &middot; <b>'+vuoti+'</b> senza dati: la linea si interrompe':'')+
   (EC?' &middot; <b>rotella</b> per lo zoom, trascina per spostarti':'')+'</div></div>'+
   statSerie(S,v);
  if(!EC){ $('g1').innerHTML=grafSerieSvg(S,COL_V[v]); return; }
  const t=S.s.map((c,i)=>istante(S,i));
  nuovoGrafico($('g1'),{
   tooltip:{trigger:'axis',valueFormatter:x=>x==null?'—':x.toFixed(DEC_V[v])+' '+UNI_V[v]},
   legend:{data:['media','minimo','massimo']},
   dataZoom:[{type:'inside'},{type:'slider',bottom:8,height:18,
     borderColor:'#2e2e33',fillerColor:'rgba(57,135,229,.15)',
     textStyle:{color:'#8e8e96'}}],
   grid:{left:56,right:18,top:30,bottom:70},
   xAxis:{type:'time',axisLabel:{color:'#8e8e96'}},
   yAxis:{type:'value',scale:true,axisLabel:{formatter:x=>x.toFixed(DEC_V[v])}},
   series:[
    {name:'minimo',type:'line',symbol:'none',data:S.s.map((c,i)=>[t[i],c?c[1]:null]),
     lineStyle:{opacity:0},stack:'banda',areaStyle:{color:'transparent'},tooltip:{show:false}},
    {name:'massimo',type:'line',symbol:'none',tooltip:{show:false},
     data:S.s.map((c,i)=>[t[i],c?+(c[2]-c[1]).toFixed(3):null]),
     lineStyle:{opacity:0},stack:'banda',areaStyle:{color:COL_V[v],opacity:.16}},
    {name:'media',type:'line',symbol:'none',connectNulls:false,
     data:S.s.map((c,i)=>[t[i],c?c[0]:null]),lineStyle:{color:COL_V[v],width:1.8}}]});
 }catch(e){ avviso('Non riesco a leggere la serie: '+esc(e.message),'err'); }
}

async function vistaConfronto(){
 const v=parseInt($('cV').value,10);
 const gg=[$('cA').value,$('cB').value,$('cC').value].filter((g,i,a)=>g&&g!='(nessuno)'&&a.indexOf(g)==i);
 if(gg.length<2){ avviso('Scegli almeno due giorni diversi.','err'); return; }
 avviso('lettura dei CSV sulla card&hellip;','info');
 try{
  const S=[];
  for(const g of gg) S.push(await serie($('nodo').value,g,g,v,96));
  $('msg').innerHTML='';
  $('cont').innerHTML='<div class="card"><h2>'+NOMI_V[v]+' &mdash; '+gg.join(' contro ')+
   '</h2><div id="g1" class="g alto"></div><div class="lg">asse orizzontale: '+
   '<b>ora del giorno</b>. Serve a confrontare la FORMA delle giornate, non gli istanti.</div></div>'+
   confrontoTabella(gg);
  if(!EC){ $('g1').innerHTML=grafConfrontoSvg(S,COL_V[v]); return; }
  const ore=S[0].s.map((c,i)=>hhmm(istante(S[0],i)));
  const tratti=['solid','dashed','dotted'];
  nuovoGrafico($('g1'),{
   tooltip:{trigger:'axis',valueFormatter:x=>x==null?'—':x.toFixed(DEC_V[v])+' '+UNI_V[v]},
   legend:{data:gg},
   xAxis:{type:'category',data:ore,axisLabel:{color:'#8e8e96',interval:11}},
   yAxis:{type:'value',scale:true,axisLabel:{formatter:x=>x.toFixed(DEC_V[v])}},
   series:S.map((s,i)=>({name:gg[i],type:'line',symbol:'none',connectNulls:false,
    data:s.s.map(c=>c?c[0]:null),
    lineStyle:{color:COL_V[v],width:1.9,type:tratti[i%3],opacity:1-i*0.22}}))});
 }catch(e){ avviso('Non riesco a confrontare: '+esc(e.message),'err'); }
}

async function vistaNodi(){
 const v=parseInt($('nV').value,10);
 if(NODI.length<2){ avviso('C&rsquo;&egrave; un nodo solo: non c&rsquo;&egrave; niente da confrontare.','err'); return; }
 avviso('lettura dei CSV di tutti i nodi&hellip;','info');
 try{
  const S=[];
  for(const n of NODI) S.push({nome:n,d:await serie(n,$('nDa').value,$('nA').value,v,400)});
  $('msg').innerHTML='';
  $('cont').innerHTML='<div class="card"><h2>'+NOMI_V[v]+' &mdash; i nodi a confronto</h2>'+
   '<div id="g1" class="g alto"></div><div class="lg">stessa finestra per tutti. '+
   'La differenza fra due nodi &egrave; la differenza fra due <b>posti</b>, '+
   'non fra due sensori: la taratura &egrave; stata verificata.</div></div>'+
   scartoNodi(S,v);
  if(!EC){ $('g1').innerHTML='<p class="lg">questa vista vuole la libreria dei grafici.</p>'; return; }
  const col=['#f0883e','#58a6ff','#3fb950','#e5a13a','#e05252','#a371f7','#79c0ff','#8b949e'];
  nuovoGrafico($('g1'),{
   tooltip:{trigger:'axis',valueFormatter:x=>x==null?'—':x.toFixed(DEC_V[v])+' '+UNI_V[v]},
   legend:{data:NODI},
   dataZoom:[{type:'inside'},{type:'slider',bottom:8,height:18,borderColor:'#2e2e33',
     fillerColor:'rgba(57,135,229,.15)',textStyle:{color:'#8e8e96'}}],
   grid:{left:56,right:18,top:30,bottom:70},
   xAxis:{type:'time',axisLabel:{color:'#8e8e96'}},
   yAxis:{type:'value',scale:true,axisLabel:{formatter:x=>x.toFixed(DEC_V[v])}},
   series:S.map((s,i)=>({name:s.nome,type:'line',symbol:'none',connectNulls:false,
    data:s.d.s.map((c,j)=>[istante(s.d,j),c?c[0]:null]),
    lineStyle:{color:col[i%col.length],width:1.7}}))});
 }catch(e){ avviso('Non riesco a confrontare i nodi: '+esc(e.message),'err'); }
}

async function vistaMappa(){
 const v=parseInt($('mV').value,10);
 avviso('lettura dei CSV sulla card&hellip;','info');
 try{
  // Un cesto per ORA: la mappa vuole esattamente quella griglia.
  const gg=giorniTra($('mDa').value,$('mA').value);
  const S=await serie($('nodo').value,$('mDa').value,$('mA').value,v,Math.min(1000,gg.length*24));
  $('msg').innerHTML='';
  $('cont').innerHTML='<div class="card"><h2>'+NOMI_V[v]+' ora per ora</h2>'+
   '<div id="g1" class="g alto"></div><div class="lg">ogni cella &egrave; un&rsquo;ora. '+
   'Le <b>colonne</b> mostrano il ciclo giorno/notte, le <b>righe</b> come cambia da un giorno all&rsquo;altro. '+
   'Le celle vuote sono ore senza dati.</div></div>';
  if(!EC){ $('g1').innerHTML='<p class="lg">questa vista vuole la libreria dei grafici.</p>'; return; }
  const dati=[]; let lo=Infinity,hi=-Infinity;
  S.s.forEach((c,i)=>{ if(!c)return;
   const d=istante(S,i), gi=gg.indexOf(d.getFullYear()+'-'+
    String(d.getMonth()+1).padStart(2,'0')+'-'+String(d.getDate()).padStart(2,'0'));
   if(gi<0) return;
   dati.push([d.getHours(),gi,+c[0].toFixed(DEC_V[v])]);
   lo=Math.min(lo,c[0]); hi=Math.max(hi,c[0]);
  });
  nuovoGrafico($('g1'),{
   tooltip:{position:'top',formatter:p=>gg[p.value[1]]+' ore '+String(p.value[0]).padStart(2,'0')+
     '<br><b>'+p.value[2]+' '+UNI_V[v]+'</b>'},
   grid:{left:92,right:18,top:14,bottom:60},
   xAxis:{type:'category',data:[...Array(24).keys()].map(h=>String(h).padStart(2,'0')),
    splitArea:{show:true},axisLabel:{color:'#8e8e96'}},
   yAxis:{type:'category',data:gg,splitArea:{show:true},axisLabel:{color:'#8e8e96'}},
   visualMap:{min:isFinite(lo)?+lo.toFixed(1):0,max:isFinite(hi)?+hi.toFixed(1):1,
    calculable:true,orient:'horizontal',left:'center',bottom:8,
    textStyle:{color:'#8e8e96'},
    inRange:{color:['#2c3e7a','#3987e5','#3fb950','#e5a13a','#f0883e','#e05252']}},
   series:[{type:'heatmap',data:dati,
    emphasis:{itemStyle:{borderColor:'#ececee',borderWidth:1}}}]});
 }catch(e){ avviso('Non riesco a disegnare la mappa: '+esc(e.message),'err'); }
}

async function vistaDistr(){
 const v=parseInt($('dV').value,10);
 avviso('lettura dei CSV sulla card&hellip;','info');
 try{
  const S=await serie($('nodo').value,$('dDa').value,$('dA').value,v,1000);
  $('msg').innerHTML='';
  const vals=S.s.filter(c=>c).map(c=>c[0]);
  if(!vals.length){ avviso('Nessun dato in questo intervallo.','err'); return; }
  vals.sort((a,b)=>a-b);
  const q=p=>vals[Math.min(vals.length-1,Math.floor(p*vals.length))];
  const media=vals.reduce((a,b)=>a+b,0)/vals.length;
  const sd=Math.sqrt(vals.reduce((s,x)=>s+(x-media)*(x-media),0)/vals.length);
  const d=DEC_V[v], u=UNI_V[v];
  const qd=(k,val,det)=>'<div class="rq"><div class="k">'+k+'</div><div class="v">'+
   val.toFixed(d)+u+'</div><div class="d">'+det+'</div></div>';
  $('cont').innerHTML='<div class="card"><h2>Come si distribuisce</h2><div class="rec">'+
   qd('mediana',q(.5),'met&agrave; del tempo sotto')+
   qd('media',media,'scarto tipico '+sd.toFixed(d)+u)+
   qd('10&deg; percentile',q(.1),'sotto per il 10% del tempo')+
   qd('90&deg; percentile',q(.9),'sopra per il 10% del tempo')+
   '</div></div>'+
   '<div class="card"><h2>Quanto tempo a ogni valore</h2><div id="g1" class="g"></div>'+
   '<div class="lg">quante ore la '+NOMI_V[v]+' &egrave; stata in ciascun intervallo, '+
   'su '+vals.length+' punti da '+Math.round(S.passo/60)+' min.</div></div>';
  if(!EC){ $('g1').innerHTML='<p class="lg">questa vista vuole la libreria dei grafici.</p>'; return; }
  const lo=vals[0], hi=vals[vals.length-1];
  const nb=Math.min(30,Math.max(8,Math.round(Math.sqrt(vals.length))));
  const w=(hi-lo)/nb||1, bins=new Array(nb).fill(0);
  vals.forEach(x=>{ bins[Math.min(nb-1,Math.floor((x-lo)/w))]++; });
  const ore=S.passo/3600;
  nuovoGrafico($('g1'),{
   tooltip:{trigger:'axis',valueFormatter:x=>x.toFixed(1)+' h'},
   xAxis:{type:'category',data:bins.map((_,i)=>(lo+i*w).toFixed(d)),
    axisLabel:{color:'#8e8e96',interval:Math.ceil(nb/12)}},
   yAxis:{type:'value',axisLabel:{formatter:x=>x.toFixed(0)+' h'}},
   series:[{type:'bar',data:bins.map(n=>+(n*ore).toFixed(2)),
    itemStyle:{color:COL_V[v]},barCategoryGap:'8%'}]});
 }catch(e){ avviso('Non riesco a calcolare: '+esc(e.message),'err'); }
}

// --- pezzi condivisi -------------------------------------------------
function giorniTra(da,a){
 const out=[]; const d=new Date(da+'T12:00:00'), f=new Date(a+'T12:00:00');
 while(d<=f){ out.push(d.getFullYear()+'-'+String(d.getMonth()+1).padStart(2,'0')+'-'+
   String(d.getDate()).padStart(2,'0')); d.setDate(d.getDate()+1); }
 return out;
}
function records(righe){
 const con=righe.filter(r=>r.tmax!=null);
 if(!con.length) return '';
 const caldo=con.reduce((a,b)=>b.tmax>a.tmax?b:a);
 const freddo=righe.filter(r=>r.tmin!=null).reduce((a,b)=>b.tmin<a.tmin?b:a);
 const esc2=righe.filter(r=>r.tmin!=null&&r.tmax!=null).reduce((a,b)=>(b.tmax-b.tmin)>(a.tmax-a.tmin)?b:a);
 const gg=righe.filter(r=>r.tmed!=null);
 const media=gg.reduce((s,r)=>s+r.tmed,0)/gg.length;
 const buchi=righe.reduce((s,r)=>s+(r.buchi||0),0);
 const q=(k,v,d)=>'<div class="rq"><div class="k">'+k+'</div><div class="v">'+v+'</div><div class="d">'+d+'</div></div>';
 return '<div class="rec">'+
  q('pi&ugrave; caldo',num(caldo.tmax,1)+'&deg;',caldo.giorno+(caldo.tmaxOra?' alle '+caldo.tmaxOra:''))+
  q('pi&ugrave; freddo',num(freddo.tmin,1)+'&deg;',freddo.giorno+(freddo.tminOra?' alle '+freddo.tminOra:''))+
  q('escursione max',num(esc2.tmax-esc2.tmin,1)+'&deg;',esc2.giorno)+
  q('media',num(media,1)+'&deg;',gg.length+' giorni')+
  q('pacchetti persi',buchi,'sulla tratta radio')+
 '</div>';
}
function statSerie(S,v){
 const vals=S.s.filter(c=>c);
 if(!vals.length) return '';
 const med=vals.reduce((a,c)=>a+c[0],0)/vals.length;
 const mn=Math.min(...vals.map(c=>c[1])), mx=Math.max(...vals.map(c=>c[2]));
 const d=DEC_V[v], u=UNI_V[v];
 const q=(k,x,det)=>'<div class="rq"><div class="k">'+k+'</div><div class="v">'+x+'</div><div class="d">'+det+'</div></div>';
 return '<div class="card"><h2>Nel periodo scelto</h2><div class="rec">'+
  q('minimo',mn.toFixed(d)+u,'')+q('massimo',mx.toFixed(d)+u,'')+
  q('media',med.toFixed(d)+u,vals.length+' punti')+
  q('escursione',(mx-mn).toFixed(d)+u,'')+'</div></div>';
}
function scartoNodi(S,v){
 if(S.length<2) return '';
 const d=DEC_V[v], u=UNI_V[v];
 let o='<div class="card"><h2>Scarto medio fra i nodi</h2><div class="tw"><table><thead><tr><th></th>'+
  S.map(s=>'<th>'+esc(s.nome)+'</th>').join('')+'</tr></thead><tbody>';
 const med=s=>{const v2=s.d.s.filter(c=>c).map(c=>c[0]);return v2.length?v2.reduce((a,b)=>a+b,0)/v2.length:NaN;};
 const m=S.map(med);
 S.forEach((a,i)=>{
  o+='<tr><td>'+esc(a.nome)+'</td>'+S.map((b,j)=>{
   if(i==j) return '<td>&mdash;</td>';
   const dd=m[i]-m[j];
   return '<td>'+(isFinite(dd)?(dd>=0?'+':'')+dd.toFixed(d):'&mdash;')+'</td>';
  }).join('')+'</tr>';
 });
 return o+'</tbody></table></div><div class="lg">differenza fra le medie del periodo, in '+u+
  '. Positivo = la riga &egrave; pi&ugrave; alta della colonna.</div></div>';
}
function confrontoTabella(gg){
 const righe=DATI[$('nodo').value]||[];
 const R=gg.map(g=>righe.find(r=>r.giorno==g)).filter(Boolean);
 if(R.length<2) return '';
 const r=(k,et,d,u)=>{
  if(R.some(x=>x[k]==null)) return '';
  return '<tr><td>'+et+'</td>'+R.map(x=>'<td>'+x[k].toFixed(d)+u+'</td>').join('')+'</tr>';
 };
 return '<div class="card"><h2>I giorni a confronto</h2><div class="tw"><table><thead><tr><th></th>'+
  R.map(x=>'<th>'+x.giorno+'</th>').join('')+'</tr></thead><tbody>'+
  r('tmin','minima',1,'&deg;')+r('tmax','massima',1,'&deg;')+r('tmed','media',1,'&deg;')+
  r('hmed','umidit&agrave; media',0,'%')+r('pmed','pressione media',1,'')+
  r('tdmed','rugiada media',1,'&deg;')+r('compl','completezza',1,'%')+
  '</tbody></table></div><div class="lg">dai riepiloghi gi&agrave; caricati: '+
  'nessuna richiesta in pi&ugrave; alla scheda.</div></div>';
}
function tabella(righe,sog){
 let o='<div class="tw"><table><thead><tr><th>giorno</th><th>compl.</th><th>cad.</th>'+
  '<th>T min</th><th>ora</th><th>T max</th><th>ora</th><th>T med</th><th>RH med</th>'+
  '<th>P med</th><th>&Delta;P 24h</th><th>rugiada</th><th>buchi</th></tr></thead><tbody>';
 for(let i=righe.length-1;i>=0;i--){
  const r=righe[i];
  const parz=sog>0&&r.compl!=null&&r.compl<sog;
  o+='<tr class="'+(parz?'parz':'')+'"><td>'+r.giorno+'</td>'+
   '<td>'+(r.compl==null?'&mdash;':r.compl.toFixed(1)+'%')+'</td>'+
   '<td>'+(r.cadenza?r.cadenza+'s':'&mdash;')+'</td>'+
   '<td>'+num(r.tmin,1)+'</td><td>'+(r.tminOra||'&mdash;')+'</td>'+
   '<td>'+num(r.tmax,1)+'</td><td>'+(r.tmaxOra||'&mdash;')+'</td>'+
   '<td>'+num(r.tmed,1)+'</td><td>'+num(r.hmed,0)+'</td><td>'+num(r.pmed,1)+'</td>'+
   '<td>'+(r.pvar==null?'&mdash;':(r.pvar>=0?'+':'')+r.pvar.toFixed(1))+'</td>'+
   '<td>'+num(r.tdmed,1)+'</td><td>'+(r.buchi==null?'&mdash;':r.buchi)+'</td></tr>';
 }
 return o+'</tbody></table></div>';
}

// =====================================================================
//  RISERVA: i grafici SVG disegnati a mano. Restano qui perche' la pagina
//  deve funzionare anche su una LAN senza internet, dove la libreria non
//  arriva. Sono gli stessi che c'erano prima della v57.
// =====================================================================
const W=920,H=200,ML=44,MR=12,MT=12,MB=26;
function scalaSvg(vs){
 const v=vs.filter(x=>x!=null&&isFinite(x));
 if(!v.length) return null;
 let lo=Math.min(...v),hi=Math.max(...v);
 if(hi-lo<0.5){const m=(hi+lo)/2;lo=m-0.5;hi=m+0.5;}
 const p=(hi-lo)*0.08; return {lo:lo-p,hi:hi+p};
}
const pxS=(i,n)=>ML+(n<2?(W-ML-MR)/2:i*(W-ML-MR)/(n-1));
const pyS=(v,s,h)=>MT+((h||H)-MT-MB)*(1-(v-s.lo)/(s.hi-s.lo));
function assiSvg(s,n,et,fmt,h){
 let o='';
 for(let k=0;k<=4;k++){
  const v=s.lo+(s.hi-s.lo)*k/4, y=pyS(v,s,h);
  o+='<line x1="'+ML+'" y1="'+y.toFixed(1)+'" x2="'+(W-MR)+'" y2="'+y.toFixed(1)+
     '" stroke="#26262b"/><text x="'+(ML-6)+'" y="'+(y+3.5).toFixed(1)+
     '" fill="#8e8e96" font-size="10" text-anchor="end">'+fmt(v)+'</text>';
 }
 const passo=Math.max(1,Math.ceil(n/8));
 for(let i=0;i<n;i+=passo)
  o+='<text x="'+pxS(i,n).toFixed(1)+'" y="'+((h||H)-8)+'" fill="#8e8e96" font-size="10" '+
     'text-anchor="middle">'+et(i)+'</text>';
 return o;
}
function disegnaGiorniSvg(righe,sog){
 const s=scalaSvg(righe.flatMap(r=>[r.tmin,r.tmax]));
 if(!s){ $('g1').innerHTML='<p class="lg">nessun dato</p>'; return; }
 const n=righe.length;
 let area='',linea='',parz='',giu=true;
 righe.forEach((r,i)=>{ if(r.tmax!=null) area+=(area?' L':'M')+pxS(i,n).toFixed(1)+' '+pyS(r.tmax,s).toFixed(1); });
 for(let i=n-1;i>=0;i--) if(righe[i].tmin!=null) area+=' L'+pxS(i,n).toFixed(1)+' '+pyS(righe[i].tmin,s).toFixed(1);
 if(area) area+=' Z';
 righe.forEach((r,i)=>{ if(r.tmed==null){giu=true;return;}
  linea+=(giu?'M':' L')+pxS(i,n).toFixed(1)+' '+pyS(r.tmed,s).toFixed(1); giu=false; });
 righe.forEach((r,i)=>{ if(sog>0&&r.compl!=null&&r.compl<sog&&r.tmed!=null)
  parz+='<circle cx="'+pxS(i,n).toFixed(1)+'" cy="'+pyS(r.tmed,s).toFixed(1)+
        '" r="3.5" fill="none" stroke="#e5a13a" stroke-width="1.6"/>'; });
 $('g1').innerHTML='<svg viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none">'+
  assiSvg(s,n,i=>righe[i].giorno.slice(5),v=>v.toFixed(1)+'°')+
  '<path d="'+area+'" fill="'+COL_V[0]+'" fill-opacity=".16"/>'+
  '<path d="'+linea+'" fill="none" stroke="'+COL_V[0]+'" stroke-width="2"/>'+parz+'</svg>';
 const base=72;
 let b='';
 righe.forEach((r,i)=>{ if(r.compl==null)return;
  const alt=Math.max(1,Math.min(r.compl,110)/110*base);
  const col=r.compl<50?'#e05252':(sog>0&&r.compl<sog?'#e5a13a':'#3fb950');
  const w=Math.max(2,(W-ML-MR)/n*0.7);
  b+='<rect x="'+(pxS(i,n)-w/2).toFixed(1)+'" y="'+(base-alt).toFixed(1)+'" width="'+w.toFixed(1)+
     '" height="'+alt.toFixed(1)+'" fill="'+col+'" fill-opacity=".8"/>'; });
 $('g2').innerHTML='<svg viewBox="0 0 '+W+' 90" preserveAspectRatio="none">'+b+'</svg>';
 $('g3').innerHTML='<p class="lg">senza la libreria questo grafico non viene disegnato.</p>';
 $('g4').innerHTML='<p class="lg">senza la libreria questo grafico non viene disegnato.</p>';
}
function grafSerieSvg(S,col){
 const h=230;
 const vals=[]; S.s.forEach(c=>{ if(c) vals.push(c[1],c[2]); });
 const s=scalaSvg(vals);
 if(!s) return '<p class="lg">nessun dato</p>';
 const n=S.s.length;
 let seg=[],bande='',linea='',giu=true;
 const chiudi=()=>{ if(seg.length<2){seg=[];return;}
  let a='M'+seg.map(q=>pxS(q.i,n).toFixed(1)+' '+pyS(q.max,s,h).toFixed(1)).join(' L');
  for(let k=seg.length-1;k>=0;k--) a+=' L'+pxS(seg[k].i,n).toFixed(1)+' '+pyS(seg[k].min,s,h).toFixed(1);
  bande+='<path d="'+a+' Z" fill="'+col+'" fill-opacity=".18"/>'; seg=[]; };
 S.s.forEach((c,i)=>{ if(c) seg.push({i:i,min:c[1],max:c[2]}); else chiudi(); });
 chiudi();
 S.s.forEach((c,i)=>{ if(!c){giu=true;return;}
  linea+=(giu?'M':' L')+pxS(i,n).toFixed(1)+' '+pyS(c[0],s,h).toFixed(1); giu=false; });
 return '<svg viewBox="0 0 '+W+' '+h+'" preserveAspectRatio="none">'+
  assiSvg(s,n,i=>hhmm(istante(S,i)),v=>v.toFixed(1),h)+bande+
  '<path d="'+linea+'" fill="none" stroke="'+col+'" stroke-width="1.8"/></svg>';
}
function grafConfrontoSvg(S,col){
 const h=230;
 const vals=[]; S.forEach(x=>x.s.forEach(c=>{ if(c) vals.push(c[1],c[2]); }));
 const s=scalaSvg(vals);
 if(!s) return '<p class="lg">nessun dato</p>';
 const n=Math.max(...S.map(x=>x.s.length));
 const tratti=['','5 4','2 3'];
 let o='';
 S.forEach((x,k)=>{ let d='',giu=true;
  x.s.forEach((c,i)=>{ if(!c){giu=true;return;}
   d+=(giu?'M':' L')+pxS(i,n).toFixed(1)+' '+pyS(c[0],s,h).toFixed(1); giu=false; });
  o+='<path d="'+d+'" fill="none" stroke="'+col+'" stroke-width="1.8"'+
     (tratti[k%3]?' stroke-dasharray="'+tratti[k%3]+'"':'')+'/>'; });
 return '<svg viewBox="0 0 '+W+' '+h+'" preserveAspectRatio="none">'+
  assiSvg(s,n,i=>hhmm(istante(S[0],i)),v=>v.toFixed(1),h)+o+'</svg>';
}

// =====================================================================
//  Schede e avvio
// =====================================================================
const BARRE={giorni:[],serie:['barraSerie'],conf:['barraConf'],
             nodi:['barraNodi'],mappa:['barraMappa'],distr:['barraDistr']};
function mostraVista(v){
 VISTA=v;
 pulisciGrafici();
 document.querySelectorAll('.tab').forEach(b=>b.className='tab'+(b.dataset.v==v?' on':''));
 Object.keys(BARRE).forEach(k=>BARRE[k].forEach(id=>$(id).hidden=(k!=v)));
 $('cPer').hidden = v!='giorni';
 $('cSog').hidden = v!='giorni';
 if(v=='giorni'){ vistaGiorni(); return; }
 const g=giorniPronti();
 if(!g){ $('cont').innerHTML=''; return; }
 const ult=g[g.length-1], quat=g[Math.max(0,g.length-4)], sett=g[Math.max(0,g.length-7)];
 if(v=='serie'){ riempiGrandezze('sV'); riempiSelect('sDa',g,quat); riempiSelect('sA',g,ult); vistaSerie(); }
 else if(v=='conf'){ riempiGrandezze('cV'); riempiSelect('cA',g,ult);
   riempiSelect('cB',g,g[g.length-2]||g[0]);
   riempiSelect('cC',['(nessuno)'].concat(g),'(nessuno)'); vistaConfronto(); }
 else if(v=='nodi'){ riempiGrandezze('nV'); riempiSelect('nDa',g,quat); riempiSelect('nA',g,ult); vistaNodi(); }
 else if(v=='mappa'){ riempiGrandezze('mV'); riempiSelect('mDa',g,sett); riempiSelect('mA',g,ult); vistaMappa(); }
 else if(v=='distr'){ riempiGrandezze('dV'); riempiSelect('dDa',g,sett); riempiSelect('dA',g,ult); vistaDistr(); }
}

async function carica(){
 avviso('lettura&hellip;','info');
 try{
  const st=await chiedi('/api/stato','json');
  $('fw').textContent='firmware '+st.fw+(EC?'':' · grafici essenziali (libreria non disponibile)');
  const nodi=await chiedi('/api/nodi','json');
  if(!nodi.nodi||!nodi.nodi.length){ avviso('nessun nodo associato all&rsquo;hub.','err'); return; }
  NODI=nodi.nodi.map(n=>n.nome);
  DATI={};
  riempiSelect('nodo',NODI,$('nodo').value||NODI[0]);
  const mancanti=[];
  for(const n of NODI){
   try{ DATI[n]=parse(await chiedi('/api/nodi/riepilogo?nodo='+encodeURIComponent(n),'text')); }
   catch(e){ DATI[n]=[]; mancanti.push(n); }
  }
  $('msg').innerHTML='';
  if(mancanti.length) avviso('Nessun riepilogo per: <b>'+mancanti.map(esc).join(', ')+
   '</b>. L&rsquo;hub lo scrive quando un giorno &egrave; chiuso.');
  // La vista ATTIVA, non per forza quella iniziale: se nel frattempo si e'
  // cambiata scheda, i suoi menu sono rimasti vuoti.
  mostraVista(VISTA);
 }catch(e){ avviso('Non riesco a leggere: '+esc(e.message),'err'); }
}

document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>mostraVista(b.dataset.v));
$('sGo').onclick=vistaSerie; $('cGo').onclick=vistaConfronto; $('nGo').onclick=vistaNodi;
$('mGo').onclick=vistaMappa; $('dGo').onclick=vistaDistr;
$('nodo').onchange=()=>mostraVista(VISTA);
$('per').onchange=vistaGiorni; $('sog').onchange=vistaGiorni;
$('ric').onclick=carica;

caricaLibreria().then(lib=>{
 EC=lib;
 if(!EC) avviso('La libreria dei grafici non &egrave; arrivata (serve internet sul dispositivo '+
  'che guarda la pagina, non sulla scheda). Uso i grafici essenziali: '+
  'alcune viste restano solo in tabella.','info');
 carica();
});
</script></div></body></html>

)ANALISIPAGE";
