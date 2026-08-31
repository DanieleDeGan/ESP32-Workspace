/*
 * pages.cpp - elenco delle pagine del pannello, rotazione, persistenza.
 * ---------------------------------------------------------------------------
 * Nessuna dipendenza dal display: qui si decide COSA mostrare, non come.
 * La persistenza e' un blob solo in NVS, come il registro dei nodi in
 * remote_nodes.cpp: e' configurazione, poche decine di byte, e un blob
 * unico evita di dover migrare N chiavi quando la struttura cambia.
 */

#include "pages.h"
#include <Preferences.h>

static Preferences s_prefs;

static PageCfg s_pag[PAGES_MAX];
static uint8_t s_cur       = 0;
static bool    s_rotazione = false;
static uint8_t s_silDa     = 23;
static uint8_t s_silPag = PAG_SIL_NESSUNA;   // slot da mostrare nel silenzio
static uint8_t s_silA      = 7;
static bool    s_fascia    = false;  // messaggio in fondo alla pagina nodi
static uint32_t s_ultimoMs = 0;   // quando e' stata disegnata la corrente

// Versione del blob: se la struttura cambia, un blob vecchio si riconosce e
// si CONVERTE invece di essere scartato come spazzatura.
//
// PAG2 (2026-08-30) esiste solo perche' PAGES_MAX e' passato da 8 a 16: la
// validita' del blob si controlla anche sulla lunghezza, quindi un elenco
// piu' lungo rende irriconoscibile quello salvato. Scartarlo e ripartire dai
// default sarebbe stato un guasto silenzioso del tipo peggiore — le immagini
// restano sulla card, le pagine spariscono dall'elenco, e succede DURANTE un
// aggiornamento, cioe' quando nessuno sta guardando quella schermata.
static const uint32_t PAG_MAGIC    = 0x50414732;  // "PAG2" — 16 slot
static const uint32_t PAG_MAGIC_V1 = 0x50414731;  // "PAG1" — 8 slot
static const uint8_t  PAGES_MAX_V1 = 8;

struct PagBlob
{
  uint32_t magic;
  uint8_t  rotazione;
  uint8_t  silDa;
  uint8_t  silA;
  uint8_t  fascia;
  PageCfg  pag[PAGES_MAX];
};

// La forma esatta di prima: serve solo a rileggere cio' che e' gia' in NVS.
// Non va piu' toccata — se un giorno servisse un PAG3, si aggiunge accanto.
struct PagBlobV1
{
  uint32_t magic;
  uint8_t  rotazione;
  uint8_t  silDa;
  uint8_t  silA;
  uint8_t  fascia;
  PageCfg  pag[PAGES_MAX_V1];
};

const char* pages_tipo_nome(uint8_t tipo)
{
  switch (tipo)
  {
    case PT_NODI:      return "nodi";
    case PT_MESSAGGIO: return "messaggio";
    case PT_BIANCA:    return "bianca";
    case PT_IMMAGINE:  return "immagine";
    case PT_GRAFICO:   return "grafico";
    case PT_DETTAGLIO: return "dettaglio";
    default:           return "?";
  }
}

// Elenco di partenza: la pagina dei nodi attiva, le altre presenti ma
// escluse dalla rotazione. E' la scelta del piano — una pagina primaria
// quasi sempre a schermo, le altre come scelta deliberata.
static void defaults()
{
  memset(s_pag, 0, sizeof(s_pag));

  s_pag[0].tipo = PT_NODI;      s_pag[0].usato = true;
  s_pag[0].attiva = true;       s_pag[0].durata_s = 600;

  s_pag[1].tipo = PT_MESSAGGIO; s_pag[1].usato = true;
  s_pag[1].attiva = false;      s_pag[1].durata_s = 300;

  s_pag[2].tipo = PT_BIANCA;    s_pag[2].usato = true;
  s_pag[2].attiva = false;      s_pag[2].durata_s = 300;

  s_rotazione = false;
  s_silDa = 23;
  s_silA  = 7;
  s_fascia = false;
}

