#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Quanto e' largo un testo sul pannello, prima di disegnarlo.

    python tools/larghezza_testo.py FreeSans9pt7b "-10,5"
    python tools/larghezza_testo.py --righe          (le due righe di testo del blocco)
    python tools/larghezza_testo.py --testata        (il nome del nodo e il badge MUTO)

Legge i .h veri dei font Adafruit GFX e somma gli xAdvance dei glifi, che e'
esattamente quello che fa Adafruit_GFX avanzando il cursore -- quindi il numero
che esce e' la larghezza che avra' sul vetro, non una stima.

PERCHE' ESISTE. Su questo pannello un testo troppo largo non da' errore: si
sovrappone a quello accanto, o esce dal bordo e sparisce. Il caso che sfugge
non e' quello di oggi ma quello di fra tre mesi -- "-10,5" e' 41 px dove "21,4"
ne era 28 -- e lo si scopre guardando il vetro, cioe' troppo tardi. Con questo,
le coordinate della pagina nodi (v38) sono state verificate PRIMA dell'OTA: tre
erano sbagliate, e una avrebbe scritto il minimo sopra la barra del giorno ogni
volta che la temperatura fosse scesa sotto zero.

E' lo stesso mestiere di tools/pannello_png.py, un passo prima: quello guarda
cosa l'hub HA disegnato, questo dice cosa ci starebbe.
"""
import os
import re
import sys

# I font stanno con la libreria Adafruit GFX, non nel repo. Se un domani
# l'installazione si sposta, e' l'unica riga da cambiare.
FONT_DIR = os.path.expanduser(
    "~/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts")


def carica(nome):
    """I glifi del font: { offset, larghezza, altezza, xAdvance, xOff, yOff }."""
    percorso = os.path.join(FONT_DIR, nome + ".h")
    if not os.path.exists(percorso):
        sys.exit("font non trovato: " + percorso)
    testo = open(percorso, encoding="utf-8", errors="replace").read()
    blocco = testo[testo.index("Glyphs[]"):]
    trovati = re.findall(
        r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}",
        blocco)
    return [tuple(int(x) for x in g) for g in trovati]


def larghezza(font, s, _cache={}):
    """Larghezza in pixel. I Free* partono tutti dallo spazio (0x20)."""
    if font not in _cache:
        _cache[font] = carica(font)
    glifi = _cache[font]
    tot = 0
    for ch in s:
        i = ord(ch) - 0x20
        if 0 <= i < len(glifi):
            tot += glifi[i][3]
    return tot


def righe():
    """Le due righe di testo del blocco nodo (v59).

    Le coordinate sono quelle scritte in drawNodoComodo(): se si toccano li',
    si toccano anche qui, o questa verifica smette di dire la verita'.

      riga A   36 [temperatura 18pt] +6 [grado] +9 ["C"]   176 [umidita' 12pt]
               ... e a destra la pressione, che finisce a 388-36, piu' "hPa"
      riga B   21 ["C"]  42 [tre variazioni, drawFila si ferma a 300]
               ... la freccia sta a 300, il delta della pressione finisce a 388
    """
    ko = 0
    G18, G12, N9 = "FreeSansBold18pt7b", "FreeSansBold12pt7b", "FreeSans9pt7b"

    print("--- riga A: temperatura, umidita', pressione ---")
    # Il caso peggiore non e' quello di oggi ma l'inverno: "-10,5" e' molto piu'
    # largo di "21,4", e la "C" col cerchietto gli va dietro.
    for t in ("27,5", "-10,5", "-9,9"):
        fine = 36 + larghezza(G18, t) + 6 + 8 + larghezza(G12, "C")
        esito = "ok " if fine <= 150 else "SFORA (invade l'icona della goccia)"
        print('  temperatura "%-6s" finisce a %3d  (la goccia sta a 150)  %s'
              % (t, fine, esito))
        if fine > 150:
            ko += 1

    for rh in ("48%", "100%"):
        fine = 176 + larghezza(G12, rh)
        # a destra: "hPa" (32 px) piu' la pressione, che comincia a 388-36-w
        inizio = 388 - 36 - larghezza(G12, "1016,9")
        esito = "ok " if fine + 12 <= inizio else "SFORA (tocca la pressione)"
        print('  umidita\'    "%-6s" finisce a %3d  (la pressione comincia a %d)  %s'
              % (rh, fine, inizio, esito))
        if fine + 12 > inizio:
            ko += 1

    # La riserva per l'unita': con 30 px il nove di "1016,9" toccava l'acca.
    print('  "hPa" e\' largo %d px: la riserva e\' 36, quindi restano %d px di stacco'
          % (larghezza(N9, "hPa"), 36 - larghezza(N9, "hPa")))
    if 36 - larghezza(N9, "hPa") < 3:
        print("    ATTENZIONE: meno di 3 px, il numero tocca l'unita'")
        ko += 1

    print("")
    print("--- riga B: le tre variazioni, poi la freccia ---")
    print('  prefisso "C" da 21: finisce a %d  (le voci partono da 42)'
          % (21 + larghezza(N9, "C")))
    for peggiore in ("+0,2", "-10,5"):
        x = 42
        for eti in ("1h", "2h", "3h"):
            x += larghezza(N9, eti + " " + peggiore) + 14
        fine = x - 14
        esito = "ok " if fine <= 300 else "TRONCATA da drawFila"
        print('  tre voci "%-5s" finiscono a %3d  (drawFila si ferma a 300)  %s'
              % (peggiore, fine, esito))
        if fine > 300:
            ko += 1
    for d in ("+1,5/3h", "+12,3/3h", "-12,3/3h"):
        inizio = 388 - larghezza(N9, d)
        esito = "ok " if inizio >= 313 else "SFORA (sotto la freccia)"
        print('  delta pressione "%-9s" comincia a %3d  (la freccia arriva a 311)  %s'
              % (d, inizio, esito))
        if inizio < 313:
            ko += 1

    print("")
    print("--- le etichette del grafico, allineate a destra a 43 ---")
    for v in ("27,5", "-10,5", "-9,9"):
        inizio = 43 - larghezza(N9, v)
        esito = "ok " if inizio >= 2 else "esce dal bordo sinistro"
        print('  "%-6s" comincia a %3d  %s' % (v, inizio, esito))
        if inizio < 2:
            ko += 1
    return ko


def testata():
    """Il nome del nodo e il badge MUTO (v59).

    Il badge sta a 388-83 = 305 ed e' largo 71: il nome parte da 10 e non deve
    arrivarci sotto. E la scritta dentro il badge non deve uscire DAL BADGE --
    e' il difetto trovato in v59: 54 px di riquadro per 55 di testo, cioe' la O
    disegnata in bianco su bianco. Sul pannello si leggeva "MUT".
    """
    ko = 0
    w = larghezza("FreeSansBold9pt7b", "MUTO")
    print('"MUTO" e\' largo %d px; il riquadro e\' 71 con il testo a +8, quindi'
          % w)
    print("  finisce a %d su 71: %s" % (8 + w, "ok" if 8 + w <= 71 else "ESCE DAL NERO"))
    if 8 + w > 71:
        ko += 1
    print("  (fino alla v58 il riquadro era 54: la O finiva fuori, invisibile)")
    print("")

    nomi = ["MeteoEsp32", "Meteo-7EAE0C", "MeteoNodeLungo12", "NodoCantinaNord1"]
    for nome in nomi:
        fine = 10 + larghezza("FreeSansBold9pt7b", nome)
        esito = "ok " if fine <= 305 else "sotto il badge MUTO"
        print('  %-18s finisce a %3d  (il badge comincia a 305)  %s'
              % ('"' + nome + '"', fine, esito))
        if fine > 305:
            ko += 1
    return ko


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--righe":
        sys.exit(1 if righe() else 0)
    if len(sys.argv) == 2 and sys.argv[1] == "--testata":
        sys.exit(1 if testata() else 0)
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    print(larghezza(sys.argv[1], sys.argv[2]))
