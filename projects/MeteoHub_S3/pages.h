#pragma once

// ---------------------------------------------------------------------
//  pages — il modello delle pagine del pannello.
//
//  E' la spina dorsale di tutto quello che si chiede all'interfaccia:
//  scegliere cosa mostrare, ruotare, fissare una pagina. Una struttura
//  sola, non tre meccanismi:
//
//    "fissa una pagina"        = tutte le altre disattivate
//    "cambio automatico"       = la rotazione, che si ferma da sola
//                                quando resta una sola pagina attiva
//
//  Questo modulo NON conosce il display: dice solo quale pagina tocca
//  adesso, e il .ino la disegna. Cosi' resta vero anche quando i tipi di
//  pagina aumenteranno (immagini), e la logica di rotazione si puo'
//  leggere senza avere in testa GxEPD2.
//
//  La configurazione sta in NVS (namespace "hubpag"): sono poche decine
//  di byte e devono sopravvivere al riavvio anche senza microSD. Si
//  scrive SOLO quando l'utente conferma un'impostazione, mai a ogni
//  cambio pagina: vale la lezione dei cicli di erase gia' imparata sui
//  contatori di sd_logger.
// ---------------------------------------------------------------------

#include <Arduino.h>
#include <time.h>

enum PageType : uint8_t
{
  PT_NODI = 0,     // i nodi della stazione: la pagina per cui l'hub esiste
  PT_MESSAGGIO,    // il bigliettino sul frigo, scritto dal telefono
  PT_BIANCA,       // pannello a riposo, nessun aggiornamento
  PT_IMMAGINE,     // un .bin da /images: param = nome del file, senza estensione
  PT_GRAFICO,      // temperatura dei nodi nelle ultime 24 h
  PT_DETTAGLIO,    // tutto su UN nodo: param = il suo nome
  PT_COUNT
};

// Slot liberi oltre ai tre di default: ci vanno le pagine immagine, che
// hanno un parametro (quale immagine) e quindi possono essere piu' di una
// dello stesso tipo.
//
// Portato da 8 a 16 il 2026-08-30: con tre pagine di sistema restavano
// cinque posti, e cinque immagini li esaurivano. Il limite non si vedeva
// come "elenco pieno" ma come un pulsante che non faceva niente, perche'
// la web UI ingoiava il 507 del server.
//
// ATTENZIONE se lo si cambia ancora: sizeof(PagBlob) entra nel confronto
// che valida il blob NVS, quindi un valore nuovo INVALIDA la configurazione
// salvata. Serve un magic nuovo e la conversione da quello vecchio, come
// e' stato fatto qui per PAG1 -> PAG2, o le pagine configurate spariscono
// dopo l'aggiornamento senza dire niente.
#define PAGES_MAX 16

struct PageCfg
{
  uint8_t  tipo;        // PageType
  bool     usato;       // slot occupato
  bool     attiva;      // partecipa alla rotazione
  uint16_t durata_s;    // per quanto resta a schermo quando ruota
  char     param[24];   // dipende dal tipo (quale immagine, ...)
};

void    pages_begin();
uint8_t pages_slots();                       // sempre PAGES_MAX
const PageCfg* pages_get(uint8_t i);         // nullptr se i fuori range

// Indice della pagina mostrata adesso. Non e' mai uno slot libero: se
// quello corrente venisse svuotato si torna alla prima pagina usata.
uint8_t pages_current();

// Cambio manuale (tasto BOOT o web): va alla prossima pagina USATA,
// attiva o no — a mano si devono poter vedere anche quelle escluse
// dalla rotazione. Ritorna il nuovo indice.
uint8_t pages_manual_next();
bool    pages_goto(uint8_t i);               // false se lo slot e' libero

// Rotazione automatica. Spenta di default, ed e' una scelta: un pannello
// che ruota fra sei pagine diventa un salvaschermo che nessuno legge —
// il valore dell'e-ink e' che l'informazione STA li'.
bool pages_rotazione();
void pages_set_rotazione(bool on);

