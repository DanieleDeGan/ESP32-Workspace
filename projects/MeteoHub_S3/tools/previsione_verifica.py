#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""La previsione del barometro azzecca? Verifica sui dati veri.

    python tools/previsione_verifica.py <ip> [nodo] [giorni]
    python tools/previsione_verifica.py <ip> [nodo] [giorni] --marea
    python tools/previsione_verifica.py <ip> [nodo] [giorni] --bande

QUELLO CHE QUESTO STRUMENTO NON PUO' FARE, ed e' la prima cosa da sapere: una
previsione si giudica contro **cio' che il tempo ha poi fatto**, e questa
stazione non ha nessun sensore che guardi il cielo. Niente pioggia, niente
vento, niente nuvole. Quindi qui NON si misura se ha indovinato il tempo.

Si misura una cosa piu' piccola e verificabile: **il trend a 3 ore ha davvero
un potere predittivo sulla pressione delle ore successive?** Se la risposta
fosse no, la previsione sarebbe rumore travestito da frase, e lo sapremmo senza
bisogno del cielo. Se e' si', resta da capire quanto -- e soprattutto quanto in
piu' rispetto al non dire niente.

TRE NUMERI, in ordine di importanza:

1. **La distribuzione delle frasi.** Se il 90% delle ore dice la stessa cosa,
   il resto dell'analisi conta poco: quella non e' una previsione, e' una
   costante. E' il primo posto dove guardare.
2. **Il confronto con la BASE.** Non serve sapere che azzecca il 70%: serve
   sapere quanto fa in piu' di "domani come oggi" (persistenza) e di "dico
sempre stabile", che sono le due previsioni che non costano niente. Una
   previsione che non batte la persistenza non sta prevedendo, sta copiando.
3. **La media condizionata.** Quando dice "peggioramento", di quanto scende
   davvero la pressione nelle sei ore dopo? E quando dice "stabile"? Se i due
   numeri non si separano, le frasi non stanno distinguendo niente.

E un avvertimento sul campione: con due settimane di dati, in una stagione
stabile, questi numeri sono indicativi e basta. Vanno riletti fra un mese, e
soprattutto dopo il primo peggioramento vero.

COSA HA TROVATO, LA PRIMA VOLTA (8 settembre 2026, 13 giorni, 833 casi).
La previsione **non aveva praticamente potere predittivo**: azzeccava il segno
il 31,5% delle volte contro il 28,3% di "dico sempre stabile", cioe' +3 punti,
e le medie condizionate erano MESCOLATE -- quando diceva "discesa" la pressione
saliva di +0,25 hPa, quando diceva "salita lenta" scendeva di -0,18.

La causa non era l'algoritmo ma il segnale: la **marea barometrica**. Il ciclo
giornaliero della pressione, che e' astronomico e non meteorologico, qui misura
**1,51 hPa da picco a picco** (massimi alle 10 e alle 23, minimi alle 17-18 e
alle 5). Le soglie di forecast.h partono da 0,5 hPa/3h: **la marea da sola le
attraversa due volte al giorno**, tutti i giorni, e il pannello cambiava frase
per ragioni che col tempo non c'entrano niente.

Togliendo il ciclo giornaliero dal solo PREDITTORE, a parita' di bersaglio, il
guadagno sulla base passa da +3,1 a **+10,4 punti**: il potere predittivo
triplica. Misurando anche il bersaglio al netto della marea -- che e' la
domanda meteorologicamente sensata, perche' la marea e' nota e non porta
informazione sul tempo -- il guadagno e' +15,2 punti e le medie condizionate si
separano nel verso giusto (discesa lenta -0,93, stabile -0,07, salita lenta
+0,70).

`--marea` stampa la tabella delle 24 correzioni, pronta da incollare.

