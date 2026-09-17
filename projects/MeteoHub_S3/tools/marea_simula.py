#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""La taratura della marea rifatta fuori dalla scheda, passo per passo.

    python tools/marea_simula.py <cartella CSV> --nodi MeteoEsp32,Meteo-7EAE0C
    python tools/marea_simula.py <cartella CSV> --nodi ... --fino 2026-09-16
    python tools/marea_simula.py <cartella CSV> --nodi ... --confronta 192.168.1.72

I CSV si prendono dall'hub come per analisi.py, uno per nodo e per giorno, e
si chiamano `<nome>_<AAAA-MM-GG>.csv`:

    curl -u admin:admin "http://<ip>/api/nodi/scarica?nodo=<nome>&d=2026-09-01" \\
         -o "<nome>_2026-09-01.csv"

(con una pausa fra una richiesta e l'altra: vedi l'avvertenza in analisi.py).
`--nodi` va dato NELL'ORDINE DEL REGISTRO (`/api/nodi`): il riepilogo li
alterna un giorno per giro, e l'ordine decide chi porta la data per primo.

PERCHE' ESISTE. La tabella `marea_tab` si giudica solo guardando i 24 valori,
e per giudicarli serve sapere che cosa la scheda DOVREBBE avere. Questo
strumento lo dice per due algoritmi:

  v75  la media del giorno tolta a ogni ora, ogni data una volta PER NODO;
  v76  prima la retta fra il primo e l'ultimo campione, ogni data UNA volta.

Il 17/09/2026 la colonna v75 ha dato la tabella della scheda IDENTICA, 24
valori su 24 -- ed e' quello che ha permesso di fidarsi della colonna v76
prima di scrivere il firmware. Dopo un OTA e un `rifai`, `--confronta` legge
`/api/stato` e dice se la scheda coincide col modello: se non coincide, il C e
il modello non fanno la stessa cosa.

