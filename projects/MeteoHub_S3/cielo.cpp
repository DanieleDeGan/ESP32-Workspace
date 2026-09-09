#include "cielo.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

#include "rtc_time.h"

// ---------------------------------------------------------------------
//  Quando si chiede, e per quanto tempo la risposta vale
// ---------------------------------------------------------------------
// Mezz'ora fra una richiesta e l'altra: una previsione oraria non cambia piu'
// in fretta di cosi', e 48 richieste al giorno a un servizio gratuito sono
// ospitalita', non abuso.
static const uint32_t CIELO_PERIODO_MS = 30UL * 60UL * 1000UL;
// Dopo un errore si riprova prima, ma non subito: un AP che se n'e' andato
// torna in minuti, e martellarlo ad ogni giro bloccherebbe il loop() ad ogni
// giro.
static const uint32_t CIELO_RIPROVA_MS = 5UL * 60UL * 1000UL;
// Oltre questo il dato NON e' piu' fresco, l'icona sparisce e il web lo dice.
// Novanta minuti sono tre periodi falliti di fila: sotto, un buco di rete
// svuoterebbe il pannello per niente; sopra, si mostrerebbe il cielo di
// un'ora e mezza fa come se fosse quello di adesso.
static const uint32_t CIELO_SCADE_S = 90UL * 60UL;

// Il tetto sul tempo di risposta e' la cosa piu' delicata di questo file. Il
// loop() e' sincrono: mentre si aspetta il server, i DATA dei nodi non
// vengono prelevati dal driver ESP-NOW, che ne tiene UNO SOLO (e' la trappola
// di streamFile, in docs/Trappole-Hardware.md, con un'altra faccia). Con i
// nodi a 300 s il rischio e' piccolo, ma il modo di tenerlo tale e' un tetto
// stretto — e la durata si MISURA (ultimaDurataMs, piu' la fase "cielo" nel
// loop), cosi' un servizio diventato lento si vede invece di nascondersi.
static const uint16_t CIELO_TIMEOUT_MS = 2500;

static const char* CIELO_NS = "cielo";

static CieloStato s_st;
static float      s_lat = NAN, s_lon = NAN;
static uint32_t   s_prossimaMs = 0;
static bool       s_subito     = true;   // il primo giro utile, non fra mezz'ora

// ---------------------------------------------------------------------
//  WMO -> le sette classi
// ---------------------------------------------------------------------
uint8_t cielo_classe_da_wmo(int wmo) {
  if (wmo < 0) return CIELO_IGNOTO;
  if (wmo == 0) return CIELO_SERENO;
  if (wmo == 1 || wmo == 2) return CIELO_POCO_NUVOLOSO;
  if (wmo == 3) return CIELO_COPERTO;
  if (wmo == 45 || wmo == 48) return CIELO_NEBBIA;
  if (wmo >= 95) return CIELO_TEMPORALE;                                  // 95,96,99
  if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) return CIELO_NEVE;
  if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 82)) return CIELO_PIOGGIA;
  return CIELO_COPERTO;   // un codice che non conosciamo non diventa un sole
}

const char* cielo_classe_nome(uint8_t c) {
  switch (c) {
    case CIELO_SERENO:        return "sereno";
    case CIELO_POCO_NUVOLOSO: return "poco nuvoloso";
    case CIELO_COPERTO:       return "coperto";
    case CIELO_NEBBIA:        return "nebbia";
    case CIELO_PIOGGIA:       return "pioggia";
    case CIELO_NEVE:          return "neve";
    case CIELO_TEMPORALE:     return "temporale";
    default:                  return "ignoto";
  }
}

// Quanto "conta" una classe quando se ne deve scegliere UNA per tre ore. Non
// e' l'ordine dell'enum: e' quanto la cosa cambia i piani di chi guarda il
// pannello. Il temporale batte tutto, il sereno perde con tutto.
static uint8_t gravita(uint8_t classe) {
  switch (classe) {
    case CIELO_TEMPORALE:     return 6;
    case CIELO_NEVE:          return 5;
    case CIELO_PIOGGIA:       return 4;
    case CIELO_NEBBIA:        return 3;
    case CIELO_COPERTO:       return 2;
    case CIELO_POCO_NUVOLOSO: return 1;
    default:                  return 0;
  }
}