E UN SOSPETTO RAGIONEVOLE CHE E' RISULTATO SBAGLIATO (`--bande`). Zambretti
decide "salita / stabile / discesa" con una banda morta di +-1,6 hPa su 3 ore,
mentre le nostre soglie partono a +-0,5: siccome la marea produce fino a ~0,8
hPa/3h, sembrava che la banda larga la scavalcasse per costruzione, e che fosse
quello il motivo per cui Zambretti gode di buona fama. Misurato, non e' cosi':

    banda +-0,5 (nostra)      guadagno  +3,2 punti
    banda +-1,0                         +0,4
    banda +-1,6 (Zambretti)             -0,2
    banda +-2,0 e oltre                  0,0   (dice sempre "stabile")

Allargare la soglia non batte la marea: **butta via il tempo insieme a lei**.
Oltre i 2 hPa la previsione degenera nella base -- dice sempre stabile e
azzecca esattamente quanto chi non prevede niente. La marea si toglie
sottraendola, perche' e' un segnale NOTO e periodico; una soglia piu' grossa
non la conosce, la nasconde.

Con una avvertenza: queste due settimane sono un regime quieto (7 hPa di
escursione sinottica in tutto). In un periodo mosso una banda a 1,6 farebbe
molti meno danni, perche' il segnale meteorologico la supererebbe da solo.
"""
import base64
import json
import sys

try:
    from urllib.request import Request, urlopen
except ImportError:                                     # python 2
    from urllib2 import Request, urlopen

# Le stesse soglie di forecast.h, in hPa sul delta a 3 ore. Se cambiano li',
# cambiano qui: sono la definizione di cosa il pannello sta dicendo.
CLASSI = [
    (-6.0,  "crollo"),
    (-3.5,  "discesa rapida"),
    (-1.6,  "discesa"),
    (-0.5,  "discesa lenta"),
    (0.5,   "stabile"),
    (1.6,   "salita lenta"),
    (3.5,   "salita"),
    (6.0,   "salita rapida"),
    (1e9,   "salita forte"),
]
ORIZZONTE_H = 6          # su quante ore si guarda "cosa e' successo dopo"
TOLLERANZA_S = 900       # quanto puo' distare un campione dall'ora cercata


def classifica(d3):
    for soglia, nome in CLASSI:
        if d3 < soglia:
            return nome
    return CLASSI[-1][1]


def scarica(host, nodo, da, a, punti):
    url = ("http://%s/api/nodi/serie?nodo=%s&da=%s&a=%s&punti=%d&v=2"
           % (host, nodo, da, a, punti))
    cred = base64.b64encode(b"admin:admin").decode()
    r = Request(url, headers={"Authorization": "Basic " + cred})
    return json.loads(urlopen(r, timeout=90).read().decode())


def serie_da_api(d):
    """[(ts, pressione media del cesto)] senza i cesti vuoti."""
    t0, passo = d["t0"], d["passo"]
    return [(t0 + i * passo, c[0]) for i, c in enumerate(d["s"]) if c is not None]


def cerca(serie, ts):
    """La pressione all'istante ts, se c'e' un campione abbastanza vicino."""
    migliore, dist = None, TOLLERANZA_S + 1
    for t, p in serie:
        dd = abs(t - ts)
        if dd < dist:
            migliore, dist = p, dd
        elif t > ts + TOLLERANZA_S:
            break
    return migliore


def verifica(serie):
    casi = []
    for t, p in serie:
        prima = cerca(serie, t - 3 * 3600)
        dopo = cerca(serie, t + ORIZZONTE_H * 3600)
        if prima is None or dopo is None:
            continue
        casi.append((t, classifica(p - prima), p - prima, dopo - p))
    return casi


