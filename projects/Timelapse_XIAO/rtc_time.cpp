#include "rtc_time.h"
#include <sys/time.h>
#include <esp_sntp.h>

// Server NTP: due, cosi' se uno non risponde (rete camper spesso instabile)
// il client SNTP prova comunque l'altro.
static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.cloudflare.com";

static char             s_tz[48]       = "UTC0";
static volatile bool    s_synced       = false;
static bool             s_cbRegistered = false;

// Chiamata dal core (task SNTP interno) al primo sync riuscito e a ogni
// resync successivo. Tenuta minima: nessuna I/O, solo un flag.
static void onTimeSync(struct timeval* tv) {
  (void)tv;
  s_synced = true;
}

void rtctime_begin(const char* tzPosix) {
  if (tzPosix && tzPosix[0] != '\0') {
    strlcpy(s_tz, tzPosix, sizeof(s_tz));
  }
  setenv("TZ", s_tz, 1);
  tzset();

  if (!s_cbRegistered) {
    sntp_set_time_sync_notification_cb(onTimeSync);
    s_cbRegistered = true;
  }
}

// Parsing di __DATE__ ("Mmm dd yyyy", giorno padded a spazio se <10) e
// __TIME__ ("hh:mm:ss"), generati dal preprocessore a ogni compilazione.
static bool parseBuildDateTime(struct tm* out) {
  static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char monStr[4] = {0};
  int day = 0, year = 0, hour = 0, minute = 0, second = 0;

  if (sscanf(__DATE__, "%3s %d %d", monStr, &day, &year) != 3) return false;
  if (sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) return false;

  const char* p = strstr(MONTHS, monStr);
  if (!p) return false;
  int mon = (int)(p - MONTHS) / 3;   // 0-based, come tm_mon

  memset(out, 0, sizeof(*out));
  out->tm_year  = year - 1900;
  out->tm_mon   = mon;
  out->tm_mday  = day;
  out->tm_hour  = hour;
  out->tm_min   = minute;
  out->tm_sec   = second;
  out->tm_isdst = -1;   // lascia decidere a mktime() in base a TZ/data
  return true;
}

void rtctime_seedFromBuild() {
  struct tm bt;
  if (!parseBuildDateTime(&bt)) {
    Serial.println("[rtc_time] parsing __DATE__/__TIME__ fallito: nessuna stima applicata.");
    return;
  }
  time_t epoch = mktime(&bt);
  if (epoch <= 0) return;

  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, nullptr);

  char buf[32];
  if (rtctime_format(epoch, "%Y-%m-%d %H:%M:%S", buf, sizeof(buf))) {
    Serial.printf("[rtc_time] stima da build-time: %s (in attesa di NTP)\n", buf);
  }
}

void rtctime_onWifiConnected() {
  configTzTime(s_tz, NTP_SERVER_1, NTP_SERVER_2);
}

bool rtctime_isSynced() { return s_synced; }

const char* rtctime_source() { return s_synced ? "NTP" : "STIMA"; }

time_t rtctime_now() { return time(nullptr); }

bool rtctime_nowLocal(struct tm* out) {
  if (!out) return false;
  time_t now = time(nullptr);
  localtime_r(&now, out);
  return true;
}

bool rtctime_format(time_t t, const char* fmt, char* out, size_t outCap) {
  if (!out || outCap == 0) return false;
  out[0] = '\0';   // strftime() puo' lasciare il buffer NON terminato se il
                    // risultato non ci sta (dipende dall'implementazione della
                    // libc): senza questo, un chiamante che ignora il valore
                    // di ritorno finito false leggerebbe oltre il buffer,
                    // scambiando la mancanza di terminatore per una stringa
                    // lunghissima (bug reale trovato: bloccava il web server
                    // dentro un ciclo che leggeva memoria dello stack in cerca
                    // di un '\0' mai trovato).
  struct tm local;
  localtime_r(&t, &local);
  return strftime(out, outCap, fmt, &local) > 0;
}