// ---------------------------------------------------------------------
//  La posizione
// ---------------------------------------------------------------------
bool  cielo_posizione_valida() { return isfinite(s_lat) && isfinite(s_lon); }
float cielo_lat() { return s_lat; }
float cielo_lon() { return s_lon; }

bool cielo_set_posizione(float lat, float lon) {
  if (!isfinite(lat) || !isfinite(lon)) return false;
  if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) return false;

  s_lat = lat; s_lon = lon;
  Preferences p;
  if (p.begin(CIELO_NS, false)) {
    p.putFloat("lat", lat);
    p.putFloat("lon", lon);
    p.end();
  }
  // La posizione nuova vale SUBITO: far aspettare mezz'ora per sapere se e'
  // quella giusta renderebbe la pagina delle impostazioni una scommessa.
  cielo_chiedi_ora();
  return true;
}

void cielo_chiedi_ora() { s_subito = true; }

// ---------------------------------------------------------------------
//  Il pezzetto di JSON che serve
// ---------------------------------------------------------------------
// Niente libreria: la risposta e' ~1,5 kB e i campi utili sono una dozzina,
// quindi un parser JSON completo qui sarebbe piu' codice di tutto il resto.
// Si cerca per POSIZIONE dentro l'oggetto giusto e mai la prima occorrenza
// nel documento: "weather_code" compare anche dentro `current_units`, e
// prenderla da li' leggerebbe la stringa "wmo code" come uno zero, cioe' un
// sereno perfettamente credibile.
//
// `"hourly":` non trova `"hourly_units":` (la virgoletta di chiusura e i due
// punti fanno parte dell'ago): e' il motivo per cui questi cinque ago sono
// scritti con le virgolette dentro.
static int trovaOggetto(const String& s, const char* oggetto) {
  return s.indexOf(oggetto);
}

static int jsonInt(const String& s, const char* oggetto, const char* campo, int def) {
  const int o = trovaOggetto(s, oggetto);
  if (o < 0) return def;
  const int c = s.indexOf(campo, o);
  if (c < 0) return def;
  return s.substring(c + strlen(campo), c + strlen(campo) + 12).toInt();
}

static float jsonFloat(const String& s, const char* oggetto, const char* campo) {
  const int o = trovaOggetto(s, oggetto);
  if (o < 0) return NAN;
  const int c = s.indexOf(campo, o);
  if (c < 0) return NAN;
  const String pezzo = s.substring(c + strlen(campo), c + strlen(campo) + 12);
  if (pezzo.startsWith("null")) return NAN;
  return pezzo.toFloat();
}

// I primi `quanti` valori di un array "campo":[a,b,c,...] dentro `oggetto`.
// Torna quanti ne ha letti. `null` diventa NAN, non zero: in mezzo a una
// serie di temperature uno zero e' un valore, e a novembre pure plausibile.
static int jsonArray(const String& s, const char* oggetto, const char* campo,
                     float* out, int quanti) {
  const int o = trovaOggetto(s, oggetto);
  if (o < 0) return 0;
  int i = s.indexOf(campo, o);
  if (i < 0) return 0;
  i += strlen(campo);

  int n = 0;
  while (n < quanti && i < (int)s.length()) {
    while (i < (int)s.length() && (s[i] == ' ' || s[i] == ',')) i++;
    if (i >= (int)s.length() || s[i] == ']') break;
    if (s.startsWith("null", i)) out[n++] = NAN;
    else                        out[n++] = s.substring(i, i + 12).toFloat();
    while (i < (int)s.length() && s[i] != ',' && s[i] != ']') i++;
  }
  return n;
}

// ---------------------------------------------------------------------
//  La richiesta
// ---------------------------------------------------------------------
static void erroreCon(const char* testo) {
  snprintf(s_st.errore, sizeof(s_st.errore), "%s", testo);
  s_st.fallite++;
  // `valido` NON si spegne qui: un dato di dieci minuti fa resta buono anche
  // se l'ultimo tentativo e' andato storto. A spegnerlo e' la scadenza, che
  // e' l'unica cosa capace di distinguere "vecchio" da "andato male una
  // volta".
}

