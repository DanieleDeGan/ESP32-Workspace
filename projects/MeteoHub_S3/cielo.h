#pragma once

// ---------------------------------------------------------------------
//  cielo — la previsione VERA, quella che il cielo lo guarda davvero.
//
//  PERCHE' ESISTE, ed e' il punto da tenere fermo: questa stazione misura
//  tre numeri e non ha NIENTE che guardi il cielo. La previsione di
//  forecast.h e' il trend del barometro, e misurata l'8-9 settembre 2026 su
//  819 casi non batte la persistenza: azzecca il segno di come si muove la
//  pressione, non che tempo fara'. Disegnare un sole o una pioggia partendo
//  da li' sarebbe un'affermazione che i dati non reggono, e sbagliata in
//  modo VISIBILE — il tipo di errore da cui un pannello non si riprende.
//
//  Quindi l'icona la porta chi la sa: Open-Meteo, in HTTP semplice (niente
//  TLS, niente chiave, ~1,5 kB a richiesta, 172 ms misurati). Il barometro
//  di casa resta dov'e' — la freccia del trend — e le due cose stanno una
//  accanto all'altra dicendo cose diverse: che tempo fara', e come si muove
//  la pressione QUI.
//
//  E siccome adesso ci sono due previsioni, si puo' finalmente rispondere a
//  «la mia regola empirica vale qualcosa?». E' la voce 7 del backlog, ed e'
//  la ragione per cui questo file tiene anche il codice WMO grezzo e non
//  solo la classe: un archivio non deve perdere dati che non si possono
//  piu' andare a riprendere.
//
//  DIPENDENZA ESTERNA, con le regole del progetto:
//   - se il servizio non risponde si dice NON RAGGIUNGIBILE. Mai mostrare
//     l'icona di ieri come se fosse di adesso: un dato vecchio spacciato per
//     fresco e' peggio di nessun dato;
//   - tutto il resto del pannello e del web funziona identico a internet giu';
//   - la posizione si manda con DUE decimali (~1 km): al meteo non serve
//     sapere in che casa si sta, e la richiesta viaggia in chiaro.
// ---------------------------------------------------------------------

#include <Arduino.h>
#include <time.h>

// Le classi che il pannello sa disegnare. Sono SETTE e non i 28 codici WMO:
// a 1 bit e 32 px si vede la silhouette, e "pioviggine leggera" contro
// "pioggia moderata" sarebbe la stessa macchia nera. E' la lezione delle due
// icone gia' scartate in tools/icone.py (il barometro e la goccia vuota).
//
// Sul WEB invece il codice grezzo c'e' tutto: li' lo spazio non manca, e
// ridurre e' una scelta del pannello, non del dato.
enum CieloClasse : uint8_t {
  CIELO_IGNOTO = 0,     // nessun dato fresco: si dice, non si indovina
  CIELO_SERENO,
  CIELO_POCO_NUVOLOSO,
  CIELO_COPERTO,
  CIELO_NEBBIA,
  CIELO_PIOGGIA,
  CIELO_NEVE,
  CIELO_TEMPORALE
};

// Quante ore avanti guarda l'icona "a breve". Tre: e' la stessa finestra del
// trend barometrico, cosi' le due previsioni parlano dello STESSO futuro e
// il confronto fra loro ha senso.
static const int CIELO_ORE_BREVI = 3;

// Quante ne tiene in RAM per il web: dodici, cioe' la mezza giornata che sta
// in una striscia leggibile. 12 x 8 byte = 96 byte.
static const int CIELO_ORE_MAX   = 12;
static const int CIELO_GIORNI_MAX = 3;

struct CieloOra {
  int16_t wmo;
  float   tempC;
  int8_t  pioggiaPct;      // -1 = non data
};

struct CieloGiorno {
  int16_t wmo;
  float   tMinC, tMaxC;
  int8_t  pioggiaPct;
};

struct CieloStato {
  bool     valido;         // c'e' un dato FRESCO (vedi la scadenza nel .cpp)
  time_t   quando;         // quando e' arrivata la risposta

  uint8_t  adesso;         // CieloClasse in questo momento
  uint8_t  breve;          // CieloClasse PEGGIORE nelle prossime 3 h
  int16_t  wmoAdesso;      // i codici grezzi: la classe e' una riduzione, e
  int16_t  wmoBreve;       // il CSV deve poter essere riletto diversamente

  float    tempC;          // le condizioni di adesso, secondo il servizio
  float    percepitaC;
  float    pioggiaMm;
  float    ventoKmh;
  float    raficheKmh;
  int8_t   umiditaPct;

  uint8_t     nOre;
  CieloOra    ore[CIELO_ORE_MAX];
  uint8_t     nGiorni;
  CieloGiorno giorni[CIELO_GIORNI_MAX];

  uint16_t prese;          // richieste riuscite da quando la scheda e' su
  uint16_t fallite;
  uint32_t ultimaMs;       // millis() dell'ultimo tentativo, 0 = mai
  uint16_t ultimaDurataMs; // quanto e' costata al loop(): si misura, non si stima
  uint16_t ultimiByte;
  char     errore[48];     // vuoto se l'ultima e' andata bene
};

void  cielo_begin();
void  cielo_loop();                     // fa la richiesta quando e' ora
bool  cielo_get(CieloStato* out);       // sempre vero: si guarda `valido`
void  cielo_chiedi_ora();               // forza un tentativo (prova da web)

bool  cielo_posizione_valida();
float cielo_lat();
float cielo_lon();
bool  cielo_set_posizione(float lat, float lon);   // false = fuori range

// Il nome della classe, per il web e per il CSV. Mai per il pannello: li'
// c'e' l'icona, ed e' tutto il punto.
const char* cielo_classe_nome(uint8_t classe);

// Il codice WMO ridotto a una delle sette classi. Esposta perche' e' anche la
// tabella che serve a chi rilegge un CSV vecchio per rifare la riduzione.
uint8_t cielo_classe_da_wmo(int wmo);
