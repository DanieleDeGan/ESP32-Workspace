#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Disegna una pagina del pannello SENZA la scheda, con i font veri.

    python tools/pannello_mock.py --prova out.png 2      (la pagina nodi)
    python tools/pannello_mock.py --casi <cartella>      (i casi limite)
    python tools/pannello_mock.py --valida <ip>          (mock contro vetro)

E' `Adafruit_GFX` rifatto in Python su una tela 400x300 a 1 bit: gli stessi
`.h` dei font (glifi E bitmap), le stesse icone di `icone.h`, le stesse
primitive (`fillRect`, `drawLine`, `drawCircle`, `fillCircle`, `fillTriangle`,
`fillRoundRect`, `drawBitmap`) e lo stesso avanzamento del cursore. Esce un PNG,
oppure i 15.000 byte nel formato di `/api/pannello/anteprima`.

PERCHE' ESISTE. Fino alla `v58` un layout nuovo si giudicava in un modo solo:
compilare, fare l'OTA, aspettare che i nodi ritrasmettessero e guardare
l'anteprima -- cinque minuti e un riavvio per spostare una riga di tre pixel.
Le larghezze si potevano gia' misurare (`larghezza_testo.py`), ma una riga che
sta dentro puo' lo stesso essere brutta: il ritmo verticale, il grigio della
pagina, cosa si vede da tre metri, quelle cose li' si vedono solo guardando.
Ora si guardano prima, quante volte si vuole. Il layout della `v59` e' nato
cosi': sei proposte rese e confrontate senza toccare la scheda.

E' il terzo strumento della stessa famiglia, e insieme coprono le tre domande:
`larghezza_testo.py` dice **se ci sta**, questo dice **come viene**, e
`pannello_png.py` dice **cosa c'e' davvero sul vetro**.

DUE REGOLE PER CHI LO TOCCA:

1. Dove il C ha un cast, qui ci va un `int()` NELLO STESSO POSTO. `x -
   (int16_t)(dx * L)` fa 275, `int(x - dx * L)` fa 274: un pixel, e l'asta
   della freccia parte da un altro punto. E' stata l'ultima differenza a
   sparire nella validazione.
2. Le divisioni intere del C troncano verso lo zero, quelle di Python
   arrotondano verso il basso. Per i numeri negativi -- una temperatura
   d'inverno, un delta -- non e' la stessa cosa: c'e' `_tronca()`.

FINO A DOVE CI SI PUO' FIDARE. `--valida` fa ridisegnare la scheda, ne scarica
l'anteprima e la confronta pixel per pixel con la stessa scena disegnata qui:
e' la prova che il mock non stia mentendo, e va rifatta quando si tocca il
codice di disegno del `.ino`. Restano fuori due cose, per costruzione: il mock
non sa **quali dati** avra' il pannello (glieli si danno) e non sa niente del
**vetro** -- contrasto, aloni, refresh parziali.
"""
import base64
import io
import os
import re
import struct
import sys
import zlib

W, H, STRIDE = 400, 300, 50
BIANCO, NERO = 1, 0                      # come nel .bin: bit 1 = bianco

FONT_DIR = os.path.expanduser(
    "~/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts")
ICONE_H = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "icone.h")


def _tronca(n, d):
    """La divisione intera del C: verso lo zero, non verso il basso."""
    if d == 0:
        return 0
    q = abs(n) // abs(d)
    return q if (n >= 0) == (d > 0) else -q


# ---------------------------------------------------------------------------
#  I font veri
# ---------------------------------------------------------------------------
_FONT_CACHE = {}


def font(nome):
    """{glifi, bitmap, primo, yAdvance} dal .h di Adafruit GFX."""
    if nome in _FONT_CACHE:
        return _FONT_CACHE[nome]
    percorso = os.path.join(FONT_DIR, nome + ".h")
    if not os.path.exists(percorso):
        sys.exit("font non trovato: " + percorso)
    testo = io.open(percorso, encoding="utf-8", errors="replace").read()

    dati = testo[testo.index("Bitmaps[]"):testo.index("Glyphs[]")]
    bitmap = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", dati)]

    blocco = testo[testo.index("Glyphs[]"):]
    glifi = [tuple(int(v) for v in g) for g in re.findall(
        r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}",
        blocco)]

    coda = re.search(r"0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,\s*(\d+)\s*\}",
                     testo)
    f = {"glifi": glifi, "bitmap": bitmap,
         "primo": int(coda.group(1), 16) if coda else 0x20,
         "yAdvance": int(coda.group(3)) if coda else 0}
    _FONT_CACHE[nome] = f
    return f


_ICONE_CACHE = {}


def icona(nome):
    """(larghezza, altezza, byte) di un'icona di icone.h."""
    if nome in _ICONE_CACHE:
        return _ICONE_CACHE[nome]
    testo = io.open(ICONE_H, encoding="utf-8", errors="replace").read()
    m = re.search(r"%s_W\s*=\s*(\d+),\s*%s_H\s*=\s*(\d+)" % (nome, nome), testo)
    if not m:
        sys.exit("icona non trovata: " + nome)
    w, h = int(m.group(1)), int(m.group(2))
    corpo = testo[testo.index("%s[] PROGMEM" % nome):]
    corpo = corpo[:corpo.index("};")]
    byte = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", corpo)]
    _ICONE_CACHE[nome] = (w, h, byte)
    return _ICONE_CACHE[nome]


