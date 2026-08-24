#pragma once
#include <Arduino.h>
#include <math.h>

// =====================================================================
//  forecast — pressione al livello del mare e previsione dal trend
//
//  Header-only e PURO, sullo stampo di comfort.h di EnvNode_C3: nessuno
//  stato globale, nessun accesso all'hardware, niente Serial. Tutto quello
//  che serve arriva come parametro, cosi' resta leggibile e si puo'
//  cambiare una soglia senza rischiare effetti altrove.
//
//  L'IDEA: un barometro casalingo non prevede il tempo dal valore assoluto
//  della pressione ma da COME STA CAMBIANDO. La finestra classica e' tre
//  ore - abbastanza lunga da ignorare il rumore, abbastanza corta da
//  accorgersi di un fronte in arrivo prima che arrivi. E' esattamente
//  quello che fa la lancetta di riferimento di un barometro analogico, che
//  si sposta a mano per segnare "dov'era tre ore fa".
// =====================================================================

// ---------------------------------------------------------------------
//  Pressione al livello del mare
// ---------------------------------------------------------------------
// Il BMP280 misura la pressione DOVE SI TROVA. Salendo cala di circa
// 1 hPa ogni 8 metri, quindi il numero grezzo non e' confrontabile con
// quello dei bollettini, che sono sempre riportati al livello del mare.
// Senza correzione, una casa a 40 m sembrerebbe avere una depressione
// permanente di 5 hPa.
//
// Formula dell'atmosfera standard, la stessa che usa
// Adafruit_BMP280::readAltitude(): approssima la temperatura della colonna
// d'aria invece di misurarla, il che vale qualche decimo di hPa - del
// tutto irrilevante quando quello che conta e' la VARIAZIONE.
static inline float forecast_sea_level_hpa(float stationHpa, float altitudeM) {
  if (isnan(stationHpa)) return NAN;
  return stationHpa / powf(1.0f - (altitudeM / 44330.0f), 5.255f);
}

// L'inversa: nota la pressione misurata e quella al livello del mare letta
// da un bollettino, ricava l'altitudine. Serve alla calibrazione dalla
// pagina web, cosi' l'altitudine non va indovinata ne' cercata su una
// mappa: si copia un numero da un bollettino locale e il conto lo fa il
// firmware.
static inline float forecast_altitude_from_sea_level(float stationHpa, float seaHpa) {
  if (isnan(stationHpa) || isnan(seaHpa) || stationHpa <= 0.0f || seaHpa <= 0.0f) return NAN;
  return 44330.0f * (1.0f - powf(stationHpa / seaHpa, 1.0f / 5.255f));
}

// ---------------------------------------------------------------------
//  Trend a tre ore
// ---------------------------------------------------------------------
// Soglie in hPa su tre ore, quelle convenzionali della meteorologia
// marittima. Sono asimmetriche nell'uso, non nel valore: una discesa
// rapida e' una brutta notizia molto piu' di quanto una salita rapida sia
// una bella notizia.
enum forecast_trend_t : uint8_t {
  TREND_IGNOTO = 0,        // meno di tre ore di storico: non si inventa
  TREND_CROLLO,            // < -6.0   burrasca in arrivo
  TREND_DISCESA_RAPIDA,    // < -3.5
  TREND_DISCESA,           // < -1.6
  TREND_DISCESA_LENTA,     // < -0.5
  TREND_STABILE,           // fra -0.5 e +0.5
  TREND_SALITA_LENTA,      // > +0.5
  TREND_SALITA,            // > +1.6
  TREND_SALITA_RAPIDA,     // > +3.5
  TREND_SALITA_FORTE       // > +6.0
};

static inline forecast_trend_t forecast_classify(float deltaHpa3h) {
  if (isnan(deltaHpa3h)) return TREND_IGNOTO;
  if (deltaHpa3h <= -6.0f) return TREND_CROLLO;
  if (deltaHpa3h <= -3.5f) return TREND_DISCESA_RAPIDA;
  if (deltaHpa3h <= -1.6f) return TREND_DISCESA;
  if (deltaHpa3h <= -0.5f) return TREND_DISCESA_LENTA;
  if (deltaHpa3h <   0.5f) return TREND_STABILE;
  if (deltaHpa3h <   1.6f) return TREND_SALITA_LENTA;
  if (deltaHpa3h <   3.5f) return TREND_SALITA;
  if (deltaHpa3h <   6.0f) return TREND_SALITA_RAPIDA;
  return TREND_SALITA_FORTE;
}

