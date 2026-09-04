#pragma once
#include <Arduino.h>
#include <math.h>
#include "meteo_calc.h"

// =====================================================================
//  daily — gli aggregati di una giornata, header-only e PURO
//
//  Sullo stampo di forecast.h e meteo_calc.h: nessuno stato globale,
//  nessun accesso alla card, niente Serial. Riceve campioni e restituisce
//  numeri, cosi' si puo' rigiocare su dati finti senza una scheda accesa.
//
//  L'IDEA: i CSV per giorno rispondono benissimo a "com'era ieri alle
//  15:40" e malissimo a "che mese e' stato". Ogni vista storica oggi deve
//  rileggere i file interi, ed e' il motivo per cui il seeding legge solo
//  la coda e il grafico si ferma a 24 h. Una riga per giorno sposta quel
//  costo a fine giornata, una volta sola, e rende "l'ultimo mese" e
//  "l'anno" letture da pochi kB.
//
//  DUE COSE CHE SEMBRANO DETTAGLI E NON LO SONO:
//
//  1. LA RUGIADA MEDIA NON E' LA RUGIADA DELLE MEDIE. meteo_dewpoint_c()
//     non e' lineare in T e RH, quindi calcolarla sui valori medi da' un
//     numero diverso. MISURATO sui giorni veri del 2 e 3 settembre 2026:
//     +0,020..+0,031 C, sempre dello stesso segno — e' la disuguaglianza di
//     Jensen, non rumore. Sono tre centesimi di grado, cioe' poco: la si fa
//     giusta perche' non costa niente farla giusta, non perche' quel numero
//     cambi una decisione. Si calcola su OGNI campione e poi si media, ed e'
//     il motivo per cui sta qui dentro come grandezza sua invece che come
//     conto fatto alla fine.
//
//  2. UN AGGREGATO SENZA LA SUA COMPLETEZZA E' UN NUMERO CHE MENTE. Un
//     minimo calcolato sul 40 % dei campioni ha lo stesso identico aspetto
//     di un minimo vero, e nessuno potra' piu' distinguerli: la giornata
//     con il buco resta nello storico per sempre, indistinguibile. Per
//     questo `campioni` e `attesi` viaggiano SEMPRE con i valori, e non
//     sono una colonna in piu' da aggiungere un giorno.
// =====================================================================

// Una grandezza seguita per una giornata. L'ora del minimo e del massimo si
// tiene solo dove serve leggerla (la temperatura): per le altre sarebbe
// spazio e colonne spese per una domanda che nessuno fa.
struct daily_stat_t {
  float    minimo;
  float    massimo;
  double   somma;      // double e non float: 288 addizioni in singola
                       // precisione perdono cifre proprio sulla media
  uint32_t n;
  time_t   oraMin;
  time_t   oraMax;
};

struct daily_t {
  daily_stat_t t;      // temperatura
  daily_stat_t h;      // umidita' relativa
  daily_stat_t p;      // pressione (grezza, come la trasmette il nodo)
  daily_stat_t td;     // punto di rugiada, calcolato campione per campione

  uint32_t campioni;   // righe con almeno una grandezza valida
  uint32_t buchi;      // pacchetti persi, dai salti di seq
  time_t   primo;
  time_t   ultimo;

  float    pPrimo;     // pressione del primo e dell'ultimo campione:
  float    pUltimo;    // la loro differenza e' la variazione sulle 24 h

  uint32_t seqPrec;
  bool     seqVisto;
  time_t   tsPrec;

  // I primi delta fra campioni consecutivi, per stimare la cadenza DI QUEL
  // GIORNO. Serve la mediana e non la media: un buco allunga un delta di un
  // multiplo intero del periodo, e la media se lo porterebbe dentro — che e'
  // lo stesso difetto corretto nella v44 sulla cadenza appresa dall'hub.
  //
  // Sessantaquattro bastano: la mediana di 64 delta e' gia' stabile, e su un
  // giorno a 60 s ce ne sono 1440. Costa 128 byte nello stack di chi aggrega
  // un giorno per volta.
  static const int DELTA_MAX = 64;
  uint16_t deltaS[DELTA_MAX];
  int      nDelta;
};

static inline void daily_stat_reset(daily_stat_t& s) {
  s.minimo = NAN; s.massimo = NAN; s.somma = 0.0; s.n = 0;
  s.oraMin = 0;   s.oraMax = 0;
}

static inline void daily_reset(daily_t& d) {
  daily_stat_reset(d.t);  daily_stat_reset(d.h);
  daily_stat_reset(d.p);  daily_stat_reset(d.td);
  d.campioni = 0; d.buchi = 0; d.primo = 0; d.ultimo = 0;
  d.pPrimo = NAN; d.pUltimo = NAN;
  d.seqPrec = 0;  d.seqVisto = false;
  d.tsPrec = 0;   d.nDelta = 0;
}

static inline void daily_stat_add(daily_stat_t& s, float v, time_t ts) {
  if (!isfinite(v)) return;                 // un buco resta un buco
  if (!isfinite(s.minimo) || v < s.minimo) { s.minimo = v; s.oraMin = ts; }
  if (!isfinite(s.massimo) || v > s.massimo) { s.massimo = v; s.oraMax = ts; }
  s.somma += (double)v;
  s.n++;
}

static inline float daily_media(const daily_stat_t& s) {
  return s.n ? (float)(s.somma / (double)s.n) : NAN;
}