# ---------------------------------------------------------------------------
#  La tela: Adafruit_GFX, ridotto a cio' che il pannello usa davvero
# ---------------------------------------------------------------------------
class Tela(object):
    def __init__(self, w=W, h=H):
        self.w, self.h = w, h
        self.px = [[BIANCO] * w for _ in range(h)]
        self.font = None
        self.cx = self.cy = 0
        self.colore = NERO

    # --- primitive ---------------------------------------------------------
    def width(self):
        return self.w

    def height(self):
        return self.h

    def fillScreen(self, c=BIANCO):
        self.px = [[c] * self.w for _ in range(self.h)]

    def drawPixel(self, x, y, c=NERO):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def fillRect(self, x, y, w, h, c=NERO):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)):
                self.drawPixel(xx, yy, c)

    def drawRect(self, x, y, w, h, c=NERO):
        self.drawFastHLine(x, y, w, c)
        self.drawFastHLine(x, y + h - 1, w, c)
        self.drawFastVLine(x, y, h, c)
        self.drawFastVLine(x + w - 1, y, h, c)

    def drawFastHLine(self, x, y, w, c=NERO):
        self.fillRect(x, y, w, 1, c)

    def drawFastVLine(self, x, y, h, c=NERO):
        self.fillRect(x, y, 1, h, c)

    def drawLine(self, x0, y0, x1, y1, c=NERO):
        """Bresenham, quello di Adafruit_GFX riga per riga."""
        x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)
        ripido = abs(y1 - y0) > abs(x1 - x0)
        if ripido:
            x0, y0, x1, y1 = y0, x0, y1, x1
        if x0 > x1:
            x0, x1, y0, y1 = x1, x0, y1, y0
        dx, dy = x1 - x0, abs(y1 - y0)
        err = _tronca(dx, 2)
        passo = 1 if y0 < y1 else -1
        y = y0
        for x in range(x0, x1 + 1):
            if ripido:
                self.drawPixel(y, x, c)
            else:
                self.drawPixel(x, y, c)
            err -= dy
            if err < 0:
                y += passo
                err += dx

    def drawCircle(self, x0, y0, r, c=NERO):
        f, ddx, ddy, x, y = 1 - r, 1, -2 * r, 0, r
        self.drawPixel(x0, y0 + r, c)
        self.drawPixel(x0, y0 - r, c)
        self.drawPixel(x0 + r, y0, c)
        self.drawPixel(x0 - r, y0, c)
        while x < y:
            if f >= 0:
                y -= 1
                ddy += 2
                f += ddy
            x += 1
            ddx += 2
            f += ddx
            for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                self.drawPixel(x0 + sx * x, y0 + sy * y, c)
                self.drawPixel(x0 + sx * y, y0 + sy * x, c)

    def fillCircleHelper(self, x0, y0, r, angoli, delta, c=NERO):
        """Adafruit_GFX::fillCircleHelper -- serve tale e quale, perche' e'
        anche la meta' di fillRoundRect (il badge MUTO, la pillola)."""
        f, ddF_x, ddF_y, x, y = 1 - r, 1, -2 * r, 0, r
        px, py = x, y
        delta += 1
        while x < y:
            if f >= 0:
                y -= 1
                ddF_y += 2
                f += ddF_y
            x += 1
            ddF_x += 2
            f += ddF_x
            if x < y + 1:
                if angoli & 1:
                    self.drawFastVLine(x0 + x, y0 - y, 2 * y + delta, c)
                if angoli & 2:
                    self.drawFastVLine(x0 - x, y0 - y, 2 * y + delta, c)
            if y != py:
                if angoli & 1:
                    self.drawFastVLine(x0 + py, y0 - px, 2 * px + delta, c)
                if angoli & 2:
                    self.drawFastVLine(x0 - py, y0 - px, 2 * px + delta, c)
                py = y
            px = x

    def fillCircle(self, x0, y0, r, c=NERO):
        self.drawFastVLine(x0, y0 - r, 2 * r + 1, c)
        self.fillCircleHelper(x0, y0, r, 3, 0, c)

    def fillRoundRect(self, x, y, w, h, r, c=NERO):
        massimo = _tronca(w if w < h else h, 2)
        if r > massimo:
            r = massimo
        self.fillRect(x + r, y, w - 2 * r, h, c)
        self.fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, c)
        self.fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, c)

    def fillTriangle(self, x0, y0, x1, y1, x2, y2, c=NERO):
        """Adafruit_GFX::fillTriangle. Non una riscrittura equivalente: la
        divisione intera qui deve troncare come in C, o la punta della freccia
        viene di un pixel diversa."""
        x0, y0, x1, y1, x2, y2 = (int(v) for v in (x0, y0, x1, y1, x2, y2))
        if y0 > y1:
            y0, y1, x0, x1 = y1, y0, x1, x0
        if y1 > y2:
            y2, y1, x2, x1 = y1, y2, x1, x2
        if y0 > y1:
            y0, y1, x0, x1 = y1, y0, x1, x0

        if y0 == y2:                       # tutti sulla stessa riga
            a = b = x0
            a, b = min(a, x1), max(b, x1)
            a, b = min(a, x2), max(b, x2)
            self.drawFastHLine(a, y0, b - a + 1, c)
            return

        dx01, dy01 = x1 - x0, y1 - y0
        dx02, dy02 = x2 - x0, y2 - y0
        dx12, dy12 = x2 - x1, y2 - y1
        sa = sb = 0
        ultima = y1 if y1 == y2 else y1 - 1

        y = y0
        while y <= ultima:
            a = x0 + _tronca(sa, dy01)
            b = x0 + _tronca(sb, dy02)
            sa += dx01
            sb += dx02
            if a > b:
                a, b = b, a
            self.drawFastHLine(a, y, b - a + 1, c)
            y += 1

        sa = dx12 * (y - y1)
        sb = dx02 * (y - y0)
        while y <= y2:
            a = x1 + _tronca(sa, dy12)
            b = x0 + _tronca(sb, dy02)
            sa += dx12
            sb += dx02
            if a > b:
                a, b = b, a
            self.drawFastHLine(a, y, b - a + 1, c)
            y += 1

    def drawBitmap(self, x, y, byte, w, h, c=NERO):
        """Righe intere di byte, MSB per primo: come Adafruit_GFX."""
        stride = (w + 7) // 8
        for yy in range(h):
            for xx in range(w):
                b = byte[yy * stride + (xx >> 3)]
                if (b >> (7 - (xx & 7))) & 1:
                    self.drawPixel(x + xx, y + yy, c)

    # --- testo -------------------------------------------------------------
    def setFont(self, nome):
        self.font = font(nome)

    def setCursor(self, x, y):
        self.cx, self.cy = int(x), int(y)

    def setTextColor(self, c):
        self.colore = c

    def _glifo(self, ch):
        i = ord(ch) - self.font["primo"]
        if i < 0 or i >= len(self.font["glifi"]):
            return None
        return self.font["glifi"][i]

    def drawChar(self, x, y, ch):
        g = self._glifo(ch)
        if g is None:
            return 0
        off, gw, gh, adv, xo, yo = g
        bits = self.font["bitmap"]
        bit, b = 0, 0
        for yy in range(gh):
            for xx in range(gw):
                if bit & 7:
                    b <<= 1
                else:
                    b = bits[off + (bit >> 3)]
                bit += 1
                if b & 0x80:
                    self.drawPixel(x + xo + xx, y + yo + yy, self.colore)
        return adv

    def print(self, s):
        for ch in str(s):
            self.cx += self.drawChar(self.cx, self.cy, ch)

    def getTextBounds(self, s, x=0, y=0):
        """(bx, by, bw, bh) con lo stesso conto di Adafruit_GFX."""
        minx, miny, maxx, maxy = 32767, 32767, -32768, -32768
        cx = x
        for ch in str(s):
            g = self._glifo(ch)
            if g is None:
                continue
            _off, gw, gh, adv, xo, yo = g
            x1, y1 = cx + xo, y + yo
            x2, y2 = x1 + gw - 1, y1 + gh - 1
            minx, miny = min(minx, x1), min(miny, y1)
            maxx, maxy = max(maxx, x2), max(maxy, y2)
            cx += adv
        if maxx < minx:
            return 0, 0, 0, 0
        return minx - x, miny - y, maxx - minx + 1, maxy - miny + 1

    # --- gli aiuti che nel .ino sono funzioni a parte -----------------------
    def drawRight(self, s, xRight, yBase):
        bx, _by, bw, _bh = self.getTextBounds(s)
        self.setCursor(xRight - bw - bx, yBase)
        self.print(s)

    def drawCenter(self, s, xCentro, yBase):
        bx, _by, bw, _bh = self.getTextBounds(s)
        x = xCentro - _tronca(bw, 2) - bx
        self.setCursor(max(2, x), yBase)
        self.print(s)

    # --- uscita ------------------------------------------------------------
    def bin(self):
        """I 15.000 byte del formato .bin/anteprima (1 = bianco)."""
        out = bytearray(STRIDE * self.h)
        for y in range(self.h):
            for x in range(self.w):
                if self.px[y][x]:
                    out[y * STRIDE + (x >> 3)] |= 1 << (7 - (x & 7))
        return bytes(out)

    def nero_pct(self):
        n = sum(riga.count(NERO) for riga in self.px)
        return n * 100.0 / (self.w * self.h)

    def png(self, path, scala=1):
        righe = []
        for y in range(self.h):
            riga = bytearray()
            for x in range(self.w):
                riga += (b"\xff" if self.px[y][x] else b"\x00") * scala
            for _ in range(scala):
                righe.append(b"\x00" + bytes(riga))

        def chunk(tipo, corpo):
            return (struct.pack(">I", len(corpo)) + tipo + corpo +
                    struct.pack(">I", zlib.crc32(tipo + corpo) & 0xFFFFFFFF))

        ihdr = struct.pack(">IIBBBBB", self.w * scala, self.h * scala, 8, 0, 0, 0, 0)
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                    chunk(b"IDAT", zlib.compress(b"".join(righe), 9)) +
                    chunk(b"IEND", b""))
        return path


