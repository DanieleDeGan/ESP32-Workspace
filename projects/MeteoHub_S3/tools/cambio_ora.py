#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Le due notti all'anno in cui l'orologio salta: la marea le regge?

    python tools/cambio_ora.py

NON parla con la scheda: rifa' in Python la logica che sta nel firmware e le fa
girare sopra cinque anni di calendario, perche' il caso vero capita due volte
l'anno e aspettarlo per provarlo sarebbe l'unico modo di scoprirlo tardi.

COSA VERIFICA, e perche' esiste:

La tabella della marea e' indicizzata per ora UTC (v66), e quello e' gia' meta'
della risposta: l'ora civile salta, il sole no, quindi in UTC la notte del
cambio non sposta ne' la taratura ne' l'applicazione. Ma il giorno resta storto
lo stesso, perche' i CSV sono spezzati per giorno LOCALE:

  - a marzo quel file copre 23 ore, e un'ora UTC resta senza campioni:
    `daily_marea` lo rifiuta da se', senza che nessuno glielo chieda;
  - a ottobre ne copre 25, con un'ora UTC riempita due volte a distanza di un
    giorno e la media del giorno sbilanciata su di lei. Questo NON si vede da
    nessuna parte: entrerebbe in media un giorno leggermente storto, una volta
    l'anno, per sempre. Lo scarta `giornoCambiaOra()`.

Quel controllo confronta l'offset da UTC a mezzogiorno -+ 11 h, cioe' l'01 e le
23 locali: se cambia, la transizione e' dentro quel giorno. Qui sotto si
verifica che trovi tutte e sole le domeniche giuste -- 10 transizioni su
2026-2030, zero mancate, zero falsi positivi -- e quante ore durano davvero i
due giorni.

Il fuso e' quello della scheda: "CET-1CEST,M3.5.0,M10.5.0/3", cioe' transizioni
all'01:00 UTC dell'ultima domenica di marzo e di ottobre. Cambiando fuso alla
stazione, cambiare anche `transizioni()`.
"""
import calendar, datetime

def ultima_domenica(anno, mese):
    ultimo = calendar.monthrange(anno, mese)[1]
    d = datetime.date(anno, mese, ultimo)
    return d - datetime.timedelta(days=(d.weekday() + 1) % 7)

def transizioni(anno):
    return (datetime.datetime.combine(ultima_domenica(anno, 3), datetime.time(1)),   # UTC
            datetime.datetime.combine(ultima_domenica(anno, 10), datetime.time(1)))

def offset_utc(dt_utc):
    """+2 h dentro l'ora legale, +1 h fuori. dt_utc e' un datetime naive UTC."""
    marzo, ottobre = transizioni(dt_utc.year)
    return 7200 if marzo <= dt_utc < ottobre else 3600

def mezzogiorno_locale(giorno):
    """L'istante UTC del mezzogiorno locale, come lo da' mktime(isdst=-1)."""
    for off in (3600, 7200):
        cand = datetime.datetime.combine(giorno, datetime.time(12)) - datetime.timedelta(seconds=off)
        if offset_utc(cand) == off:
            return cand
    return datetime.datetime.combine(giorno, datetime.time(12)) - datetime.timedelta(hours=1)

def giorno_cambia_ora(giorno):
    mez = mezzogiorno_locale(giorno)
    return offset_utc(mez - datetime.timedelta(hours=11)) != offset_utc(mez + datetime.timedelta(hours=11))

falsi_positivi, trovati, mancati = [], [], []
for anno in range(2026, 2031):
    attesi = {ultima_domenica(anno, 3), ultima_domenica(anno, 10)}
    g = datetime.date(anno, 1, 1)
    while g.year == anno:
        r = giorno_cambia_ora(g)
        if r and g in attesi:      trovati.append(g)
        elif r and g not in attesi: falsi_positivi.append(g)
        elif not r and g in attesi: mancati.append(g)
        g += datetime.timedelta(days=1)

print("giorni esaminati: 5 anni interi (2026-2030)")
print("transizioni trovate  : %d  %s" % (len(trovati), ", ".join(str(d) for d in trovati)))
print("transizioni mancate  : %d  %s" % (len(mancati), mancati or "-"))
print("falsi positivi       : %d  %s" % (len(falsi_positivi), falsi_positivi or "-"))

# e le ore che quei due giorni durano davvero, in ore UTC coperte dal CSV locale
def inizio_giorno_locale(giorno):
    """L'istante UTC della mezzanotte locale: l'offset e' quello valido LI'."""
    for off in (3600, 7200):
        cand = datetime.datetime.combine(giorno, datetime.time(0)) - datetime.timedelta(seconds=off)
        if offset_utc(cand) == off:
            return cand
    return datetime.datetime.combine(giorno, datetime.time(0)) - datetime.timedelta(hours=1)

print('')
print("quante ore dura il file CSV di quei giorni (e' spezzato per giorno LOCALE):")
for g in trovati[:4]:
    ini = inizio_giorno_locale(g)
    fin = inizio_giorno_locale(g + datetime.timedelta(days=1))
    ore = int((fin - ini).total_seconds() / 3600)
    ore_utc = {}
    for h in range(ore):
        k = (ini + datetime.timedelta(hours=h)).hour
        ore_utc[k] = ore_utc.get(k, 0) + 1
    vuote = 24 - len(ore_utc)
    doppie = sum(1 for v in ore_utc.values() if v > 1)
    print('  %s  %2d ore locali -> %2d ore UTC coperte, %d vuote, %d doppie   %s'
          % (g, ore, len(ore_utc), vuote, doppie,
             "daily_marea lo rifiuta da se'" if vuote else 'lo scarta giornoCambiaOra'))
