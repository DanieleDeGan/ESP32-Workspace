# C3_OLED_OTA — ESP32-C3 Supermini + OLED + OTA

Guida di progetto: si carica quando si lavora sui file di questa cartella. Per
l'architettura del workspace, i comandi di build e le regole valide su tutte le
schede vedi `CLAUDE.md` alla radice; per le trappole hardware gia' pagate (USB
CDC, deep sleep, OTA, scritture su SD, default NVS) `docs/Trappole-Hardware.md`.

## I file di questa cartella

| File | Ruolo |
|---|---|
| `C3_OLED_OTA.ino` | `setup()`/`loop()` + disegno OLED — qui va la logica applicativa |
| `net_ota.h/.cpp` | boilerplate WiFi + ArduinoOTA + web server `/update` — di norma non si tocca |
| `secrets.h.example` | template delle credenziali: si copia in `secrets.h`, che è **gitignorato** (repo pubblico) |


Template per nodi con display piccolo. Non condivide niente con la board
AMOLED: usa **Adafruit SSD1306 + GFX** e i moduli del core (`WiFi`, `ESPmDNS`,
`ArduinoOTA`, `WebServer`, `Update`, `Wire`), non `libraries/`. Il punto è
l'**OTA**: una scheda montata dentro qualcosa non si raggiunge col cavo, quindi
si carica via USB una volta sola e poi si aggiorna via rete, in due modi —
ArduinoOTA (compare come porta di rete in Arduino IDE) e una pagina web
`http://<OTA_HOSTNAME>.local/update` dove si carica il `.bin`.

**Credenziali**: `secrets.h` contiene SSID/password WiFi e le password OTA ed è
**escluso dal `.gitignore`** — questo repo è pubblico. Versionato c'è solo
`secrets.h.example` coi segnaposto: si copia in `secrets.h` e si riempie. Se
qualcosa di reale dovesse finire committato, cambiare la password della rete è
più affidabile che riscrivere la storia di git: una volta pubblicata va
considerata compromessa.

**Vincoli hardware (C3 Supermini)**:
- **I2C OLED**: default SDA=**GPIO5**, SCL=**GPIO6**, indirizzo `0x3C` (alcuni
  moduli `0x3D`), moduli a 4 pin → reset `-1`. Rimappabili da `PIN_SDA`/`PIN_SCL`
  in cima al `.ino`.
- **GPIO8**: LED blu onboard, **attivo LOW** (usato come heartbeat a riposo).
- **GPIO9**: tasto BOOT (strapping) — non usarlo per periferiche.
- **GPIO18/19**: USB nativo (Serial/JTAG) — non toccare.
- I pin I2C di default sono scelti apposta per non toccare né il LED né BOOT.

**`net_ota.*`**: `net_begin()` fa connessione WiFi (bloccante, timeout 15 s, poi
ritenta in background), `ArduinoOTA.begin()` (che avvia anche mDNS) e il web
server con `/` e `/update`. `net_loop()` va chiamata **a ogni giro** di `loop()`,
altrimenti l'OTA muore. Il feedback a schermo durante l'update passa da
`net_setOtaProgressCb(cb)`, che riceve `percent` 0..100 oppure `-1` se la
dimensione è ignota (upload web): va impostata **prima** di `net_begin()`.
L'autenticazione (password ArduinoOTA + basic-auth su `/update`) è pensata per
una **LAN fidata**, non per esporre la scheda su Internet.

**Dove scrivere la logica**: UI/stato a riposo dentro `drawStatus()` nel `.ino`
(chiamata ~4 fps dal loop); nuove periferiche in `loop()`, senza bloccare a lungo
e tenendo vivo `net_loop()`. `net_ota.*` di norma non si tocca.

