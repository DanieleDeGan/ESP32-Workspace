#include "net_ota.h"
#include "secrets.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Update.h>

// ---------------------------------------------------------------------
//  Stato interno
// ---------------------------------------------------------------------
static WebServer         server(80);
static ota_progress_cb_t s_progressCb          = nullptr;
static bool              s_webUpdateAuthorized = false;
static char              s_msg[24];

// Vero mentre un OTA (Arduino o web) sta scrivendo la partizione: il
// watchdog WiFi sotto non deve toccare la connessione in quella finestra.
static bool s_updateInProgress = false;

void net_setOtaProgressCb(ota_progress_cb_t cb) { s_progressCb = cb; }

bool    net_isConnected() { return WiFi.status() == WL_CONNECTED; }
String  net_ip()          { return WiFi.localIP().toString(); }
int     net_rssi()        { return WiFi.RSSI(); }
uint8_t net_channel()     { return WiFi.channel(); }

WebServer& net_server()   { return server; }

bool net_webAuthOk() {
  if (strlen(WEB_USER) == 0) return true;      // auth disattivata
  return server.authenticate(WEB_USER, WEB_PASS);
}

// ---------------------------------------------------------------------
//  Pagina /update (form di upload, self-contained, nessuna CDN)
// ---------------------------------------------------------------------
static const char UPDATE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeteoNode-C3 &mdash; OTA</title><style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:2rem;display:flex;justify-content:center}
 .card{max-width:420px;width:100%;background:#1c1c1c;border:1px solid #333;border-radius:12px;padding:1.5rem}
 h1{font-size:1.05rem;margin:0 0 1rem}
 input[type=file]{width:100%;margin:.5rem 0 1rem;color:#ccc}
 button{width:100%;padding:.7rem;border:0;border-radius:8px;background:#3b82f6;color:#fff;font-size:1rem;cursor:pointer}
 button:disabled{background:#555}
 progress{width:100%;height:1rem;margin-top:1rem}
 .muted{color:#8a8a8a;font-size:.8rem;margin-top:1rem;line-height:1.4}
 a{color:#3b82f6}
</style></head><body><div class="card">
 <h1>Aggiornamento firmware &mdash; nodo ambientale</h1>
 <form id="f"><input type="file" name="update" accept=".bin" required>
 <button type="submit" id="b">Carica e aggiorna</button>
 <progress id="p" value="0" max="100" hidden></progress></form>
 <p class="muted" id="s">Seleziona il .bin generato da Arduino IDE:
 <br>Sketch &gt; Export Compiled Binary, poi prendi il file <code>*.ino.bin</code>.
 <br><br><a href="/">&larr; torna alla dashboard</a></p>
<script>
const f=document.getElementById('f'),b=document.getElementById('b'),p=document.getElementById('p'),s=document.getElementById('s');
f.addEventListener('submit',e=>{e.preventDefault();const fd=new FormData(f),x=new XMLHttpRequest();
 x.open('POST','/update');p.hidden=false;b.disabled=true;
 x.upload.onprogress=ev=>{if(ev.lengthComputable){const pc=Math.round(ev.loaded/ev.total*100);p.value=pc;s.textContent='Caricamento '+pc+'%';}};
 x.onload=()=>{s.textContent=(x.status==200)?'OK! La scheda si riavvia...':('Errore: '+x.responseText);};
 x.onerror=()=>{s.textContent='Errore di rete';b.disabled=false;};
 x.send(fd);});
</script></div></body></html>
)HTML";

// ---------------------------------------------------------------------
//  Handler web
// ---------------------------------------------------------------------
static void handleUpdatePage() {
  if (!net_webAuthOk()) { server.requestAuthentication(); return; }
  server.send_P(200, "text/html", UPDATE_PAGE);
}

// Fine dell'upload: risposta e (se ok) reboot.
static void handleUpdateDone() {
  if (!s_webUpdateAuthorized) { server.requestAuthentication(); return; }
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Aggiornamento fallito");
  if (ok) { delay(300); ESP.restart(); }
}

// Ricezione del file a blocchi -> scrittura su partizione OTA.
static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      s_webUpdateAuthorized = net_webAuthOk();
      if (!s_webUpdateAuthorized) return;
      s_updateInProgress = true;
      Serial.printf("[WebOTA] Start: %s\n", up.filename.c_str());
      if (s_progressCb) s_progressCb(-1, "Web OTA...");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      break;

    case UPLOAD_FILE_WRITE:
      if (!s_webUpdateAuthorized) return;
      if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
      if (s_progressCb) {
        snprintf(s_msg, sizeof(s_msg), "Web OTA %uKB", (unsigned)(Update.progress() / 1024));
        s_progressCb(-1, s_msg);
      }
      break;

    case UPLOAD_FILE_END:
      if (!s_webUpdateAuthorized) return;
      if (Update.end(true)) {
        Serial.printf("[WebOTA] OK: %u byte\n", up.totalSize);
        if (s_progressCb) s_progressCb(100, "Completato");
      } else {
        Update.printError(Serial);
      }
      s_updateInProgress = false;
      break;

    case UPLOAD_FILE_ABORTED:
      // Update.abort() NON e' opzionale: senza, l'oggetto Update resta
      // "in corso" per sempre dopo un upload interrotto, e OGNI tentativo
      // successivo fallisce in silenzio - begin() torna false, le write()
      // non scrivono niente e end() da' errore, quindi la pagina risponde
      // 500 "Aggiornamento fallito" anche con un file perfettamente valido.
      // L'unico modo di uscirne sarebbe riavviare la scheda, che e'
      // esattamente cio' che via rete non si puo' fare. Trovato sul serio
      // il 2026-08-23 su MeteoNode_C3: un primo upload caduto a meta' ha
      // reso il nodo impossibile da aggiornare via rete.
      s_updateInProgress = false;
      Update.abort();
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------
//  Setup
// ---------------------------------------------------------------------
static void wifiConnectBlocking(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  // Il power-save del WiFi (modem-sleep) su alcuni router causa disconnessioni
  // a raffica con "Reason: 2 - AUTH_EXPIRE" subito dopo l'associazione: il
  // radio si riaddormenta durante lo scambio di autenticazione e l'AP lo
  // considera scaduto. Disabilitarlo e' la correzione standard per l'ESP32
  // in modalita' stazione (costa qualche mA in piu', irrilevante per un nodo
  // alimentato via USB).
  WiFi.setSleep(false);
  // Il fix del power-save da solo non e' bastato: l'AP disconnette la scheda
  // con "Reason: 2 - AUTH_EXPIRE" a intervalli fissi, che coi router alcuni
  // moduli WiFi ESP32 a piena potenza (19.5dBm) risolvono abbassando la
  // potenza TX. Se il problema e' altrove (impostazioni router), va
  // rialzata: 19.5dBm resta il default se questa riga si rimuove.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connessione a \"%s\"", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connesso. IP: %s  RSSI: %d dBm  canale: %u\n",
                  WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), (unsigned)WiFi.channel());
  } else {
    Serial.println("[WiFi] Timeout: continuo, ritento in background.");
  }
}

// ---------------------------------------------------------------------
//  Watchdog di riconnessione WiFi
//
//  WiFi.setAutoReconnect(true) copre le cadute brevi, ma su hardware reale
//  lo stack WiFi dell'ESP32 puo' restare "impantanato" dopo un'assenza
//  prolungata del router (riavvio, salto ISP) e non riprendersi da solo: e'
//  il sintomo osservato dopo l'uptime di 3 giorni di questo nodo. Watchdog
//  come rete di sicurezza in piu' sui tempi lunghi, non un sostituto.
// ---------------------------------------------------------------------
static constexpr uint32_t WIFI_RETRY_MS     = 30UL * 1000;    // giu' da 30s -> ritento
static constexpr uint32_t WIFI_HARDRESET_MS = 5UL * 60000;    // giu' da 5 min -> re-init completo
static constexpr uint32_t WIFI_REBOOT_MS    = 15UL * 60000;   // giu' da 15 min -> riavvio scheda

static uint32_t s_lastConnectedMs = 0;
static uint32_t s_lastRetryMs     = 0;

// Contatori dell'assenza dell'AP (vedi net_ota.h). Stanno qui perche' qui vive
// il watchdog: e' lo stesso passaggio di stato a incrementarli e a decidere il
// riavvio, e tenerli altrove vorrebbe dire osservare la connessione due volte
// con due idee diverse di quando e' caduta.
static bool     s_wasConnected  = false;
static uint32_t s_wifiDrops     = 0;
static uint32_t s_wifiDownMaxMs = 0;

uint32_t net_wifi_drops()      { return s_wifiDrops; }
uint32_t net_wifi_down_max_s() { return s_wifiDownMaxMs / 1000; }
uint32_t net_wifi_down_now_s() {
  if (WiFi.status() == WL_CONNECTED) return 0;
  return (millis() - s_lastConnectedMs) / 1000;
}

static void wifiWatchdog() {
  if (s_updateInProgress) return;   // non toccare il WiFi mentre scrive la partizione

  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    // Chiudere la finestra PRIMA di azzerare il riferimento: dopo
    // s_lastConnectedMs = now la durata dell'assenza non e' piu' ricavabile, e
    // down_max resterebbe a zero proprio nei casi che interessano.
    if (!s_wasConnected) {
      const uint32_t down = now - s_lastConnectedMs;
      if (down > s_wifiDownMaxMs) s_wifiDownMaxMs = down;
      s_wasConnected = true;
      Serial.printf("[WiFi] Riconnesso dopo %lu s.\n", (unsigned long)(down / 1000));
    }
    s_lastConnectedMs = now;
    return;
  }

  if (s_wasConnected) {            // appena caduto: si conta una volta sola
    s_wasConnected = false;
    s_wifiDrops++;
  }

  uint32_t down = now - s_lastConnectedMs;
  // Aggiornato anche mentre l'AP e' ancora giu', non solo al rientro: se il
  // watchdog arriva a riavviare la scheda il rientro non avviene mai, e senza
  // questo la caduta piu' lunga - l'unica che conta - non verrebbe registrata.
  if (down > s_wifiDownMaxMs) s_wifiDownMaxMs = down;
  if (down > WIFI_REBOOT_MS) {
    Serial.println("[WiFi] Giu' da troppo tempo, riavvio la scheda.");
    delay(100);
    ESP.restart();
  }

  if (now - s_lastRetryMs < WIFI_RETRY_MS) return;   // non ritentare a ogni giro
  s_lastRetryMs = now;

  if (down > WIFI_HARDRESET_MS) {
    Serial.println("[WiFi] Giu' da troppo tempo, re-init completo dello stack.");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);                      // vedi nota AUTH_EXPIRE in wifiConnectBlocking()
    WiFi.setTxPower(WIFI_POWER_11dBm);         // idem: da riapplicare, non e' persistente
    WiFi.setHostname(OTA_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    Serial.println("[WiFi] Non connesso, ritento...");
    WiFi.reconnect();
  }
}

void net_begin() {
  wifiConnectBlocking(15000);
  s_lastConnectedMs = millis();   // riferimento iniziale per il watchdog, connesso o no
  s_wasConnected    = net_isConnected();   // se non e' mai salito, non e' una "caduta"

  // --- 1) ArduinoOTA: upload da Arduino IDE (porta di rete) ---
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    Serial.println("[ArduinoOTA] Start");
    s_updateInProgress = true;
    if (s_progressCb) s_progressCb(0, "ArduinoOTA...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (s_progressCb && total) s_progressCb((int)(progress * 100 / total), "ArduinoOTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[ArduinoOTA] Fine");
    s_updateInProgress = false;
    if (s_progressCb) s_progressCb(100, "Completato");
  });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("[ArduinoOTA] Errore %u\n", err);
    s_updateInProgress = false;
  });
  ArduinoOTA.begin();   // avvia anche mDNS con OTA_HOSTNAME

  // --- 2) Web OTA: pagina /update (la "/" la registra web_ui) ---
  MDNS.addService("http", "tcp", 80);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.printf("[Web] http://%s.local/  (OTA: /update)\n", OTA_HOSTNAME);
}

void net_loop() {
  wifiWatchdog();
  ArduinoOTA.handle();
  server.handleClient();
}
