#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Genera dither_page.h da dither.html.

La pagina di composizione delle immagini vive in UN posto solo, `dither.html`,
che si apre anche da disco per lavorarci comodi. Il firmware pero' la serve
su /immagini, e per farlo le serve dentro un array PROGMEM: invece di tenere
due copie — che divergerebbero al primo ritocco, e nessuno se ne accorgerebbe
finche' la pagina servita dalla scheda non e' diversa da quella che si sta
guardando sul PC — l'header si RIGENERA da qui.

    python www/gen_page.py

Da rilanciare dopo ogni modifica a dither.html, prima di ricompilare. Il file
generato e' versionato apposta: chi clona il repo compila senza dover eseguire
niente.
"""

import io
import os
import sys

QUI = os.path.dirname(os.path.abspath(__file__))
SORGENTE = os.path.join(QUI, 'dither.html')
USCITA = os.path.join(os.path.dirname(QUI), 'dither_page.h')

# Delimitatore del raw string C++: deve essere una sequenza che nella pagina
# non compare mai, o chiuderebbe la stringa a meta' file.
DELIM = 'DITHERPAGE'


def main():
    html = io.open(SORGENTE, encoding='utf-8').read()

    fine = ')' + DELIM + '"'
    if fine in html:
        sys.exit('ERRORE: dither.html contiene %r, che chiuderebbe il raw '
                 'string. Cambiare DELIM in questo script.' % fine)

    testata = (
        '#pragma once\n'
        '\n'
        '// ============================================================\n'
        '//  GENERATO DA www/gen_page.py - NON MODIFICARE A MANO.\n'
        '//  La sorgente e\' www/dither.html: si modifica quella e si\n'
        '//  rilancia  python www/gen_page.py  prima di ricompilare.\n'
        '//  (%d byte di pagina, serviti su /immagini)\n'
        '// ============================================================\n'
        '\n'
        'static const char DITHER_PAGE[] PROGMEM = R"%s(\n'
    ) % (len(html.encode('utf-8')), DELIM)

    coda = '\n)%s";\n' % DELIM

    io.open(USCITA, 'w', encoding='utf-8', newline='\n').write(testata + html + coda)
    print('dither_page.h generato: %d byte di HTML' % len(html.encode('utf-8')))


if __name__ == '__main__':
    main()
