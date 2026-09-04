#pragma once

// ============================================================
//  GENERATO DA www/gen_page.py - NON MODIFICARE A MANO.
//  La sorgente e' www/analisi.html: si modifica quella e si
//  rilancia  python www/gen_page.py analisi  prima di ricompilare.
//  (14284 byte di pagina, serviti su /analisi)
// ============================================================

static const char ANALISI_PAGE[] PROGMEM = R"ANALISIPAGE(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark"><title>MeteoHub-S3 &mdash; Analisi</title><style>
 :root{--bg:#0e0e10;--card:#1a1a1d;--bordo:#2e2e33;--txt:#ececee;--dim:#8e8e96;
  --acc:#3987e5;--ok:#3fb950;--warn:#e5a13a;--bad:#e05252;--cold:#58a6ff;--hot:#f0883e}
 *{box-sizing:border-box}
 body{font-family:system-ui,-apple-system,Segoe UI,Arial,sans-serif;background:var(--bg);
  color:var(--txt);margin:0;padding:14px}
 .wrap{max-width:980px;margin:0 auto}
 h1{font-size:1.15rem;margin:.2rem 0 .3rem}
 h2{font-size:.95rem;margin:0 0 .6rem;font-weight:600}
 .sub{color:var(--dim);font-size:.85rem;line-height:1.5;margin-bottom:1rem}
 .card{background:var(--card);border:1px solid var(--bordo);border-radius:12px;
  padding:13px 15px;margin-bottom:10px}
 .barra{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center;margin-bottom:1rem}
 select,button{background:#22222a;color:var(--txt);border:1px solid var(--bordo);
  border-radius:8px;padding:6px 10px;font-size:.85rem;font-family:inherit}
 button{cursor:pointer}
 button:hover{border-color:var(--acc)}
 .rec{display:flex;gap:.8rem;flex-wrap:wrap}
 .rq{flex:1 1 150px;background:#15151a;border:1px solid var(--bordo);border-radius:10px;padding:9px 11px}
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
 svg{display:block;width:100%;height:auto}
 .lg{color:var(--dim);font-size:.75rem;margin-top:.4rem;line-height:1.5}
 .lg i{font-style:normal;padding:0 .3rem}
 nav{margin:1.6rem 0 .5rem;display:flex;flex-wrap:wrap;gap:.4rem 1rem;font-size:.85rem}
 a{color:var(--acc);text-decoration:none}
</style></head><body><div class="wrap">
<h1>Analisi <span id="fw" class="sub"></span></h1>
<p class="sub">Legge <code>/api/nodi/riepilogo</code>, cio&egrave; <b>una riga per giorno
gi&agrave; chiuso</b> calcolata dall&rsquo;hub sui CSV: una richiesta per nodo invece di una
per giorno. I giorni <b>incompleti restano visibili e segnati</b> &mdash; un minimo
calcolato sul 40&nbsp;% dei campioni ha lo stesso aspetto di un minimo vero, e
nasconderlo sarebbe il modo pi&ugrave; comodo di sbagliare.</p>

<div class="barra">
 <select id="nodo"></select>
 <select id="per">
  <option value="0">tutti i giorni</option>
  <option value="7">ultimi 7</option>
  <option value="30">ultimi 30</option>
 </select>
 <select id="sog">
  <option value="90">segna sotto il 90%</option>
  <option value="99">segna sotto il 99%</option>
  <option value="0">non segnare niente</option>
 </select>
 <button id="ric">ricarica</button>
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
let DATI={};   // nodo -> righe

// Un errore che il server gestisce non e' un errore che l'utente vede: senza
// guardare r.ok il messaggio scritto con cura dalla scheda non arriva a
// nessuno, e il pulsante sembra semplicemente non fare niente.
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
 const col=rr[0].split(',');
 const idx={}; col.forEach((c,i)=>idx[c.trim()]=i);
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

// ---- disegno SVG, senza librerie: la scheda non ha internet e la pagina
// ---- dev'essere un file solo.
const W=920,H=200,ML=44,MR=12,MT=12,MB=26;
function scala(vs){
 const v=vs.filter(x=>x!=null&&isFinite(x));
 if(!v.length) return null;
 let lo=Math.min(...v),hi=Math.max(...v);
 if(hi-lo<0.5){const m=(hi+lo)/2;lo=m-0.5;hi=m+0.5;}
 const p=(hi-lo)*0.08; return {lo:lo-p,hi:hi+p};
}
const px=(i,n)=>ML+(n<2?(W-ML-MR)/2:i*(W-ML-MR)/(n-1));
const py=(v,s)=>MT+(H-MT-MB)*(1-(v-s.lo)/(s.hi-s.lo));

function assi(s,righe,fmt){
 let o='';
 for(let k=0;k<=4;k++){
  const v=s.lo+(s.hi-s.lo)*k/4, y=py(v,s);
  o+='<line x1="'+ML+'" y1="'+y.toFixed(1)+'" x2="'+(W-MR)+'" y2="'+y.toFixed(1)+
     '" stroke="#26262b" stroke-width="1"/>'+
     '<text x="'+(ML-6)+'" y="'+(y+3.5).toFixed(1)+'" fill="#8e8e96" font-size="10" text-anchor="end">'+
     fmt(v)+'</text>';
 }
 const n=righe.length, passo=Math.max(1,Math.ceil(n/8));
 for(let i=0;i<n;i+=passo){
  o+='<text x="'+px(i,n).toFixed(1)+'" y="'+(H-8)+'" fill="#8e8e96" font-size="10" text-anchor="middle">'+
     righe[i].giorno.slice(5)+'</text>';
 }
 return o;
}

// La banda min-max piu' la linea della media. I giorni incompleti non si
// tolgono e non si "aggiustano": si disegnano tratteggiati, cosi' si vede che
// ci sono e che valgono meno.
function grafBanda(righe,kmin,kmax,kmed,col,fmt,sogl){
 const s=scala(righe.flatMap(r=>[r[kmin],r[kmax]]));
 if(!s) return '<p class="lg">nessun dato</p>';
 const n=righe.length;
 let area='',linea='',parz='';
 righe.forEach((r,i)=>{ if(r[kmax]!=null) area+=(area?' L':'M')+px(i,n).toFixed(1)+' '+py(r[kmax],s).toFixed(1); });
 for(let i=n-1;i>=0;i--){ const r=righe[i]; if(r[kmin]!=null) area+=' L'+px(i,n).toFixed(1)+' '+py(r[kmin],s).toFixed(1); }
 if(area) area+=' Z';
 let giu=true;
 righe.forEach((r,i)=>{
  if(r[kmed]==null){giu=true;return;}
  linea+=(giu?'M':' L')+px(i,n).toFixed(1)+' '+py(r[kmed],s).toFixed(1); giu=false;
 });
 righe.forEach((r,i)=>{
  if(sogl>0&&r.compl!=null&&r.compl<sogl&&r[kmed]!=null)
   parz+='<circle cx="'+px(i,n).toFixed(1)+'" cy="'+py(r[kmed],s).toFixed(1)+
         '" r="3.5" fill="none" stroke="'+'var(--warn)'+'" stroke-width="1.6"/>';
 });
 return '<svg viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none">'+assi(s,righe,fmt)+
  '<path d="'+area+'" fill="'+col+'" fill-opacity=".16"/>'+
  '<path d="'+linea+'" fill="none" stroke="'+col+'" stroke-width="2"/>'+parz+'</svg>';
}

// Barre della completezza. E' il grafico che rende leggibili gli altri, quindi
// sta con loro e non in fondo come nota a pie' di pagina.
function grafCompl(righe,sogl){
 const n=righe.length, h=90, base=h-18;
 let o='';
 righe.forEach((r,i)=>{
  const c=r.compl; if(c==null) return;
  const alt=Math.max(1,Math.min(c,110)/110*base);
  const col=c<sogl?'var(--warn)':(c<50?'var(--bad)':'var(--ok)');
  const w=Math.max(2,(W-ML-MR)/n*0.7);
  o+='<rect x="'+(px(i,n)-w/2).toFixed(1)+'" y="'+(base-alt).toFixed(1)+'" width="'+w.toFixed(1)+
     '" height="'+alt.toFixed(1)+'" fill="'+col+'" fill-opacity=".8"><title>'+
     r.giorno+': '+c.toFixed(1)+'% ('+r.campioni+'/'+r.attesi+')</title></rect>';
 });
 const y100=base-100/110*base;
 o+='<line x1="'+ML+'" y1="'+y100.toFixed(1)+'" x2="'+(W-MR)+'" y2="'+y100.toFixed(1)+
    '" stroke="#3fb950" stroke-width="1" stroke-dasharray="4 3"/>'+
    '<text x="'+(ML-6)+'" y="'+(y100+3.5).toFixed(1)+'" fill="#8e8e96" font-size="10" text-anchor="end">100%</text>';
 return '<svg viewBox="0 0 '+W+' '+h+'" preserveAspectRatio="none">'+o+'</svg>';
}

function records(righe){
 const con=righe.filter(r=>r.tmax!=null);
 if(!con.length) return '';
 const caldo=con.reduce((a,b)=>b.tmax>a.tmax?b:a);
 const freddo=righe.filter(r=>r.tmin!=null).reduce((a,b)=>b.tmin<a.tmin?b:a);
 const esc2=righe.filter(r=>r.tmin!=null&&r.tmax!=null)
                 .reduce((a,b)=>(b.tmax-b.tmin)>(a.tmax-a.tmin)?b:a);
 const gg=righe.filter(r=>r.tmed!=null);
 const media=gg.reduce((s,r)=>s+r.tmed,0)/gg.length;
 const q=(k,v,d)=>'<div class="rq"><div class="k">'+k+'</div><div class="v">'+v+'</div><div class="d">'+d+'</div></div>';
 return '<div class="rec">'+
  q('pi&ugrave; caldo',num(caldo.tmax,1)+'&deg;',caldo.giorno+(caldo.tmaxOra?' alle '+caldo.tmaxOra:''))+
  q('pi&ugrave; freddo',num(freddo.tmin,1)+'&deg;',freddo.giorno+(freddo.tminOra?' alle '+freddo.tminOra:''))+
  q('escursione max',num(esc2.tmax-esc2.tmin,1)+'&deg;',esc2.giorno)+
  q('media del periodo',num(media,1)+'&deg;',gg.length+' giorni')+
 '</div>';
}

function tabella(righe,sogl){
 let o='<div class="tw"><table><thead><tr><th>giorno</th><th>compl.</th><th>cad.</th>'+
  '<th>T min</th><th>T max</th><th>T med</th><th>RH med</th><th>P med</th><th>&Delta;P 24h</th>'+
  '<th>rugiada</th><th>buchi</th></tr></thead><tbody>';
 for(let i=righe.length-1;i>=0;i--){
  const r=righe[i];
  const parz=sogl>0&&r.compl!=null&&r.compl<sogl;
  o+='<tr class="'+(parz?'parz':'')+'"><td>'+r.giorno+'</td>'+
   '<td>'+(r.compl==null?'&mdash;':r.compl.toFixed(1)+'%')+'</td>'+
   '<td>'+(r.cadenza?r.cadenza+'s':'&mdash;')+'</td>'+
   '<td>'+num(r.tmin,1)+'</td><td>'+num(r.tmax,1)+'</td><td>'+num(r.tmed,1)+'</td>'+
   '<td>'+num(r.hmed,0)+'</td><td>'+num(r.pmed,1)+'</td>'+
   '<td>'+(r.pvar==null?'&mdash;':(r.pvar>=0?'+':'')+r.pvar.toFixed(1))+'</td>'+
   '<td>'+num(r.tdmed,1)+'</td><td>'+(r.buchi==null?'&mdash;':r.buchi)+'</td></tr>';
 }
 return o+'</tbody></table></div>';
}

function disegna(){
 const nodo=$('nodo').value, per=parseInt($('per').value,10), sogl=parseInt($('sog').value,10);
 let righe=(DATI[nodo]||[]).slice();
 if(per>0) righe=righe.slice(-per);
 if(!righe.length){ $('cont').innerHTML='<div class="card">nessun giorno chiuso per questo nodo.</div>'; return; }

 const parziali=righe.filter(r=>sogl>0&&r.compl!=null&&r.compl<sogl);
 let avv='';
 if(parziali.length) avv='<div class="avv"><b>'+parziali.length+' giorn'+(parziali.length==1?'o':'i')+
   ' sotto il '+sogl+'% dei campioni attesi</b> ('+parziali.map(r=>r.giorno).join(', ')+
   '). Restano nei grafici, cerchiati, e in tabella in arancione: i loro minimi e massimi '+
   'sono calcolati su meno dati, quindi valgono meno &mdash; ma toglierli sarebbe peggio, '+
   'perch&eacute; sparirebbe anche l&rsquo;informazione che sono mancati.</div>';

 $('cont').innerHTML=avv+
  '<div class="card"><h2>Record del periodo</h2>'+records(righe)+'</div>'+
  '<div class="card"><h2>Temperatura &mdash; banda min/max e media</h2>'+
   grafBanda(righe,'tmin','tmax','tmed','var(--hot)',v=>v.toFixed(1)+'&deg;',sogl)+
   '<div class="lg">la banda &egrave; l&rsquo;escursione del giorno, la linea la media. '+
   '<i>&#9711;</i> giorno con meno campioni del previsto.</div></div>'+
  '<div class="card"><h2>Completezza dei dati</h2>'+grafCompl(righe,sogl)+
   '<div class="lg">campioni ricevuti sul totale atteso alla cadenza di quel giorno. '+
   'Sopra il 100&nbsp;% si pu&ograve; andare: la cadenza &egrave; stimata, e un secondo '+
   'di errore su 300 vale gi&agrave; un campione al giorno.</div></div>'+
  '<div class="card"><h2>Pressione &mdash; banda min/max e media</h2>'+
   grafBanda(righe,'pmin','pmax','pmed','var(--acc)',v=>v.toFixed(0),sogl)+
   '<div class="lg">grezza, come la trasmette il nodo: la correzione al livello del mare '+
   '&egrave; un offset costante e il trend non ne dipende.</div></div>'+
  '<div class="card"><h2>Umidit&agrave; e punto di rugiada</h2>'+
   grafBanda(righe,'hmin','hmax','hmed','var(--cold)',v=>v.toFixed(0)+'%',sogl)+
   '<div class="lg">la rugiada media in tabella &egrave; mediata campione per campione, '+
   'non calcolata dalle medie: la formula non &egrave; lineare.</div></div>'+
  '<div class="card"><h2>Tutti i giorni</h2>'+tabella(righe,sogl)+'</div>';
}

async function carica(){
 avviso('lettura dei riepiloghi&hellip;');
 try{
  const st=await chiedi('/api/stato','json');
  $('fw').textContent='firmware '+st.fw;
  const nodi=await chiedi('/api/nodi','json');
  if(!nodi.nodi||!nodi.nodi.length){ avviso('nessun nodo associato all&rsquo;hub.','err'); return; }
  DATI={};
  const sel=$('nodo'); const prima=sel.value;
  sel.innerHTML=nodi.nodi.map(n=>'<option>'+esc(n.nome)+'</option>').join('');
  if(prima) sel.value=prima;
  let mancanti=[];
  for(const n of nodi.nodi){
   try{ DATI[n.nome]=parse(await chiedi('/api/nodi/riepilogo?nodo='+encodeURIComponent(n.nome),'text')); }
   catch(e){ DATI[n.nome]=[]; mancanti.push(n.nome); }
  }
  $('msg').innerHTML='';
  if(mancanti.length) avviso('Nessun riepilogo per: <b>'+mancanti.map(esc).join(', ')+
   '</b>. L&rsquo;hub lo scrive quando un giorno &egrave; chiuso, un giorno per giro; '+
   'per rifarlo da capo c&rsquo;&egrave; <code>POST /api/nodi/riepilogo/rifai</code>.');
  disegna();
 }catch(e){ avviso('Non riesco a leggere: '+esc(e.message),'err'); }
}

$('nodo').onchange=disegna; $('per').onchange=disegna; $('sog').onchange=disegna;
$('ric').onclick=carica;
carica();
</script></div></body></html>

)ANALISIPAGE";
