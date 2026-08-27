#pragma once
#include <Arduino.h>
#include <time.h>

// =====================================================================
//  rtc_time — orario del nodo: stima al boot + NTP quando c'e' WiFi
//
//  Il C3 Supermini non ha un cristallo 32kHz esterno ne' un RTC tamponato
//  a batteria (scelta gia' confermata: solo RTC interno, nessun DS3231).
//  L'orologio di sistema (letto/scritto con le API standard time.h, che
//  sotto usano l'RTC interno del chip) va quindi "seminato" a un valore
//  plausibile prima ancora che il WiFi sia su, altrimenti riparte da
//  epoch 0 (1 gennaio 1970) — vedi rtctime_seedFromBuild().
//
//  Sequenza d'uso in setup(), IN QUEST'ORDINE:
//    1) rtctime_begin(settings_get().tz)   — imposta il fuso (setenv+tzset)
//    2) rtctime_seedFromBuild()            — stima iniziale da __DATE__/__TIME__
//    ... (WiFi, se disponibile) ...
//    3) rtctime_onWifiConnected()          — avvia/rilancia il sync NTP
//  rtctime_begin() DEVE precedere seedFromBuild(): mktime() interpreta la
//  data di build come ora locale nel fuso corrente, quindi il fuso va gia'
//  impostato. rtctime_onWifiConnected() va richiamata a ogni riconnessione
//  WiFi (non solo la prima), per non dipendere dal solo resync periodico
//  implicito del client SNTP.
//
//  Approssimazione nota e accettata su rtctime_seedFromBuild(): __TIME__ e'
//  l'ora locale della macchina che ha compilato lo sketch, non UTC; non
//  potendo conoscere quel fuso a runtime, si tratta come se fosse gia' ora
//  locale nel fuso target. Errore nel caso peggiore di qualche ora,
//  corretto al primo sync NTP.
// =====================================================================

// Imposta il fuso orario (stringa POSIX TZ, es. "CET-1CEST,M3.5.0,M10.5.0/3")
// e registra la callback interna di notifica sync NTP. Richiamabile in
// qualunque momento (es. l'utente cambia tz da web): riapplica subito il
// fuso alle conversioni locali successive.
void rtctime_begin(const char* tzPosix);

// Stima l'orario dalla data/ora di compilazione (__DATE__/__TIME__). Da
// chiamare una sola volta in setup(), dopo rtctime_begin() e prima del
// WiFi. Non fa nulla se il parsing fallisce (non dovrebbe mai succedere:
// __DATE__/__TIME__ sono generate dal compilatore).
void rtctime_seedFromBuild();

// Avvia (o rilancia) il sync NTP. Da chiamare la prima volta che il WiFi
// si connette, e di nuovo a ogni riconnessione successiva.
void rtctime_onWifiConnected();

// true dal momento in cui il PRIMO sync NTP e' andato a buon fine.
bool rtctime_isSynced();

// "NTP" oppure "STIMA" (build-time, nessun sync NTP ancora riuscito) — va
// nella colonna fonte_ora del CSV e nella UI, cosi' si vede sempre quanto
// fidarsi di un timestamp.
const char* rtctime_source();

time_t rtctime_now();

// Ora locale corrente (secondo il fuso di rtctime_begin) in *out. Ritorna
// false solo se out e' nullptr.
bool rtctime_nowLocal(struct tm* out);

// Formatta `t` (ora locale) con strftime in out/outCap. Ritorna false se
// out/outCap non bastano o out e' nullptr.
bool rtctime_format(time_t t, const char* fmt, char* out, size_t outCap);
