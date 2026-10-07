#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Il giro delle immagini della notte, rifatto fuori dalla scheda.

Da `v79` l'immagine delle ore di silenzio non si sorteggia piu' fra tutte: le
immagini escono A GIRO, ognuna una volta sola, e quando sono finite si
ricomincia con un ordine nuovo. La proprieta' che conta -- "nessuna si ripete
finche' non le hai viste tutte" -- su hardware si verifica in DICIASSETTE
notti, e un difetto si vedrebbe come "mi sembra che si ripeta", cioe' come il
difetto che questa versione serve a togliere. Per questo si prova qui.

L'algoritmo e' quello di `immagineDelGiro()` nel `.ino`, riscritto riga per
riga: un SEME a 32 bit, l'ordine dato dalle chiavi FNV-1a `hash(nome, seme)`, e
come stato il solo nome dell'ultima uscita. La prossima immagine e' il suo
SUCCESSORE in quell'ordine; quando un successore non c'e' piu', il giro e'
finito. Se le due implementazioni divergono, questo file diventa una seconda
opinione invece di un giudice: la regola e' la stessa di `pannello_mock.py` --
una modifica al firmware va rifatta qui lo stesso giorno.

    python tools/giro_simula.py                  # le sette prove, sui nomi veri
    python tools/giro_simula.py --host 192.168.1.72 --user admin --pass admin
    python tools/giro_simula.py --giri 3 --mostra # i giri, nome per nome

Esce 1 se una prova fallisce, quindi si puo' mettere in un hook.
"""

import argparse
import base64
import json
import random
import sys
import urllib.request

# Diciassette nomi di esempio, tanti quante sono le immagini sulla card: le
# prove devono girare anche senza la scheda accesa. Sono INVENTATI di
# proposito, con lunghezze e iniziali varie come quelli veri -- questo repo e'
# pubblico, e i nomi delle foto di casa sono dati di casa. Con `--host` lo
# strumento usa quelli veri, che e' anche il modo di provare il giro
# sull'archivio che conta davvero.
NOMI_DEFAULT = [
    "Montagna", "Lago", "Tramonto", "PiazzaGrande", "Fiori",
    "Neve", "Cucina", "Gatto", "BiciclettaRossa",
    "SpiaggiaDAutunno", "CastelloVecchio", "Ponte", "Mercato",
    "Finestra", "GiardinoDiSera", "Pioggia", "Stelle",
]

IMG_NOME_MAX = 20          # sd_logger.h
FNV_OFFSET   = 2166136261  # il valore iniziale quando il seme e' 0
FNV_PRIME    = 16777619
M32          = 0xFFFFFFFF


# ----------------------------------------------------------------------
#  L'algoritmo del firmware
# ----------------------------------------------------------------------
def chiave(nome, seme):
    """giroChiave(): FNV-1a a 32 bit col seme come valore iniziale."""
    h = seme if seme else FNV_OFFSET
    for b in nome.encode("ascii"):
        h = ((h ^ b) * FNV_PRIME) & M32
    return h


def cmp_giro(a, b):
    """giroCmp(): prima la chiave, a pari chiave il nome. -1, 0 o 1."""
    if a[0] != b[0]:
        return -1 if a[0] < b[0] else 1
    if a[1] == b[1]:
        return 0
    return -1 if a[1] < b[1] else 1


class Giro(object):
    """Lo stato persistente: il seme e il nome dell'ultima uscita (NVS)."""

    def __init__(self, rnd=None):
        self.seme = 0
        self.ultimo = ""
        self.rnd = rnd or random.Random(12345)
        self.scritture = 0      # quante volte si tocca la NVS
        self.ricomincia = 0     # quante volte il giro e' ripartito

    def nuovo(self):
        """giroNuovo(): un seme diverso da quello che c'era."""
        s = 0
        while s == 0 or s == self.seme:
            s = self.rnd.getrandbits(32)
        self.seme = s
        self.ultimo = ""
        self.ricomincia += 1

    def registra(self, nome):
        self.ultimo = nome
        self.scritture += 1

    def prossima(self, immagini, evita=None):
        """immagineDelGiro(). Torna (nome, restano) o (None, -1)."""
        if self.seme == 0:
            self.nuovo()
            self.ricomincia -= 1    # il primo giro non e' un "ricomincia"

        out = None
        for _ in range(6):
            dopo = (chiave(self.ultimo, self.seme), self.ultimo) if self.ultimo else None
            out, k_out, restanti, totale = None, None, 0, 0

            for nome in immagini:                      # la passata di giroCb()
                if len(nome) >= IMG_NOME_MAX + 1:
                    continue                           # nome troppo lungo: fuori dal giro
                totale += 1
                k = (chiave(nome, self.seme), nome)
                if dopo is not None and cmp_giro(k, dopo) <= 0:
                    continue                           # gia' uscita in questo giro
                restanti += 1
                if out is None or cmp_giro(k, k_out) < 0:
                    out, k_out = nome, k

            if out is not None:
                if totale > 1 and evita and out == evita:
                    self.nuovo()
                    continue
                self.registra(out)
                return out, restanti - 1
            if totale == 0:
                return None, -1
            self.nuovo()

        if out is not None:
            self.registra(out)
            return out, -1
        return None, -1


# ----------------------------------------------------------------------
#  Le prove
# ----------------------------------------------------------------------
class Prove(object):
    def __init__(self):
        self.falliti = []

    def esito(self, nome, ok, dettaglio=""):
        print("  %s  %s%s" % ("ok  " if ok else "FALLITA", nome,
                              ("  -- " + dettaglio) if dettaglio else ""))
        if not ok:
            self.falliti.append(nome)