# ---------------------------------------------------------------------------
#  Aiuti comuni alle scene (le stesse funzioni del .ino)
# ---------------------------------------------------------------------------
def fmtNum(v, dec):
    if v is None:
        return "--"
    return ("%%.%df" % dec % v).replace(".", ",")


def fmtDelta(v, dec):
    if v is None:
        return "--"
    if round(v * (10 ** dec)) == 0:
        return fmtNum(0.0, dec)
    return ("+" if v > 0 else "") + fmtNum(v, dec)


def drawGrado(t, x, y, r):
    t.drawCircle(x, y, r)
    if r > 2:
        t.drawCircle(x, y, r - 1)


def drawFrecciaTrend(t, x, y, trend):
    """trend: 0 ignoto, 1..9 come forecast_trend_t."""
    import math
    ANGOLI = [0, -70, -45, -30, -15, 0, 15, 30, 45, 70]
    if trend == 0:
        t.drawFastHLine(x - 8, y, 6)
        t.drawFastHLine(x + 2, y, 6)
        return
    rad = ANGOLI[trend] * math.pi / 180.0
    dx, dy = math.cos(rad), -math.sin(rad)
    L = 11
    # Il troncamento sul PRODOTTO, come il cast del .ino: vedi la regola 1 in
    # cima al file.
    ddx, ddy = int(dx * L), int(dy * L)
    x0, y0 = x - ddx, y - ddy
    x1, y1 = x + ddx, y + ddy
    t.drawLine(x0, y0, x1, y1)
    t.drawLine(x0, y0 + 1, x1, y1 + 1)
    px, py = -dy, dx
    bx, by = x + int(dx * (L - 7)), y + int(dy * (L - 7))
    t.fillTriangle(x1, y1, bx + int(px * 5), by + int(py * 5),
                   bx - int(px * 5), by - int(py * 5))


