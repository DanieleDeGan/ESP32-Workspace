// ===========================================================================
//  meteo_calc.h — quello che si ricava da temperatura e umidita', e basta
// ===========================================================================
//
// Header-only e puro, come forecast.h: niente stato, niente hardware, niente
// Arduino. Si prova a mente e non puo' rompersi in modo silenzioso.
//
// PERCHE' STA SULL'HUB E NON SUL NODO. Tutte queste grandezze si ricavano da
// T e RH, che il nodo trasmette gia': calcolarle a bordo del nodo vorrebbe
// dire spedire numeri derivati al posto delle misure, e un errore di formula
// finirebbe dentro lo storico su card per sempre. Qui invece si ricalcolano
// ad ogni disegno, e il giorno che una formula cambia bastano i CSV di ieri
// per rifare i conti. E' la stessa ragione per cui la pressione viaggia
// GREZZA e la correzione al livello del mare la applica l'hub.
//
// Tutte tornano NAN se gli ingressi non hanno senso: un valore non finito si
// disegna come "--", che e' onesto, mentre uno zero sarebbe una misura che
// nessuno ha fatto.
#pragma once

#include <math.h>

// --- punto di rugiada (Magnus-Tetens) --------------------------------------
// La temperatura a cui quell'aria satura: sotto quella si forma condensa. E'
// il numero che dice se i vetri si appanneranno stanotte, e d'estate quanto
// e' afosa l'aria molto meglio dell'umidita' relativa -- 60% a 18 gradi e 60%
// a 30 sono due mondi diversi, ma la rugiada li distingue subito.
//
// Coefficienti di Sonntag (1990), validi fra -45 e +60 gradi: b = 17.62,
// c = 243.12 C. Errore tipico sotto 0,1 gradi, cioe' molto meglio di quanto
// misuri un AHT20.
static inline float meteo_dewpoint_c(float tempC, float rhPct)
{
  if (!isfinite(tempC) || !isfinite(rhPct)) return NAN;
  if (rhPct <= 0.0f || rhPct > 100.0f)      return NAN;
  const float b = 17.62f, c = 243.12f;
  const float g = logf(rhPct / 100.0f) + (b * tempC) / (c + tempC);
  return (c * g) / (b - g);
}

// --- pressione di vapore saturo, in hPa -------------------------------------
// Serve alle due funzioni qui sotto; sta fuori perche' e' la stessa curva.
static inline float meteo_e_saturo_hpa(float tempC)
{
  if (!isfinite(tempC)) return NAN;
  return 6.112f * expf((17.67f * tempC) / (tempC + 243.5f));
}

// --- umidita' assoluta, in g/m^3 --------------------------------------------
// Quanti grammi d'acqua ci sono davvero in un metro cubo d'aria. A differenza
// dell'umidita' relativa non dipende dalla temperatura, quindi e' l'unico modo
// onesto di confrontare due stanze diverse -- o l'interno con l'esterno.
static inline float meteo_umidita_assoluta_gm3(float tempC, float rhPct)
{
  if (!isfinite(tempC) || !isfinite(rhPct)) return NAN;
  if (rhPct < 0.0f || rhPct > 100.0f)       return NAN;
  return meteo_e_saturo_hpa(tempC) * rhPct * 2.1674f / (273.15f + tempC);
}

// --- humidex ----------------------------------------------------------------
// La temperatura percepita quando fa caldo e umido (Masterton & Richardson,
// servizio meteo canadese). Si e' scelto questo e non il wind chill perche'
// quello vuole la velocita' del vento, che questa stazione non misura: meglio
// un indice giusto per meta' anno che uno inventato per tutto l'anno.
//
// TORNA NAN SOTTO I 20 GRADI, e non e' una svista: l'humidex li' non e'
// definito, e mostrarlo lo stesso darebbe un numero dall'aria autorevole che
// non vuol dire niente. Chi disegna salta la voce e usa lo spazio per altro.
static inline float meteo_humidex_c(float tempC, float rhPct)
{
  if (!isfinite(tempC) || !isfinite(rhPct)) return NAN;
  if (tempC < 20.0f)                        return NAN;
  const float td = meteo_dewpoint_c(tempC, rhPct);
  if (!isfinite(td)) return NAN;
  // Pressione di vapore alla rugiada, secondo la formula canonica.
  const float e = 6.11f * expf(5417.7530f * (1.0f / 273.16f - 1.0f / (273.15f + td)));
  return tempC + 0.5555f * (e - 10.0f);
}
