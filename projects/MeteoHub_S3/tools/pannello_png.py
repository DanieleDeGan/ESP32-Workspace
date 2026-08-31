#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Guarda il pannello dell'hub da riga di comando.

    python tools/pannello_png.py 192.168.1.73 pannello.png [scala]

Scarica GET /api/pannello/anteprima (15.000 byte: 400x300 a 1 bit, MSB per
primo, 50 byte per riga, 1 = bianco -- lo stesso formato dei .bin delle
immagini) e ne fa un PNG. Niente dipendenze: zlib basta a scrivere un PNG, e
Pillow non serve.

Serve perche' il pannello e-ink e' l'unica parte di questa scheda che non si
puo' interrogare: /api/pannello dice QUALE pagina e' in mostra, non che cosa
c'e' sopra. Con -c si confronta l'anteprima con un .bin atteso, ed e' una
verifica esatta invece che a occhio:

    python tools/pannello_png.py 192.168.1.73 out.png -c QuadCanada.bin

Attenzione al limite: l'anteprima e' cio' che l'hub HA DISEGNATO, non i fotoni
sul vetro. Se il display non rispondesse, l'anteprima sarebbe comunque giusta.
"""
import base64
import struct
import sys
import zlib

try:
    from urllib.request import Request, urlopen
except ImportError:                                    # python 2
    from urllib2 import Request, urlopen

W, H, STRIDE = 400, 300, 50
BYTES = STRIDE * H
UTENTE, PASSWORD = "admin", "admin"                    # basic-auth della scheda


def scarica(host):
    url = "http://%s/api/pannello/anteprima" % host
    cred = base64.b64encode(("%s:%s" % (UTENTE, PASSWORD)).encode()).decode()
    r = Request(url, headers={"Authorization": "Basic " + cred})
    d = urlopen(r, timeout=20).read()
    if len(d) != BYTES:
        sys.exit("attesi %d byte, ne sono arrivati %d" % (BYTES, len(d)))
    return d


def png(dati, path, scala=1):
    righe = []
    for y in range(H):
        riga = bytearray()
        for x in range(W):
            bit = (dati[y * STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1
            riga += (b"\xff" if bit else b"\x00") * scala
        for _ in range(scala):
            righe.append(b"\x00" + bytes(riga))        # filtro 0 a inizio riga

    def chunk(tipo, corpo):
        return (struct.pack(">I", len(corpo)) + tipo + corpo +
                struct.pack(">I", zlib.crc32(tipo + corpo) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", W * scala, H * scala, 8, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                chunk(b"IDAT", zlib.compress(b"".join(righe), 9)) +
                chunk(b"IEND", b""))


def nero(dati):
    return sum(8 - bin(b).count("1") for b in bytearray(dati))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    host, dst = sys.argv[1], sys.argv[2]
    scala = 1
    atteso = None
    resto = sys.argv[3:]
    if "-c" in resto:
        atteso = resto[resto.index("-c") + 1]
        resto = resto[:resto.index("-c")]
    if resto:
        scala = int(resto[0])

    d = scarica(host)
    png(d, dst, scala)
    print("%s -> %s (%dx%d), %.1f%% di nero"
          % (host, dst, W * scala, H * scala, nero(d) * 100.0 / (W * H)))

    if atteso:
        a = open(atteso, "rb").read()
        uguali = (a == d)
        print("confronto con %s: %s" % (atteso, "IDENTICI" if uguali else "DIVERSI"))
        if not uguali:
            diversi = sum(1 for x, y in zip(bytearray(a), bytearray(d)) if x != y)
            print("  %d byte diversi su %d" % (diversi, len(d)))
            sys.exit(1)