void pages_begin()
{
  defaults();

  s_prefs.begin("hubpag", true);   // sola lettura: non crea il namespace
  s_silPag = s_prefs.getUChar("silpag", PAG_SIL_NESSUNA);
  if (s_silPag != PAG_SIL_NESSUNA && s_silPag != PAG_SIL_CASUALE && s_silPag >= PAGES_MAX)
    s_silPag = PAG_SIL_NESSUNA;
  const size_t quanti = s_prefs.getBytesLength("cfg");

  bool migrato = false;
  if (quanti == sizeof(PagBlob))
  {
    PagBlob b;
    if (s_prefs.getBytes("cfg", &b, sizeof(b)) == sizeof(b) && b.magic == PAG_MAGIC)
    {
      memcpy(s_pag, b.pag, sizeof(b.pag));
      s_rotazione = b.rotazione != 0;
      s_silDa     = b.silDa;
      s_silA      = b.silA;
      s_fascia    = b.fascia != 0;
    }
  }
  else if (quanti == sizeof(PagBlobV1))
  {
    PagBlobV1 v1;
    if (s_prefs.getBytes("cfg", &v1, sizeof(v1)) == sizeof(v1) && v1.magic == PAG_MAGIC_V1)
    {
      // Gli otto slot vecchi finiscono nei primi otto nuovi, nello stesso
      // ordine: gli indici sono quello che l'utente vede, e rimescolarli
      // qui sposterebbe le pagine sotto i suoi occhi.
      memcpy(s_pag, v1.pag, sizeof(v1.pag));
      s_rotazione = v1.rotazione != 0;
      s_silDa     = v1.silDa;
      s_silA      = v1.silA;
      s_fascia    = v1.fascia != 0;
      migrato     = true;
    }
  }
  s_prefs.end();

  // Lo slot 0 deve esistere sempre: e' la pagina per cui l'hub esiste, e
  // un elenco tutto vuoto lascerebbe il pannello senza niente da mostrare
  // e senza modo di rimediare dal tasto BOOT.
  if (!s_pag[0].usato)
  {
    s_pag[0].tipo = PT_NODI; s_pag[0].usato = true;
    s_pag[0].attiva = true;  s_pag[0].durata_s = 600;
  }

  s_cur      = 0;
  s_ultimoMs = millis();

  // Riscritto subito nel formato nuovo: cosi' la conversione avviene UNA
  // volta e non ad ogni avvio, e un eventuale ritorno indietro al firmware
  // vecchio si presenta come "elenco ai default" invece che come mezza
  // configurazione letta storta.
  if (migrato) pages_save();
}

void pages_save()
{
  PagBlob b;
  memset(&b, 0, sizeof(b));
  b.magic     = PAG_MAGIC;
  b.rotazione = s_rotazione ? 1 : 0;
  b.silDa     = s_silDa;
  b.silA      = s_silA;
  b.fascia    = s_fascia ? 1 : 0;
  memcpy(b.pag, s_pag, sizeof(s_pag));

  s_prefs.begin("hubpag", false);
  s_prefs.putBytes("cfg", &b, sizeof(b));
  s_prefs.end();
}

uint8_t pages_slots() { return PAGES_MAX; }

const PageCfg* pages_get(uint8_t i)
{
  if (i >= PAGES_MAX) return nullptr;
  return &s_pag[i];
}

uint8_t pages_current()
{
  // Se lo slot corrente e' stato svuotato nel frattempo (pagina immagine
  // cancellata mentre era a schermo), si torna alla prima usata invece di
  // restare a puntare il vuoto.
  if (!s_pag[s_cur].usato)
  {
    for (uint8_t i = 0; i < PAGES_MAX; i++)
      if (s_pag[i].usato) { s_cur = i; break; }
  }
  return s_cur;
}

bool pages_goto(uint8_t i)
{
  if (i >= PAGES_MAX || !s_pag[i].usato) return false;
  s_cur = i;
  return true;
}

uint8_t pages_manual_next()
{
  // A mano si vedono anche le pagine escluse dalla rotazione: il tasto
  // BOOT e' la via di governo quando la rete non c'e', e non deve
  // dipendere da come e' configurata la rotazione.
  for (uint8_t k = 1; k <= PAGES_MAX; k++)
  {
    const uint8_t i = (uint8_t)((s_cur + k) % PAGES_MAX);
    if (s_pag[i].usato) { s_cur = i; break; }
  }
  return s_cur;
}

bool pages_fascia()                { return s_fascia; }
void pages_set_fascia(bool on)     { s_fascia = on; }

bool pages_rotazione()             { return s_rotazione; }
void pages_set_rotazione(bool on)  { s_rotazione = on; }

uint8_t pages_silenzio_da() { return s_silDa; }
uint8_t pages_silenzio_a()  { return s_silA; }

void pages_set_silenzio(uint8_t da, uint8_t a)
{
  if (da > 23 || a > 23) return;
  s_silDa = da;
  s_silA  = a;
}

uint8_t pages_silenzio_pagina() { return s_silPag; }

void pages_set_silenzio_pagina(uint8_t slot)
{
  if (slot != PAG_SIL_NESSUNA && slot != PAG_SIL_CASUALE && slot >= PAGES_MAX) return;
  s_silPag = slot;
  s_prefs.begin("hubpag", false);
  s_prefs.putUChar("silpag", s_silPag);
  s_prefs.end();
}