// Ore di silenzio: niente rotazione fra queste due ore locali (23 -> 7
// significa "dalle 23 alle 7"). Uguali fra loro = silenzio mai.
uint8_t pages_silenzio_da();
uint8_t pages_silenzio_a();
void    pages_set_silenzio(uint8_t da, uint8_t a);
bool    pages_in_silenzio(time_t oraLocale);

// Quale pagina mostrare durante le ore di silenzio, e SOPRATTUTTO il segnale
// che in quelle ore il pannello va fermato del tutto: PAG_SIL_NESSUNA (255)
// vuol dire "come prima", cioe' si ferma solo la rotazione.
//
// Perche' conta: fino a v27 il silenzio fermava la rotazione ma non i
// refresh, e l'orologio continuava a riscrivere il suo angolo ogni minuto.
// Fra le 23 e le 7 sono ~640 refresh per una stanza al buio, il 43% di quelli
// di una giornata -- e un parziale ripetuto sempre sullo stesso rettangolo e'
// il modo peggiore in cui un e-ink invecchia. Con una pagina scelta ne bastano
// DUE: uno per entrare nella fascia e uno per uscirne.
//
// Sta in una chiave NVS SEPARATA e non nel blob delle pagine: cambiare
// sizeof(PagBlob) invaliderebbe la configurazione salvata e servirebbe un
// magic nuovo con la conversione, come per PAG1 -> PAG2. Un byte non vale una
// migrazione.
#define PAG_SIL_NESSUNA 255
// Un'immagine a caso fra quelle in elenco, sorteggiata ad OGNI ingresso nella
// fascia: la notte diventa una cornice che cambia da sola. Non e' uno slot,
// e' un modo di sceglierlo -- per questo un valore fuori dall'intervallo.
#define PAG_SIL_CASUALE 254
uint8_t pages_silenzio_pagina();
void    pages_set_silenzio_pagina(uint8_t slot);   // 255 = nessuna, 254 = a caso

// Fascia del messaggio in fondo alla pagina dei nodi. Quando e' attiva E
// c'e' un messaggio da mostrare, la pagina nodi cede 70 px al testo: i nodi
// passano al blocco compatto, quindi la temperatura scende da 24 a 18 pt.
// E' un baratto vero (si guadagna il messaggio sempre visibile, si perde
// corpo sui numeri), e per questo lo decide l'utente e non il firmware.
bool pages_fascia();
void pages_set_fascia(bool on);

// Da chiamare nel loop(): ritorna l'indice della pagina su cui andare, o
// -1 se non c'e' niente da fare. Tiene conto di rotazione spenta, ore di
// silenzio, e del caso "una sola pagina attiva" (dove non c'e' nessun
// posto dove andare, e quindi non si tocca il pannello).
int pages_tick(uint32_t nowMs, time_t oraLocale);

// Da chiamare quando la pagina e' stata effettivamente disegnata: fa
// ripartire il conto della durata. Serve anche dopo un cambio manuale,
// altrimenti la rotazione scatterebbe subito dopo.
void pages_disegnata(uint32_t nowMs);

// --- modifiche dalla web UI (ognuna scrive in NVS solo su pages_save) ---
bool pages_set_attiva(uint8_t i, bool on);
bool pages_set_durata(uint8_t i, uint16_t durata_s);

// "Fissa questa pagina": la attiva e disattiva tutte le altre. Non e' una
// modalita' a parte, e' un'operazione sull'elenco — cosi' non esiste uno
// stato "fissato" che possa andare fuori sincrono con la lista.
bool pages_fissa(uint8_t i);

// Sposta uno slot di un posto (dir < 0 su, dir > 0 giu'), scambiandolo con
// il vicino USATO. L'ordine degli slot e' l'ordine della rotazione, quindi
// riordinare e' l'unico modo di decidere in che sequenza si susseguono.
bool pages_move(uint8_t i, int dir);

// Aggiunge/rimuove uno slot (per le pagine immagine del passo successivo).
int  pages_add(uint8_t tipo, const char* param);   // -1 se non c'e' posto
bool pages_remove(uint8_t i);

void pages_save();       // scrive in NVS: SOLO su conferma dell'utente
const char* pages_tipo_nome(uint8_t tipo);
