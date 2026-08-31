# -*- coding: utf-8 -*-
"""Le icone del pannello: si disegnano qui, si guardano, si generano.

    python tools/icone.py            # le mostra a schermo, per giudicarle
    python tools/icone.py --c > icone.h

A 1 bit e 14 px di un'icona si vede la SILHOUETTE, non il dettaglio: quelle
che funzionano sono pochissime, e il modo per accorgersene e' guardarle prima
di mandarle sul pannello. Due esempi di cose provate e scartate proprio qui:

  - il BAROMETRO (cerchio con lancetta) veniva identico all'orologio, che sta
    a due centimetri di distanza nella stessa pagina. Per la pressione non c'e'
    icona: "hPa" e' gia' la sua etichetta, e un simbolo ambiguo e' peggio di
    nessun simbolo;
  - la GOCCIA VUOTA per il punto di rugiada si confondeva con la goccia piena
    dell'umidita' relativa. La rugiada resta scritta a parole.

Il file generato (icone.h) e' versionato: chi clona compila senza eseguire
niente, esattamente come per dither_page.h.
"""
import sys

# Ogni icona ha la SUA dimensione: quella accanto al numero da 24 punti deve
# reggere il confronto, quella dentro una riga da 9 punti no. A 14 px la
# goccia si leggeva come un rombo -- la punta si perdeva.
ICONE = {}

# Umidita' relativa. Punta in alto e pancia in basso: si riconosce anche a
# occhi socchiusi, ed e' il motivo per cui e' l'unica forma "liquida" qui.
ICONE["goccia"] = [
    "........##..........",
    "........##..........",
    ".......####.........",
    ".......####.........",
    "......######........",
    "......######........",
    ".....########.......",
    "....##########......",
    "....##########......",
    "...############.....",
    "..##############....",
    "..##############....",
    ".################...",
    ".################...",
    ".################...",
    "..##############....",
    "..##############....",
    "...############.....",
    ".....########.......",
    "....................",
]

# Temperatura: bulbo tondo e colonna. L'unica icona che si legge ancora come
# "temperatura" e non come qualcos'altro. Grande, perche' sta accanto al
# numero piu' grande della pagina.
ICONE["termometro"] = [
    "........####........",
    ".......##..##.......",
    ".......##..##.......",
    ".......##..##.......",
    ".......##..##.......",
    ".......##..##.......",
    ".......##..##.......",
    ".......##..##.......",
    ".......##..##.......",
    ".......##..##.......",
    "......##....##......",
    ".....##..##..##.....",
    "....##..####..##....",
    "....##.######.##....",
    "....##.######.##....",
    "....##.######.##....",
    ".....##..##..##.....",
    "......##....##......",
    ".......######.......",
    "....................",
]

# Minimo e massimo: due frecce opposte. Dice "fra questi due estremi" senza
# una parola, ed e' la piu' leggibile di tutte a distanza.
ICONE["minmax"] = [
    "......##......",
    ".....####.....",
    "....######....",
    "...##.##.##...",
    "......##......",
    "......##......",
    "......##......",
    "......##......",
    "......##......",
    "...##.##.##...",
    "....######....",
    ".....####.....",
    "......##......",
    "..............",
]

# L'ora dell'ultimo pacchetto. Sta dentro la barra nera, quindi si disegna in
# bianco: la forma deve reggere anche invertita, e un cerchio regge.
ICONE["orologio"] = [
    "....######....",
    "..##......##..",
    ".#..........#.",
    "#.....#......#",
    "#.....#......#",
    "#.....#......#",
    "#.....####...#",
    "#............#",
    "#............#",
    "#............#",
    ".#..........#.",
    "..##......##..",
    "....######....",
    "..............",
]


def dim(righe):
    return len(righe[0]), len(righe)


def mostra(nome, righe):
    print("  " + nome)
    for r in righe:
        print("    " + r.replace("#", "@@").replace(".", "  "))
    print()


def in_c(nome, righe):
    w, h = dim(righe)
    byte = []
    largo = ((w + 7) // 8) * 8           # drawBitmap vuole righe intere di byte
    for r in righe:
        assert len(r) == w, (nome, r, len(r))
        v = 0
        for x in range(largo):
            v = (v << 1) | (1 if (x < w and r[x] == "#") else 0)
            if x % 8 == 7:
                byte.append(v)
                v = 0
    linee = []
    for i in range(0, len(byte), 8):
        linee.append("  " + ", ".join("0x%02X" % b for b in byte[i:i + 8]) + ",")
    N = nome.upper()
    return ("static const int16_t IC_%s_W = %d, IC_%s_H = %d;\n"
            "static const uint8_t IC_%s[] PROGMEM = {\n%s\n};"
            % (N, w, N, h, N, "\n".join(linee)))


if __name__ == "__main__":
    if "--c" in sys.argv:
        print("// Generato da tools/icone.py -- non modificare a mano.")
        print("// 1 bit per pixel, righe intere di byte: il formato che")
        print("// Adafruit_GFX::drawBitmap() si aspetta.")
        print("#pragma once")
        print("")
        for nome, righe in ICONE.items():
            print(in_c(nome, righe))
            print("")
    else:
        for nome, righe in ICONE.items():
            mostra(nome, righe)