def stampa(casi, nodo, ore):
    if not casi:
        print("non ci sono abbastanza dati per dire niente")
        return
    n = len(casi)
    print("Nodo %s, %d ore di storico, %d casi utili "
          "(ogni caso: il trend a 3 h e cosa ha fatto la pressione nelle %d h dopo)\n"
          % (nodo, ore, n, ORIZZONTE_H))

    # --- 1) la distribuzione delle frasi ---------------------------------
    print("1. QUANTO SPESSO DICE COSA")
    conta = {}
    for _t, cl, _d3, _dopo in casi:
        conta[cl] = conta.get(cl, 0) + 1
    for _s, nome in CLASSI:
        c = conta.get(nome, 0)
        if not c:
            continue
        print("   %-16s %4d  %5.1f%%  %s" % (nome, c, 100.0 * c / n, "#" * (50 * c // n)))
    dominante = max(conta.items(), key=lambda x: x[1])
    print("   -> la frase piu' frequente copre il %.0f%% delle ore"
          % (100.0 * dominante[1] / n))

    # --- 2) il confronto con le previsioni che non costano niente --------
    #
    # "Indovinare" qui vuol dire una cosa sola e dichiarata: azzeccare il SEGNO
    # di cio' che la pressione fa dopo, con una banda morta di +-0,5 hPa entro
    # cui si dice "stabile". Senza banda morta, "stabile" non sarebbe mai
    # giusto e il confronto sarebbe truccato a favore delle altre.
    def segno(x):
        return 0 if abs(x) < 0.5 else (1 if x > 0 else -1)

    def segno_previsto(cl):
        if "discesa" in cl or cl == "crollo":
            return -1
        if "salita" in cl:
            return 1
        return 0

    nostri = sum(1 for _t, cl, _d3, dopo in casi if segno_previsto(cl) == segno(dopo))
    fermo = sum(1 for _t, _cl, _d3, dopo in casi if segno(dopo) == 0)
    persist = sum(1 for _t, _cl, d3, dopo in casi if segno(d3) == segno(dopo))
    print("\n2. QUANTO AZZECCA IL SEGNO, CONTRO CHI NON PREVEDE NIENTE")
    print("   la nostra previsione        %5.1f%%" % (100.0 * nostri / n))
    print("   \"dico sempre stabile\"       %5.1f%%" % (100.0 * fermo / n))
    print("   persistenza (segno del d3)  %5.1f%%" % (100.0 * persist / n))
    print("   -> guadagno sulla base piu' forte: %+.1f punti"
          % (100.0 * (nostri - max(fermo, persist)) / n))

    # --- 3) le frasi separano davvero? -----------------------------------
    print("\n3. QUANDO DICE X, LA PRESSIONE POI FA")
    for _s, nome in CLASSI:
        g = [dopo for _t, cl, _d3, dopo in casi if cl == nome]
        if not g:
            continue
        media = sum(g) / len(g)
        print("   %-16s %4d casi   media %+6.2f hPa   (da %+.2f a %+.2f)"
              % (nome, len(g), media, min(g), max(g)))
    print("\n   Se le medie non si separano, le frasi non stanno distinguendo")
    print("   niente: e' li' che si vede se la previsione funziona.")


def ciclo_giornaliero(serie):
    """Lo scarto medio dalla media del giorno, ora per ora: la marea.

    Si toglie la media di CIASCUN GIORNO prima di accumulare, o la deriva
    sinottica (una settimana di alta pressione) entrerebbe nel ciclo e lo
    sporcherebbe con qualcosa che non e' periodico.
    """
    import time as _t
    per_giorno = {}
    for ts, p in serie:
        g = _t.strftime("%Y-%m-%d", _t.localtime(ts))
        per_giorno.setdefault(g, []).append((_t.localtime(ts).tm_hour, p))
    acc = {}
    for _g, v in per_giorno.items():
        if len(v) < 18:                 # un giorno mezzo vuoto non fa media
            continue
        m = sum(p for _h, p in v) / len(v)
        for h, p in v:
            acc.setdefault(h, []).append(p - m)
    return {h: sum(v) / len(v) for h, v in acc.items()}, len(per_giorno)


def stampa_marea(serie):
    marea, giorni = ciclo_giornaliero(serie)
    vals = [marea.get(h, 0.0) for h in range(24)]
    print("Ciclo giornaliero della pressione su %d giorni "
          "(ampiezza %.2f hPa da picco a picco):" % (giorni, max(vals) - min(vals)))
    for h in range(24):
        x = vals[h]
        print("  %02d:00  %+5.2f  %s" % (h, x, "#" * int(round(abs(x) * 30))))
    print("")
    print("Pronta da incollare in forecast.h (centesimi di hPa, interi):")
    print("static const int8_t MAREA_CHPA[24] = {")
    for r in range(0, 24, 8):
        print("  " + ", ".join("%4d" % int(round(vals[h] * 100)) for h in range(r, r + 8)) + ",")
    print("};")
    print("")
    print("ATTENZIONE: e' una costante di TARATURA, non una legge fisica.")
    print("Dipende dal posto e dalla stagione (l'ampiezza della marea cresce")
    print("d'estate e alle basse latitudini): va rifatta ogni qualche mese, e")
    print("ricalcolata su almeno un mese di dati, non su due settimane.")


def bande(serie):
    """Quanto rende la banda morta del trend, al variare della sua larghezza,
    con e senza marea. Il bersaglio e' SEMPRE la pressione vera: cambia solo
    come si decide cosa dire."""
    import time as _t
    marea, _g = ciclo_giornaliero(serie)
    sc = [(ts, p - marea.get(_t.localtime(ts).tm_hour, 0.0)) for ts, p in serie]
    seg = lambda x, b=0.5: 0 if abs(x) < b else (1 if x > 0 else -1)

    def prova(banda, corretto):
        src = sc if corretto else serie
        ok = base = n = 0
        for i, (ts, _p) in enumerate(serie):
            prima = cerca(src, ts - 3 * 3600)
            dopo = cerca(serie, ts + ORIZZONTE_H * 3600)
            if prima is None or dopo is None:
                continue
            n += 1
            ok += (seg(src[i][1] - prima, banda) == seg(dopo - serie[i][1]))
            base += (seg(dopo - serie[i][1]) == 0)
        return 100.0 * ok / n, 100.0 * base / n

    print("Bersaglio uguale per tutti: il segno di cio' che la pressione VERA")
    print("fa nelle %d h dopo. Cambia solo come si decide cosa dire." % ORIZZONTE_H)
    print("")
    for titolo, corretto in (("PRESSIONE GREZZA", False), ("DE-MAREATA", True)):
        print(titolo)
        for b in (0.5, 1.0, 1.6, 2.0):
            a, base = prova(b, corretto)
            nota = "  <- la nostra" if b == 0.5 else ("  <- quella di Zambretti" if b == 1.6 else "")
            print("  banda +-%.1f hPa/3h   azzecca %5.1f%%  base %5.1f%%  guadagno %+5.1f%s"
                  % (b, a, base, a - base, nota))
        print("")


def main(host, nodo, giorni):
    import datetime
    oggi = datetime.date.today()
    da = (oggi - datetime.timedelta(days=giorni - 1)).isoformat()
    d = scarica(host, nodo, da, oggi.isoformat(), 1000)
    serie = serie_da_api(d)
    if "--marea" in sys.argv:
        stampa_marea(serie)
        return
    if "--bande" in sys.argv:
        bande(serie)
        return
    ore = int((serie[-1][0] - serie[0][0]) / 3600) if len(serie) > 1 else 0
    stampa(verifica(serie), nodo, ore)
    print("\nLIMITE: qui si verifica il barometro contro se stesso, non contro")
    print("il tempo. Per sapere se ha azzeccato la PIOGGIA serve un'osservazione")
    print("del cielo, che questa stazione non ha.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1],
         sys.argv[2] if len(sys.argv) > 2 else "MeteoEsp32",
         int(sys.argv[3]) if len(sys.argv) > 3 else 14)