static void aggiorna() {
  const uint32_t t0 = millis();
  s_st.ultimaMs = t0 ? t0 : 1;

  if (!cielo_posizione_valida()) { erroreCon("posizione non impostata"); return; }
  if (WiFi.status() != WL_CONNECTED) { erroreCon("WiFi assente"); return; }

  // Due decimali: ~1 km. Al meteo basta, e la richiesta viaggia in chiaro.
  // timezone=UTC di proposito: le ore tornano in UTC e restano confrontabili
  // con tutto il resto della scheda senza passare dal fuso, che su questo
  // progetto ha gia' fatto danni una volta (v65).
  char url[420];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast"
           "?latitude=%.2f&longitude=%.2f"
           "&current=weather_code,temperature_2m,apparent_temperature,"
           "relative_humidity_2m,precipitation,wind_speed_10m,wind_gusts_10m"
           "&hourly=weather_code,temperature_2m,precipitation_probability"
           "&forecast_hours=%d"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
           "precipitation_probability_max&forecast_days=%d"
           "&timezone=UTC",
           s_lat, s_lon, CIELO_ORE_MAX, CIELO_GIORNI_MAX);

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(CIELO_TIMEOUT_MS);
  http.setTimeout(CIELO_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, url)) { erroreCon("URL rifiutato"); return; }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    char buf[48];
    snprintf(buf, sizeof(buf), "HTTP %d", code);
    erroreCon(buf);
    http.end();
    return;
  }

  const String body = http.getString();
  http.end();
  s_st.ultimiByte = (uint16_t)body.length();

  const int wmoOra = jsonInt(body, "\"current\":", "\"weather_code\":", -1);
  if (wmoOra < 0) { erroreCon("risposta senza weather_code"); return; }

  // --- le prossime ore --------------------------------------------------
  float wmo[CIELO_ORE_MAX], tmp[CIELO_ORE_MAX], pio[CIELO_ORE_MAX];
  const int nW = jsonArray(body, "\"hourly\":", "\"weather_code\":[", wmo, CIELO_ORE_MAX);
  const int nT = jsonArray(body, "\"hourly\":", "\"temperature_2m\":[", tmp, CIELO_ORE_MAX);
  const int nP = jsonArray(body, "\"hourly\":", "\"precipitation_probability\":[", pio, CIELO_ORE_MAX);

  s_st.nOre = (uint8_t)nW;
  for (int i = 0; i < nW; i++) {
    s_st.ore[i].wmo        = isfinite(wmo[i]) ? (int16_t)wmo[i] : -1;
    s_st.ore[i].tempC      = (i < nT) ? tmp[i] : NAN;
    s_st.ore[i].pioggiaPct = (i < nP && isfinite(pio[i])) ? (int8_t)pio[i] : -1;
  }

  // Il PEGGIORE delle prossime ore, non il primo: un'icona sola che deve
  // avvisare della pioggia in arrivo non puo' mostrare il sereno di adesso.
  // L'ora corrente entra nel conto, o alle 10:59 si guarderebbe un futuro che
  // comincia fra un minuto.
  int wmoBreve = wmoOra;
  for (int i = 0; i < nW && i < CIELO_ORE_BREVI; i++) {
    const int c = s_st.ore[i].wmo;
    if (c >= 0 && gravita(cielo_classe_da_wmo(c)) > gravita(cielo_classe_da_wmo(wmoBreve)))
      wmoBreve = c;
  }

  // --- i giorni ---------------------------------------------------------
  float gw[CIELO_GIORNI_MAX], gmax[CIELO_GIORNI_MAX], gmin[CIELO_GIORNI_MAX], gp[CIELO_GIORNI_MAX];
  const int nG  = jsonArray(body, "\"daily\":", "\"weather_code\":[", gw, CIELO_GIORNI_MAX);
  const int nGx = jsonArray(body, "\"daily\":", "\"temperature_2m_max\":[", gmax, CIELO_GIORNI_MAX);
  const int nGn = jsonArray(body, "\"daily\":", "\"temperature_2m_min\":[", gmin, CIELO_GIORNI_MAX);
  const int nGp = jsonArray(body, "\"daily\":", "\"precipitation_probability_max\":[", gp, CIELO_GIORNI_MAX);

  s_st.nGiorni = (uint8_t)nG;
  for (int i = 0; i < nG; i++) {
    s_st.giorni[i].wmo        = isfinite(gw[i]) ? (int16_t)gw[i] : -1;
    s_st.giorni[i].tMaxC      = (i < nGx) ? gmax[i] : NAN;
    s_st.giorni[i].tMinC      = (i < nGn) ? gmin[i] : NAN;
    s_st.giorni[i].pioggiaPct = (i < nGp && isfinite(gp[i])) ? (int8_t)gp[i] : -1;
  }

  // --- adesso -----------------------------------------------------------
  s_st.wmoAdesso  = (int16_t)wmoOra;
  s_st.wmoBreve   = (int16_t)wmoBreve;
  s_st.adesso     = cielo_classe_da_wmo(wmoOra);
  s_st.breve      = cielo_classe_da_wmo(wmoBreve);
  s_st.tempC      = jsonFloat(body, "\"current\":", "\"temperature_2m\":");
  s_st.percepitaC = jsonFloat(body, "\"current\":", "\"apparent_temperature\":");
  s_st.pioggiaMm  = jsonFloat(body, "\"current\":", "\"precipitation\":");
  s_st.ventoKmh   = jsonFloat(body, "\"current\":", "\"wind_speed_10m\":");
  s_st.raficheKmh = jsonFloat(body, "\"current\":", "\"wind_gusts_10m\":");
  const float rh  = jsonFloat(body, "\"current\":", "\"relative_humidity_2m\":");
  s_st.umiditaPct = isfinite(rh) ? (int8_t)rh : -1;

  s_st.quando   = rtctime_now();
  s_st.valido   = true;
  s_st.prese++;
  s_st.errore[0] = '\0';
  s_st.ultimaDurataMs = (uint16_t)(millis() - t0);
}

