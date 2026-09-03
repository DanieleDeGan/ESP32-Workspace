#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Cosa dicono davvero i CSV dei nodi. Analisi che a bordo non si puo' fare.

    python tools/analisi.py <cartella con i CSV scaricati>
    python tools/analisi.py <cartella> --dentro Salotto --fuori MeteoNode

I CSV si prendono dall'hub, uno per nodo e per giorno, con lo stesso comando di
refresh_simula.py:

    curl -u admin:admin "http://<ip>/api/nodi/scarica?nodo=<nome>&d=2026-09-01" \\
         -o "<nome>_2026-09-01.csv"

ATTENZIONE mentre si scarica: ogni richiesta e' tempo in cui il loop() dell'hub
non preleva i DATA dalla radio, e il driver ne tiene UNO solo per nodo. Un anno
di CSV sono centinaia di richieste: farle con una pausa in mezzo, e non mentre
si sta guardando il pannello.

PERCHE' ESISTE. Stessa ragione di refresh_simula.py, che ha gia' dimostrato di
funzionare: si risponde a una domanda PRIMA di scrivere il firmware, sui dati
veri, invece di aspettare giorni e scoprire di aver tarato male una soglia. Li'
il risultato fu che un sospetto ragionevole era sbagliato; qui le domande sono
quelle di docs/Proposte-2026-09-02.md, e ognuna e' la taratura di una proposta:

  sezione          risponde a                                       voce
  ---------------  -----------------------------------------------  ----
  completezza      quanto e' piena una giornata, per nodo           5.1 / 28
  cadenza          la formula attuale sbaglia? di quanto?           1.2 / 17
  previsione       la tendenza a 3 h batte la persistenza?          5.4 / 30
  sensore fermo    quante volte le tre grandezze restano identiche  5.5 / 32
  verdetto         quante volte al giorno cambierebbe, per soglia   3.1 / 22
  riepilogo        le righe giornaliere, e i gradi giorno           5.2 / 29
  profilo orario   il sole batte sul sensore esterno?               6.4 / 33
  arieggiamenti    quante volte si e' aperta una finestra           6.4 / 33
  costante casa    quanto tempo ci mette a raffreddarsi             6.4 / 33

Le ultime cinque vogliono di sapere quale nodo sta dentro e quale fuori: si
passano con --dentro e --fuori. Senza, quelle sezioni dicono cosa manca invece
di indovinare.

Solo libreria standard, come tutti gli script di questo repo: niente numpy,
niente pandas, gira ovunque ci sia un python.
"""
import csv
import glob
import io
import math
import os
import sys
from collections import defaultdict

# --- soglie di plausibilita', le stesse che usa remote_nodes.cpp -------------
PRESS_MIN_HPA, PRESS_MAX_HPA = 800.0, 1100.0
TEMP_MIN_C, TEMP_MAX_C = -40.0, 80.0

# Basi dei gradi giorno: 18 C e' lo standard internazionale, 20 C quello
# italiano (DPR 412/93). Si riportano entrambe perche' il numero ha senso solo
# nel confronto con se stesso: quello che conta e' dichiarare quale si usa.
GG_BASI = (18.0, 20.0)


# ===========================================================================
#  Lettura
# ===========================================================================
def _num(riga, chiave, minimo=None, massimo=None):
    v = (riga.get(chiave) or "").strip()
    if not v:
        return None
    try:
        x = float(v)
    except ValueError:
        return None
    if x != x:                      # NAN scritto come testo
        return None
    if minimo is not None and not (minimo <= x <= massimo):
        return None
    return x


def leggi(path):
    """Righe (ts, seq, t, h, p, fonte_ora) di un CSV dell'hub."""
    fuori = []
    with io.open(path, encoding="utf-8", errors="replace") as f:
        for r in csv.DictReader(f):
            try:
                ts = int(r["ts_unix"])
            except (KeyError, ValueError, TypeError):
                continue
            try:
                seq = int((r.get("seq") or "0").strip() or 0)
            except ValueError:
                seq = 0
            fuori.append((
                ts, seq,
                _num(r, "temp_c", TEMP_MIN_C, TEMP_MAX_C),
                _num(r, "hum_pct", 0.0, 100.0),
                _num(r, "press_hpa", PRESS_MIN_HPA, PRESS_MAX_HPA),
                (r.get("fonte_ora") or "").strip(),
            ))
    return fuori


def carica(cartella):
    nodi = {}
    for f in sorted(glob.glob(os.path.join(cartella, "*.csv"))):
        nome = os.path.basename(f).rsplit("_", 1)[0]
        nodi.setdefault(nome, []).extend(leggi(f))
    for n in nodi:
        nodi[n].sort()
        # Lo stesso pacchetto puo' comparire due volte se si scarica un giorno
        # gia' scaricato: si tiene la prima occorrenza di ogni (ts, seq).
        visti, puliti = set(), []
        for r in nodi[n]:
            k = (r[0], r[1])
            if k in visti:
                continue
            visti.add(k)
            puliti.append(r)
        nodi[n] = puliti
    return nodi


# ===========================================================================
#  Grandezze derivate (le stesse formule di meteo_calc.h, in python)
# ===========================================================================
def rugiada_c(t, rh):
    if t is None or rh is None or not (0.0 < rh <= 100.0):
        return None
    b, c = 17.62, 243.12
    g = math.log(rh / 100.0) + (b * t) / (c + t)
    return (c * g) / (b - g)