# ---------------------------------------------------------------------------
#  Dati di prova: i due nodi VERI, il 6 settembre 2026 alle 16:30
# ---------------------------------------------------------------------------
# Non inventati: valori, variazioni e le 48 mezz'ore vengono dai CSV sulla card
# e da /api/nodi. Una curva finta (una sinusoide) mente su cio' che conta -- una
# giornata vera ha il gradino dell'alba, i pianerottoli e il rumore di mezzo
# grado che decidono se un grafico da 40 px si legge o e' un ghirigoro.
#
# `serie` sono 48 celle da mezz'ora, la piu' vecchia per prima, come le da'
# remote_temp_history(). None = cella vuota: la curva li' NON deve passare.
SERIE_A = [27.5, 27.5, 27.5, 27.5, 27.5, 27.5, 27.5, 27.5, 27.5, 27.5, 27.4,
           27.2, 27.0, 26.9, 26.8, 26.8, 26.7, 26.6, 26.4, 26.3, 26.3, 26.2,
           26.1, 26.0, 25.9, 25.9, 25.7, 25.5, 25.5, 25.6, 25.8, 26.1, 26.4,
           26.4, 26.5, 26.6, 26.6, 26.7, 26.8, 26.8, 26.9, 26.9, 27.0, 27.0,
           27.3, 27.4, 27.5, 27.5]