// ---------------------------------------------------------------------
void cielo_begin() {
  memset(&s_st, 0, sizeof(s_st));
  s_st.wmoAdesso = s_st.wmoBreve = -1;
  s_st.tempC = s_st.percepitaC = s_st.pioggiaMm = NAN;
  s_st.ventoKmh = s_st.raficheKmh = NAN;
  s_st.umiditaPct = -1;

  Preferences p;
  if (p.begin(CIELO_NS, true)) {
    s_lat = p.getFloat("lat", NAN);
    s_lon = p.getFloat("lon", NAN);
    p.end();
  }
  if (!cielo_posizione_valida())
    snprintf(s_st.errore, sizeof(s_st.errore), "%s", "posizione non impostata");
}

void cielo_loop() {
  const uint32_t ora = millis();

  // Il dato scade DA SOLO, anche senza tentativi: e' cio' che impedisce al
  // pannello di mostrare il cielo di stamattina quando la rete manca da
  // un'ora e mezza. Serve l'orologio vero, o "quando" non vuol dire niente.
  if (s_st.valido && s_st.quando > 0 && rtctime_isSynced() &&
      (uint32_t)(rtctime_now() - s_st.quando) > CIELO_SCADE_S) {
    s_st.valido = false;
    snprintf(s_st.errore, sizeof(s_st.errore), "%s", "dato scaduto");
  }

  if (!s_subito && (int32_t)(ora - s_prossimaMs) < 0) return;
  // Non si tenta nemmeno se manca la rete o la posizione: sarebbero errori ad
  // ogni giro, e il contatore delle fallite direbbe che il SERVIZIO non va
  // quando invece manca il WiFi o non e' mai stata impostata la posizione.
  // (Visto: appena accesa la scheda diceva gia' "1 fallita".)
  if (WiFi.status() != WL_CONNECTED) return;
  if (!cielo_posizione_valida()) return;

  s_subito = false;
  aggiorna();
  s_prossimaMs = millis() + (s_st.errore[0] ? CIELO_RIPROVA_MS : CIELO_PERIODO_MS);
}

bool cielo_get(CieloStato* out) {
  if (!out) return false;
  *out = s_st;
  return true;
}
