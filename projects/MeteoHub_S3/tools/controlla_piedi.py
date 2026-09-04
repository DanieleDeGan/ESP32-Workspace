#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Controlla che OGNI pagina servita dall'hub porti il piede di navigazione
completo.

    python tools/controlla_piedi.py                      # i sorgenti nel repo
    python tools/controlla_piedi.py --host 192.168.1.72  # cio' che la SCHEDA serve

PERCHE' ESISTE. Il piede vive in piu' posti — e' il prezzo, gia' documentato in
CLAUDE.md, di non avere un template condiviso — e per giunta in DUE formati
diversi: <nav> nelle pagine nuove, <p class="muted"> in quelle piu' vecchie.
Aggiungendo /analisi in v52 una sostituzione fatta su un formato solo ne ha
lasciati indietro DUE, fra cui la home.

E' un difetto che a mano non si trova, perche' le pagine dimenticate sono
proprio quelle che non si aprono mai. E non e' cosmetico: `/` puo' essere
sostituita da una dashboard sulla card, e se quella non ha i link — o e' rotta
— le altre pagine restano raggiungibili solo digitando l'URL a memoria.

PERCHE' SERVONO DUE MODI, ed e' la lezione del 2026-09-04: con i soli sorgenti
questo controllo dava "tutte e nove a posto" mentre la home **servita** non
aveva il link. Le pagine sostituibili vivono sulla **card**, e il file nel repo
e' solo un sorgente: ricompilare il firmware non la cambia. Il controllo sui
sorgenti verifica cio' che si e' scritto, `--host` verifica cio' che la scheda
fa davvero — e quando divergono ha ragione la scheda.

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


def dalla_scheda(host, utente, password):
    """Le pagine COME LE SERVE la scheda: e' l'unico controllo che vede anche
    quelle sostituite dalla card."""
    import base64
    try:
        import urllib.request as ur
    except ImportError:
        import urllib2 as ur

    auth = base64.b64encode(('%s:%s' % (utente, password)).encode()).decode()

    def prendi(path):
        req = ur.Request('http://%s%s' % (host, path),
                         headers={'Authorization': 'Basic ' + auth})
        return ur.urlopen(req, timeout=30).read().decode('utf-8', 'replace')

    # L'elenco delle pagine lo dice la scheda, non questo script: una rotta
    # nuova compare perche' e' REGISTRATA, non perche' qualcuno l'ha aggiunta
    # qui. Stessa disciplina di /api/elenco.
    import json
    elenco = json.loads(prendi('/api/pagine/elenco'))
    out = []
    for p in elenco['pagine']:
        etichetta = '%s  (%s)' % (p['path'],
                                  'card, caricata con %s' % p['fw_caricata']
                                  if p.get('su_card') else 'firmware')
        out.append((etichetta, prendi(p['path'])))
    out.append(('/update  (firmware)', prendi('/update')))
    return out, elenco


def main():
    argv = sys.argv[1:]
    host = None
    utente, password = 'admin', 'admin'
    if '--host' in argv:
        i = argv.index('--host')
        host = argv[i + 1]
        if '--user' in argv:
            utente = argv[argv.index('--user') + 1]
        if '--pass' in argv:
            password = argv[argv.index('--pass') + 1]

    if host:
        pagine, elenco = dalla_scheda(host, utente, password)
        print('dalla scheda %s, firmware %s\n' % (host, elenco['fw']))
        vecchie = [p for p in elenco['pagine']
                   if p.get('su_card') and p.get('fw_caricata') != elenco['fw']]
        for p in vecchie:
            print('ATTENZIONE: %s sta sulla card ed e\' stata caricata con %s, '
                  'non con %s' % (p['path'], p['fw_caricata'], elenco['fw']))
        if vecchie:
            print()
        return controlla(pagine)

    pagine = []
    for f in SORGENTI_CPP:
        testo = io.open(os.path.join(BASE, f), encoding='utf-8').read()
        pagine += blocchi_progmem(testo, f)
    for f in SORGENTI_HTML:
        pagine.append((f, io.open(os.path.join(BASE, f), encoding='utf-8').read()))
    return controlla(pagine)


def controlla(pagine):

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