LA FEDELTA' STA NEI DETTAGLI, e ognuno e' stato verificato: la pressione e'
un float a 32 bit (atof -> float in sd_read_remote_day), l'arrotondamento e'
lround (meta' lontano da zero), la media mobile divide con troncamento verso
lo zero, la cadenza e' la mediana dei primi 64 delta consecutivi.

Non tratta il giorno del cambio dell'ora (giornoCambiaOra): se la cartella ne
contiene uno, il risultato puo' differire da quello della scheda.
"""
import base64
import csv
import glob
import json
import math
import os
import statistics
import struct
import sys
import urllib.request

UTENTE, PASSWORD = "admin", "admin"                    # basic-auth della scheda


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def leggi(percorso):
    righe = []
    with open(percorso, encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            p = r["press_hpa"].strip()
            righe.append((int(r["ts_unix"]), int(r["seq"]),
                          f32(float(p)) if p else math.nan))
    return righe


def lround(x):
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))


def tronca(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def marea_giorno(righe, retta):
    """daily_add + filtri del .ino + daily_marea. None se il giorno non entra."""
    somma = [0.0] * 24
    n = [0] * 24
    tsom = [0.0] * 24
    p_primo = p_ultimo = math.nan
    t_primo = t_ultimo = 0
    delta = []
    seq_prec = None
    ts_prec = 0
    campioni = 0
    for ts, seq, p in righe:
        if not math.isnan(p):
            h = (ts // 3600) % 24
            somma[h] += p
            n[h] += 1
            tsom[h] += ts
        if ts_prec and len(delta) < 64 and seq_prec is not None and seq == seq_prec + 1:
            if 0 < ts - ts_prec < 65535:
                delta.append(ts - ts_prec)
        ts_prec, seq_prec = ts, seq
        if not math.isnan(p):
            if math.isnan(p_primo):
                p_primo, t_primo = p, ts
            p_ultimo, t_ultimo = p, ts
            campioni += 1
    if len(delta) < 5:
        return None
    cadenza = sorted(delta)[len(delta) // 2]
    attesi = (86400 + cadenza // 2) // cadenza
    if 100.0 * campioni / attesi < 90.0 or abs(p_ultimo - p_primo) > 5.0:
        return None
    if 0 in n:
        return None
    k = (p_ultimo - p_primo) / (t_ultimo - t_primo) if retta and t_ultimo > t_primo else 0.0
    media = [somma[h] / n[h] - k * (tsom[h] / n[h] - t_primo) for h in range(24)]
    g = sum(media) / 24
    return [max(-127, min(127, lround((m - g) * 100))) for m in media]


def tara(cartella, nodi, versione, fino):
    retta = unico = (versione == "v76")
    tab = [0] * 24
    giorni_in_media = 0
    ultima = ""
    date = sorted({os.path.basename(f)[-14:-4]
                   for f in glob.glob(os.path.join(cartella, "*.csv"))})
    for d in date:
        if fino and d > fino:
            break
        for nome in nodi:
            f = os.path.join(cartella, "%s_%s.csv" % (nome, d))
            if not os.path.exists(f) or (unico and d <= ultima):
                continue
            m = marea_giorno(leggi(f), retta)
            if m is None:
                continue
            peso = giorni_in_media + 1 if giorni_in_media < 8 else 8
            tab = [max(-127, min(127, v + tronca(m[h] - v, peso))) for h, v in enumerate(tab)]
            giorni_in_media += 1
            ultima = d
    return tab, giorni_in_media, ultima


def residuo(cartella, nome, tab):
    """Il delta a 3 h corretto, medio per ora UTC, meno la sua media: se la
    tabella e' giusta non resta un ciclo. Rms in hPa."""
    serie = []
    for f in sorted(glob.glob(os.path.join(cartella, "%s_*.csv" % nome))):
        serie += [(t, p) for t, _s, p in leggi(f) if not math.isnan(p)]
    per5 = {t // 300: p for t, p in serie}
    ore = [[] for _ in range(24)]
    for t, p in serie:
        q = per5.get((t - 10800) // 300)
        if q is None:
            continue
        h, h3 = (t // 3600) % 24, ((t - 10800) // 3600) % 24
        ore[h].append((p - tab[h] / 100) - (q - tab[h3] / 100))
    medie = [statistics.mean(v) for v in ore if v]
    return statistics.pstdev(medie)


def main():
    if len(sys.argv) < 2 or "--nodi" not in sys.argv:
        print(__doc__)
        return
    cartella = sys.argv[1]
    arg = lambda k: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else None
    nodi = arg("--nodi").split(",")
    fino = arg("--fino")
    risultati = {}
    for versione in ("v75", "v76"):
        tab, g, ultima = tara(cartella, nodi, versione, fino)
        risultati[versione] = tab
        print("%s  %2d giorni in media, ultimo %s, ampiezza %.2f hPa, gradino 21->22 UTC %+.2f hPa, "
              "residuo del delta 3 h %.3f hPa" % (versione, g, ultima, (max(tab) - min(tab)) / 100,
                                                  (tab[22] - tab[21]) / 100,
                                                  residuo(cartella, nodi[0], tab)))
        print("      " + " ".join("%d" % v for v in tab))
    ip = arg("--confronta")
    if ip:
        cred = base64.b64encode(("%s:%s" % (UTENTE, PASSWORD)).encode()).decode()
        req = urllib.request.Request("http://%s/api/stato" % ip,
                                     headers={"Authorization": "Basic " + cred})
        with urllib.request.urlopen(req, timeout=15) as r:
            stato = json.load(r)
        scheda = stato.get("marea_tab")
        print("\nscheda %s: %s giorni, ultimo %s" % (stato.get("fw"), stato.get("marea_giorni"),
                                                     stato.get("marea_ultima")))
        print("      " + (" ".join("%d" % v for v in scheda) if scheda else "nessuna tabella"))
        for versione, tab in risultati.items():
            if scheda:
                diff = max(abs(a - b) for a, b in zip(tab, scheda))
                print("  contro %s: %s" % (versione, "IDENTICA" if diff == 0
                                            else "scarto massimo %d centesimi" % diff))


if __name__ == "__main__":
    main()
