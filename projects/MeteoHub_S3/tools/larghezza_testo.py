#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Quanto e' largo un testo sul pannello, prima di disegnarlo.

    python tools/larghezza_testo.py FreeSans9pt7b "-10,5"
    python tools/larghezza_testo.py --riga3          (riga min/max del blocco nodo)
    python tools/larghezza_testo.py --piede          (le due meta del piede)

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


def riga3():
    """Le prove del caso peggiore per la terza riga del blocco comodo (v38).

    Le coordinate sono quelle scritte in drawNodoComodo(): se si toccano li',
    si toccano anche qui, o questa verifica smette di dire la verita'.
    """
    F = "FreeSans9pt7b"
    prove = [
        # (testo, x di partenza, x oltre il quale invade qualcos'altro, cosa)
        ("24h",           12,  46, "il minimo"),
        ("-10,5",         46,  92, "la barra del giorno"),
        ("in raccolta",   46, 274, "la freccia del trend"),
        ("-10,5",        224, 274, "la freccia del trend"),
    ]
    ko = 0
    for testo, x, limite, chi in prove:
        w = larghezza(F, testo)
        fine = x + w
        esito = "ok " if fine <= limite else "SFORA"
        if fine > limite:
            ko += 1
        print("%-14s x=%3d  w=%3d  fine=%3d  (limite %3d, poi c'e' %s)  %s"
              % ('"' + testo + '"', x, w, fine, limite, chi, esito))

    # Il delta e' allineato a DESTRA a 388: si controlla da dove comincia.
    for testo in ("+1,5/3h", "+12,3/3h", "-12,3/3h"):
        w = larghezza(F, testo)
        inizio = 388 - w
        esito = "ok " if inizio >= 296 else "SFORA"
        if inizio < 296:
            ko += 1
        print("%-14s w=%3d  inizia a %3d  (la freccia arriva a 296)      %s"
              % ('"' + testo + '"', w, inizio, esito))

    # Controprova: la variante SCARTATA. Deve sforare -- e' il motivo per cui
    # sul pannello si legge "+1,5/3h" e non "+1,5 hPa/3h", e senza questa riga
    # fra sei mesi qualcuno rimetterebbe l'unita' "che ci sta benissimo".
    w = larghezza(F, "+12,3 hPa/3h")
    print('%-14s w=%3d  inizia a %3d  --> scartata apposta, invade la freccia'
          % ('"+12,3 hPa/3h"', w, 388 - w))
    if 388 - w >= 296:
        print("  ATTENZIONE: non sfora piu'. Se il layout e' cambiato, "
              "rileggere questa prova invece di fidarsi.")
        ko += 1
    return ko


def piede():
    """Il piede della pagina nodi: sinistra + destra sulla stessa riga (v39).

    Da v39 la riserva non e' piu' un numero fisso -- si misura la stringa di
    destra e la sinistra si accorcia finche' ci sta. Qui si verifica che la
    piu' lunga delle sinistre entri accanto alla piu' larga delle destre.
    """
    F = "FreeSans9pt7b"
    destre = ["SD 14,6 GB", "SD NON MONTATA", "ESP-NOW NON ATTIVO",
              "ASSOCIAZIONE 2:00"]
    # Da v39 il conteggio si scrive solo quando dice qualcosa (nodi muti o non
    # mostrati): nel caso normale la riga comincia dall'IP.
    sinistre = [
        "192.168.1.72   agg. ~11:09",          # il caso normale, tutto intero
        "192.168.1.12",
        "WiFi assente",
    ]
    sinistre_allarme = [
        "8 nodi, 2 muto (+4 non mostrati)   192.168.1.72   agg. ~11:09",
        "8 nodi, 2 muto (+4 non mostrati)   192.168.1.12",
        "8 nodi, 2 muto (+4 non mostrati)",
        "2 muto",                              # l'ultimo gradino: solo l'allarme
    ]
    ko = 0
    for etichetta, gruppo in (("normale", sinistre), ("allarme", sinistre_allarme)):
        print("--- riga di sinistra, caso %s ---" % etichetta)
        for d in destre:
            dw = larghezza(F, d)
            negativo = (d == "SD NON MONTATA")
            xDestra = 400 - (16 if negativo else 12)
            limite = xDestra - dw - (6 if negativo else 0) - 12
            scelta = None
            for s in gruppo:
                if 12 + larghezza(F, s) <= limite:
                    scelta = s
                    break
            print('  destra %-20s (%3d px, limite sx %3d)  ->  %s'
                  % ('"' + d + '"', dw, limite,
                     ('"' + scelta + '"') if scelta else "NESSUNA sinistra ci sta"))
            if scelta is None:
                ko += 1
        print("")
    print("Regola: si prende la prima riga di sinistra che entra, e l'ultima e'")
    print("sempre corta abbastanza -- cosi' il piede non si sovrappone mai.")
    return ko


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--riga3":
        sys.exit(1 if riga3() else 0)
    if len(sys.argv) == 2 and sys.argv[1] == "--piede":
        sys.exit(1 if piede() else 0)
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    print(larghezza(sys.argv[1], sys.argv[2]))
