#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Controlla che OGNI pagina servita dall'hub porti il piede di navigazione
completo.

    python tools/controlla_piedi.py

PERCHE' ESISTE. Il piede vive in piu' posti — e' il prezzo, gia' documentato in
CLAUDE.md, di non avere un template condiviso — e per giunta in DUE formati
diversi: <nav> nelle pagine nuove, <p class="muted"> in quelle piu' vecchie.
Aggiungendo /analisi in v52 una sostituzione fatta su un formato solo ne ha
lasciati indietro DUE, fra cui la home.

E' un difetto che a mano non si trova, perche' le pagine dimenticate sono
proprio quelle che non si aprono mai. E non e' cosmetico: `/` puo' essere
sostituita da una dashboard sulla card, e se quella non ha i link — o e' rotta
— le altre pagine restano raggiungibili solo digitando l'URL a memoria.

Esce con codice 1 se manca qualcosa, cosi' si puo' mettere in un hook.
"""

import io
import os
import re
import sys

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')

# Le voci che ogni piede deve avere.
VOCI = ['/pannello', '/analisi', '/immagini', '/pagine', '/api', '/update']

# Dove vivono le pagine: sorgenti C++ con blocchi PROGMEM, piu' i file .html
# che stanno sulla card (e da cui si rigenerano gli header).
SORGENTI_CPP = ['web_ui.cpp', 'net_ota.cpp']
SORGENTI_HTML = ['www/dashboard.html', 'www/dither.html', 'www/analisi.html']


def blocchi_progmem(testo, nomefile):
    """Ogni pagina HTML in PROGMEM, come (etichetta, corpo)."""
    out = []
    for m in re.finditer(r'static const char (\w+)\[\] PROGMEM = R"(\w+)\(', testo):
        nome, delim = m.group(1), m.group(2)
        fine = testo.find(')%s"' % delim, m.end())
        if fine < 0:
            continue
        corpo = testo[m.end():fine]
        # Solo le pagine vere: un blocco PROGMEM che non e' HTML non ha piede.
        if '<body' in corpo or '<html' in corpo:
            out.append((nomefile + ':' + nome, corpo))
    return out


def main():
    pagine = []
    for f in SORGENTI_CPP:
        testo = io.open(os.path.join(BASE, f), encoding='utf-8').read()
        pagine += blocchi_progmem(testo, f)
    for f in SORGENTI_HTML:
        pagine.append((f, io.open(os.path.join(BASE, f), encoding='utf-8').read()))

    print('%-42s %s' % ('PAGINA', 'VOCI MANCANTI'))
    print('-' * 70)
    guai = 0
    for nome, corpo in pagine:
        manca = [v for v in VOCI if ('href="%s"' % v) not in corpo]
        if manca:
            guai += 1
            print('%-42s %s' % (nome, ', '.join(manca)))
        else:
            print('%-42s -' % nome)

    print()
    if guai:
        print('%d pagina/e con voci mancanti nel piede' % guai)
        return 1
    print('tutte le %d pagine portano il piede completo' % len(pagine))
    return 0


if __name__ == '__main__':
    sys.exit(main())