def umidita_assoluta(t, rh):
    """g/m3. NON dipende dalla temperatura: e' l'unico confronto onesto fra
    due stanze diverse, o fra dentro e fuori."""
    if t is None or rh is None or not (0.0 <= rh <= 100.0):
        return None
    es = 6.112 * math.exp((17.67 * t) / (t + 243.5))
    return es * rh * 2.1674 / (273.15 + t)


# ===========================================================================
#  Utilita'
# ===========================================================================
def mediana(xs):
    if not xs:
        return None
    ys = sorted(xs)
    n = len(ys)
    return ys[n // 2] if n % 2 else (ys[n // 2 - 1] + ys[n // 2]) / 2.0


def giorno_di(ts):
    import time
    return time.strftime("%Y-%m-%d", time.localtime(ts))


def ora_di(ts):
    import time
    return time.localtime(ts).tm_hour


def hhmm(ts):
    import time
    return time.strftime("%H:%M", time.localtime(ts))


def barra(v, vmin, vmax, larghezza=28):
    if vmax <= vmin:
        return ""
    n = int(round((v - vmin) / (vmax - vmin) * larghezza))
    return "#" * max(0, min(larghezza, n))


def titolo(testo):
    print("\n=== " + testo + " " + "=" * max(0, 66 - len(testo)))


def manca_ruoli(cosa):
    print("  (serve --dentro NOME e --fuori NOME: %s)" % cosa)


# ===========================================================================
#  1. Completezza  (proposta 5.1 / voce 28)
# ===========================================================================
def cadenza_stimata(righe):
    """La mediana dei delta fra pacchetti CONSECUTIVI (seq+1). La mediana e non
    la media proprio per la ragione della voce 17: i buchi non sono rumore da
    mediare, sono delta di un'altra grandezza."""
    d = []
    for i in range(1, len(righe)):
        if righe[i][1] == righe[i - 1][1] + 1:
            dt = righe[i][0] - righe[i - 1][0]
            if 1 <= dt <= 21600:
                d.append(dt)
    return mediana(d)


def sezione_completezza(nodi):
    titolo("completezza: quanto e' piena ogni giornata")
    print("  Un minimo calcolato sul 40% dei campioni non e' il minimo del")
    print("  giorno: e' il minimo di quello che si e' visto, e ha lo stesso")
    print("  aspetto. Sotto il 50% un aggregato non andrebbe mostrato.\n")
    for nome in sorted(nodi):
        righe = nodi[nome]
        cad = cadenza_stimata(righe)
        if not cad:
            print("  %-16s cadenza non stimabile (meno di due pacchetti consecutivi)" % nome)
            continue
        per_giorno = defaultdict(int)
        for r in righe:
            per_giorno[giorno_di(r[0])] += 1
        attesi = int(round(86400.0 / cad))
        print("  %s   cadenza %d s   attesi %d/giorno" % (nome, cad, attesi))
        for g in sorted(per_giorno):
            n = per_giorno[g]
            pct = 100.0 * n / attesi
            nota = ""
            if pct < 50:
                nota = "  <- sotto il 50%: aggregati da non mostrare"
            elif pct > 105:
                nota = "  <- oltre il 100%: cadenza stimata troppo lunga"
            print("    %s  %4d/%4d  %5.1f%%  %s%s"
                  % (g, n, attesi, pct, barra(pct, 0, 100), nota))


# ===========================================================================
#  2. Cadenza appresa: formula attuale contro formula corretta  (1.2 / voce 17)
# ===========================================================================
def sezione_cadenza(nodi):
    titolo("cadenza appresa: la formula di oggi contro quella corretta")
    print("  aggiornaDaLibreria() impara da QUALUNQUE seq crescente, buchi")
    print("  compresi. Con un pacchetto perso il delta e' due periodi, e la")
    print("  media mobile a peso 1/4 se lo porta dentro.")
    print("  sogliaMuto = 2,5 x intervallo + 30: ogni secondo di errore qui")
    print("  ritarda di 2,5 s il rilevamento di un nodo morto.\n")
    for nome in sorted(nodi):
        righe = nodi[nome]
        vera = cadenza_stimata(righe)
        ema_oggi = 0
        ema_giusta = 0
        peggio = 0.0
        buchi = 0
        for i in range(1, len(righe)):
            ts, seq = righe[i][0], righe[i][1]
            pts, pseq = righe[i - 1][0], righe[i - 1][1]
            if seq <= pseq:
                continue                      # riavvio del nodo: nessuno impara
            dt = ts - pts
            if not (1 <= dt <= 21600):
                continue
            consecutivo = (seq == pseq + 1)
            if not consecutivo:
                buchi += 1
            # com'e' adesso
            ema_oggi = dt if ema_oggi == 0 else (ema_oggi * 3 + dt) // 4
            # come dovrebbe essere
            if consecutivo:
                ema_giusta = dt if ema_giusta == 0 else (ema_giusta * 3 + dt) // 4
            if vera:
                peggio = max(peggio, abs(ema_oggi - vera) / float(vera) * 100.0)
        if not vera:
            print("  %-16s non stimabile" % nome)
            continue
        s_oggi = ema_oggi * 5 // 2 + 30
        s_giusta = ema_giusta * 5 // 2 + 30
        print("  %s" % nome)
        print("    cadenza vera (mediana dei consecutivi) : %d s" % vera)
        print("    formula di oggi, alla fine             : %d s   (soglia muto %d s)"
              % (ema_oggi, s_oggi))
        print("    formula corretta, alla fine            : %d s   (soglia muto %d s)"
              % (ema_giusta, s_giusta))
        print("    buchi nel seq incontrati               : %d" % buchi)
        print("    errore massimo della formula di oggi   : %.1f%% della cadenza vera"
              % peggio)
        if buchi == 0:
            print("    -> in questi dati non ci sono buchi: la correzione non cambia")
            print("       niente QUI, e non e' una prova che non serva. Serve il")
            print("       giorno in cui i buchi ci sono, che e' quello in cui conta.")


# ===========================================================================
#  3. La previsione contro la persistenza  (5.4 / voce 30)
# ===========================================================================
def _serie(righe, idx_val):
    """(lista di ts, lista di valori) dei soli campioni presenti, in ordine."""
    ts, vs = [], []
    for r in righe:
        if r[idx_val] is not None:
            ts.append(r[0])
            vs.append(r[idx_val])
    return ts, vs


def campione_a(ts, vs, ts_bersaglio, toll):
    """Il valore piu' vicino a ts_bersaglio entro toll secondi, o None.

    Con bisect e non con una scansione: la funzione viene chiamata due volte
    per campione, quindi la scansione la renderebbe quadratica -- accettabile
    su una settimana, minuti su un anno, che e' proprio il caso per cui questo
    script esiste."""
    import bisect
    i = bisect.bisect_left(ts, ts_bersaglio)
    migliore, dist = None, None
    for j in (i - 1, i):
        if 0 <= j < len(ts):
            d = abs(ts[j] - ts_bersaglio)
            if d <= toll and (dist is None or d < dist):
                migliore, dist = vs[j], d
    return migliore


def punteggio(righe, idx_val, orizzonte_s, toll_s, unita):
    """MAE di persistenza e tendenza, e punteggio di abilita'."""
    ts, vs = _serie(righe, idx_val)
    err_p, err_t, n = 0.0, 0.0, 0
    for k in range(len(ts)):
        v = vs[k]
        passato = campione_a(ts, vs, ts[k] - orizzonte_s, toll_s)
        futuro = campione_a(ts, vs, ts[k] + orizzonte_s, toll_s)
        if passato is None or futuro is None:
            continue
        err_p += abs(futuro - v)
        err_t += abs(futuro - (v + (v - passato)))
        n += 1
    if n == 0:
        return None
    mae_p, mae_t = err_p / n, err_t / n
    skill = (1.0 - mae_t / mae_p) * 100.0 if mae_p > 0 else 0.0
    return n, mae_p, mae_t, skill, unita


def sezione_previsione(nodi):
    titolo("la previsione contro la persistenza")
    print("  La persistenza -- 'fra tre ore sara' come adesso' -- e' il")
    print("  riferimento con cui si giudica ogni previsione, perche' e' quella")
    print("  che chiunque puo' fare a costo zero. Una tendenza che non la batte")
    print("  non sta aggiungendo informazione.")
    print("  Punteggio > 0: la tendenza serve. < 0: e' rumore.\n")
    prove = (
        ("pressione a +3 h",   4, 10800, 900,  "hPa"),
        ("temperatura a +1 h", 2,  3600, 600,  "C"),
        ("temperatura a +3 h", 2, 10800, 900,  "C"),
    )
    for nome in sorted(nodi):
        print("  %s" % nome)
        for etichetta, idx, oriz, toll, unita in prove:
            res = punteggio(nodi[nome], idx, oriz, toll, unita)
            if res is None:
                print("    %-20s  nessun confronto possibile" % etichetta)
                continue
            n, mae_p, mae_t, skill, u = res
            avviso = "" if n >= 300 else "   (pochi: sotto i 300 e' rumore)"
            print("    %-20s  n=%4d   persistenza %.3f %s   tendenza %.3f %s   "
                  "punteggio %+.1f%%%s"
                  % (etichetta, n, mae_p, u, mae_t, u, skill, avviso))


# ===========================================================================
#  4. Sensore fermo: le tre grandezze insieme  (5.5 / voce 32)
# ===========================================================================
def sezione_fermo(nodi):
    titolo("sensore fermo: quante volte T, RH e P restano identiche insieme")
    print("  Che UNA grandezza resti ferma per mezz'ora e' normale. Che le tre")
    print("  restino identiche insieme non e' meteorologia: sono due sensori")
    print("  diversi (AHT20 e BMP280) sullo stesso bus I2C.")
    print("  Il caso 'lettura fallita' e' gia' distinto: li' il nodo manda NAN.\n")
    for nome in sorted(nodi):
        serie = [r for r in nodi[nome] if None not in (r[2], r[3], r[4])]
        if len(serie) < 3:
            print("  %-16s troppi pochi campioni completi" % nome)
            continue
        run, massimo, dove = 1, 1, None
        conteggio = defaultdict(int)
        for i in range(1, len(serie)):
            a, b = serie[i - 1], serie[i]
            if (a[2], a[3], a[4]) == (b[2], b[3], b[4]):
                run += 1
                if run > massimo:
                    massimo, dove = run, b[0]
            else:
                if run > 1:
                    conteggio[run] += 1
                run = 1
        if run > 1:
            conteggio[run] += 1
        print("  %s   campioni completi %d" % (nome, len(serie)))
        print("    ripetizione piu' lunga: %d letture di fila%s"
              % (massimo, ("  (fino alle %s del %s)" % (hhmm(dove), giorno_di(dove)))
                 if dove else ""))
        if conteggio:
            for k in sorted(conteggio):
                print("      %2d letture identiche di fila : %d volte" % (k, conteggio[k]))
            print("    -> la soglia va messa SOPRA la ripetizione piu' lunga vista")
            print("       qui, o si crea un allarme falso ricorrente.")
        else:
            print("    nessuna ripetizione: qualunque soglia >= 2 sarebbe sicura")
            print("    su questi dati (che pero' sono pochi giorni, non un anno).")


# ===========================================================================
#  5. Il verdetto delle finestre  (3.1 / voce 22)
# ===========================================================================
def appaia(dentro, fuori, toll=600):
    """Coppie (ts, riga_dentro, riga_fuori) col campione esterno piu' vicino."""
    coppie = []
    j = 0
    for rd in dentro:
        while j + 1 < len(fuori) and abs(fuori[j + 1][0] - rd[0]) <= abs(fuori[j][0] - rd[0]):
            j += 1
        if j < len(fuori) and abs(fuori[j][0] - rd[0]) <= toll:
            coppie.append((rd[0], rd, fuori[j]))
    return coppie


def sezione_verdetto(nodi, n_dentro, n_fuori):
    titolo("il verdetto delle finestre: quante volte cambierebbe al giorno")
    if not n_dentro or not n_fuori:
        manca_ruoli("il verdetto confronta l'umidita' ASSOLUTA dentro e fuori")
        return
    if n_dentro not in nodi or n_fuori not in nodi:
        print("  nodo non trovato fra: %s" % ", ".join(sorted(nodi)))
        return
    coppie = appaia(nodi[n_dentro], nodi[n_fuori])
    if not coppie:
        print("  nessuna coppia di letture vicine: i due nodi non si sovrappongono")
        return
    print("  Aprire ASCIUGA se l'assoluta fuori e' minore di quella dentro.")
    print("  L'umidita' relativa direbbe spesso il contrario, ed e' il motivo")
    print("  per cui il confronto si fa sull'assoluta.\n")
    print("  La soglia si applica con ISTERESI, non come banda secca, ed e' la")
    print("  differenza che decide se la feature e' sostenibile:")
    print("    - banda secca: attraversando la zona neutra il verdetto cambia")
    print("      DUE volte (asciuga -> indifferente -> bagna), quindi allargare")
    print("      la soglia puo' far AUMENTARE i refresh invece che diminuirli;")
    print("    - isteresi: il verdetto resta dov'e' finche' la differenza non")
    print("      supera la soglia dall'altra parte. Un attraversamento = un")
    print("      cambio, sempre.")
    print("  E' la stessa scelta gia' fatta in forecast.h per il trend.\n")
    giorni = len(set(giorno_di(c[0]) for c in coppie))
    print("  %d coppie su %d giorni (%s dentro, %s fuori)\n"
          % (len(coppie), giorni, n_dentro, n_fuori))
    print("    soglia    cambi/giorno (isteresi)   [banda secca]   asciuga  bagna")
    for soglia in (0.0, 0.3, 0.5, 1.0, 1.5, 2.0):
        stato, cambi_ist = None, 0
        prec_banda, cambi_banda = None, 0
        conta = defaultdict(int)
        for ts, rd, rf in coppie:
            ad = umidita_assoluta(rd[2], rd[3])
            af = umidita_assoluta(rf[2], rf[3])
            if ad is None or af is None:
                continue
            d = af - ad

            # con isteresi: si cambia solo superando la soglia dall'altra parte
            nuovo = stato
            if d <= -soglia:
                nuovo = "asciuga"
            elif d >= soglia:
                nuovo = "bagna"
            if stato is not None and nuovo != stato:
                cambi_ist += 1
            stato = nuovo
            conta[stato or "ignoto"] += 1

            # con banda secca, solo per il confronto
            v = "indiff" if abs(d) < soglia else ("asciuga" if d < 0 else "bagna")
            if prec_banda is not None and v != prec_banda:
                cambi_banda += 1
            prec_banda = v
        tot = float(sum(conta.values())) or 1.0
        print("    %4.1f g/m3          %8.1f            %8.1f      %4.0f%%   %4.0f%%"
              % (soglia, cambi_ist / float(giorni), cambi_banda / float(giorni),
                 100 * conta["asciuga"] / tot, 100 * conta["bagna"] / tot))
    print("\n  Da leggere cosi': si sceglie la soglia piu' piccola che tiene i")
    print("  cambi (colonna isteresi) sotto una decina al giorno. Sopra, la")
    print("  striscia diventa un costo di refresh invece che un'informazione.")


# ===========================================================================
#  6. Riepilogo giornaliero e gradi giorno  (5.2 - 5.3 / voci 3, 29)
# ===========================================================================
def sezione_riepilogo(nodi):
    titolo("riepilogo giornaliero: le righe che andrebbero su card")
    print("  E' il formato proposto per /nodi/<NOME>/riepilogo.csv. La regola")
    print("  che lo rende sicuro e' che dev'essere RICALCOLABILE dai CSV: se")
    print("  lo e', e' una cache e si puo' rifare; se no, e' una seconda fonte")
    print("  di verita' che puo' divergere. Questo script E' il ricalcolo.\n")
    for nome in sorted(nodi):
        righe = nodi[nome]
        cad = cadenza_stimata(righe) or 300
        attesi = int(round(86400.0 / cad))
        per_giorno = defaultdict(list)
        for r in righe:
            per_giorno[giorno_di(r[0])].append(r)
        print("  %s" % nome)
        print("    giorno      camp/att   t_min (ora)   t_max (ora)   t_med   "
              "hdd18  cdd18  hdd20")
        for g in sorted(per_giorno):
            rs = per_giorno[g]
            temps = [(r[2], r[0]) for r in rs if r[2] is not None]
            if not temps:
                continue
            tmin, ts_min = min(temps)
            tmax, ts_max = max(temps)
            tmed = sum(t for t, _ in temps) / float(len(temps))
            hdd18 = max(0.0, GG_BASI[0] - tmed)
            cdd18 = max(0.0, tmed - GG_BASI[0])
            hdd20 = max(0.0, GG_BASI[1] - tmed)
            parziale = "" if len(rs) >= attesi * 0.5 else "  <- incompleto"
            print("    %s  %4d/%4d  %6.1f (%s)  %6.1f (%s)  %5.1f   %5.1f  %5.1f  %5.1f%s"
                  % (g, len(rs), attesi, tmin, hhmm(ts_min), tmax, hhmm(ts_max),
                     tmed, hdd18, cdd18, hdd20, parziale))
        print("    NB: la media e' quella ARITMETICA dei campioni, non")
        print("        (min+max)/2. I due numeri differiscono e non sono")
        print("        confrontabili: va dichiarato quale si usa.")


# ===========================================================================
#  7. Profilo orario: il sole batte sul sensore?  (6.4 / voce 33)
# ===========================================================================
def sezione_profilo(nodi, n_fuori):
    titolo("profilo orario: il sole batte sul sensore esterno?")
    print("  Un sensore esposto mostra una gobba STRETTA sempre alla stessa ora")
    print("  nei giorni sereni, invece della curva larga del ciclo diurno. Se")
    print("  c'e', la stazione sta misurando il proprio sensore e non l'aria:")
    print("  tutte le massime dell'estate sarebbero sbagliate per eccesso, e")
    print("  nessuna diagnostica di bordo lo vedrebbe.\n")
    for nome in sorted(nodi):
        marchio = "  <- dichiarato ESTERNO" if nome == n_fuori else ""
        per_ora = defaultdict(list)
        for r in nodi[nome]:
            if r[2] is not None:
                per_ora[ora_di(r[0])].append(r[2])
        if not per_ora:
            continue
        medie = dict((h, sum(v) / len(v)) for h, v in per_ora.items())
        vmin, vmax = min(medie.values()), max(medie.values())
        print("  %s   escursione media oraria %.1f C%s" % (nome, vmax - vmin, marchio))
        for h in range(24):
            if h not in medie:
                continue
            print("    %02d:00  %6.2f C  %s" % (h, medie[h], barra(medie[h], vmin, vmax)))
        # Una gobba stretta si riconosce dal salto fra ore adiacenti rispetto
        # all'escursione totale: qui non si decide, si mette il numero in mano.
        salti = []
        for h in range(1, 24):
            if h in medie and (h - 1) in medie:
                salti.append((medie[h] - medie[h - 1], h))
        if salti:
            s, h = max(salti)
            print("    salita piu' ripida fra due ore: %+.2f C verso le %02d:00" % (s, h))
            # Perche' 0,25 e non un numero a caso: un ciclo diurno naturale e'
            # vicino a una sinusoide, e per una sinusoide di escursione R il
            # salto orario massimo vale R*(pi/24) = 0,13*R. Un salto sopra il
            # 25% e' quindi PIU' DEL DOPPIO di quanto la natura possa fare in
            # un'ora: non e' aria che si scalda, e' qualcosa che illumina il
            # sensore.
            if vmax - vmin > 0 and s / (vmax - vmin) > 0.25:
                print("    -> %.0f%% dell'escursione in un'ora sola, contro il 13%%"
                      % (100 * s / (vmax - vmin)))
                print("       massimo di un ciclo diurno naturale.")
                # Uno scalino ripido su un nodo INTERNO non e' il sole: e' quasi
                # sempre il riscaldamento che parte. Dirlo qui evita il falso
                # positivo piu' facile -- e l'informazione e' comunque utile,
                # perche' ricavare gli orari dell'impianto dai dati serve a
                # scegliere le notti "libere" per la costante di tempo.
                if nome == n_fuori:
                    print("       Su un nodo ESTERNO e' la forma tipica del sole")
                    print("       diretto sul sensore: da guardare.")
                else:
                    print("       Su un nodo INTERNO non e' il sole: e' quasi")
                    print("       sempre l'impianto che parte. Utile lo stesso —")
                    print("       serve a scegliere le notti senza riscaldamento")
                    print("       per la costante di tempo qui sotto.")


# ===========================================================================
#  8. Arieggiamenti  (6.4 / voce 33)
# ===========================================================================
def sezione_arieggiamenti(nodi, n_dentro, n_fuori):
    titolo("arieggiamenti: quante volte si e' aperta una finestra")
    if not n_dentro or not n_fuori:
        manca_ruoli("serve l'assoluta di dentro e quella di fuori verso cui corre")
        return
    if n_dentro not in nodi or n_fuori not in nodi:
        print("  nodo non trovato")
        return
    print("  Aprendo una finestra l'umidita' ASSOLUTA interna corre verso")
    print("  quella esterna in pochi minuti: molto piu' in fretta di qualunque")
    print("  fenomeno naturale. E' anche la controprova del verdetto (voce 22):")
    print("  dopo un'apertura, l'assoluta interna e' davvero andata dove il")
    print("  pannello aveva detto?\n")
    coppie = appaia(nodi[n_dentro], nodi[n_fuori])
    eventi = []
    for i in range(1, len(coppie)):
        ts0, rd0, rf0 = coppie[i - 1]
        ts1, rd1, rf1 = coppie[i]
        if ts1 - ts0 > 900:
            continue
        a0 = umidita_assoluta(rd0[2], rd0[3])
        a1 = umidita_assoluta(rd1[2], rd1[3])
        e1 = umidita_assoluta(rf1[2], rf1[3])
        if None in (a0, a1, e1):
            continue
        delta = a1 - a0
        # verso l'esterno, e di uno scatto che la casa da sola non fa
        if abs(delta) >= 0.8 and (e1 - a0) * delta > 0:
            eventi.append((ts1, delta, a0, a1, e1))
    if not eventi:
        print("  nessun evento sopra 0,8 g/m3 fra due letture consecutive.")
        print("  Con cadenza 300 s un'apertura breve puo' passare inosservata:")
        print("  l'assenza qui non e' una prova che non si sia mai aperto.")
        return
    per_giorno = defaultdict(int)
    for e in eventi:
        per_giorno[giorno_di(e[0])] += 1
    print("  %d eventi su %d giorni (%.1f al giorno)"
          % (len(eventi), len(per_giorno), len(eventi) / float(len(per_giorno))))
    for e in eventi[:25]:
        ts, d, a0, a1, ext = e
        print("    %s %s   interna %.1f -> %.1f g/m3 (%+.1f), esterna %.1f"
              % (giorno_di(ts), hhmm(ts), a0, a1, d, ext))
    if len(eventi) > 25:
        print("    ... e altri %d" % (len(eventi) - 25))


# ===========================================================================
#  9. La costante di tempo termica della casa  (6.4 / voce 33)
# ===========================================================================
def sezione_costante(nodi, n_dentro, n_fuori):
    titolo("costante di tempo termica: quanto ci mette la casa a raffreddarsi")
    if not n_dentro or not n_fuori:
        manca_ruoli("serve la differenza dentro-fuori nelle ore senza riscaldamento")
        return
    if n_dentro not in nodi or n_fuori not in nodi:
        print("  nodo non trovato")
        return
    print("  Di notte, a impianto fermo, la differenza dentro-fuori decade in")
    print("  modo esponenziale: d(dT)/dt = -dT/tau. Il tau che ne esce, in ore,")
    print("  descrive l'involucro. Non serve a niente di immediato -- ed e' il")
    print("  punto: e' un numero che NON esiste finche' non si hanno due")
    print("  sensori e uno storico. Se anno su anno cambia, e' cambiato")
    print("  qualcosa di fisico.\n")
    print("  IL METODO OVVIO SBAGLIA, ed e' stato verificato: adattare una")
    print("  esponenziale alla differenza dentro-fuori (ln(dT) contro il tempo)")
    print("  presuppone che l'ESTERNA sia costante. Di notte non lo e' -- scende")
    print("  anche lei -- e la differenza cala piu' lentamente di quanto la casa")
    print("  si raffreddi, quindi il tau esce TROPPO GRANDE. Su dati costruiti")
    print("  con tau = 20,0 h quel metodo restituiva 37 h: un numero plausibile,")
    print("  stabile notte dopo notte, e sbagliato dell'87%.")
    print("  Qui si adatta invece l'equazione vera, dT_in/dt = (T_out-T_in)/tau,")
    print("  dove l'esterna compare esplicitamente e puo' muoversi.\n")
    coppie = appaia(nodi[n_dentro], nodi[n_fuori])
    per_notte = defaultdict(list)
    for ts, rd, rf in coppie:
        h = ora_di(ts)
        if not (0 <= h < 6):                  # notte: impianto presumibilmente fermo
            continue
        if rd[2] is None or rf[2] is None:
            continue
        per_notte[giorno_di(ts)].append((ts, rd[2], rf[2]))

    FINESTRA_S = 1800     # la derivata su mezz'ora, non su un passo: con 300 s
                          # e 0,05 C di rumore la derivata a passo singolo ha
                          # un rumore dello stesso ordine del segnale.
    taus = []
    for g in sorted(per_notte):
        punti = per_notte[g]
        xs, ys = [], []
        j = 0
        for i in range(len(punti)):
            while j < len(punti) and punti[j][0] - punti[i][0] < FINESTRA_S:
                j += 1
            if j >= len(punti):
                break
            dt_h = (punti[j][0] - punti[i][0]) / 3600.0
            if dt_h <= 0:
                continue
            dTin = (punti[j][1] - punti[i][1]) / dt_h      # C/h
            # la differenza media sull'intervallo: e' la x dell'equazione
            dentro_fuori = ((punti[i][2] - punti[i][1]) + (punti[j][2] - punti[j][1])) / 2.0
            if abs(dentro_fuori) < 3.0:       # sotto i 3 C il rapporto e' rumore
                continue
            xs.append(dentro_fuori)
            ys.append(dTin)
        if len(xs) < 8:
            continue
        # regressione PER L'ORIGINE: a differenza nulla il flusso e' nullo, e
        # imporlo toglie un parametro libero che il rumore riempirebbe.
        sxx = sum(x * x for x in xs)
        sxy = sum(xs[i] * ys[i] for i in range(len(xs)))
        if sxx == 0 or sxy <= 0:
            continue
        pendenza = sxy / sxx                  # = 1/tau
        tau = 1.0 / pendenza
        ss_tot = sum(y * y for y in ys)
        ss_res = sum((ys[i] - pendenza * xs[i]) ** 2 for i in range(len(xs)))
        r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else 0.0
        if r2 < 0.2 or not (1.0 < tau < 200.0):
            continue
        taus.append(tau)
        print("    notte del %s : tau = %5.1f h   (R2 %.2f, %d intervalli)"
              % (g, tau, r2, len(xs)))
    if taus:
        print("\n  mediana di %d notti utili: tau = %.1f h" % (len(taus), mediana(taus)))
        print("  cioe' la differenza dentro-fuori si riduce di un terzo circa")
        print("  ogni %.1f ore." % mediana(taus))
    else:
        print("  nessuna notte utilizzabile: servono almeno 8 letture fra le 0 e")
        print("  le 6 con oltre 3 C di differenza, e una discesa pulita.")
    print("\n  ATTENZIONE: e' un'analisi ESPLORATIVA. Con due nodi e qualche")
    print("  settimana si trovano correlazioni che non ci sono. Il modo giusto")
    print("  di usarla e' formulare una domanda precisa e accettare anche la")
    print("  risposta 'il sospetto era ragionevole e sbagliato'.")


# ===========================================================================
#  Autoprova: gli stimatori ritrovano valori NOTI?
# ===========================================================================
#  PERCHE' ESISTE, e non e' una formalita'. Uno stimatore sbagliato non si
#  vede: produce numeri plausibili e stabili. E' successo qui, scrivendo
#  questo script -- il primo metodo per la costante di tempo (adattare
#  un'esponenziale alla differenza dentro-fuori) dava 37 h su dati costruiti
#  con 20, notte dopo notte, con un R2 di 0,7. Senza un valore vero da
#  confrontare sarebbe finito nel documento come una misura della casa.
#
#  Quindi la regola e' la stessa che il repo applica ai comandi di prova del
#  nodo: una verifica che non gira mai non si sa se funziona. Questa gira con
#  `python tools/analisi.py --autoprova`, non serve nessun file, e dura un
#  secondo.
def _dati_finti():
    """Serie sintetiche con verita' iniettate. Ritorna (nodi, verita)."""
    import time as _t
    CAD, GIORNI, TAU, SET = 300, 8, 20.0, 21.0
    t0 = int(_t.mktime(_t.strptime("2026-01-10 00:00:00", "%Y-%m-%d %H:%M:%S")))
    random_ = __import__("random").Random(7)

    def t_ext(ts, sole=True):
        lt = _t.localtime(ts)
        ora = lt.tm_hour + lt.tm_min / 60.0
        t = 6.0 - 6.0 * math.cos((ora - 4.0) / 24.0 * 2 * math.pi)
        if sole and 14.0 <= ora < 16.0:
            t += 4.5              # il sole che batte sul sensore
        return t

    passo_h = CAD / 3600.0
    t_in = SET
    casa = {}
    for k in range(GIORNI * 86400 // CAD):
        ts = t0 + k * CAD
        lt = _t.localtime(ts)
        ora = lt.tm_hour + lt.tm_min / 60.0
        t_in += (t_ext(ts, sole=False) - t_in) / TAU * passo_h
        if (6.0 <= ora < 8.5) or (18.0 <= ora < 22.0):
            t_in += (SET - t_in) * 0.25
        casa[ts] = t_in

    # La finestra "sensore fermo" si delimita per INDICE di slot, non per
    # timestamp: con il jitter di +-2 s un confine a tempo lascia entrare o
    # uscire un campione, e la verita' dichiarata non coinciderebbe con quella
    # scritta. Quante letture siano finite davvero dentro lo conta il
    # generatore -- perche' un pacchetto perso o una lettura fallita possono
    # accorciare la sequenza, e la verita' e' quello che c'e' nei dati, non
    # quello che si era inteso fare.
    k_ferma_da = (3 * 86400 + 7200) // CAD
    k_ferma_a = k_ferma_da + 6
    nodi = {"Fuori": [], "Salotto": []}
    fermo_reale = 0
    for nome in ("Fuori", "Salotto"):
        dentro = (nome == "Salotto")
        seq, bloccati, run_fermo = 1, None, 0
        for k in range(GIORNI * 86400 // CAD):
            slot = t0 + k * CAD
            ts = slot + random_.randint(-2, 2)
            lt = _t.localtime(ts)
            ora = lt.tm_hour + lt.tm_min / 60.0
            if random_.random() < 0.012:              # pacchetto perso
                seq += 1
                continue
            if dentro:
                t = casa[slot] + random_.gauss(0, 0.05)
                rh = 45.0 - 1.2 * (t - SET) + random_.gauss(0, 0.8)
                if 8.5 <= ora < 8.75:                 # finestra aperta
                    t -= 2.0
                    rh -= 9.0
            else:
                t = t_ext(ts) + random_.gauss(0, 0.15)
                rh = 78.0 - 2.2 * (t - 6.0) + random_.gauss(0, 1.5)
            rh = max(5.0, min(99.0, rh))
            p = 1012.0 + 4.0 * math.sin((ts - t0) / 3600.0 / 26.0)
            if k_ferma_da <= k < k_ferma_a:
                if bloccati is None:
                    bloccati = (round(t, 2), round(rh, 2), round(p, 2))
                t, rh, p = bloccati
                run_fermo += 1
            elif k >= k_ferma_a:
                bloccati = None
            nodi[nome].append((ts, seq, round(t, 2), round(rh, 2), round(p, 2), "NTP"))
            seq += 1
        if dentro:
            fermo_reale = run_fermo
    return nodi, dict(cadenza=CAD, tau=TAU, fermo=fermo_reale,
                      arieggiamenti_giorno=1.0)


def autoprova():
    nodi, vero = _dati_finti()
    esiti = []

    def check(nome, ottenuto, atteso, toll, unita=""):
        ok = ottenuto is not None and abs(ottenuto - atteso) <= toll
        esiti.append(ok)
        print("  [%s] %-34s atteso %.1f%s, ottenuto %s"
              % ("ok " if ok else "NO ", nome, atteso, unita,
                 ("%.1f%s" % (ottenuto, unita)) if ottenuto is not None else "niente"))

    print("=== autoprova: gli stimatori ritrovano i valori iniettati? ===\n")
    check("cadenza stimata", cadenza_stimata(nodi["Fuori"]), vero["cadenza"], 1, " s")

    # sensore fermo: la ripetizione piu' lunga
    serie = [r for r in nodi["Salotto"] if None not in (r[2], r[3], r[4])]
    run = massimo = 1
    for i in range(1, len(serie)):
        if (serie[i - 1][2], serie[i - 1][3], serie[i - 1][4]) == \
           (serie[i][2], serie[i][3], serie[i][4]):
            run += 1
            massimo = max(massimo, run)
        else:
            run = 1
    check("ripetizione piu' lunga", massimo, vero["fermo"], 0, " letture")

    # costante di tempo: si rilegge dall'output della sezione, ricalcolandola
    import re
    from contextlib import contextmanager

    @contextmanager
    def cattura():
        vecchio = sys.stdout
        buf = io.StringIO() if str is not bytes else __import__("StringIO").StringIO()
        sys.stdout = buf
        try:
            yield buf
        finally:
            sys.stdout = vecchio

    with cattura() as buf:
        sezione_costante(nodi, "Salotto", "Fuori")
    testo = buf.getvalue()
    m = re.search(r"mediana di \d+ notti utili: tau = ([\d.]+) h", testo)
    check("costante di tempo (tau)", float(m.group(1)) if m else None,
          vero["tau"], vero["tau"] * 0.10, " h")

    # il sole sul sensore esterno dev'essere RILEVATO, e sul salotto no
    with cattura() as buf:
        sezione_profilo(nodi, "Fuori")
    testo = buf.getvalue()
    trovato_fuori = "forma tipica del sole" in testo
    esiti.append(trovato_fuori)
    print("  [%s] %-34s atteso si', ottenuto %s"
          % ("ok " if trovato_fuori else "NO ", "sole rilevato sul nodo esterno",
             "si'" if trovato_fuori else "no"))

    print("\n%s  (%d prove su %d)" %
          ("TUTTO A POSTO" if all(esiti) else "QUALCOSA NON TORNA",
           sum(1 for e in esiti if e), len(esiti)))
    return 0 if all(esiti) else 1


# ===========================================================================
def main(argv):
    cartella = None
    n_dentro = n_fuori = None
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--autoprova":
            sys.exit(autoprova())
        elif a == "--dentro" and i + 1 < len(argv):
            n_dentro = argv[i + 1]; i += 2
        elif a == "--fuori" and i + 1 < len(argv):
            n_fuori = argv[i + 1]; i += 2
        elif a.startswith("--"):
            sys.exit("opzione sconosciuta: " + a)
        else:
            cartella = a; i += 1
    if not cartella:
        sys.exit(__doc__)

    nodi = carica(cartella)
    if not nodi:
        sys.exit("nessun CSV in " + cartella)

    print("=== dati letti " + "=" * 60)
    for nome in sorted(nodi):
        righe = nodi[nome]
        stime = sum(1 for r in righe if r[5] == "STIMA")
        print("  %-16s %5d campioni   dal %s al %s%s"
              % (nome, len(righe), giorno_di(righe[0][0]), giorno_di(righe[-1][0]),
                 ("   (%d con orario STIMATO)" % stime) if stime else ""))
    if any(r[5] == "STIMA" for righe in nodi.values() for r in righe):
        print("\n  Le righe con fonte_ora=STIMA hanno un orario che NON viene da")
        print("  NTP: restano nei conti perche' il valore e' buono, ma tutto")
        print("  cio' che dipende dall'ORA (profilo orario, notti, gradi")
        print("  giorno) le eredita. Vale la pena rifare l'analisi senza, se")
        print("  sono molte.")

    sezione_completezza(nodi)
    sezione_cadenza(nodi)
    sezione_previsione(nodi)
    sezione_fermo(nodi)
    sezione_riepilogo(nodi)
    sezione_verdetto(nodi, n_dentro, n_fuori)
    sezione_profilo(nodi, n_fuori)
    sezione_arieggiamenti(nodi, n_dentro, n_fuori)
    sezione_costante(nodi, n_dentro, n_fuori)
    print("")


if __name__ == "__main__":
    main(sys.argv)
