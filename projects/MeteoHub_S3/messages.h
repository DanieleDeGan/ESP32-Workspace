#pragma once

// ---------------------------------------------------------------------
//  messages — il bigliettino sul frigo, scritto dal telefono.
//
//  Due posti, due ruoli diversi (non la stessa cosa scritta due volte,
//  quindi non possono divergere):
//
//    NVS  il messaggio ATTIVO adesso, ~200 byte. E' quello che sta sul
//         pannello: deve tornare identico dopo un riavvio ANCHE con la
//         microSD tolta. Se stesse solo sulla card, l'hub si riavvierebbe
//         senza sapere cosa sta mostrando.
//    SD   l'ARCHIVIO di tutti i messaggi scritti, per riusarli e per non
//         consumare cicli di erase della NVS tenendo uno storico.
//
//  Il testo e' UTF-8 e va disegnato con U8g2_for_Adafruit_GFX: i font
//  Adafruit GFX sono ASCII puro e "perche'" diventerebbe "perch?".
// ---------------------------------------------------------------------

#include <Arduino.h>
#include <time.h>

#define MSG_TESTO_MAX 200          // caratteri utili, UTF-8 (byte: +1 NUL)

enum MsgPriorita : uint8_t
{
  MSG_NORMALE = 0,
  MSG_URGENTE = 1     // porta il pannello sulla sua pagina subito
};

struct Message
{
  char     testo[MSG_TESTO_MAX + 1];
  time_t   creato;
  time_t   scadenza;      // 0 = non scade
  uint8_t  priorita;      // MsgPriorita
};

void msg_begin();

// Il messaggio da mostrare adesso, o nullptr se non ce n'e' o e' scaduto.
// La scadenza si valuta ad ogni chiamata: nessun timer da tenere.
const Message* msg_active(time_t adesso);

// Scrive il messaggio attivo (NVS) e lo accoda all'archivio (SD, se c'e').
// Un testo vuoto equivale a msg_clear(). false = testo troppo lungo.
bool msg_set(const char* testo, time_t creato, time_t scadenza, uint8_t priorita);

void msg_clear();

// true una volta sola, a chi la chiede per primo: serve al .ino per
// portare il pannello sulla pagina messaggio quando ne arriva uno
// urgente, senza che il modulo debba conoscere le pagine.
bool msg_take_urgent();

// true se dall'ultima chiamata il messaggio e' cambiato o e' scaduto:
// il .ino lo usa per sapere che il pannello va ridisegnato.
bool msg_take_dirty(time_t adesso);

// --- archivio su microSD ---------------------------------------------
#define MSG_DIR       "/messaggi"
#define MSG_ARCHIVIO  "/messaggi/archivio.csv"

typedef void (*msg_row_cb_t)(const Message& m, void* arg);

// Legge l'archivio dal piu' recente al piu' vecchio, al massimo maxItems.
// Ritorna quanti ne ha passati alla callback (0 se SD assente o vuoto).
int msg_archive_list(msg_row_cb_t cb, void* arg, int maxItems);