// Variante con ISTERESI, da usare quando il risultato finisce sotto gli
// occhi di qualcuno. Il delta a tre ore e' gia' liscio di suo, ma se resta
// fermo esattamente su una soglia l'etichetta sfarfalla fra due valori a
// ogni campione, e una previsione che cambia idea ogni due minuti non la
// crede piu' nessuno. Qui si cambia classe solo se si supera la soglia di
// un margine: il modulo resta puro perche' lo stato - la classe di prima -
// lo tiene il chiamante e lo passa come parametro.
static inline forecast_trend_t forecast_classify_hyst(float deltaHpa3h,
                                                      forecast_trend_t precedente,
                                                      float margine = 0.15f) {
  const forecast_trend_t nuova = forecast_classify(deltaHpa3h);
  if (precedente == TREND_IGNOTO || nuova == TREND_IGNOTO) return nuova;
  if (nuova == precedente) return precedente;

  // Di quanto siamo entrati nella classe nuova? Se e' un'inezia, si resta
  // dov'eravamo. Il confronto si fa sulla soglia attraversata, che e'
  // quella fra le due classi.
  static const float soglie[] = { -6.0f, -3.5f, -1.6f, -0.5f, 0.5f, 1.6f, 3.5f, 6.0f };
  const int idx = (nuova > precedente) ? (int)precedente - 1 : (int)nuova - 1;
  if (idx < 0 || idx >= (int)(sizeof(soglie) / sizeof(soglie[0]))) return nuova;
  return (fabsf(deltaHpa3h - soglie[idx]) < margine) ? precedente : nuova;
}

static inline const char* forecast_trend_label(forecast_trend_t t) {
  switch (t) {
    case TREND_CROLLO:          return "in crollo";
    case TREND_DISCESA_RAPIDA:  return "in rapida discesa";
    case TREND_DISCESA:         return "in discesa";
    case TREND_DISCESA_LENTA:   return "in lieve discesa";
    case TREND_STABILE:         return "stabile";
    case TREND_SALITA_LENTA:    return "in lieve salita";
    case TREND_SALITA:          return "in salita";
    case TREND_SALITA_RAPIDA:   return "in rapida salita";
    case TREND_SALITA_FORTE:    return "in forte salita";
    default:                    return "non ancora noto";
  }
}

// ---------------------------------------------------------------------
//  La previsione vera e propria
// ---------------------------------------------------------------------
// Trend E valore assoluto insieme: la stessa discesa significa cose
// diverse se parte da 1025 hPa (bel tempo che si guasta) o da 995 hPa
// (brutto che peggiora ancora). Il valore assoluto va passato GIA'
// riportato al livello del mare, o le soglie sotto non hanno senso.
//
// Da leggere per quello che e': una regola empirica da barometro di casa,
// non un modello meteo. Indovina la tendenza delle prossime ore, non che
// tempo fara' domani.
static inline const char* forecast_text(forecast_trend_t t, float seaHpa) {
  if (t == TREND_IGNOTO) return "raccolgo dati: servono tre ore di storico";

  const bool alta  = !isnan(seaHpa) && seaHpa >= 1020.0f;
  const bool bassa = !isnan(seaHpa) && seaHpa <= 1000.0f;

  switch (t) {
    case TREND_CROLLO:
      return "peggioramento deciso, possibile vento forte";
    case TREND_DISCESA_RAPIDA:
      return "peggioramento in arrivo entro poche ore";
    case TREND_DISCESA:
      return alta ? "bel tempo che si guasta lentamente"
                  : "tendenza al peggioramento";
    case TREND_DISCESA_LENTA:
      return bassa ? "resta variabile" : "lieve tendenza al peggioramento";
    case TREND_STABILE:
      if (alta)  return "bel tempo stabile";
      if (bassa) return "tempo perturbato che non si sblocca";
      return "situazione stabile, nessun cambiamento in vista";
    case TREND_SALITA_LENTA:
      return alta ? "bel tempo confermato" : "lento miglioramento";
    case TREND_SALITA:
      return "miglioramento in corso";
    case TREND_SALITA_RAPIDA:
    case TREND_SALITA_FORTE:
      return "schiarite rapide, possibile vento nel passaggio";
    default:
      return "non ancora noto";
  }
}