SERIE_B = [27.4, 27.4, 27.4, 27.4, 27.4, 27.4, 27.4, 27.4, 27.4, 27.4, 27.4,
           27.1, 26.9, 26.9, 26.7, 26.7, 26.6, 26.6, 26.3, 26.3, 26.2, 26.2,
           26.0, 26.0, 25.9, 25.8, 25.6, 25.5, 25.4, 25.5, 25.8, 26.1, 26.4,
           26.3, 26.4, 26.5, 26.6, 26.6, 26.7, 26.8, 26.9, 26.9, 27.0, 27.0,
           27.2, 27.4, 27.4, 27.5]


def dati_prova(quanti=2, buchi=False, muto=False):
    """I nodi da disegnare. `buchi` apre due finestre senza dati nella serie
    del primo nodo -- il caso che la curva deve saper mostrare; `muto` spegne
    il secondo nodo, che e' l'altra condizione che cambia la pagina."""
    a, b = list(SERIE_A), list(SERIE_B)
    if buchi:
        for i in range(12, 15):
            a[i] = None
        a[33] = None
    nodi = [
        {"nome": "MeteoEsp32", "t": 27.53, "rh": 47.85, "p": 1016.94,
         "d": [0.1, 0.5, 0.6], "dp3h": -0.82, "trend": 4,
         "online": True, "ritardo": False, "ora_ultimo": 16.5, "serie": a},
        {"nome": "Meteo-7EAE0C", "t": 27.53, "rh": 47.52, "p": 1018.04,
         "d": [0.2, 0.5, 0.6], "dp3h": -0.81, "trend": 4,
         "online": not muto, "ritardo": False, "ora_ultimo": 16.5, "serie": b},
        {"nome": "Cantina", "t": 18.2, "rh": 62.0, "p": 1017.1,
         "d": [-0.1, -0.2, 0.0], "dp3h": -0.8, "trend": 4,
         "online": True, "ritardo": False, "ora_ultimo": 16.5,
         "serie": [None if v is None else v - 8.5 for v in SERIE_B]},
        {"nome": "Serra", "t": 31.8, "rh": 30.0, "p": None,
         "d": [1.2, 2.4, 3.1], "dp3h": None, "trend": 0,
         "online": True, "ritardo": True, "ora_ultimo": 16.5,
         "serie": [None if v is None else v + 5.0 for v in SERIE_A]},
    ]
    return nodi[:quanti]


# ---------------------------------------------------------------------------
#  La scena della pagina NODI (v59)
# ---------------------------------------------------------------------------
# Deve essere la traduzione FEDELE di screenNodi()/drawNodoComodo() nel .ino,
# non "la stessa idea": stesse costanti, stesso ordine di disegno, stessa
# aritmetica intera. Se le due cose divergono, --valida se ne accorge e questo
# file smette di valere come prova.
NODI_TOP, NODI_BOT = 2, 294
ALLARME_H = 24
NODI_H_GRAFICO = 130
NODI_VISIBILI = 4
NODI_COMODI_FINO_A = 2


def drawFila(t, voci, x, y, xMax, gap):
    for v in voci:
        if not v:
            continue
        _bx, _by, bw, _bh = t.getTextBounds(v)
        if x + bw > xMax:
            return
        t.setCursor(x, y)
        t.print(v)
        x += bw + gap


