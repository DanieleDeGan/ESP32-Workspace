/*
 * messages.cpp - messaggio attivo in NVS, archivio su microSD.
 * ---------------------------------------------------------------------------
 * La scrittura in NVS avviene solo quando l'utente manda un messaggio (o lo
 * cancella): sono eventi rari, non un contatore, quindi qui i cicli di erase
 * non sono un problema come lo erano in sd_logger.
 *
 * L'archivio su card e' un CSV con il testo come ULTIMO campo: cosi' le
 * virgole dentro al messaggio non hanno bisogno di quoting, che su un parser
 * scritto a mano sarebbe la prima cosa a rompersi. Gli a-capo invece si
 * sostituiscono con uno spazio in scrittura: quelli spezzerebbero la riga, e
 * un archivio con righe fantasma non si rilegge piu'.
 */

#include "messages.h"
#include "sd_logger.h"

#include <Preferences.h>
#include <SD.h>

static Preferences s_prefs;
static Message     s_msg;
static bool        s_has     = false;
static bool        s_urgente = false;   // consumato da msg_take_urgent()
static bool        s_dirty   = false;
static bool        s_eraVivo = false;   // per accorgersi della scadenza

void msg_begin()
{
  memset(&s_msg, 0, sizeof(s_msg));
  s_has = false;

  s_prefs.begin("hubmsg", true);
  const size_t letti = s_prefs.getBytes("attivo", &s_msg, sizeof(s_msg));
  s_prefs.end();

  if (letti == sizeof(s_msg) && s_msg.testo[0] != '\0')
  {
    s_msg.testo[MSG_TESTO_MAX] = '\0';   // difesa: il blob viene dalla flash
    s_has = true;
  }
  else
  {
    memset(&s_msg, 0, sizeof(s_msg));
  }
  s_dirty   = true;
  s_eraVivo = s_has;
}

const Message* msg_active(time_t adesso)
{
  if (!s_has) return nullptr;

  // La scadenza si valuta qui e basta: nessun timer, nessuno stato da
  // tenere allineato. Un messaggio scaduto sparisce dal pannello ma resta
  // nell'archivio sulla card.
  if (s_msg.scadenza != 0 && adesso != 0 && adesso >= s_msg.scadenza) return nullptr;
  return &s_msg;
}

static void archiviaSuSD(const Message& m)
{
  if (!sd_mounted()) return;

  if (!SD.exists(MSG_DIR)) SD.mkdir(MSG_DIR);

  const bool nuovo = !SD.exists(MSG_ARCHIVIO);
  File f = SD.open(MSG_ARCHIVIO, FILE_APPEND);
  if (!f) return;

  if (nuovo) f.println("creato_iso,creato_unix,scadenza_unix,priorita,testo");

  char iso[24] = "";
  if (m.creato != 0)
  {
    struct tm tmv;
    localtime_r(&m.creato, &tmv);
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", &tmv);
  }

  f.printf("%s,%lld,%lld,%u,", iso, (long long)m.creato,
           (long long)m.scadenza, (unsigned)m.priorita);

  // Il testo per ultimo, con gli a-capo appiattiti: vedi la nota in testa.
  for (const char* p = m.testo; *p; ++p)
    f.write((*p == '\n' || *p == '\r') ? ' ' : *p);
  f.println();
  f.close();
}

bool msg_set(const char* testo, time_t creato, time_t scadenza, uint8_t priorita)
{
  if (testo == nullptr || testo[0] == '\0') { msg_clear(); return true; }
  if (strlen(testo) > MSG_TESTO_MAX) return false;

  memset(&s_msg, 0, sizeof(s_msg));
  strlcpy(s_msg.testo, testo, sizeof(s_msg.testo));
  s_msg.creato   = creato;
  s_msg.scadenza = scadenza;
  s_msg.priorita = priorita;
  s_has = true;

  s_prefs.begin("hubmsg", false);
  s_prefs.putBytes("attivo", &s_msg, sizeof(s_msg));
  s_prefs.end();

  archiviaSuSD(s_msg);

  s_dirty   = true;
  s_eraVivo = true;
  if (priorita == MSG_URGENTE) s_urgente = true;
  return true;
}

void msg_clear()
{
  memset(&s_msg, 0, sizeof(s_msg));
  s_has = false;

  s_prefs.begin("hubmsg", false);
  s_prefs.remove("attivo");
  s_prefs.end();

  s_dirty   = true;
  s_eraVivo = false;
}

bool msg_take_urgent()
{
  const bool v = s_urgente;
  s_urgente = false;
  return v;
}

bool msg_take_dirty(time_t adesso)
{
  // Un messaggio che scade e' un cambiamento come un altro: senza questo
  // controllo il pannello resterebbe a mostrarlo fino al refresh di
  // cadenza successivo, cioe' fino a mezz'ora dopo la sua scadenza.
  const bool vivo = (msg_active(adesso) != nullptr);
  if (vivo != s_eraVivo) { s_eraVivo = vivo; s_dirty = true; }

  const bool v = s_dirty;
  s_dirty = false;
  return v;
}

// ---------------------------------------------------------------------
//  Archivio: si legge in avanti tenendo in RAM solo le ultime maxItems
//  righe (anello), poi si consegnano dalla piu' recente. Leggere un file
//  al contrario su SD costerebbe una seek per riga; l'archivio e' piccolo
//  e questo e' un giro solo.
// ---------------------------------------------------------------------
#define MSG_LIST_MAX 10

int msg_archive_list(msg_row_cb_t cb, void* arg, int maxItems)
{
  if (cb == nullptr || !sd_mounted()) return 0;
  if (maxItems <= 0) return 0;
  if (maxItems > MSG_LIST_MAX) maxItems = MSG_LIST_MAX;

  File f = SD.open(MSG_ARCHIVIO, FILE_READ);
  if (!f) return 0;

  static Message ring[MSG_LIST_MAX];
  int scritti = 0, totali = 0;

  char riga[320];
  while (f.available())
  {
    const size_t n = f.readBytesUntil('\n', riga, sizeof(riga) - 1);
    riga[n] = '\0';
    if (n == 0) continue;
    if (strncmp(riga, "creato_iso", 10) == 0) continue;   // intestazione

    // creato_iso,creato_unix,scadenza_unix,priorita,testo
    char* p1 = strchr(riga, ',');          if (!p1) continue;
    char* p2 = strchr(p1 + 1, ',');        if (!p2) continue;
    char* p3 = strchr(p2 + 1, ',');        if (!p3) continue;
    char* p4 = strchr(p3 + 1, ',');        if (!p4) continue;

    Message m;
    memset(&m, 0, sizeof(m));
    m.creato   = (time_t)atoll(p1 + 1);
    m.scadenza = (time_t)atoll(p2 + 1);
    m.priorita = (uint8_t)atoi(p3 + 1);
    strlcpy(m.testo, p4 + 1, sizeof(m.testo));

    // L'ultimo carattere puo' essere un '\r' (file scritto con CRLF).
    const size_t len = strlen(m.testo);
    if (len && m.testo[len - 1] == '\r') m.testo[len - 1] = '\0';

    ring[totali % maxItems] = m;
    totali++;
  }
  f.close();

  const int quanti = (totali < maxItems) ? totali : maxItems;
  for (int k = 1; k <= quanti; k++)
  {
    const int idx = (totali - k) % maxItems;
    cb(ring[idx], arg);
    scritti++;
  }
  return scritti;
}