def un_giro(g, immagini, evita=None):
    """Tira avanti finche' il giro non ricomincia. Torna i nomi in ordine."""
    partenza = g.ricomincia
    usciti = []
    for _ in range(len(immagini) * 3 + 5):
        nome, _resta = g.prossima(immagini, evita)
        if nome is None:
            break
        if g.ricomincia > partenza:          # il giro e' ripartito: questa e' del nuovo
            return usciti, nome
        usciti.append(nome)
        evita = nome
    return usciti, None


def prova(immagini, giri, mostra):
    p = Prove()
    n = len(immagini)
    print("%d immagini\n" % n)

    # 1 - un giro copre tutto, una volta per immagine
    g = Giro()
    usciti, prima_del_dopo = un_giro(g, immagini)
    p.esito("un giro copre tutte le immagini una volta sola",
            sorted(usciti) == sorted(immagini),
            "%d uscite, %d distinte" % (len(usciti), len(set(usciti))))
    if mostra:
        for i, nome in enumerate(usciti, 1):
            print("        %2d. %s" % (i, nome))

    # 2 - i giri successivi hanno un ordine diverso
    ordini = [tuple(usciti)]
    ultimo = usciti[-1] if usciti else None
    primo_nuovo = prima_del_dopo
    for _ in range(max(1, giri) - 1):
        altri, prima_del_dopo = un_giro(g, immagini, ultimo)
        if primo_nuovo:
            altri = [primo_nuovo] + altri
        ordini.append(tuple(altri))
        if mostra:
            print("        giro: " + " ".join(altri))
        ultimo = altri[-1] if altri else ultimo
        primo_nuovo = prima_del_dopo
    p.esito("ogni giro ha un ordine suo", len(set(ordini)) == len(ordini),
            "%d giri, %d ordini distinti" % (len(ordini), len(set(ordini))))
    p.esito("ogni giro resta completo",
            all(sorted(o) == sorted(immagini) for o in ordini))

    # 3 - niente ripetizioni a cavallo di due giri
    g2 = Giro(random.Random(99))
    uscite = []
    for _ in range(n * 4):
        nome, _r = g2.prossima(immagini, uscite[-1] if uscite else None)
        uscite.append(nome)
    ripetute = [i for i in range(1, len(uscite)) if uscite[i] == uscite[i - 1]]
    p.esito("nessuna immagine due notti di fila", not ripetute,
            "%d notti simulate" % len(uscite))

    # 4 - un'immagine caricata a giro iniziato
    g3 = Giro(random.Random(7))
    viste = []
    for _ in range(n // 2):
        nome, _r = g3.prossima(immagini, viste[-1] if viste else None)
        viste.append(nome)
    cresciute = immagini + ["ImmagineNuova"]
    resto, _ = un_giro(g3, cresciute, viste[-1])
    p.esito("un'immagine caricata a giro iniziato non fa ripetere le viste",
            not (set(resto) & set(viste)),
            "%d viste, poi %d senza sovrapposizioni" % (len(viste), len(resto)))
    p.esito("l'immagine nuova entra entro il giro dopo",
            "ImmagineNuova" in resto or
            "ImmagineNuova" in un_giro(g3, cresciute, resto[-1] if resto else None)[0])

    # 5 - un'immagine cancellata non riappare
    g4 = Giro(random.Random(31))
    nome1, _r = g4.prossima(immagini)
    ridotte = [x for x in immagini if x != nome1]
    seguito, _ = un_giro(g4, ridotte, nome1)
    p.esito("un'immagine cancellata non riappare", nome1 not in seguito)

    # 6 - una sola immagine sulla card: esce quella, senza bloccarsi
    g5 = Giro(random.Random(5))
    sola = [immagini[0]]
    esiti = [g5.prossima(sola, immagini[0])[0] for _ in range(4)]
    p.esito("con una sola immagine esce sempre quella",
            esiti == [immagini[0]] * 4)

    # 7 - il costo in scritture NVS: una per notte, non di piu'
    g6 = Giro(random.Random(2024))
    for _ in range(100):
        g6.prossima(immagini, None)
    p.esito("una scrittura NVS per notte", g6.scritture == 100,
            "%d scritture su 100 notti" % g6.scritture)

    print("")
    if p.falliti:
        print("FALLITE: " + ", ".join(p.falliti))
        return 1
    print("tutte le prove passate")
    return 0


def nomi_dallhub(host, user, pw):
    url = "http://%s/api/immagini?da=0&quante=200" % host
    req = urllib.request.Request(url)
    if user:
        token = base64.b64encode(("%s:%s" % (user, pw)).encode()).decode()
        req.add_header("Authorization", "Basic " + token)
    with urllib.request.urlopen(req, timeout=15) as r:
        d = json.loads(r.read().decode("utf-8", "replace"))
    # Ogni voce e' {"nome","byte","ok"}: qui serve solo il nome, che e' anche
    # l'unica cosa da cui il giro dipende.
    return [x["nome"] for x in (d.get("immagini") or []) if x.get("nome")]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", help="IP dell'hub: prende i nomi veri dalla card")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--pass", dest="pw", default="admin")
    ap.add_argument("--giri", type=int, default=4, help="quanti giri confrontare")
    ap.add_argument("--mostra", action="store_true", help="stampa i giri")
    a = ap.parse_args()

    immagini = NOMI_DEFAULT
    if a.host:
        try:
            immagini = nomi_dallhub(a.host, a.user, a.pw)
            print("nomi presi da %s" % a.host)
        except Exception as e:
            print("hub non raggiungibile (%s): uso l'elenco del 2026-10-07" % e)
    if len(immagini) < 2:
        print("servono almeno due immagini")
        return 1
    return prova(immagini, a.giri, a.mostra)


if __name__ == "__main__":
    sys.exit(main())