def drawTestataNodo(t, n, y, h):
    t.setFont("FreeSansBold9pt7b")
    _bx, by, _bw, bh = t.getTextBounds(n["nome"])
    t.setCursor(10, y + _tronca(h - bh, 2) - by)
    t.print(n["nome"])
    t.drawFastHLine(0, y + h - 2, t.width())
    t.drawFastHLine(0, y + h - 1, t.width())

    if not n.get("online", True):
        x, yb = t.width() - 83, y + _tronca(h - 18, 2)
        t.fillRoundRect(x, yb, 71, 18, 4)
        t.setFont("FreeSansBold9pt7b")
        t.setTextColor(BIANCO)
        t.setCursor(x + 8, yb + 14)
        t.print("MUTO")
        t.setTextColor(NERO)
    elif n.get("ritardo"):
        t.setFont("FreeSansBold12pt7b")
        _bx, by2, _bw2, bh2 = t.getTextBounds("!")
        t.drawRight("!", t.width() - 12, y + _tronca(h - bh2, 2) - by2)


def drawDeltaTemp(t, d, yBase, xMax):
    if not any(v is not None for v in d):
        return
    t.setFont("FreeSans9pt7b")
    drawGrado(t, 15, yBase - 10, 3)
    t.setCursor(21, yBase)
    t.print("C")
    voci = [e + " " + fmtDelta(v, 1) for e, v in zip(("1h", "2h", "3h"), d)]
    drawFila(t, voci, 42, yBase, xMax, 14)


def drawGrafico24h(t, n, x0, y0, x1, y1, yEtichette):
    """La curva delle 24 h. `serie` arriva in gradi; dentro si torna ai DECIMI,
    che e' cio' su cui il firmware fa i conti."""
    serie = [None if v is None else int(round(v * 10)) for v in n["serie"]]
    vals = [v for v in serie if v is not None]
    t.setFont("FreeSans9pt7b")
    if len(vals) < 2:
        t.setCursor(x0, y1 - 8)
        t.print("in raccolta")
        return

    vMinVero, vMaxVero = min(vals), max(vals)
    vMin, vMax = vMinVero, vMaxVero
    if vMax - vMin < 20:
        centro = _tronca(vMax + vMin, 2)
        vMin, vMax = centro - 10, centro + 10
    margine = _tronca(vMax - vMin, 12) + 1
    vMin -= margine
    vMax += margine

    t.drawFastHLine(x0, y1, x1 - x0)
    oraLoc = int(n["ora_ultimo"])
    minLoc = int(round((n["ora_ultimo"] - oraLoc) * 60))
    dt0 = (oraLoc % 6) * 3600 + minLoc * 60
    for k in range(5):
        dt = dt0 + k * 6 * 3600
        if dt > 24 * 3600:
            break
        x = x1 - _tronca((x1 - x0) * dt, 24 * 3600)
        if x < x0:
            break
        t.drawFastVLine(x, y1 + 1, 3)
        ora = ((oraLoc - (oraLoc % 6)) - 6 * k + 24) % 24
        s = "%02d" % ora
        _bx, _by, bw, _bh = t.getTextBounds(s)
        cx = x - _tronca(bw, 2)
        if cx < x0:
            cx = x0
        if cx + bw > 388:
            cx = 388 - bw
        t.setCursor(cx, yEtichette)
        t.print(s)

    span = (vMax - vMin) or 1
    yMax = y1 - _tronca((vMaxVero - vMin) * (y1 - y0), span)
    yMin = y1 - _tronca((vMinVero - vMin) * (y1 - y0), span)
    t.drawRight(fmtNum(vMaxVero / 10.0, 1), x0 - 5, yMax + 5)
    t.drawRight(fmtNum(vMinVero / 10.0, 1), x0 - 5, yMin + 5)

    xPrec = yPrec = None
    n_slot = len(serie)
    for i, v in enumerate(serie):
        if v is None:
            xPrec = None
            continue
        x = x0 + _tronca((x1 - x0) * i, n_slot - 1 if n_slot > 1 else 1)
        y = y1 - _tronca((v - vMin) * (y1 - y0), span)
        if xPrec is not None:
            t.drawLine(xPrec, yPrec, x, y)
            t.drawLine(xPrec, yPrec + 1, x, y + 1)
        xPrec, yPrec = x, y
    if xPrec is not None:
        t.fillCircle(xPrec, yPrec, 2)


def drawAllarme(t, testo):
    t.setFont("FreeSansBold9pt7b")
    _bx, _by, bw, _bh = t.getTextBounds(testo)
    t.fillRoundRect(12, NODI_BOT - 20, bw + 20, 20, 5)
    t.setTextColor(BIANCO)
    t.setCursor(22, NODI_BOT - 5)
    t.print(testo)
    t.setTextColor(NERO)


