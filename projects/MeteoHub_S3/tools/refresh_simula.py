#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Quanti refresh farebbe il pannello, rigiocando i CSV veri dei nodi.

    python tools/refresh_simula.py <cartella con i CSV scaricati>

I CSV si prendono dall'hub, uno per nodo e per giorno:

    curl -u admin:admin "http://<ip>/api/nodi/scarica?nodo=<nome>&d=2026-09-01" \\
         -o "<nome>_2026-09-01.csv"

PERCHE' ESISTE. Il pannello si ridisegna quando `firmaValori()` cambia, quindi
ogni ritocco a quella funzione cambia quanto lavora il display -- e dal vivo la
risposta arriva dopo ore, con l'uptime azzerato ad ogni OTA. Qui si rigiocano i
dati veri e si conta subito.

E' servito il 2026-09-01 per una domanda precisa: le voci aggiunte in v38
(min/max 24 h, delta a 3 ore) hanno aumentato i refresh? Risposta: **uno al
giorno** -- min e max cambiano 11 e 22 volte su 771 pacchetti, e il delta si
muove quasi sempre insieme alla pressione, che era gia' in firma. Il sospetto
era ragionevole e sbagliato, e senza rigiocare i CSV sarebbe rimasto tale.

La scoperta piu' utile e' un'altra: **il limite ai refresh e' la CADENZA dei
nodi, non la firma.** A 299 s il massimo consentito e' 288 refresh al giorno e
se ne fanno 287: il confronto dei valori ne evita uno. Chi vuole ridurli deve
alzare la cadenza minima, non affinare la firma.

Il modello e' validato: prevede ~12 refresh/h e l'hub ne ha misurati 12,2.
"""
import csv
import glob
import io
import os
import sys
from collections import defaultdict

# La cadenza osservata dei nodi: sotto questa distanza un refresh non parte,
# anche se i valori sono cambiati (NODI_MIN_MS e cadenzaNodiMs nel .ino).
CADENZA_S = 299


def leggi(path):
    righe = []
    with io.open(path, encoding="utf-8", errors="replace") as f:
        for r in csv.DictReader(f):
            try:
                ts = int(r["ts_unix"])
            except (KeyError, ValueError):
                continue

            def num(k):
                v = (r.get(k) or "").strip()
                try:
                    return float(v)
                except ValueError:
                    return None
            righe.append((ts, num("temp_c"), num("hum_pct"), num("press_hpa")))
    return righe


def q(v, dec):
    """Come firmaComeScritto() nel firmware: il valore com'e' SCRITTO."""
    if v is None:
        return None
    return int(round(v * (1.0 if dec == 0 else 10.0)))


def simula(nodi, ore=24, hum_dec=0, con_minmax=True):
    """(refresh, eventi, cambi per componente) nelle ultime `ore`."""
    eventi = sorted((r[0], nome, r) for nome, righe in nodi.items() for r in righe)
    if not eventi:
        return 0, 0, {}
    limite = eventi[-1][0] - ore * 3600
    eventi = [e for e in eventi if e[0] >= limite]

    storia = defaultdict(list)
    stato_nodo = {}
    disegnata = None
    ultimo_refresh = None
    refresh = 0
    cambi = defaultdict(int)

    for ts, nome, r in eventi:
        storia[nome].append(r)
        finestra = [x for x in storia[nome] if x[0] >= ts - 24 * 3600]
        temps = [x[1] for x in finestra if x[1] is not None]
        mn = q(min(temps), 1) if temps else None
        mx = q(max(temps), 1) if temps else None
        d3 = None
        if r[3] is not None:
            vecchi = [x for x in storia[nome] if x[3] is not None and x[0] <= ts - 3 * 3600]
            if vecchi:
                d3 = q(r[3] - vecchi[-1][3], 1)

        stato = (q(r[1], 1), q(r[2], hum_dec), q(r[3], 1))
        if con_minmax:
            stato += (mn, mx, d3)

        prec = stato_nodo.get(nome)
        if prec is not None:
            for i, nome_comp in enumerate(("temp", "umid", "press", "min24", "max24", "delta3h")):
                if i < len(stato) and stato[i] != prec[i]:
                    cambi[nome_comp] += 1
        stato_nodo[nome] = stato

        firma = tuple(sorted(stato_nodo.items()))
        if disegnata is None:
            disegnata, ultimo_refresh, refresh = firma, ts, 1
        elif firma != disegnata and (ts - ultimo_refresh) >= CADENZA_S:
            refresh += 1
            disegnata, ultimo_refresh = firma, ts

    return refresh, len(eventi), cambi


def main(cartella):
    nodi = {}
    for f in sorted(glob.glob(os.path.join(cartella, "*.csv"))):
        nome = os.path.basename(f).rsplit("_", 1)[0]
        nodi.setdefault(nome, []).extend(leggi(f))
    if not nodi:
        sys.exit("nessun CSV in " + cartella)
    for n in nodi:
        nodi[n].sort()
        print("%-16s %5d campioni" % (n, len(nodi[n])))

    print("\n=== refresh in 24 h ===")
    varianti = (
        ("v37   umidita' a 0,1, senza min/max",  dict(hum_dec=1, con_minmax=False)),
        ("v40   umidita' a 0,1, con min/max",    dict(hum_dec=1, con_minmax=True)),
        ("v41   umidita' com'e' SCRITTA (0 dec)", dict(hum_dec=0, con_minmax=True)),
    )
    for etichetta, kw in varianti:
        r, ev, _ = simula(nodi, **kw)
        print("  %-40s %4d refresh   (%d pacchetti)" % (etichetta, r, ev))

    # Il tetto: un refresh ogni CADENZA_S, cioe' il massimo che la cadenza
    # consente. Se il numero qui sopra ci somiglia, la firma non sta evitando
    # niente -- e il collo di bottiglia e' la cadenza.
    print("  %-40s %4d refresh   <- il tetto della cadenza"
          % ("(nessun confronto dei valori)", 24 * 3600 // CADENZA_S))

    print("\n=== chi fa cambiare la firma ===")
    _, ev, cambi = simula(nodi)
    for k in ("temp", "umid", "press", "min24", "max24", "delta3h"):
        print("  %-8s %4d volte su %d pacchetti" % (k, cambi[k], ev))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