// Un campione del giorno. `seq` serve solo a contare i buchi: il salto e' un
// dato, non un incidente da nascondere. Le grandezze mancanti si passano NAN
// e non entrano da nessuna parte.
static inline void daily_add(daily_t& d, time_t ts, uint32_t seq,
                             float tempC, float humPct, float pressHpa) {
  // Il tetto sul salto e' quello di remote_nodes (PERSI_SALTO_MAX): il seq
  // attraversa il deep sleep passando dalla RTC memory, e un valore sporco
  // letto da li' diventerebbe qualche milione di "persi" dentro una riga che
  // resta sulla card per sempre.
  if (d.seqVisto && seq > d.seqPrec) {
    const uint32_t salto = seq - d.seqPrec - 1;
    if (salto > 0 && salto <= 1000) d.buchi += salto;
  }
  // Il delta fra campioni CONSECUTIVI nel seq: uno che scavalca un buco non e'
  // un periodo, e' due periodi (stessa regola della cadenza appresa dall'hub).
  if (d.tsPrec != 0 && d.nDelta < daily_t::DELTA_MAX &&
      d.seqVisto && seq == d.seqPrec + 1) {
    const long dt = (long)(ts - d.tsPrec);
    if (dt > 0 && dt < 65535) d.deltaS[d.nDelta++] = (uint16_t)dt;
  }
  d.tsPrec = ts;
  d.seqPrec = seq; d.seqVisto = true;

  const bool qualcosa = isfinite(tempC) || isfinite(humPct) || isfinite(pressHpa);
  if (!qualcosa) return;

  daily_stat_add(d.t, tempC,    ts);
  daily_stat_add(d.h, humPct,   ts);
  daily_stat_add(d.p, pressHpa, ts);

  // La rugiada campione per campione, mai dalle medie (vedi il commento in
  // testa). Vuole entrambe le grandezze: con una sola non esiste.
  if (isfinite(tempC) && isfinite(humPct))
    daily_stat_add(d.td, meteo_dewpoint_c(tempC, humPct), ts);

  if (isfinite(pressHpa)) {
    if (!isfinite(d.pPrimo)) d.pPrimo = pressHpa;
    d.pUltimo = pressHpa;
  }

  if (d.primo == 0) d.primo = ts;
  d.ultimo = ts;
  d.campioni++;
}

// Variazione della pressione sulla giornata: l'analogo a 24 h del trend a 3 h.
static inline float daily_p_var(const daily_t& d) {
  if (!isfinite(d.pPrimo) || !isfinite(d.pUltimo)) return NAN;
  return d.pUltimo - d.pPrimo;
}

// Quanti campioni ci si aspettava, data la cadenza del nodo. Si passa la
// cadenza APPRESA dall'hub (remote_nodes la misura sui DATA consecutivi):
// duplicare qui il valore configurato sul nodo sarebbe solo un modo per
// andare fuori sincrono.
//
// Il giorno in corso non si chiude mai, quindi `durataS` esiste per il caso
// di un giorno parziale: un nodo associato a meta' pomeriggio ha meno
// campioni attesi, e senza questo la sua completezza direbbe 40 % per un
// giorno in cui non e' mancato niente.
// La cadenza DI QUEL GIORNO, dai suoi stessi campioni. 0 se non ci sono
// abbastanza delta per dirlo.
//
// PERCHE' NON SI USA LA CADENZA APPRESA DALL'HUB, che pure e' li' pronta: e'
// la cadenza di ADESSO, e un riepilogo dev'essere autosufficiente. Misurato
// sui dati veri di questa stazione: il nodo a muro stava a 60 s fino al
// 26/08 e a 300 s dopo, quindi i giorni di fine agosto hanno ~1440 campioni
// e quelli di settembre ~288. Usare i 299 s di oggi per il 28/08 darebbe una
// completezza del 497 % — un numero assurdo scritto una volta sola dentro
// una riga che nessuno riscrive piu'.
//
// Mediana e non media, per la stessa ragione della v44: un delta che
// scavalca un buco e' un multiplo del periodo, e la media se lo porta dentro.
static inline uint32_t daily_cadenza_s(const daily_t& d) {
  if (d.nDelta < 5) return 0;          // troppo pochi per dire qualcosa
  uint16_t v[daily_t::DELTA_MAX];
  for (int i = 0; i < d.nDelta; i++) v[i] = d.deltaS[i];
  // Insertion sort: n <= 64, e non vale portarsi dietro qsort per questo.
  for (int i = 1; i < d.nDelta; i++) {
    const uint16_t x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
  return v[d.nDelta / 2];
}

// Si ARROTONDA, non si tronca, e il motivo e' stato misurato: la cadenza vera
// dei nodi e' 299 s, quindi in un giorno ci stanno 288,9 campioni. Troncando
// gli attesi diventano 288 mentre ne arrivano 289-291, e la completezza esce
// 100,3 % o 101,0 % — un numero che sembra un errore di conto ed e' invece
// solo un arrotondamento fatto dalla parte sbagliata.
//
// SOPRA IL 100 % CI SI PUO' ANDARE LO STESSO, e non si tappa: la cadenza e'
// STIMATA (media mobile su interi), quindi un secondo di errore su 299 vale
// gia' un campione al giorno. Cappare a 100 nasconderebbe proprio il caso in
// cui la stima si e' spostata — e una completezza che non supera mai il 100 %
// non e' piu' una misura, e' una rassicurazione.
static inline uint32_t daily_attesi(uint32_t cadenzaS, uint32_t durataS) {
  if (cadenzaS == 0) return 0;
  return (durataS + cadenzaS / 2) / cadenzaS;
}

static inline float daily_completezza_pct(uint32_t campioni, uint32_t attesi) {
  if (attesi == 0) return NAN;
  return 100.0f * (float)campioni / (float)attesi;
}
