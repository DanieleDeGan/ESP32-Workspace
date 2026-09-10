#pragma once

// ============================================================
//  GENERATO DA www/gen_page.py - NON MODIFICARE A MANO.
//  La sorgente e' www/batteria.html: si modifica quella e si
//  rilancia  python www/gen_page.py batteria  prima di ricompilare.
//  (26614 byte di pagina, serviti su /batteria)
// ============================================================

static const char BATTERIA_PAGE[] PROGMEM = R"BATTERIAPAGE(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark"><title>MeteoHub-S3 &mdash; Batteria</title><style>
 :root{--bg:#0e0e10;--card:#1a1a1d;--bordo:#2e2e33;--txt:#ececee;--dim:#8e8e96;
  --acc:#3987e5;--ok:#3fb950;--warn:#e5a13a;--bad:#e05252;--cell:#7ee787}
 *{box-sizing:border-box}
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
 button{cursor:pointer} button:hover{border-color:var(--acc)}
 label{color:var(--dim);font-size:.85rem;display:flex;align-items:center;gap:.35rem}
 .rec{display:flex;gap:.8rem;flex-wrap:wrap}
 .rq{flex:1 1 150px;background:#15151a;border:1px solid var(--bordo);border-radius:10px;padding:9px 11px}
 .rq .k{color:var(--dim);font-size:.72rem;text-transform:uppercase;letter-spacing:.04em}
 .rq .v{font-size:1.25rem;font-weight:650;margin-top:2px}
 .rq .d{color:var(--dim);font-size:.75rem;margin-top:1px;line-height:1.35}
 table{border-collapse:collapse;width:100%;font-size:.8rem}
 th,td{padding:4px 7px;text-align:right;border-bottom:1px solid #26262b;white-space:nowrap}
 th{color:var(--dim);font-weight:600;position:sticky;top:0;background:var(--card)}
 td:first-child,th:first-child{text-align:left}
 .tw{overflow-x:auto;max-height:420px;overflow-y:auto}
 tr.parz td{color:var(--warn)}
 tr.ric td{color:var(--acc)}
 .avv{background:#3a2a1e;border:1px solid #5a4128;color:#e5a13a;border-radius:10px;
  padding:9px 12px;font-size:.82rem;line-height:1.5;margin-bottom:10px}
 .err{background:#3a1e1e;border-color:#5a2828;color:#e08a8a}
 .info{background:#1a2a33;border-color:#284a5a;color:#79c0ff}
 svg{display:block;width:100%;height:auto}
 .lg{color:var(--dim);font-size:.75rem;margin-top:.4rem;line-height:1.6}
 .lg b{color:var(--txt);font-weight:600}
 nav{margin:1.6rem 0 .5rem;display:flex;flex-wrap:wrap;gap:.4rem 1rem;font-size:.85rem}
 a{color:var(--acc);text-decoration:none}
 .tac{display:inline-flex;gap:2px;vertical-align:middle;margin-left:.4rem}
 .tac i{width:9px;height:16px;border-radius:2px;background:#2e2e33;display:inline-block}
 .tac i.on{background:var(--cell)}
 .tac i.on.bassa{background:var(--warn)}
 .tac i.on.critica{background:var(--bad)}
 .note{font-size:.82rem;line-height:1.65;color:var(--dim)}
 .note b{color:var(--txt)}
 .note li{margin-bottom:.35rem}
</style></head><body><div class="wrap">
<h1>Batteria <span id="fw" class="sub"></span></h1>
<p class="sub">Tutto quello che l&rsquo;hub sa della cella dei nodi che vanno a batteria.
I numeri vengono da tre posti diversi: l&rsquo;<b>ultimo pacchetto</b> (<code>/api/nodi</code>),
il <b>riepilogo dei giorni chiusi</b> (<code>/api/nodi/riepilogo</code>, colonne
<code>b_primo_mv</code>, <code>b_ultimo_mv</code>, <code>b_min_mv</code>) e la
<b>serie decimata a bordo</b> (<code>/api/nodi/serie</code> con <code>v=3</code>). Un campo vuoto
resta vuoto: la colonna della batteria &egrave; nata il <b>9 settembre 2026</b>, quando il
partitore &egrave; stato cablato, e prima non c&rsquo;&egrave; nessuna misura da mostrare.</p>

<div id="msg"></div>

<div class="barra">
 <label>nodo <select id="nodo"></select></label>
 <label>curva <select id="giorni">
   <option value="2">ultimi 2 giorni</option>
   <option value="7" selected>ultimi 7 giorni</option>
   <option value="14">ultimi 14 giorni</option>
 </select></label>
 <button id="bRic">ricarica</button>
 <span class="sub" id="agg" style="margin:0"></span>
</div>

<section class="card">
 <h2>Adesso <span class="sub" id="qAdesso" style="margin:0"></span></h2>
 <div class="rec" id="adesso"><div class="sub">lettura&hellip;</div></div>
</section>

<section class="card">
 <h2>La giornata in corso <span class="sub" id="qOggi" style="margin:0"></span></h2>
 <div class="rec" id="oggi"><div class="sub">lettura&hellip;</div></div>
</section>

<section class="card">
 <h2>Quanto dura</h2>
 <div class="rec" id="durata"><div class="sub">lettura&hellip;</div></div>
 <div class="lg" id="durataNota"></div>
</section>

<section class="card">
 <h2>La curva della tensione <span class="sub" id="qCurva" style="margin:0"></span></h2>
 <div id="graf"><div class="sub">lettura&hellip;</div></div>
 <div class="lg" id="grafLg"></div>
</section>

<section class="card">
 <h2>Giorno per giorno <span class="sub" id="qTab" style="margin:0"></span></h2>
 <div class="tw"><table id="tab">
  <thead><tr><th>giorno</th><th>completezza</th><th>prima</th><th>ultima</th>
   <th>consumo</th><th>minimo</th><th>tuffo</th></tr></thead>
  <tbody></tbody></table></div>
 <div class="lg">La colonna <b>consumo</b> &egrave; <i>prima &minus; ultima</i> della giornata.
  Il <b>tuffo</b> &egrave; quanto il minimo sta sotto l&rsquo;ultima lettura: &egrave; la caduta
  sotto carico, e se cresce nel tempo dice che la resistenza interna della cella sta
  salendo &mdash; l&rsquo;unico modo che abbiamo di vedere l&rsquo;invecchiamento.
  Le righe <span style="color:var(--warn)">arancioni</span> sono giorni incompleti,
  quelle <span style="color:var(--acc)">azzurre</span> giorni in cui la tensione &egrave;
  <i>salita</i>: una ricarica, non un consumo, e restano fuori dalle medie.</div>
</section>

<section class="card">
 <h2>Come si misura, e cosa non dice</h2>
 <ul class="note">
  <li><b>La tensione non &egrave; una percentuale.</b> Fra 3,9 e 3,7 V la curva di scarica di
   una 18650 &egrave; quasi piatta: 150 mV coprono circa un quinto della capacit&agrave;, e lo
   stesso &laquo;78 %&raquo; pu&ograve; durare ore o settimane. Per questo qui non c&rsquo;&egrave;
   nessuna percentuale, e le tacche sono cinque &mdash; le stesse del pannello, con le
   <b>stesse soglie</b>, che arrivano dall&rsquo;hub (<code>batteria_soglie_mv</code>) invece
   di essere ricopiate qui dentro.</li>
  <li><b>Il partitore &egrave; cablato dal 9 settembre 2026</b> sulla XIAO C3, con rapporto
   misurato al multimetro (1,996, non 2,000) e 100 nF verso massa; la lettura &egrave; la
   media di otto conversioni. Il nodo a muro non ne ha uno e non manda niente: la sua
   colonna resta vuota, che &egrave; diverso da zero.</li>
  <li><b>Con l&rsquo;USB attaccata si legge il caricatore, non la cella</b>: i valori presi
   mentre il nodo era al PC stanno sopra il vero e non vanno confrontati con gli altri.</li>
  <li><b>Il limite della misura non &egrave; il partitore</b>, &egrave; la taratura di fabbrica
   dell&rsquo;ADC (&plusmn;3 % tipici, ~60 mV). Il confronto col multimetro del 9 settembre ha
   dato 4,03 V letti contro 4,04 V misurati, molto meglio del tipico &mdash; ma resta un
   offset costante, e qui conta comunque la <i>differenza</i> fra due letture, non il
   valore assoluto.</li>
  <li><b>Nel sonno il pin del partitore &egrave; inchiodato</b> (nodo <code>v20</code>): senza,
   il suo buffer d&rsquo;ingresso resterebbe acceso a met&agrave; tensione e brucerebbe decine
   di &micro;A su un nodo che in deep sleep ne consuma ~45.</li>
 </ul>
</section>

<nav>
 <a href="/">Nodi</a><a href="/pannello">Pannello</a><a href="/analisi">Analisi</a>
 <a href="/batteria">Batteria</a>
 <a href="/immagini">Immagini</a><a href="/api">API</a><a href="/pagine">Pagine</a>
 <a href="/update">Aggiornamento</a>
</nav>
</div>
<script>
// =====================================================================
//  /batteria - la cella dei nodi, con quello che si sa e senza il resto
//
//  REGOLA DI QUESTA PAGINA: un numero che non c'e' si scrive con un trattino,
//  mai zero e mai una stima travestita. La colonna della batteria e' nata il
//  09/09/2026 e per settimane i giorni chiusi saranno pochi: una media su due
//  giorni non e' una media, e dirlo e' piu' utile che mostrarla.
// =====================================================================
const $=x=>document.getElementById(x);
const esc=x=>String(x==null?'':x).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const VUOTO='—';
const num=(v,d)=>v==null||!isFinite(v)?VUOTO:v.toFixed(d);
// I millivolt in volt. Passa da qui e non da num(x/1000): `null/1000` fa ZERO,
// non NaN, e "0,000 V" e' una cella scarica che non e' mai esistita - lo stesso
// zero credibile gia' costato con l'acqua nell'aria a 0 gradi.
const mv3=v=>v==null||!isFinite(v)?VUOTO:(v/1000).toFixed(3);
const volt=mv=>mv==null||!isFinite(mv)||mv<=0?VUOTO:(mv/1000).toFixed(3)+' <span class="sub">V</span>';

let NODI=[], SOGLIE=[3350,3580,3730,3880,4050], RIGHE=[], NODO=null, STATO=null, PENDENZA=null;
const COMPLETO=90, GIORNI_MIN=3;

async function chiedi(u,tipo){
 const r=await fetch(u,{cache:'no-store'});
 if(!r.ok) throw new Error(r.status+' '+(await r.text()).slice(0,120));
 return tipo=='json'?r.json():r.text();
}
function avviso(t,cls){ $('msg').innerHTML = t ? '<div class="avv '+(cls||'')+'">'+t+'</div>' : ''; }
function q(id,t){ $(id).textContent = t||''; }
const rq=(k,v,d)=>'<div class="rq"><div class="k">'+k+'</div><div class="v">'+v+
 '</div>'+(d?'<div class="d">'+d+'</div>':'')+'</div>';

// Le tacche: le stesse cinque del pannello. Il LIVELLO lo calcola l'hub
// (batteria_livello) e qui si disegna soltanto - ricalcolarlo sarebbe una
// seconda implementazione della stessa curva di scarica.
function tacche(liv){
 if(liv==null||liv>5) return '';
 const cls=liv<=1?'critica':(liv<=2?'bassa':'');
 let o='<span class="tac">';
 for(let i=0;i<5;i++) o+='<i class="'+(i<liv?('on '+cls):'')+'"></i>';
 return o+'</span>';
}
const ETICHETTA=['ricaricare adesso','riserva','zona medio-bassa','il plateau',
                 'prima fase di scarica','cella carica'];

// Il CSV del riepilogo in righe. Campo vuoto uguale null, mai zero: la batteria
// manca in tutti i giorni anteriori al partitore, e uno zero li' sarebbe una
// cella scarica che non e' mai esistita.
function parse(txt){
 const rr=txt.trim().split('\n');
 if(rr.length<2) return [];
 const idx={}; rr[0].split(',').forEach((c,i)=>idx[c.trim()]=i);
 const out=[];
 for(let i=1;i<rr.length;i++){
  const c=rr[i].split(',');
  if(c.length<5) continue;
  // Il trim non e' pignoleria: il CSV finisce con un ritorno a capo, quindi l'ULTIMO campo
  // di ogni riga non e' mai la stringa vuota e parseFloat ne fa NaN. E NaN
  // non e' null (NaN != null e' true), quindi passava i filtri e finiva a
  // schermo come un valore. Il null si riconosce dopo il trim, e un
  // parseFloat che non torna un numero finito e' comunque un buco.
  const g=v=>{const s=c[idx[v]];
   if(s===undefined) return null;
   const t=s.trim(); if(t==='') return null;
   const x=parseFloat(t); return isFinite(x)?x:null;};
  out.push({giorno:c[0],campioni:g('campioni'),attesi:g('attesi'),compl:g('completezza_pct'),
   buchi:g('buchi'),primo:g('b_primo_mv'),ultimo:g('b_ultimo_mv'),min:g('b_min_mv')});
 }
 return out;
}

// I giorni buoni per le medie: batteria presente, giornata piena, consumo non
// negativo. Una risalita e' una ricarica, non un consumo, e mediarla insieme
// alle altre abbasserebbe il consumo medio proprio nei giorni in cui la cella
// e' stata staccata.
const utili=()=>RIGHE.filter(r=>r.primo!=null&&r.ultimo!=null&&r.compl!=null&&
                               r.compl>=COMPLETO&&(r.primo-r.ultimo)>=0);
const oggiISO=()=>(STATO&&STATO.ora)?STATO.ora.slice(0,10):new Date().toISOString().slice(0,10);

async function caricaTutto(){
 avviso('');
 try{ STATO=await chiedi('/api/stato','json'); $('fw').textContent=STATO.fw+' · '+STATO.ora; }catch(e){}

 let d;
 try{ d=await chiedi('/api/nodi','json'); }
 catch(e){ avviso('Non riesco a leggere <code>/api/nodi</code>: '+esc(e.message),'err'); return; }

 if(Array.isArray(d.batteria_soglie_mv)&&d.batteria_soglie_mv.length===5) SOGLIE=d.batteria_soglie_mv;

 // Solo i nodi che la batteria la MISURANO. Uno che sta alla rete non ha niente
 // da dire qui, e metterlo in elenco con tutte le caselle vuote farebbe sembrare
 // guasto quello che semplicemente non esiste.
 NODI=(d.nodi||[]).filter(n=>n.batteria_mv>0);
 // Due modi diversi di non avere una tensione, e NON vanno detti allo stesso
 // modo: un nodo che ha trasmesso e manda zero sta alla rete (il partitore non
 // ce l'ha), uno che non ha ancora trasmesso non ha detto niente - e da questo
 // avvio dell'hub potrebbe averlo e non saperlo ancora nessuno.
 let senza=(d.nodi||[]).filter(n=>n.dati&&!(n.batteria_mv>0)).map(n=>n.nome);
 let muti =(d.nodi||[]).filter(n=>!n.dati).map(n=>n.nome);

 // NESSUNO ADESSO NON VUOL DIRE NESSUNO MAI, e la differenza si vede solo qui:
 // i valori dell'ultimo pacchetto vivono in RAM, quindi appena l'hub riparte
 // sono zero per tutti, e un nodo che dorme puo' metterci cinque minuti a farsi
 // vivo. In quella finestra «nessun nodo a batteria» sarebbe falso, e con lo
 // storico sulla card a portata di mano sarebbe anche pigro: si guarda lì.
 let soloStorico=false;
 if(!NODI.length&&(d.nodi||[]).length){
  const cand=[];
  for(const n of d.nodi){
   try{
    const txt=await chiedi('/api/nodi/riepilogo?nodo='+encodeURIComponent(n.nome),'text');
    if(parse(txt).some(r=>r.primo!=null||r.ultimo!=null||r.min!=null)) cand.push(n);
   }catch(e){}
  }
  if(cand.length){
   NODI=cand; soloStorico=true;
   senza=(d.nodi||[]).filter(n=>!cand.some(c=>c.nome===n.nome)).map(n=>n.nome);
   avviso('Dall’ultimo riavvio dell’hub ('+esc(STATO?STATO.reset_reason:'')+
    ', avvio n. '+(STATO?STATO.boot_count:'?')+') non è ancora arrivato nessun pacchetto '+
    'con la tensione: <b>&laquo;adesso&raquo; resta vuoto</b> finché il nodo non trasmette, '+
    'perché quei valori vivono in RAM. Il nodo dorme e si fa vivo ogni ~5 minuti; '+
    'tutto il resto di questa pagina viene dalla card e c’è già.','info');
  }
 }
 if(!NODI.length){
  avviso('Nessun nodo manda la tensione della cella.'+(senza.length
   ? ' In elenco c’è solo <b>'+senza.map(esc).join('</b>, <b>')+
     '</b>: sta alla rete, il partitore non ce l’ha e la colonna resta vuota.' : ''),'info');
  $('adesso').innerHTML=$('oggi').innerHTML=$('durata').innerHTML='<div class="sub">'+VUOTO+'</div>';
  $('graf').innerHTML='<p class="lg">niente da disegnare</p>';
  return;
 }
 if(!soloStorico){
  const parti=[];
  if(senza.length) parti.push('<b>'+senza.map(esc).join('</b>, <b>')+
   '</b> non compare qui: ha trasmesso e non manda nessuna tensione, cioè sta '+
   'alla rete e il partitore non ce l’ha. La sua colonna resta vuota, che non è zero.');
  if(muti.length) parti.push('<b>'+muti.map(esc).join('</b>, <b>')+
   '</b> non ha ancora trasmesso da questo avvio dell’hub: se manda la tensione, '+
   'comparirà al primo pacchetto.');
  if(parti.length) avviso(parti.join('<br>'),'info');
 }

 const sel=$('nodo'), prima=sel.value;
 sel.innerHTML=NODI.map(n=>'<option>'+esc(n.nome)+'</option>').join('');
 if(prima&&NODI.some(n=>n.nome===prima)) sel.value=prima;
 NODO=NODI.find(n=>n.nome===sel.value)||NODI[0];

 $('agg').textContent='letto alle '+new Date().toLocaleTimeString();
 renderAdesso();
 await caricaGiorni();
 await caricaCurva();
}

function renderAdesso(){
 const n=NODO; if(!n) return;
 const liv=n.batteria_livello, mv=n.batteria_mv, margine=mv-SOGLIE[0];
 q('qAdesso', mv>0 ? ('· dall’ultimo pacchetto, '+(n.ultimo||''))
                    : '· nessun pacchetto da questo avvio dell’hub');
 $('adesso').innerHTML=
  rq('tensione', volt(mv), 'media di otto conversioni, misurata dal nodo')+
  rq('carica', (liv==null||liv>5?VUOTO:liv+'<span class="sub">/5</span>')+tacche(liv),
     (liv==null||liv>5)?'nessuna lettura':ETICHETTA[liv]+' · soglie '+
     SOGLIE.map(s=>(s/1000).toFixed(2)).join(' / ')+' V')+
  rq('margine sul fondo', margine>0?('+'+margine+' <span class="sub">mV</span>'):VUOTO,
     'quanto manca alla soglia dell’ultima tacca ('+(SOGLIE[0]/1000).toFixed(2)+
     ' V), sotto la quale restano ancora ~350 mV prima del cutoff di protezione')+
  rq('cadenza', n.intervallo_s?(n.intervallo_s+' <span class="sub">s</span>'):VUOTO,
     (n.intervallo_s?'appresa dai pacchetti':'non ancora appresa: serve più di un pacchetto')+
     ' · silenzio '+(n.silenzio_s||0)+' s su '+(n.soglia_muto_s||0))+
  rq('consegne', (n.pacchetti||0)+(n.persi?(' <span style="color:var(--bad)">-'+n.persi+'</span>'):''),
     'da questo avvio dell’hub · riavvii del nodo: '+(n.riavvii||0));
}

// Il giorno in corso non ha un riepilogo (nasce a mezzanotte): si ricava dalla
// serie decimata di oggi. E' anche il modo di vedere SUBITO se una modifica al
// nodo ha cambiato il consumo, senza aspettare la chiusura della giornata.
async function caricaOggi(){
 const oggi=oggiISO();
 try{
  const S=await chiedi('/api/nodi/serie?nodo='+encodeURIComponent(NODO.nome)+
    '&da='+oggi+'&a='+oggi+'&v=3&punti=48','json');
  const c=S.s.filter(x=>x);
  if(!c.length){ $('oggi').innerHTML='<div class="sub">nessun campione con la batteria, oggi.</div>';
   q('qOggi','· '+oggi); return; }
  const primo=c[0][0], ultimo=c[c.length-1][0];
  const mini=Math.min.apply(null,c.map(x=>x[1])), maxi=Math.max.apply(null,c.map(x=>x[2]));
  const delta=primo-ultimo;
  q('qOggi','· '+oggi+', ancora aperta');
  $('oggi').innerHTML=
   rq('prima lettura', volt(primo))+
   rq('ultima lettura', volt(ultimo))+
   rq('finora', (Math.abs(delta)<1.5?'':(delta>0?'-':'+'))+
      Math.abs(Math.round(delta))+' <span class="sub">mV</span>',
      Math.abs(delta)<1.5 ? 'ferma entro la risoluzione della lettura'
                          : (delta>0?'consumo della giornata, fin qui'
                                    :'la tensione è salita: ricarica'))+
   rq('minimo', volt(mini), 'massimo '+(maxi/1000).toFixed(3)+' V · '+S.righe_lette+' campioni');
 }catch(e){ $('oggi').innerHTML='<div class="sub">'+esc(e.message)+'</div>'; }
}

async function caricaGiorni(){
 try{
  const txt=await chiedi('/api/nodi/riepilogo?nodo='+encodeURIComponent(NODO.nome),'text');
  RIGHE=parse(txt).filter(r=>r.primo!=null||r.ultimo!=null||r.min!=null);
 }catch(e){ RIGHE=[]; }

 const tb=$('tab').querySelector('tbody');
 if(!RIGHE.length){
  tb.innerHTML='<tr><td colspan="7">Nessun giorno chiuso con la batteria dentro. '+
   'La colonna è nata il 9 settembre 2026, e il primo riepilogo che la contiene '+
   'è quello scritto il giorno dopo.</td></tr>';
 } else {
  tb.innerHTML=RIGHE.slice().reverse().map(r=>{
   const cons=(r.primo!=null&&r.ultimo!=null)?(r.primo-r.ultimo):null;
   const tuffo=(r.min!=null&&r.ultimo!=null)?(r.ultimo-r.min):null;
   const parz=r.compl!=null&&r.compl<COMPLETO;
   const ric=cons!=null&&cons<0;
   return '<tr class="'+(ric?'ric':(parz?'parz':''))+'"><td>'+esc(r.giorno)+'</td>'+
    '<td>'+num(r.compl,1)+' %</td><td>'+mv3(r.primo)+'</td><td>'+mv3(r.ultimo)+'</td>'+
    '<td>'+(cons==null?VUOTO:(cons>=0?'-':'+')+Math.abs(Math.round(cons))+' mV')+'</td>'+
    '<td>'+mv3(r.min)+'</td>'+
    '<td>'+(tuffo==null?VUOTO:(tuffo>0?tuffo.toFixed(0)+' mV':'0'))+'</td></tr>';
  }).join('');
 }
 q('qTab','· '+RIGHE.length+' giorn'+(RIGHE.length===1?'o':'i')+' con la batteria');

 await caricaOggi();
 renderDurata();
}

// L'autonomia: due strade diverse, mostrate una accanto all'altra perche' NON
// dicono la stessa cosa. Dai giorni chiusi e' la media dei consumi veri; dalla
// pendenza e' una retta sui campioni, che sul plateau proietta molto piu' in
// la' - li' la curva e' quasi orizzontale. Nessuna delle due compare finche' i
// giorni non bastano.
function renderDurata(){
 const u=utili();
 const mv=NODO?NODO.batteria_mv:0;
 const media=(u.length>=GIORNI_MIN)?u.reduce((a,r)=>a+(r.primo-r.ultimo),0)/u.length:null;

 const stima=(cons)=>{
  if(!cons||cons<=0||!mv) return VUOTO;
  const g=(mv-SOGLIE[0])/cons;
  if(g>400) return 'oltre un anno';
  return Math.round(g)+' <span class="sub">giorni</span>';
 };
 $('durata').innerHTML=
  rq('consumo medio', media==null?VUOTO:Math.round(media)+' <span class="sub">mV/giorno</span>',
     media==null?('servono '+GIORNI_MIN+' giornate piene: finora ce n’è '+u.length)
               :'su '+u.length+' giornate piene')+
  rq('fino all’ultima tacca', stima(media),
     'a '+(SOGLIE[0]/1000).toFixed(2)+' V, quando la cella va cambiata')+
  rq('dalla pendenza', PENDENZA==null?VUOTO:Math.round(PENDENZA)+' <span class="sub">mV/giorno</span>',
     PENDENZA==null?'serve più di un giorno di curva':'retta sui campioni del periodo scelto')+
  rq('con quella pendenza', stima(PENDENZA), 'stessa soglia, altro metodo');

 const tuffi=RIGHE.filter(r=>r.min!=null&&r.ultimo!=null).map(r=>r.ultimo-r.min);
 const tuffoMed=tuffi.length?tuffi.reduce((a,b)=>a+b,0)/tuffi.length:null;
 $('durataNota').innerHTML=
  'Le due stime <b>non devono coincidere</b>, e quando divergono la ragione è sempre la '+
  'stessa: la curva di scarica di una Li-ion non è una retta. Sul plateau '+
  '(3,73&ndash;3,88 V) la tensione quasi non si muove, quindi una pendenza misurata lì '+
  'proietta un’autonomia lunghissima; sopra e sotto crolla in fretta. <b>Vanno lette come '+
  'ordine di grandezza</b>, non come una data sul calendario.'+
  (tuffoMed!=null?(' Il tuffo medio sotto carico, finora, è <b>'+tuffoMed.toFixed(0)+
   ' mV</b>: è il numero da guardare crescere nel tempo.'):'');
}

// --- il grafico, in SVG e senza librerie ------------------------------------
// Niente CDN: questa pagina si guarda in LAN, spesso da un telefono che in quel
// momento non ha internet, e un grafico che sparisce quando manca la rete e'
// peggio di uno brutto.
const W=920,H=260,ML=52,MR=14,MT=14,MB=28;
const px=(i,n)=>ML+(n<2?(W-ML-MR)/2:i*(W-ML-MR)/(n-1));
const py=(v,s)=>MT+(H-MT-MB)*(1-(v-s.lo)/(s.hi-s.lo));

async function caricaCurva(){
 const gg=parseInt($('giorni').value,10);
 const oggi=oggiISO();
 const d0=new Date(oggi+'T12:00:00'); d0.setDate(d0.getDate()-(gg-1));
 const da=d0.toISOString().slice(0,10);
 let S;
 try{ S=await chiedi('/api/nodi/serie?nodo='+encodeURIComponent(NODO.nome)+
      '&da='+da+'&a='+oggi+'&v=3&punti=240','json'); }
 catch(e){ $('graf').innerHTML='<p class="lg">'+esc(e.message)+'</p>'; return; }

 const pieni=[]; S.s.forEach((c,i)=>{ if(c) pieni.push([i,c[0],c[1],c[2]]); });
 q('qCurva','· '+da+' → '+oggi+', '+S.righe_lette+' campioni letti');
 if(pieni.length<2){
  $('graf').innerHTML='<p class="lg">Meno di due punti con la batteria, in questo periodo.</p>';
  $('grafLg').textContent='';
  PENDENZA=null; renderDurata(); return;
 }

 // La scala include SEMPRE le soglie che cadono vicino ai dati: altrimenti le
 // bande finirebbero fuori dal disegno e il grafico direbbe meno di quanto sa.
 const vals=[]; pieni.forEach(p=>{vals.push(p[2],p[3]);});
 let lo=Math.min.apply(null,vals), hi=Math.max.apply(null,vals);
 SOGLIE.forEach(s=>{ if(s>lo-120&&s<hi+120){ lo=Math.min(lo,s); hi=Math.max(hi,s); } });
 if(hi-lo<40){ const m=(hi+lo)/2; lo=m-20; hi=m+20; }
 const pad=(hi-lo)*0.08, s={lo:lo-pad,hi:hi+pad}, n=S.punti;

 let area='',linea='',giu=true;
 pieni.forEach(p=>{ area+=(area?' L':'M')+px(p[0],n).toFixed(1)+' '+py(p[3],s).toFixed(1); });
 for(let k=pieni.length-1;k>=0;k--) area+=' L'+px(pieni[k][0],n).toFixed(1)+' '+py(pieni[k][2],s).toFixed(1);
 area+=' Z';
 S.s.forEach((c,i)=>{ if(!c){giu=true;return;}
  linea+=(giu?'M':' L')+px(i,n).toFixed(1)+' '+py(c[0],s).toFixed(1); giu=false; });

 // Le bande delle tacche: le soglie dell'hub, non una copia locale.
 let bande='';
 SOGLIE.forEach((sg,k)=>{ if(sg<s.lo||sg>s.hi) return;
  const y=py(sg,s);
  bande+='<line x1="'+ML+'" y1="'+y.toFixed(1)+'" x2="'+(W-MR)+'" y2="'+y.toFixed(1)+
   '" stroke="'+(k===0?'#e05252':'#3a3a42')+'" stroke-dasharray="4 4"/>'+
   '<text x="'+(W-MR-4)+'" y="'+(y-4).toFixed(1)+'" fill="'+(k===0?'#e05252':'#6e6e78')+
   '" font-size="10" text-anchor="end">'+(k+1)+(k?' tacche':' tacca')+'</text>';
 });

 let assi='';
 for(let k=0;k<=4;k++){
  const v=s.lo+(s.hi-s.lo)*k/4, y=py(v,s);
  assi+='<line x1="'+ML+'" y1="'+y.toFixed(1)+'" x2="'+(W-MR)+'" y2="'+y.toFixed(1)+
   '" stroke="#26262b"/><text x="'+(ML-6)+'" y="'+(y+3.5).toFixed(1)+
   '" fill="#8e8e96" font-size="10" text-anchor="end">'+(v/1000).toFixed(3)+'</text>';
 }
 const passo=Math.max(1,Math.ceil(n/8));
 for(let i=0;i<n;i+=passo){
  const d=new Date((S.t0+i*S.passo)*1000);
  assi+='<text x="'+px(i,n).toFixed(1)+'" y="'+(H-8)+'" fill="#8e8e96" font-size="10" '+
   'text-anchor="middle">'+d.getDate()+'/'+(d.getMonth()+1)+' '+
   String(d.getHours()).padStart(2,'0')+'</text>';
 }

 $('graf').innerHTML='<svg viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none">'+
  assi+bande+
  '<path d="'+area+'" fill="#7ee787" fill-opacity=".14"/>'+
  '<path d="'+linea+'" fill="none" stroke="#7ee787" stroke-width="2"/></svg>';

 // La pendenza: minimi quadrati sui cesti pieni, in mV al giorno. Sui cesti e
 // non sui campioni grezzi perche' e' quello che la scheda ha gia' decimato, e
 // la media di cesto ha gia' tolto un po' del rumore dell'ADC.
 const sx=[], sy=[];
 pieni.forEach(p=>{ sx.push((S.t0+p[0]*S.passo)/86400); sy.push(p[1]); });
 const mx=sx.reduce((a,b)=>a+b,0)/sx.length, my=sy.reduce((a,b)=>a+b,0)/sy.length;
 let nn=0, dd=0;
 for(let i=0;i<sx.length;i++){ nn+=(sx[i]-mx)*(sy[i]-my); dd+=(sx[i]-mx)*(sx[i]-mx); }
 const giorniVeri=sx[sx.length-1]-sx[0];
 PENDENZA=(dd>0&&giorniVeri>=1)? -(nn/dd) : null;    // positiva uguale consumo

 $('grafLg').innerHTML='La banda chiara è <b>minimo e massimo</b> del cesto, la linea è '+
  'la <b>media</b>. I tratteggi sono le soglie delle tacche, quelle vere dell’hub. '+
  (PENDENZA==null
   ? 'La pendenza non si calcola: serve almeno un giorno intero di curva.'
   : 'Pendenza sul periodo: <b>'+(PENDENZA>=0?'-':'+')+Math.abs(PENDENZA).toFixed(1)+
     ' mV al giorno</b>, su '+giorniVeri.toFixed(1)+' giorni di dati.')+
  ' I buchi restano buchi: dove il nodo non ha trasmesso, la linea si interrompe.';
 renderDurata();
}

$('nodo').onchange=async()=>{ NODO=NODI.find(n=>n.nome===$('nodo').value)||NODO;
  renderAdesso(); await caricaGiorni(); await caricaCurva(); };
$('giorni').onchange=()=>caricaCurva();
$('bRic').onclick=()=>caricaTutto();
caricaTutto();
</script></body></html>

)BATTERIAPAGE";