def _numeri(t, n, yBase, yIcona, yGrado):
    if n["t"] is not None:
        iw, ih, ib = icona("IC_TERMOMETRO")
        t.drawBitmap(10, yIcona, ib, iw, ih)
        t.setFont("FreeSansBold18pt7b")
        t.setCursor(36, yBase)
        s = fmtNum(n["t"], 1)
        t.print(s)
        _bx, _by, bw, _bh = t.getTextBounds(s, 36, yBase)
        xu = 36 + bw + 6
        drawGrado(t, xu + 3, yGrado, 3)
        t.setFont("FreeSansBold12pt7b")
        t.setCursor(xu + 9, yBase)
        t.print("C")
    if n["rh"] is not None:
        iw, ih, ib = icona("IC_GOCCIA")
        t.drawBitmap(150, yIcona + 3, ib, iw, ih)
        t.setFont("FreeSansBold12pt7b")
        t.setCursor(176, yBase)
        t.print(fmtNum(n["rh"], 0) + "%")
    if n["p"] is not None:
        t.setFont("FreeSans9pt7b")
        t.drawRight("hPa", 388, yBase)
        t.setFont("FreeSansBold12pt7b")
        t.drawRight(fmtNum(n["p"], 1), 388 - 36, yBase)


def _barometro(t, n, yBase):
    if n["dp3h"] is None:
        return
    t.setFont("FreeSans9pt7b")
    t.drawRight(fmtDelta(n["dp3h"], 1) + "/3h", 388, yBase)
    drawFrecciaTrend(t, 300, yBase - 5, n["trend"])


def blocco_comodo(t, n, y, h):
    drawTestataNodo(t, n, y, 21)
    if n["t"] is None and n["rh"] is None:
        t.setFont("FreeSans9pt7b")
        t.setCursor(14, y + 44)
        t.print("in attesa del primo dato")
        return
    _numeri(t, n, y + 50, y + 27, y + 33)
    drawDeltaTemp(t, n["d"], y + 72, 300)
    _barometro(t, n, y + 72)
    drawGrafico24h(t, n, 48, y + 82, 388, y + h - 22, y + h - 6)


def blocco_compatto(t, n, y, h):
    drawTestataNodo(t, n, y, 20)
    if n["t"] is None and n["rh"] is None:
        t.setFont("FreeSans9pt7b")
        t.setCursor(14, y + 40)
        t.print("in attesa del primo dato")
        return
    _numeri(t, n, y + 46, y + 25, y + 29)
    drawDeltaTemp(t, n["d"], y + h - 6, 300)
    _barometro(t, n, y + h - 6)


def scena_nodi(t, nodi, allarme=""):
    t.fillScreen(BIANCO)
    t.setTextColor(NERO)
    if not nodi:
        t.setFont("FreeSansBold24pt7b")
        t.drawCenter("NESSUN NODO", 200, 145)
        return
    quanti = min(len(nodi), NODI_VISIBILI)
    bot = NODI_BOT - (ALLARME_H if allarme else 0)
    h = _tronca(bot - NODI_TOP, quanti)
    comodo = quanti <= NODI_COMODI_FINO_A and h >= NODI_H_GRAFICO
    for i in range(quanti):
        (blocco_comodo if comodo else blocco_compatto)(t, nodi[i], NODI_TOP + i * h, h)
    if allarme:
        drawAllarme(t, allarme)


# ---------------------------------------------------------------------------
#  La prova che il mock non mente
# ---------------------------------------------------------------------------
TREND_DA_PAROLA = {
    "in crollo": 1, "in rapida discesa": 2, "in discesa": 3,
    "in lieve discesa": 4, "stabile": 5, "in lieve salita": 6,
    "in salita": 7, "in rapida salita": 8, "in forte salita": 9,
}


def _api(host, percorso, post=False):
    import json
    try:
        from urllib.request import Request, urlopen
    except ImportError:
        from urllib2 import Request, urlopen
    cred = base64.b64encode(b"admin:admin").decode()
    r = Request("http://%s%s" % (host, percorso),
                headers={"Authorization": "Basic " + cred},
                data=b"" if post else None)
    corpo = urlopen(r, timeout=20).read().decode()
    try:
        return json.loads(corpo)
    except ValueError:
        return corpo                       # i POST rispondono "ok", non JSON


