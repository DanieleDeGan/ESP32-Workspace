#pragma once

// ============================================================
//  GENERATO DA www/gen_page.py - NON MODIFICARE A MANO.
//  La sorgente e' www/dither.html: si modifica quella e si
//  rilancia  python www/gen_page.py  prima di ricompilare.
//  (47995 byte di pagina, serviti su /immagini)
// ============================================================

static const char DITHER_PAGE[] PROGMEM = R"DITHERPAGE(
<!DOCTYPE html>
<!--
  SORGENTE UNICA di questa pagina. L'hub la serve su /immagini da
  dither_page.h, che si RIGENERA da qui con  python www/gen_page.py
  (lo fa da solo un hook di .claude/settings.json). Se si modifica questo
  file e non si rigenera, la scheda continua a servire la versione vecchia
  senza dirlo.
-->
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>E-ink 400x300 — ritaglio e dithering</title>
<style>
  :root{
    --bg:#1b1d20; --panel:#25282c; --panel2:#2e3238; --line:#3a3f46;
    --fg:#e6e8ea; --dim:#9aa1a9; --accent:#6fb3ff; --ok:#78c48a; --warn:#e0a84a;
  }
  *{box-sizing:border-box}
  body{
    margin:0; background:var(--bg); color:var(--fg);
    font:14px/1.5 system-ui,Segoe UI,Roboto,sans-serif;
  }
  header{
    padding:14px 20px; border-bottom:1px solid var(--line);
    display:flex; align-items:baseline; gap:14px; flex-wrap:wrap;
  }
  header h1{font-size:16px; margin:0; font-weight:600}
  header .sub{color:var(--dim); font-size:13px}
  main{
    display:grid; grid-template-columns:minmax(0,1fr) 340px;
    gap:18px; padding:18px 20px 40px; align-items:start;
  }
  @media (max-width:1000px){ main{grid-template-columns:minmax(0,1fr)} }

  .card{background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:14px}
  .card h2{font-size:12px; text-transform:uppercase; letter-spacing:.08em;
           color:var(--dim); margin:0 0 10px; font-weight:600}

  /* --- palco di ritaglio --------------------------------------------- */
  #stageWrap{position:relative; line-height:0}
  #stage{width:100%; max-width:600px; height:auto; border-radius:6px;
         background:#111316; cursor:grab; touch-action:none; display:block}
  #stage.drag{cursor:grabbing}
  #drop{
    position:absolute; inset:0; display:flex; align-items:center; justify-content:center;
    color:var(--dim); font-size:13px; text-align:center; pointer-events:none;
    line-height:1.6;
  }
  #stageWrap.hasImg #drop{display:none}
  #stageWrap.over::after{
    content:''; position:absolute; inset:0; border:2px dashed var(--accent);
    border-radius:6px; background:rgba(111,179,255,.08);
  }

  /* --- anteprima ------------------------------------------------------ */
  #preview{
    image-rendering:pixelated; display:block; border-radius:2px;
    width:400px; max-width:100%; height:auto;
  }
  #preview.z2{width:800px}
  #previewWrap{overflow:auto; background:#111316; padding:10px; border-radius:6px}

  /* --- controlli ------------------------------------------------------ */
  .row{display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin-bottom:10px}
  .row:last-child{margin-bottom:0}
  label.f{display:block; margin-bottom:12px}
  label.f > span{display:flex; justify-content:space-between; color:var(--dim);
                 font-size:12px; margin-bottom:4px}
  label.f > span b{color:var(--fg); font-weight:500; font-variant-numeric:tabular-nums}
  input[type=range]{width:100%; accent-color:var(--accent); margin:0}
  input[type=text]{
    background:var(--panel2); border:1px solid var(--line); color:var(--fg);
    border-radius:6px; padding:6px 8px; font:inherit; min-width:0; flex:1;
  }
  input[type=number]{
    background:var(--panel2); border:1px solid var(--line); color:var(--fg);
    border-radius:6px; padding:6px 8px; font:inherit; width:74px;
    font-variant-numeric:tabular-nums;
  }
  .avviso{font-size:12px; color:var(--warn); margin:-4px 0 10px}
  select{
    background:var(--panel2); border:1px solid var(--line); color:var(--fg);
    border-radius:6px; padding:6px 8px; font:inherit; width:100%;
  }
  button{
    background:var(--panel2); border:1px solid var(--line); color:var(--fg);
    border-radius:6px; padding:6px 11px; font:inherit; cursor:pointer;
  }
  button:hover:not(:disabled){border-color:var(--accent); color:var(--accent)}
  button:disabled{opacity:.4; cursor:not-allowed}
  button.primary{background:var(--accent); border-color:var(--accent); color:#0d1620; font-weight:600}
  button.primary:hover:not(:disabled){filter:brightness(1.1); color:#0d1620}
  .check{display:flex; align-items:center; gap:7px; color:var(--dim); cursor:pointer}
  .check input{accent-color:var(--accent); margin:0}
  hr{border:0; border-top:1px solid var(--line); margin:14px 0}

  .stat{display:flex; justify-content:space-between; font-size:12px; color:var(--dim);
        font-variant-numeric:tabular-nums}
  .stat b{color:var(--fg); font-weight:500}
  #msg{font-size:12px; min-height:18px; margin-top:8px}
  #msg.ok{color:var(--ok)} #msg.err{color:var(--warn)}

  details.spec{margin:0 20px 30px; color:var(--dim); font-size:13px; max-width:900px}
  details.spec summary{cursor:pointer; color:var(--fg); margin-bottom:8px}
  details.spec pre{
    background:var(--panel); border:1px solid var(--line); border-radius:8px;
    padding:12px; overflow-x:auto; font-size:12px; line-height:1.5; color:var(--fg);
  }
</style>
</head>
<body>

<header>
  <h1>E-ink 400×300 — ritaglio e dithering</h1>
  <span class="sub">produce il framebuffer da 15.000 byte per il pannello WeAct 4.2" B/N</span>
</header>

<main>
  <!-- ================= colonna sinistra ================= -->
  <div style="display:grid; gap:18px; min-width:0">
    <div class="card">
      <h2>Sorgente — trascina per spostare, rotella per zoomare</h2>
      <div id="stageWrap">
        <canvas id="stage" width="600" height="440"></canvas>
        <div id="drop">
          Trascina qui un'immagine, incollala con Ctrl+V<br>oppure usa «Apri immagine»
        </div>
      </div>
      <div class="row" style="margin-top:12px">
        <button id="btnOpen">Apri immagine…</button>
        <button id="btnFit" disabled>Adatta</button>
        <button id="btnFill" disabled>Riempi</button>
        <button id="btnRotL" disabled title="Ruota a sinistra">⟲</button>
        <button id="btnRotR" disabled title="Ruota a destra">⟳</button>
        <span class="stat" style="margin-left:auto"><b id="zoomLbl">—</b></span>
      </div>
      <input type="file" id="fileImg" accept="image/*" hidden>
    </div>

    <div class="card">
      <h2>Anteprima 1:1 — è esattamente ciò che vedrà il pannello</h2>
      <div id="previewWrap">
        <canvas id="preview" width="400" height="300"></canvas>
      </div>
      <div class="row" style="margin-top:12px">
        <label class="check"><input type="checkbox" id="zoom2"> zoom 2×</label>
        <label class="check"><input type="checkbox" id="inkSim" checked> simula resa e-ink</label>
        <span class="stat" style="margin-left:auto">nero <b id="blackPct">—</b></span>
      </div>
    </div>
  </div>

  <!-- ================= colonna destra ================= -->
  <div style="display:grid; gap:18px; min-width:0">
    <div class="card">
      <h2>Regolazioni (prima del dithering)</h2>
      <label class="f"><span>Luminosità <b id="vBri">0</b></span>
        <input type="range" id="bri" min="-100" max="100" value="0"></label>
      <label class="f"><span>Contrasto <b id="vCon">0</b></span>
        <input type="range" id="con" min="-100" max="100" value="0"></label>
      <label class="f" style="margin-bottom:0"><span>Gamma <b id="vGam">1.00</b></span>
        <input type="range" id="gam" min="40" max="250" value="100"></label>
      <div class="row" style="margin-top:12px">
        <button id="btnReset">Azzera regolazioni</button>
      </div>
    </div>

    <div class="card">
      <h2>Dithering</h2>
      <div class="row">
        <select id="algo">
          <option value="fs" selected>Floyd–Steinberg</option>
          <option value="atk">Atkinson</option>
          <option value="bayer">Bayer 8×8 (ordinato)</option>
          <option value="thr">Soglia secca</option>
        </select>
      </div>
      <div class="row">
        <label class="check"><input type="checkbox" id="serp" checked> scansione a serpentina</label>
      </div>
      <div class="row" style="margin-bottom:0">
        <label class="check"><input type="checkbox" id="inv"> inverti bianco/nero</label>
      </div>
    </div>

    <div class="card">
      <h2>Testo sopra (dopo il dithering)</h2>
      <div class="row">
        <textarea id="txt" rows="2" spellcheck="false" style="width:100%;resize:vertical;
         background:#0f151c;color:inherit;border:1px solid var(--bordo);border-radius:8px;
         padding:9px 10px;font:inherit" placeholder="Buon compleanno!"></textarea>
      </div>
      <div class="row">
        <select id="txtFont" style="flex:1">
          <option value="system-ui,Segoe UI,Arial,sans-serif">Bastone</option>
          <option value="Arial Narrow,Roboto Condensed,Liberation Sans Narrow,sans-serif">Bastone stretto</option>
          <option value="Verdana,DejaVu Sans,Geneva,sans-serif">Bastone largo</option>
          <option value="Arial Black,Arial Bold,Helvetica,sans-serif">Bastone nero</option>
          <option value="Tahoma,Segoe UI,Geneva,sans-serif">Bastone asciutto</option>
          <option value="Georgia,Times New Roman,serif">Grazie</option>
          <option value="Times New Roman,Liberation Serif,Times,serif">Grazie stretto</option>
          <option value="Palatino Linotype,Book Antiqua,Palatino,serif">Grazie tondo</option>
          <option value="Impact,Haettenschweiler,Arial Black,sans-serif">Manifesto</option>
          <option value="Segoe Script,Comic Sans MS,cursive">Mano libera</option>
          <option value="Lucida Handwriting,Brush Script MT,Segoe Script,cursive">Corsivo inglese</option>
          <option value="Courier New,Liberation Mono,monospace">Macchina da scrivere</option>
          <option value="Consolas,Menlo,DejaVu Sans Mono,monospace">Terminale</option>
        </select>
        <label class="check"><input type="checkbox" id="txtBold" checked> grassetto</label>
      </div>
      <div class="avviso" id="fontWarn" hidden></div>
      <label class="f"><span>Corpo <b id="vTxtSize">30</b> px</span>
        <span class="row" style="margin-bottom:0">
          <input type="range" id="txtSize" min="8" max="140" value="30" style="flex:1;width:auto">
          <input type="number" id="txtSizeN" min="8" max="140" value="30" step="1"
                 aria-label="corpo in px">
        </span></label>
      <label class="f"><span>Altezza <b id="vTxtY">82</b>%</span>
        <input type="range" id="txtY" min="0" max="100" value="82"></label>
      <div class="row">
        <select id="txtBg" style="flex:1">
          <option value="band" selected>su una banda piena</option>
          <option value="outline">con l'alone attorno</option>
          <option value="none">nudo sull'immagine</option>
        </select>
        <label class="check"><input type="checkbox" id="txtInv"> chiaro su scuro</label>
      </div>
      <div class="row">
        <label class="check"><input type="checkbox" id="txtEmojiFlat">
          emoji a sagoma piena</label>
      </div>
      <div class="stat" style="margin-bottom:0; display:block">
        Il testo <b>non passa dal dithering</b>: si stende a soglia secca sopra i
        pixel gi&agrave; retinati. Una lettera ditherata perde i tratti sottili e a
        400&times;300 diventa illeggibile. Le <b>emoji sono l'eccezione</b> &mdash;
        sono figure piene, non tratti, quindi vengono retinate come una foto:
        sotto i ~40&nbsp;px conviene la sagoma.
      </div>
    </div>

    <div class="card">
      <h2>Esporta</h2>
      <div class="row">
        <input type="text" id="name" value="immagine" spellcheck="false">
        <span class="stat" style="color:var(--dim)">.bin</span>
      </div>
      <div class="stat" style="margin-bottom:10px">
        sulla SD → <b id="pathLbl">/images/immagine.bin</b>
      </div>
      <div class="row">
        <button class="primary" id="btnSave" disabled>Scarica .bin (15.000 byte)</button>
      </div>
      <hr>
      <h2>Manda all'hub</h2>
      <div id="remoto">
        <div class="row">
          <input type="text" id="hubHost" value="192.168.1.73" spellcheck="false" style="flex:1">
        </div>
        <div class="row">
          <input type="text" id="hubUser" value="admin" spellcheck="false" style="width:80px">
          <input type="password" id="hubPass" value="admin" style="width:80px">
        </div>
      </div>
      <div class="row">
        <button class="primary" id="btnSend" disabled>Invia all'hub</button>
      </div>
      <div class="stat" style="color:var(--dim)">
        Scrive /images/&lt;nome&gt;.bin sulla card dell'hub. Da li' la pagina
        <a href="/pannello" id="lnkPannello">Pannello</a> la mostra e la si puo'
        aggiungere all'elenco delle pagine.
      </div>
      <hr>
      <h2>Verifica</h2>
      <div class="row" style="margin-bottom:0">
        <button id="btnLoadBin">Carica un .bin e mostralo</button>
      </div>
      <input type="file" id="fileBin" accept=".bin,application/octet-stream" hidden>
      <div id="msg"></div>
    </div>
  </div>
</main>

<footer id="nav" style="display:none;max-width:1200px;margin:0 auto;padding:0 16px 24px;
  font-size:13px;color:var(--dim)">
  <a href="/">nodi</a> &mdash; <a href="/pannello">pannello e messaggi</a> &mdash; <a href="/immagini">componi immagine</a> &mdash; <a href="/pagine">pagine</a> &mdash; <a href="/api">API</a> &mdash; <a href="/update">aggiornamento firmware</a>
</footer>

<details class="spec">
  <summary>Formato del file — il contratto tra questa pagina e il firmware</summary>
  <pre>400 x 300, 1 bit per pixel, NESSUN header: il file E' il framebuffer.

riga  = 50 byte (400 / 8)
righe = 300
tot   = 15.000 byte esatti

MSB-first: il bit 7 del primo byte di ogni riga e' il pixel x = 0
1 = bianco, 0 = nero

E' il formato nativo della RAM del pannello: GxEPD2 lo spinge con
writeImage() senza nessuna conversione. Se al primo test su hardware
i colori risultassero scambiati, si corregge con il flag `invert` di
drawImage()/writeImage() — non serve rifare nulla qui.

Il file si chiama /images/&lt;nome&gt;.bin sulla SD dell'hub. Il nome e'
limitato a [A-Za-z0-9_-], max 24 caratteri: finisce in un path sul
filesystem e in una query string HTTP, quindi vale la stessa regola
difensiva di sd_name_is_safe() in EnvNode_C3.

Nota: 15.000 byte sono anche l'anteprima. La galleria web dell'hub
rilegge questo stesso file e lo ridisegna su un canvas con la funzione
unpack()/paint() qui sotto — niente miniature da generare o salvare.</pre>
</details>

<script>
"use strict";

// =====================================================================
//  Costanti del pannello. Tutto il resto del file lavora in "spazio di
//  uscita": 1 unita' = 1 pixel del pannello.
// =====================================================================
const OUT_W = 400, OUT_H = 300;
const ROW_BYTES = OUT_W / 8;              // 50
const OUT_BYTES = ROW_BYTES * OUT_H;      // 15000

// Palco di ritaglio: il riquadro 400x300 sta al centro di questa tela,
// cosi' si vede anche la parte di immagine che resta fuori.
const STAGE_W = 600, STAGE_H = 440;
const FRAME_X = (STAGE_W - OUT_W) / 2;
const FRAME_Y = (STAGE_H - OUT_H) / 2;

const $ = id => document.getElementById(id);

// ---------------------------------------------------------------------
//  Stato
// ---------------------------------------------------------------------
let baseBitmap = null;   // immagine decodificata, orientamento EXIF gia' applicato
let srcCanvas  = null;   // baseBitmap dopo la rotazione manuale
let pyramid    = [];     // mipmap di srcCanvas: pyramid[0] = srcCanvas, poi meta' ogni volta
let rotation   = 0;      // 0 / 90 / 180 / 270
let view       = { scale: 1, ox: 0, oy: 0 };   // dest = src * scale + offset
let lastPacked = null;   // Uint8Array(15000) dell'ultimo render
let showingBin = false;  // true mentre si visualizza un .bin caricato da disco

const stage   = $('stage'),   sctx = stage.getContext('2d');
const preview = $('preview'), pctx = preview.getContext('2d');
// Tela di lavoro 400x300: da qui esce l'ImageData che va nella pipeline.
const work = document.createElement('canvas');
work.width = OUT_W; work.height = OUT_H;
const wctx = work.getContext('2d', { willReadFrequently: true });

const clamp = (v, lo, hi) => v < lo ? lo : (v > hi ? hi : v);

// =====================================================================
//  Caricamento immagine
// =====================================================================
async function loadImage(src) {
  let bmp;
  try {
    bmp = await createImageBitmap(src, { imageOrientation: 'from-image' });
  } catch (e) {
    // Fallback per browser senza l'opzione imageOrientation.
    bmp = await new Promise((res, rej) => {
      const img = new Image();
      img.onload = () => res(img);
      img.onerror = rej;
      img.src = URL.createObjectURL(src);
    });
  }
  baseBitmap = bmp;
  rotation = 0;
  applyRotation();
  fitView('cover');           // di default riempie il riquadro, senza bordi bianchi
  showingBin = false;
  $('stageWrap').classList.add('hasImg');
  ['btnFit','btnFill','btnRotL','btnRotR','btnSave','btnSend'].forEach(id => $(id).disabled = false);
  say('');
  render();
}

// Ricostruisce srcCanvas applicando la rotazione, poi la piramide. Tenere la
// rotazione QUI invece che nella trasformata di disegno mantiene il percorso
// di rendering (e la matematica del pan/zoom) completamente privo di rotazioni.
function applyRotation() {
  const w = baseBitmap.width, h = baseBitmap.height;
  const swap = (rotation % 180) !== 0;
  const c = document.createElement('canvas');
  c.width  = swap ? h : w;
  c.height = swap ? w : h;
  const g = c.getContext('2d');
  g.translate(c.width / 2, c.height / 2);
  g.rotate(rotation * Math.PI / 180);
  g.drawImage(baseBitmap, -w / 2, -h / 2);
  srcCanvas = c;
  buildPyramid();
}

// Mipmap per dimezzamenti successivi. Serve alla qualita', non alla velocita':
// un drawImage che riduce una foto da 4000 px a 400 in un colpo solo aliasa, e
// il dithering trasforma l'aliasing in puntinatura sporca. Dimezzare a tappe
// e' il filtro passa-basso che manca.
function buildPyramid() {
  pyramid = [srcCanvas];
  let cur = srcCanvas;
  while (cur.width >= 16 && cur.height >= 16 && pyramid.length < 14) {
    const n = document.createElement('canvas');
    n.width  = Math.max(1, cur.width  >> 1);
    n.height = Math.max(1, cur.height >> 1);
    const g = n.getContext('2d');
    g.imageSmoothingEnabled = true;
    g.imageSmoothingQuality = 'high';
    g.drawImage(cur, 0, 0, n.width, n.height);
    pyramid.push(n);
    cur = n;
  }
}

// Livello piu' piccolo che sia ancora >= della dimensione richiesta.
function pickLevel(scale) {
  let L = 0;
  while (L + 1 < pyramid.length && Math.pow(2, -(L + 1)) >= scale) L++;
  return L;
}

// =====================================================================
//  Vista (pan / zoom), tutto in spazio di uscita
// =====================================================================
function fitView(mode) {
  const sx = OUT_W / srcCanvas.width, sy = OUT_H / srcCanvas.height;
  view.scale = (mode === 'cover') ? Math.max(sx, sy) : Math.min(sx, sy);
  view.ox = (OUT_W - srcCanvas.width  * view.scale) / 2;
  view.oy = (OUT_H - srcCanvas.height * view.scale) / 2;
}

function zoomAt(px, py, factor) {
  const minS = Math.min(OUT_W / srcCanvas.width, OUT_H / srcCanvas.height) * 0.4;
  const ns = clamp(view.scale * factor, minS, 12);
  const k = ns / view.scale;
  view.ox = px - (px - view.ox) * k;   // il punto sotto il cursore resta fermo
  view.oy = py - (py - view.oy) * k;
  view.scale = ns;
}

// Disegna la porzione inquadrata dentro un contesto 400x300.
function drawCrop(ctx) {
  ctx.fillStyle = '#ffffff';           // fuori dall'immagine = carta bianca
  ctx.fillRect(0, 0, OUT_W, OUT_H);
  if (!srcCanvas) return;
  const lv = pyramid[pickLevel(view.scale)];
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(lv, view.ox, view.oy,
                srcCanvas.width * view.scale, srcCanvas.height * view.scale);
}

function drawStage() {
  sctx.fillStyle = '#111316';
  sctx.fillRect(0, 0, STAGE_W, STAGE_H);
  if (srcCanvas) {
    const lv = pyramid[pickLevel(view.scale)];
    sctx.imageSmoothingEnabled = true;
    sctx.imageSmoothingQuality = 'high';
    sctx.drawImage(lv, FRAME_X + view.ox, FRAME_Y + view.oy,
                   srcCanvas.width * view.scale, srcCanvas.height * view.scale);
    // Oscura tutto cio' che resta fuori dal ritaglio.
    sctx.fillStyle = 'rgba(10,12,14,.62)';
    sctx.fillRect(0, 0, STAGE_W, FRAME_Y);
    sctx.fillRect(0, FRAME_Y + OUT_H, STAGE_W, STAGE_H - FRAME_Y - OUT_H);
    sctx.fillRect(0, FRAME_Y, FRAME_X, OUT_H);
    sctx.fillRect(FRAME_X + OUT_W, FRAME_Y, STAGE_W - FRAME_X - OUT_W, OUT_H);
  }
  sctx.strokeStyle = '#6fb3ff';
  sctx.lineWidth = 1;
  sctx.strokeRect(FRAME_X + .5, FRAME_Y + .5, OUT_W - 1, OUT_H - 1);
  $('zoomLbl').textContent = srcCanvas ? Math.round(view.scale * 100) + '%' : '—';
}

// =====================================================================
//  Pipeline: luminanza -> LUT -> dithering -> impacchettamento
// =====================================================================

// Tabella 256 voci con contrasto, luminosita' e gamma gia' combinati: una
// lettura per pixel invece di tre pow()/mul su 120.000 pixel ad ogni
// spostamento di slider.
function buildLut() {
  const bri = +$('bri').value / 100;            // -1 .. +1
  const con = (+$('con').value + 100) / 100;    //  0 .. 2   (1 = neutro)
  const gam = +$('gam').value / 100;            // 0.4 .. 2.5
  const lut = new Float32Array(256);
  for (let i = 0; i < 256; i++) {
    let v = i / 255;
    v = (v - 0.5) * con + 0.5;
    v += bri;
    v = clamp(v, 0, 1);
    v = Math.pow(v, 1 / gam);                   // gamma > 1 schiarisce i mezzitoni
    lut[i] = v * 255;
  }
  return lut;
}

function toGray(imgData) {
  const d = imgData.data, lut = buildLut();
  const g = new Float32Array(OUT_W * OUT_H);
  for (let i = 0, p = 0; i < g.length; i++, p += 4) {
    // Rec.709: e' lo spazio in cui sono codificati i JPEG sRGB.
    const y = 0.2126 * d[p] + 0.7152 * d[p + 1] + 0.0722 * d[p + 2];
    g[i] = lut[clamp(Math.round(y), 0, 255)];
  }
  return g;
}

// Diffusione dell'errore. Il kernel e' APPIATTITO in [dx,dy,peso, dx,dy,peso, ...]
// dentro una Float32Array e i controlli di bordo sono in linea: misurato ~5 ms
// contro ~6,2 ms della versione leggibile (array di array + una addErr()
// chiamata per ogni vicino), a parita' esatta di bit prodotti. Su ~13 ms di
// render completo non e' un collo di bottiglia — e' margine gratuito, non una
// ottimizzazione necessaria. dx e' espresso nel verso di avanzamento, cosi' la
// serpentina si ottiene solo cambiando segno a `d`.
function ditherDiffuse(g, kernel, serpentine) {
  const bits = new Uint8Array(OUT_W * OUT_H);
  const kn = kernel.length;
  for (let y = 0; y < OUT_H; y++) {
    const ltr = !serpentine || (y & 1) === 0;
    const d = ltr ? 1 : -1;
    const row = y * OUT_W;
    for (let k = 0; k < OUT_W; k++) {
      const x = ltr ? k : OUT_W - 1 - k;
      const i = row + x;
      const old = g[i];
      const nw = old < 128 ? 0 : 255;
      bits[i] = nw ? 1 : 0;
      const err = old - nw;
      if (err === 0) continue;          // nero/bianco esatti: niente da diffondere
      for (let j = 0; j < kn; j += 3) {
        const ny = y + kernel[j + 1];   // dy non e' mai negativo: basta il limite alto
        if (ny >= OUT_H) continue;
        const nx = x + kernel[j] * d;
        if (nx < 0 || nx >= OUT_W) continue;
        g[ny * OUT_W + nx] += err * kernel[j + 2];
      }
    }
  }
  return bits;
}

//      X   7/16
// 3/16 5/16 1/16
const K_FS = new Float32Array([
   1, 0, 7 / 16,
  -1, 1, 3 / 16,
   0, 1, 5 / 16,
   1, 1, 1 / 16,
]);
// Atkinson: diffonde solo 6/8 dell'errore, quindi piu' contrasto e meno
// "vermi" del FS — di solito piu' pulito su un pannello a 1 bit.
const K_ATKINSON = new Float32Array([
   1, 0, 1 / 8,
   2, 0, 1 / 8,
  -1, 1, 1 / 8,
   0, 1, 1 / 8,
   1, 1, 1 / 8,
   0, 2, 1 / 8,
]);
const BAYER8 = [
   0,32, 8,40, 2,34,10,42,
  48,16,56,24,50,18,58,26,
  12,44, 4,36,14,46, 6,38,
  60,28,52,20,62,30,54,22,
   3,35,11,43, 1,33, 9,41,
  51,19,59,27,49,17,57,25,
  15,47, 7,39,13,45, 5,37,
  63,31,55,23,61,29,53,21,
];

function ditherOrdered(g) {
  const bits = new Uint8Array(OUT_W * OUT_H);
  for (let y = 0; y < OUT_H; y++)
    for (let x = 0; x < OUT_W; x++) {
      const t = (BAYER8[(y & 7) * 8 + (x & 7)] + 0.5) / 64 * 255;
      bits[y * OUT_W + x] = g[y * OUT_W + x] > t ? 1 : 0;
    }
  return bits;
}

function ditherThreshold(g) {
  const bits = new Uint8Array(OUT_W * OUT_H);
  for (let i = 0; i < g.length; i++) bits[i] = g[i] >= 128 ? 1 : 0;
  return bits;
}

// bit a 1 = bianco (vedi il contratto in fondo alla pagina)
function pack(bits) {
  const out = new Uint8Array(OUT_BYTES);       // gia' tutto a 0 = tutto nero
  for (let y = 0; y < OUT_H; y++) {
    const rowB = y * ROW_BYTES, rowP = y * OUT_W;
    for (let x = 0; x < OUT_W; x++)
      if (bits[rowP + x]) out[rowB + (x >> 3)] |= 0x80 >> (x & 7);
  }
  return out;
}

function unpack(bytes) {
  const bits = new Uint8Array(OUT_W * OUT_H);
  for (let y = 0; y < OUT_H; y++) {
    const rowB = y * ROW_BYTES, rowP = y * OUT_W;
    for (let x = 0; x < OUT_W; x++)
      bits[rowP + x] = (bytes[rowB + (x >> 3)] >> (7 - (x & 7))) & 1;
  }
  return bits;
}

// Disegna i bit sull'anteprima. Con "simula e-ink" usa carta e inchiostro veri
// invece di #fff/#000: su un monitor il bianco puro promette un contrasto che
// il pannello non ha, e si finisce per tarare le regolazioni troppo scure.
function paint(bits) {
  const sim = $('inkSim').checked;
  const W = sim ? [233, 231, 224] : [255, 255, 255];
  const B = sim ? [ 53,  54,  56] : [  0,   0,   0];
  const img = pctx.createImageData(OUT_W, OUT_H);
  const d = img.data;
  let black = 0;
  for (let i = 0, p = 0; i < bits.length; i++, p += 4) {
    const c = bits[i] ? W : B;
    if (!bits[i]) black++;
    d[p] = c[0]; d[p + 1] = c[1]; d[p + 2] = c[2]; d[p + 3] = 255;
  }
  pctx.putImageData(img, 0, 0);
  $('blackPct').textContent = (black / bits.length * 100).toFixed(1) + '%';
}

// =====================================================================
//  Testo sopra l'immagine — a soglia secca, DOPO il dithering
// =====================================================================
// Il testo non passa per il dithering, ed e' tutto il punto di questa
// sezione: una lettera retinata perde i tratti sottili, e a 400x300 su un
// pannello a 1 bit diventa illeggibile. Si disegna quindi su un canvas a
// parte, si legge con una soglia secca sull'alpha e si stende sopra i bit
// gia' prodotti dalla foto.
//
// E' anche la ragione per cui comporre il biglietto QUI e non a bordo e'
// la strada giusta: il firmware riceve 15.000 byte gia' pronti e non deve
// sapere niente di font, a capo e centrature.
const tcan = document.createElement('canvas');
tcan.width = OUT_W; tcan.height = OUT_H;
const tctx = tcan.getContext('2d', { willReadFrequently: true });

// A capo sulla larghezza utile, rispettando gli a capo scritti a mano.
function wrapTesto(ctx, testo, maxW) {
  const righe = [];
  testo.split('\n').forEach(par => {
    const parole = par.split(/\s+/).filter(Boolean);
    if (!parole.length) { righe.push(''); return; }
    let riga = parole[0];
    for (let i = 1; i < parole.length; i++) {
      const prova = riga + ' ' + parole[i];
      if (ctx.measureText(prova).width <= maxW) riga = prova;
      else { righe.push(riga); riga = parole[i]; }
    }
    righe.push(riga);
  });
  return righe;
}

// Le emoji hanno un canvas SOLO PER LORO perche' vogliono una lettura
// diversa da quella delle lettere: la lettera si legge dall'alpha, e va bene;
// un'emoji a colori ha l'alpha pieno su tutto il glifo, quindi letta allo
// stesso modo diventa una macchia nera. Va letta dalla LUMINANZA e retinata
// come una foto - l'eccezione alla regola qui sopra, e regge solo perche'
// un'emoji e' una figura piena e non ha tratti sottili da perdere.
const ecan = document.createElement('canvas');
ecan.width = OUT_W; ecan.height = OUT_H;
const ectx = ecan.getContext('2d', { willReadFrequently: true });

// Extended_Pictographic non basta da solo: le BANDIERE sono coppie di
// "regional indicator", che pittografici non sono. Senza il secondo ramo una
// bandiera finirebbe fra le lettere e verrebbe letta dall'alpha, cioe' resa
// come un rettangolo nero pieno.
const RE_PITTO = /\p{Extended_Pictographic}|[\u{1F1E6}-\u{1F1FF}]/u;
// Pezzi che non stanno in piedi da soli e vanno appiccicati all'emoji che
// precede: giuntore ZWJ, selettore di presentazione, toni della pelle, tag.
const RE_CONT = /^[\u200D\uFE0F\u{1F3FB}-\u{1F3FF}\u{E0020}-\u{E007F}]+$/u;
const segmenter = (typeof Intl !== 'undefined' && Intl.Segmenter)
  ? new Intl.Segmenter('it', { granularity: 'grapheme' }) : null;

// Spezza una riga in tratti omogenei {emo, s}. Si ragiona per GRAFEMI, non
// per code point: una bandiera, una famiglia o un'emoji col tono della pelle
// sono piu' code point che vanno tenuti insieme, o si disegnerebbero a pezzi
// (e il pezzo staccato non e' nemmeno un'emoji valida). Intl.Segmenter lo fa
// bene; il ramo senza di lui serve solo da rete, e per questo ricuce a mano i
// caratteri di continuazione.
function segmentaRiga(riga) {
  const grafemi = segmenter ? [...segmenter.segment(riga)].map(g => g.segment)
                            : [...riga];
  const out = [];
  for (const g of grafemi) {
    const ultimo = out.length ? out[out.length - 1] : null;
    if (ultimo && ultimo.emo && RE_CONT.test(g)) { ultimo.s += g; continue; }
    const emo = RE_PITTO.test(g);
    if (ultimo && ultimo.emo === emo) ultimo.s += g;
    else out.push({ emo, s: g });
  }
  return out;
}

// --- il font lo risolve CHI APRE LA PAGINA, non l'hub --------------------
// "Impact" c'e' su Windows e non su Android, e il browser ripiega in silenzio
// su un altro carattere. Siccome il .bin lo produce questa pagina, la
// sostituzione finisce dritta sul pannello senza che nessuno l'abbia scelta,
// e la stessa composizione fatta da due dispositivi da' due immagini diverse.
// Non si puo' impedire: si puo' dire.
const GENERICI = new Set(['sans-serif', 'serif', 'monospace', 'cursive',
                          'fantasy', 'system-ui', 'ui-sans-serif',
                          'ui-serif', 'ui-monospace', 'ui-rounded']);
const fctx = document.createElement('canvas').getContext('2d');
function larghezzaCon(fam) {
  fctx.font = '72px ' + fam;
  return fctx.measureText('MWmwil@1I0OoABCabcgjq').width;
}
// Due riferimenti, non uno: un font assente eredita la metrica del ripiego,
// quindi lo si dichiara assente solo se coincide con ENTRAMBI. Con un solo
// riferimento, un carattere che per caso misura come il monospace darebbe un
// falso allarme ad ogni cambio di selezione.
function fontInstallato(nome) {
  if (GENERICI.has(nome.toLowerCase())) return true;
  const q = '"' + nome.replace(/"/g, '') + '",';
  return larghezzaCon(q + 'monospace') !== larghezzaCon('monospace')
      || larghezzaCon(q + 'serif')     !== larghezzaCon('serif');
}
function verificaFont() {
  const nomi = $('txtFont').value.split(',')
                 .map(n => n.trim().replace(/^[\'"]|[\'"]$/g, ''));
  const box = $('fontWarn');
  if (fontInstallato(nomi[0])) { box.hidden = true; return; }
  const ripiego = nomi.slice(1).find(fontInstallato);
  box.hidden = false;
  box.textContent = nomi[0] + ' non c\u2019\u00e8 su questo dispositivo: si comporr\u00e0 con '
    + (ripiego || 'il carattere di ripiego del sistema')
    + ', quindi da un altro computer la stessa scritta verr\u00e0 diversa.';
}

function applicaTesto(bits) {
  const testo = $('txt').value.replace(/\s+$/, '');
  if (!testo.trim()) return;

  const size   = +$('txtSize').value;
  const chiaro = $('txtInv').checked;     // inchiostro bianco su fondo nero
  const fondo  = $('txtBg').value;
  const MARG   = 14;

  tctx.clearRect(0, 0, OUT_W, OUT_H);
  ectx.clearRect(0, 0, OUT_W, OUT_H);
  tctx.font = ($('txtBold').checked ? '700 ' : '400 ') + size + 'px ' + $('txtFont').value;
  // I tratti si posizionano a mano, uno dopo l'altro: il centraggio non puo'
  // piu' farlo il canvas, perche' la riga si disegna su DUE canvas diversi e
  // ciascuno vedrebbe solo la propria meta'.
  tctx.textAlign = 'left';
  tctx.textBaseline = 'alphabetic';
  ectx.font = tctx.font;
  ectx.textAlign = 'left';
  ectx.textBaseline = 'alphabetic';

  const righe = wrapTesto(tctx, testo, OUT_W - 2 * MARG);
  // Le emoji sono piu' alte delle lettere: con l'interlinea del solo testo si
  // toccherebbero fra una riga e l'altra.
  const conEmoji = RE_PITTO.test(testo);
  const passo = Math.round(size * (conEmoji ? 1.34 : 1.22));
  const alt   = righe.length * passo;

  // Lo slider muove il CENTRO del blocco, non la prima riga: cosi' si
  // comporta allo stesso modo con una riga o con cinque.
  const meta = alt / 2;
  let cy = Math.round(OUT_H * (+$('txtY').value) / 100);
  cy = clamp(cy, meta + 4, Math.max(meta + 4, OUT_H - meta - 4));
  const y0 = cy - meta;

  // La banda si stende PRIMA, sui bit della foto: deve coprirla, non
  // fondersi con lei.
  if (fondo === 'band') {
    const yA = clamp(Math.round(y0 - size * 0.34), 0, OUT_H);
    const yB = clamp(Math.round(y0 + alt + size * 0.20), 0, OUT_H);
    const v  = chiaro ? 0 : 1;
    for (let y = yA; y < yB; y++) bits.fill(v, y * OUT_W, y * OUT_W + OUT_W);
  }

  tctx.fillStyle = '#fff';
  let haEmoji = false;
  righe.forEach((r, i) => {
    if (!r) return;
    const y = y0 + i * passo + size * 0.80;
    // La riga intera si misura una volta sola: partendo dal suo bordo sinistro
    // e avanzando tratto per tratto, testo ed emoji restano allineati anche se
    // finiscono su canvas diversi.
    let x = (OUT_W - tctx.measureText(r).width) / 2;
    for (const t of segmentaRiga(r)) {
      if (t.emo) { ectx.fillText(t.s, x, y); haEmoji = true; }
      else       { tctx.fillText(t.s, x, y); }
      x += tctx.measureText(t.s).width;
    }
  });

  const px  = tctx.getImageData(0, 0, OUT_W, OUT_H).data;
  const epx = haEmoji ? ectx.getImageData(0, 0, OUT_W, OUT_H).data : null;
  const ink = chiaro ? 1 : 0;

  // Alone: senza, un testo nero su una zona scura della foto sparisce, e la
  // banda non sempre si vuole. Si dilata la maschera di 2 px e la corona si
  // tinge del colore opposto.
  if (fondo === 'outline') {
    const m = new Uint8Array(OUT_W * OUT_H);
    // Anche le emoji entrano nella maschera: senza, un'emoji scura su una zona
    // scura della foto sparirebbe mentre le lettere accanto si vedono.
    for (let i = 0, p = 3; i < m.length; i++, p += 4)
      m[i] = (px[p] > 128 || (epx && epx[p] > 128)) ? 1 : 0;
    const R = 2;
    for (let y = 0; y < OUT_H; y++) {
      for (let x = 0; x < OUT_W; x++) {
        if (m[y * OUT_W + x]) continue;
        let vicino = false;
        for (let dy = -R; dy <= R && !vicino; dy++) {
          const yy = y + dy;
          if (yy < 0 || yy >= OUT_H) continue;
          for (let dx = -R; dx <= R; dx++) {
            const xx = x + dx;
            if (xx < 0 || xx >= OUT_W) continue;
            if (m[yy * OUT_W + xx]) { vicino = true; break; }
          }
        }
        if (vicino) bits[y * OUT_W + x] = ink ? 0 : 1;
      }
    }
  }

  for (let i = 0, p = 3; i < bits.length; i++, p += 4)
    if (px[p] > 128) bits[i] = ink;

  if (epx) applicaEmoji(bits, epx, ink, $('txtEmojiFlat').checked);
}

// L'emoji si stende DOPO il testo; non si sovrappongono mai, perche' i due
// tratti occupano posizioni diverse della stessa riga.
function applicaEmoji(bits, epx, ink, sagoma) {
  if (sagoma) {
    for (let i = 0, p = 3; i < bits.length; i++, p += 4)
      if (epx[p] > 128) bits[i] = ink;
    return;
  }
  // Composita su bianco prima di misurare il grigio: l'emoji e' disegnata su
  // trasparente, e un pixel semitrasparente letto come nero darebbe un bordo
  // sporco tutt'intorno alla figura.
  const g = new Float32Array(OUT_W * OUT_H);
  for (let i = 0, p = 0; i < g.length; i++, p += 4) {
    const a = epx[p + 3] / 255;
    const y = 0.2126 * epx[p] + 0.7152 * epx[p + 1] + 0.0722 * epx[p + 2];
    g[i] = y * a + 255 * (1 - a);
  }
  // Atkinson e non Floyd-Steinberg: piu' contrasto e niente "vermi", che su
  // una figura piccola e piena e' quello che serve.
  const eb = ditherDiffuse(g, K_ATKINSON, false);

  // Solo DENTRO la sagoma: fuori, il bianco del composito cancellerebbe la
  // foto sotto. La soglia bassa sull'alpha tiene i bordi antialiasati, che a
  // questa dimensione sono quasi tutto il contorno del glifo. Con l'inchiostro
  // chiaro l'emoji si rovescia come il testo, o resterebbe una figura scura su
  // una banda scura.
  for (let i = 0, p = 3; i < bits.length; i++, p += 4)
    if (epx[p] > 24) bits[i] = ink ? (eb[i] ^ 1) : eb[i];
}

// =====================================================================
//  Render completo, coalescato su requestAnimationFrame
// =====================================================================
let pending = false;
function render() {
  if (pending) return;
  pending = true;
  requestAnimationFrame(() => {
    pending = false;
    drawStage();
    if (showingBin) return;
    // Senza foto ma con del testo si compone lo stesso: un biglietto di sole
    // parole su carta bianca e' una pagina legittima del pannello.
    if (!srcCanvas && !$('txt').value.trim()) return;

    let bits;
    if (srcCanvas) {
      drawCrop(wctx);
      const gray = toGray(wctx.getImageData(0, 0, OUT_W, OUT_H));

      const algo = $('algo').value, serp = $('serp').checked;
      if (algo === 'fs')         bits = ditherDiffuse(gray, K_FS, serp);
      else if (algo === 'atk')   bits = ditherDiffuse(gray, K_ATKINSON, serp);
      else if (algo === 'bayer') bits = ditherOrdered(gray);
      else                       bits = ditherThreshold(gray);

      if ($('inv').checked) for (let i = 0; i < bits.length; i++) bits[i] ^= 1;
    } else {
      bits = new Uint8Array(OUT_W * OUT_H).fill(1);   // carta bianca
    }

    // Dopo l'inversione, o si ribalterebbe anche il testo.
    applicaTesto(bits);

    paint(bits);
    lastPacked = pack(bits);
  });
}

// =====================================================================
//  Interazione col palco
// =====================================================================
// Il canvas ha una backing store di 600x440 ma il CSS puo' mostrarlo piu'
// piccolo: ogni coordinata del mouse va riportata in scala.
function stageScale() { return STAGE_W / stage.getBoundingClientRect().width; }

let dragging = false, lastX = 0, lastY = 0;
stage.addEventListener('pointerdown', e => {
  if (!srcCanvas) return;
  dragging = true; lastX = e.clientX; lastY = e.clientY;
  stage.setPointerCapture(e.pointerId);
  stage.classList.add('drag');
});
stage.addEventListener('pointermove', e => {
  if (!dragging) return;
  const k = stageScale();
  view.ox += (e.clientX - lastX) * k;
  view.oy += (e.clientY - lastY) * k;
  lastX = e.clientX; lastY = e.clientY;
  showingBin = false;
  render();
});
['pointerup', 'pointercancel'].forEach(ev => stage.addEventListener(ev, e => {
  dragging = false;
  stage.classList.remove('drag');
  try { stage.releasePointerCapture(e.pointerId); } catch (_) {}
}));
stage.addEventListener('wheel', e => {
  if (!srcCanvas) return;
  e.preventDefault();
  const r = stage.getBoundingClientRect(), k = stageScale();
  const px = (e.clientX - r.left) * k - FRAME_X;   // in spazio di uscita
  const py = (e.clientY - r.top)  * k - FRAME_Y;
  zoomAt(px, py, e.deltaY < 0 ? 1.12 : 1 / 1.12);
  showingBin = false;
  render();
}, { passive: false });

// Frecce = spostamento fine di 1 pixel del pannello.
window.addEventListener('keydown', e => {
  if (!srcCanvas || /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
  const step = e.shiftKey ? 10 : 1;
  const map = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] };
  const m = map[e.key];
  if (!m) return;
  e.preventDefault();
  view.ox += m[0]; view.oy += m[1];
  showingBin = false;
  render();
});

// Trascinamento file + incolla dagli appunti.
const wrap = $('stageWrap');
['dragenter', 'dragover'].forEach(ev =>
  document.addEventListener(ev, e => { e.preventDefault(); wrap.classList.add('over'); }));
['dragleave', 'drop'].forEach(ev =>
  document.addEventListener(ev, e => { e.preventDefault(); wrap.classList.remove('over'); }));
document.addEventListener('drop', e => {
  const f = e.dataTransfer.files[0];
  if (f && f.type.startsWith('image/')) loadImage(f);
});
document.addEventListener('paste', e => {
  for (const it of e.clipboardData.items)
    if (it.type.startsWith('image/')) { loadImage(it.getAsFile()); break; }
});

// =====================================================================
//  Controlli
// =====================================================================
$('btnOpen').onclick = () => $('fileImg').click();
$('fileImg').onchange = e => { if (e.target.files[0]) loadImage(e.target.files[0]); };

$('btnFit').onclick  = () => { fitView('contain'); showingBin = false; render(); };
$('btnFill').onclick = () => { fitView('cover');   showingBin = false; render(); };
$('btnRotL').onclick = () => { rotation = (rotation + 270) % 360; applyRotation(); fitView('cover'); showingBin = false; render(); };
$('btnRotR').onclick = () => { rotation = (rotation +  90) % 360; applyRotation(); fitView('cover'); showingBin = false; render(); };

// Slider: aggiorna l'etichetta e ridisegna ad ogni movimento.
const sliders = [['bri', 'vBri', v => v], ['con', 'vCon', v => v], ['gam', 'vGam', v => (v / 100).toFixed(2)]];
sliders.forEach(([id, lbl, fmt]) => {
  $(id).addEventListener('input', () => {
    $(lbl).textContent = fmt(+$(id).value);
    showingBin = false;
    render();
  });
});
$('btnReset').onclick = () => {
  $('bri').value = 0; $('con').value = 0; $('gam').value = 100;
  $('vBri').textContent = '0'; $('vCon').textContent = '0'; $('vGam').textContent = '1.00';
  showingBin = false; render();
};

['algo', 'serp', 'inv'].forEach(id =>
  $(id).addEventListener('change', () => { showingBin = false; render(); }));
// Testo: ogni ritocco ridisegna. E il testo da solo rende esportabile la
// pagina, anche senza nessuna foto caricata — altrimenti i due pulsanti
// resterebbero spenti davanti a un biglietto perfettamente pronto.
function testoCambiato() {
  if ($('txt').value.trim() || srcCanvas)
    ['btnSave', 'btnSend'].forEach(id => $(id).disabled = false);
  showingBin = false;
  render();
}
$('txt').addEventListener('input', testoCambiato);
['txtFont', 'txtBold', 'txtBg', 'txtInv', 'txtEmojiFlat'].forEach(id =>
  $(id).addEventListener('change', testoCambiato));
$('txtFont').addEventListener('change', verificaFont);
$('txtY').addEventListener('input', () => {
  $('vTxtY').textContent = $('txtY').value;
  testoCambiato();
});

// Il corpo si regola da due comandi che restano un valore solo: lo slider per
// cercarlo a occhio, la casella per rimettere domani lo stesso numero di oggi.
function corpo(n) {
  $('txtSize').value = n; $('txtSizeN').value = n;
  $('vTxtSize').textContent = n;
  testoCambiato();
}
$('txtSize').addEventListener('input', e => corpo(+e.target.value));
// Mentre si digita il campo NON si corregge: per scrivere 120 si passa da 1, e
// un clamp ad ogni tasto lo riscriverebbe sotto le dita. Si accetta solo un
// numero gia' plausibile, e si sistema all'uscita.
$('txtSizeN').addEventListener('input', e => {
  const v = Math.round(+e.target.value);
  if (v >= 8 && v <= 140) {
    $('txtSize').value = v; $('vTxtSize').textContent = v;
    testoCambiato();
  }
});
$('txtSizeN').addEventListener('change', e =>
  corpo(clamp(Math.round(+e.target.value) || 30, 8, 140)));

verificaFont();

$('inkSim').addEventListener('change', render);
$('zoom2').addEventListener('change', e => preview.classList.toggle('z2', e.target.checked));

// Nome file: stessa regola difensiva di sd_name_is_safe() lato firmware.
function safeName() {
  let n = $('name').value.replace(/[^A-Za-z0-9_-]/g, '').slice(0, 24);
  return n || 'immagine';
}
$('name').addEventListener('input', () => {
  $('pathLbl').textContent = '/images/' + safeName() + '.bin';
});

$('btnSave').onclick = () => {
  if (!lastPacked) return;
  const n = safeName();
  $('name').value = n;
  $('pathLbl').textContent = '/images/' + n + '.bin';
  const url = URL.createObjectURL(new Blob([lastPacked], { type: 'application/octet-stream' }));
  const a = document.createElement('a');
  a.href = url; a.download = n + '.bin';
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  say(`Salvato ${n}.bin — ${lastPacked.length} byte`, 'ok');
};

// Invio diretto alla scheda: chiude la catena browser -> hub senza passare
// da un file salvato a mano. E' un POST cross-origin (questa pagina gira da
// file locale), quindi l'hub deve mandare gli header CORS - li' c'e' il
// CorsMiddleware apposta. L'autenticazione va messa a mano nell'header
// Authorization: il browser non puo' usare le credenziali salvate su una
// richiesta cross-origin con origin "*".
// Questa pagina vive in due modi: servita dall'hub su /immagini, oppure aperta
// da disco per lavorarci comodi. Cambia solo come si manda il .bin.
//
//  servita dalla scheda  ->  richiesta RELATIVA e same-origin: le credenziali
//                            le rimette il browser da solo (e' gia' dentro la
//                            basic auth di quel dominio), quindi la riga con
//                            host e password non serve e si nasconde.
//  aperta da file://     ->  richiesta cross-origin verso l'IP dell'hub, con
//                            Authorization scritto a mano: con origin "*" il
//                            browser NON manda le credenziali salvate.
const SERVITA_DA_SCHEDA = location.protocol.startsWith('http');

if (SERVITA_DA_SCHEDA) {
  $('remoto').style.display = 'none';
  $('nav').style.display = 'block';
} else {
  // Da disco i link interni dell'hub non portano da nessuna parte.
  $('lnkPannello').removeAttribute('href');
}

$('btnSend').onclick = async () => {
  if (!lastPacked) return;
  const n = safeName();
  $('name').value = n;
  $('pathLbl').textContent = '/images/' + n + '.bin';

  const fd = new FormData();
  fd.append('img', new Blob([lastPacked], { type: 'application/octet-stream' }), n + '.bin');
  const url = '/api/immagini?nome=' + encodeURIComponent(n);
  const opts = { method: 'POST', body: fd };
  let dove = url;

  if (!SERVITA_DA_SCHEDA) {
    const host = $('hubHost').value.trim().replace(/^https?:\/\//, '').replace(/\/$/, '');
    dove = 'http://' + host + url;
    opts.headers = { Authorization: 'Basic ' + btoa($('hubUser').value + ':' + $('hubPass').value) };
  }

  say('Invio...', '');
  try {
    const r = await fetch(dove, opts);
    const t = await r.text();
    say(r.ok ? `Caricata: /images/${n}.bin (15.000 byte)` : `Errore ${r.status}: ${t}`,
        r.ok ? 'ok' : 'err');
  } catch (e) {
    say('Non raggiungibile: ' + e.message, 'err');
  }
};

// Rilettura di un .bin: verifica il giro completo e, allo stesso tempo, e' il
// renderer che servira' alla galleria dell'hub.
$('btnLoadBin').onclick = () => $('fileBin').click();
$('fileBin').onchange = async e => {
  const f = e.target.files[0];
  if (!f) return;
  const buf = new Uint8Array(await f.arrayBuffer());
  if (buf.length !== OUT_BYTES) {
    say(`${f.name}: ${buf.length} byte, ne servono esattamente ${OUT_BYTES}`, 'err');
    return;
  }
  showingBin = true;
  paint(unpack(buf));
  say(`${f.name} riletto e disegnato — le regolazioni ripartono dall'immagine sorgente`, 'ok');
  e.target.value = '';
};

function say(t, cls) { const m = $('msg'); m.textContent = t; m.className = cls || ''; }

drawStage();
</script>
</body>
</html>

)DITHERPAGE";
