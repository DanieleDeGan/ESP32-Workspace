#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Genera <nome>_page.h da <nome>.html.

Le pagine grosse dell'hub vivono in UN posto solo, il loro .html, che si apre
anche da disco per lavorarci comodi. Il firmware pero' le serve, e per farlo
gli servono dentro un array PROGMEM: invece di tenere due copie — che
divergerebbero al primo ritocco, e nessuno se ne accorgerebbe finche' la
pagina servita dalla scheda non e' diversa da quella che si sta guardando sul
PC — l'header si RIGENERA da qui.

    python www/gen_page.py            # dither  (il default storico)
    python www/gen_page.py analisi

Da rilanciare dopo ogni modifica alla pagina, prima di ricompilare. I file
generati sono versionati apposta: chi clona il repo compila senza eseguire
niente.

Il default senza argomenti resta `dither` perche' l'hook di Claude Code in
.claude/settings.json lo invoca cosi'.
"""

import io
import os
import re
import sys

QUI = os.path.dirname(os.path.abspath(__file__))

# nome -> (simbolo PROGMEM, rotta su cui viene servita)
PAGINE = {
    'dither':  ('DITHER_PAGE',  '/immagini'),
    'analisi': ('ANALISI_PAGE', '/analisi'),
}


def avvisa_hidden(nome, html):
    """Avvisa se la pagina usa l'attributo `hidden` senza una regola per
    [hidden] mentre delle classi impostano `display`.

    NON e' teoria: [hidden]{display:none} lo mette il foglio di stile del
    BROWSER, che ha priorita' minore di qualunque regola d'autore. Con
    .barra{display:flex} le barre marcate `hidden` restavano visibili, e nella
    pagina di analisi il sintomo era "i selettori non mostrano i giorni" —
    perche' si vedevano, vuoti, prima che qualcuno li riempisse. La causa non
    somiglia al sintomo, e in JavaScript non c'e' niente da trovare.
    """
    usa = re.search(r'<[^>]+\shidden(\s|>)', html) is not None
    if not usa:
        return

    # I commenti si tolgono PRIMA di cercare, o il controllo si autoassolve:
    # la prima versione taceva perche' trovava "[hidden]" dentro il commento
    # che spiegava il difetto. Un controllo che legge la documentazione invece
    # del codice non controlla niente.
    pulito = re.sub(r'/\*.*?\*/', '', html, flags=re.S)

    # E si cerca la REGOLA (`[hidden]` seguito da una graffa), non la stringa.
    if re.search(r'\[hidden\][^{}]*\{', pulito):
        return
    classi = re.findall(r'\.[A-Za-z_-][\w-]*\s*\{[^}]*display\s*:', pulito)
    if classi:
        print('ATTENZIONE (%s.html): usa l\'attributo `hidden` e %d regola/e di '
              'classe impostano `display`, ma non c\'e\' nessuna regola per '
              '[hidden]. Nel browser quelle regole VINCONO e gli elementi '
              'nascosti restano visibili. Aggiungi:  [hidden]{display:none'
              '!important}' % (nome, len(classi)))


def genera(nome):
    if nome not in PAGINE:
        sys.exit('ERRORE: pagina %r sconosciuta. Note: %s'
                 % (nome, ', '.join(sorted(PAGINE))))

    simbolo, rotta = PAGINE[nome]
    sorgente = os.path.join(QUI, nome + '.html')
    uscita = os.path.join(os.path.dirname(QUI), nome + '_page.h')

    # Delimitatore del raw string C++: deve essere una sequenza che nella
    # pagina non compare mai, o chiuderebbe la stringa a meta' file.
    delim = nome.upper() + 'PAGE'

    html = io.open(sorgente, encoding='utf-8').read()
    avvisa_hidden(nome, html)

    fine = ')' + delim + '"'
    if fine in html:
        sys.exit('ERRORE: %s.html contiene %r, che chiuderebbe il raw '
                 'string. Cambiare il delimitatore.' % (nome, fine))

    testata = (
        '#pragma once\n'
        '\n'
        '// ============================================================\n'
        '//  GENERATO DA www/gen_page.py - NON MODIFICARE A MANO.\n'
        '//  La sorgente e\' www/%s.html: si modifica quella e si\n'
        '//  rilancia  python www/gen_page.py %s  prima di ricompilare.\n'
        '//  (%d byte di pagina, serviti su %s)\n'
        '// ============================================================\n'
        '\n'
        'static const char %s[] PROGMEM = R"%s(\n'
    ) % (nome, nome, len(html.encode('utf-8')), rotta, simbolo, delim)

    coda = '\n)%s";\n' % delim

    io.open(uscita, 'w', encoding='utf-8', newline='\n').write(testata + html + coda)
    print('%s_page.h generato: %d byte di HTML' % (nome, len(html.encode('utf-8'))))


if __name__ == '__main__':
    genera(sys.argv[1] if len(sys.argv) > 1 else 'dither')