def allarme_da_stato(st):
    """La stessa scala di allarmeCorrente() nel .ino, per quel che se ne vede
    da fuori. Se un giorno divergono, --valida lo dice."""
    if st.get("pairing"):
        r = int(st.get("pairing_resta_s", 0))
        return "ASSOCIAZIONE %d:%02d" % (r // 60, r % 60)
    if not st.get("sd"):
        return "SD NON MONTATA"
    if not st.get("wifi"):
        return "WIFI ASSENTE"
    return ""


def valida(host):
    """Il mock contro il vetro: stessa scena, stessi pixel?

    Chiede alla scheda di RIDISEGNARE e aspetta che l'abbia fatto, poi legge i
    valori e l'anteprima. Senza il ridisegno si confronterebbe il disegno di
    adesso con i numeri di adesso, che sono quelli di DOPO: fra un refresh e
    l'altro passano minuti, e in mezzo arrivano pacchetti. Le differenze che ne
    escono sembrano difetti del mock e non lo sono -- e' successo, ed e' il
    motivo per cui questa funzione fa il giro lungo.

    Vuole il firmware da v59: /api/nodi/anello (le 48 celle da cui esce la
    curva) esiste da li'. Ricalcolarle dai CSV darebbe altri numeri -- il CSV ha
    tutti i campioni, l'anello uno per mezz'ora -- e il confronto non sarebbe
    esatto.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from pannello_png import scarica
    import time

    prima = _api(host, "/api/stato")["epd_refresh"]
    _api(host, "/api/pannello/refresh", post=True)
    for _ in range(30):
        st = _api(host, "/api/stato")
        if st["epd_refresh"] != prima:
            break
        time.sleep(1)
    else:
        print("la scheda non ha ridisegnato: mi fermo, il confronto non sarebbe onesto")
        return -1

    j = _api(host, "/api/nodi")
    vero = bytearray(scarica(host))

    nodi = []
    for n in j["nodi"]:
        v = n["valori"] or [None, None, None]
        nodi.append({
            "nome": n["nome"], "t": v[0], "rh": v[1], "p": v[2],
            "d": [n["delta_t_1h"], n["delta_t_2h"], n["delta_t_3h"]],
            "dp3h": n["delta_3h"], "trend": TREND_DA_PAROLA.get(n["trend"], 0),
            "online": n["online"], "ritardo": False,
        })
        a = _api(host, "/api/nodi/anello?nodo=" + n["nome"].replace(" ", "%20"))
        loc = time.localtime(a["ts_ultimo"])
        nodi[-1]["serie"] = a["t"]
        nodi[-1]["ora_ultimo"] = loc.tm_hour + loc.tm_min / 60.0

    t = Tela()
    scena_nodi(t, nodi, allarme_da_stato(st))
    mio = bytearray(t.bin())

    diversi = 0
    for y in range(H):
        for x in range(W):
            a = (vero[y * STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1
            b = (mio[y * STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1
            if a != b:
                diversi += 1
    print("pixel diversi: %d su %d (%.3f%%)"
          % (diversi, W * H, diversi * 100.0 / (W * H)))
    if diversi:
        print("Il mock e il vetro non coincidono piu': o e' cambiato il disegno")
        print("nel .ino senza aggiornare scena_nodi(), o il mock ha un difetto.")
        print("In ogni caso, da qui in avanti le proposte rese con questo")
        print("strumento non valgono come prova.")
    return diversi


def casi(cartella):
    """I casi che sul pannello vero capitano una volta l'anno, e che proprio
    per questo nessuno guarda mentre disegna."""
    prove = [
        ("normale", dati_prova(),           ""),
        ("buchi",   dati_prova(buchi=True), ""),
        ("muto",    dati_prova(muto=True),  "SD NON MONTATA"),
        ("tre",     dati_prova(quanti=3),   ""),
        ("quattro", dati_prova(quanti=4),   ""),
        ("nessuno", [],                     ""),
    ]
    for nome, nodi, al in prove:
        t = Tela()
        scena_nodi(t, nodi, al)
        p = os.path.join(cartella, "caso_%s.png" % nome)
        t.png(p, 2)
        print("%-9s %s  nero %.1f%%" % (nome, p, t.nero_pct()))


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--valida":
        sys.exit(0 if valida(sys.argv[2]) == 0 else 1)
    if len(sys.argv) >= 3 and sys.argv[1] == "--casi":
        casi(sys.argv[2])
        sys.exit(0)
    if len(sys.argv) >= 3 and sys.argv[1] == "--prova":
        t = Tela()
        scena_nodi(t, dati_prova())
        scala = int(sys.argv[3]) if len(sys.argv) > 3 else 2
        t.png(sys.argv[2], scala)
        print("%s (%dx%d), %.1f%% di nero"
              % (sys.argv[2], W * scala, H * scala, t.nero_pct()))
        sys.exit(0)
    sys.exit(__doc__)