bool pages_in_silenzio(time_t oraLocale)
{
  if (s_silDa == s_silA) return false;      // silenzio disabilitato

  struct tm tmv;
  localtime_r(&oraLocale, &tmv);
  const int h = tmv.tm_hour;

  // La finestra puo' scavalcare la mezzanotte (23 -> 7): sono due casi
  // diversi e vanno scritti tutti e due, o le notti non contano.
  if (s_silDa < s_silA) return (h >= s_silDa && h < s_silA);
  return (h >= s_silDa || h < s_silA);
}

void pages_disegnata(uint32_t nowMs) { s_ultimoMs = nowMs; }

int pages_tick(uint32_t nowMs, time_t oraLocale)
{
  if (!s_rotazione) return -1;
  if (pages_in_silenzio(oraLocale)) return -1;

  const PageCfg& cur = s_pag[pages_current()];
  const uint32_t durata = (uint32_t)(cur.durata_s ? cur.durata_s : 600) * 1000UL;
  if (nowMs - s_ultimoMs < durata) return -1;

  // Prossima pagina ATTIVA. Se non ce n'e' un'altra, non si tocca il
  // pannello: un refresh completo per tornare sulla stessa pagina sono
  // 2,2 s di lampeggio e usura per niente.
  for (uint8_t k = 1; k < PAGES_MAX; k++)
  {
    const uint8_t i = (uint8_t)((s_cur + k) % PAGES_MAX);
    if (s_pag[i].usato && s_pag[i].attiva) return (int)i;
  }
  return -1;
}

bool pages_set_attiva(uint8_t i, bool on)
{
  if (i >= PAGES_MAX || !s_pag[i].usato) return false;
  s_pag[i].attiva = on;
  return true;
}

bool pages_set_durata(uint8_t i, uint16_t durata_s)
{
  if (i >= PAGES_MAX || !s_pag[i].usato) return false;
  // Sotto il minuto non ha senso: ogni cambio pagina e' un refresh
  // completo da ~2,2 s che lampeggia. Il limite e' una misura, non un
  // gusto.
  if (durata_s < 60) durata_s = 60;
  s_pag[i].durata_s = durata_s;
  return true;
}

bool pages_fissa(uint8_t i)
{
  if (i >= PAGES_MAX || !s_pag[i].usato) return false;
  for (uint8_t k = 0; k < PAGES_MAX; k++) s_pag[k].attiva = (k == i);
  s_cur = i;
  return true;
}

int pages_add(uint8_t tipo, const char* param)
{
  if (tipo >= PT_COUNT) return -1;
  for (uint8_t i = 0; i < PAGES_MAX; i++)
  {
    if (s_pag[i].usato) continue;
    memset(&s_pag[i], 0, sizeof(s_pag[i]));
    s_pag[i].tipo     = tipo;
    s_pag[i].usato    = true;
    s_pag[i].attiva   = false;
    s_pag[i].durata_s = 300;
    if (param) strlcpy(s_pag[i].param, param, sizeof(s_pag[i].param));
    return (int)i;
  }
  return -1;
}

// Scambia con il vicino USATO nella direzione chiesta. Si salta sopra gli
// slot liberi, perche' l'utente vede un elenco compatto e non i buchi: se
// "su" facesse uno scambio con uno slot vuoto, da fuori sembrerebbe che il
// pulsante non abbia fatto niente.
bool pages_move(uint8_t i, int dir)
{
  if (i >= PAGES_MAX || !s_pag[i].usato || dir == 0) return false;

  const int passo = (dir < 0) ? -1 : 1;
  int j = (int)i + passo;
  while (j >= 0 && j < PAGES_MAX && !s_pag[j].usato) j += passo;
  if (j < 0 || j >= PAGES_MAX) return false;      // gia' in fondo o in cima

  // Lo slot 0 e' la pagina dei nodi e non si sposta: e' il posto fisso da
  // cui si riparte, e tenerlo li' rende prevedibile il tasto BOOT.
  if (i == 0 || j == 0) return false;

  const PageCfg tmp = s_pag[i];
  s_pag[i] = s_pag[(uint8_t)j];
  s_pag[(uint8_t)j] = tmp;

  // La pagina a schermo deve restare la stessa DOPO lo scambio, o riordinare
  // farebbe saltare il pannello a un'altra pagina senza che nessuno lo abbia
  // chiesto — e ogni cambio pagina costa un refresh completo da 2,2 s.
  if      (s_cur == i)             s_cur = (uint8_t)j;
  else if (s_cur == (uint8_t)j)    s_cur = i;
  return true;
}

bool pages_remove(uint8_t i)
{
  if (i >= PAGES_MAX || !s_pag[i].usato) return false;
  if (i == 0) return false;         // la pagina dei nodi non si toglie
  memset(&s_pag[i], 0, sizeof(s_pag[i]));
  if (s_cur == i) pages_current();  // rimette s_cur su uno slot valido
  return true;
}
