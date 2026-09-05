# XIAO_S3_Camera — nodo camera (XIAO ESP32-S3 Sense)

Guida di progetto: si carica quando si lavora sui file di questa cartella. Per
l'architettura del workspace, i comandi di build e le regole valide su tutte le
schede vedi `CLAUDE.md` alla radice; per le trappole hardware gia' pagate (USB
CDC, deep sleep, OTA, scritture su SD, default NVS) `docs/Trappole-Hardware.md`.

## I file di questa cartella

| File | Ruolo |
|---|---|
| `XIAO_S3_Camera.ino` | `setup()`/`loop()` + logica del PIR e dello scatto — qui va la logica applicativa |
| `camera.h/.cpp` | camera OV2640/OV3660 (pin cablati sulla Sense), cattura e impostazioni |
| `storage.h/.cpp` | microSD **SPI** della Sense: foto `IMG_*.JPG` + CSV degli eventi |
| `net_ota.h/.cpp` | WiFi + ArduinoOTA + `/update`, gemello di quello del C3 — di norma non si tocca |
| `web_ui.h/.cpp` | pagina di controllo + API HTTP (stream MJPEG, scatto, galleria) |
| `hub_link.h/.cpp` | nodo ESP-NOW sopra `EspNowLink` (pairing, notifiche, comandi) |
| `secrets.h.example` | come per il C3: si copia in `secrets.h`, **gitignorato** |


La catena è

```
PIR HC-SR501 -> scatto -> JPEG su microSD -> DATA ESP-NOW all'hub
```

più una web UI (`http://<OTA_HOSTNAME>.local/`) con video live MJPEG, scatto
manuale, galleria delle foto sulla card e impostazioni (armato/disarmato, pausa
tra gli scatti, risoluzione, qualità, flip), e l'OTA come sul C3.

**Perché non è self-contained** (a differenza di `starters/C3_OLED_OTA/`):
include `libraries/EspNowLink`, perché il protocollo hub↔nodi deve essere lo
stesso da entrambe le parti. Spostando la cartella fuori dal repo va portata
anche `libraries/EspNowLink` (o una junction). Tutto il resto è core ESP32:
`esp_camera` è bundled, non serve installare niente.

**Vincoli hardware (XIAO ESP32-S3 Sense)**:
- **Camera** (cablata sulla scheda di espansione, pin non negoziabili):
  XCLK=GPIO10, SIOD=GPIO40, SIOC=GPIO39, D0-D7=15/17/18/16/14/12/11/48,
  VSYNC=GPIO38, HREF=GPIO47, PCLK=GPIO13. Sono gli stessi di
  `CAMERA_MODEL_XIAO_ESP32S3` in `camera_pins.h` del core. **Serve la PSRAM
  abilitata**.
- **microSD della Sense**: su **SPI** (non SDMMC come la board AMOLED, quindi
  `AMOLED191_SD` qui non c'entra): CS=**GPIO21**, SCK=GPIO7 (D8), MISO=GPIO8 (D9),
  MOSI=GPIO9 (D10). Lo schematico Seeed indica il CS su GPIO3: è un **errore
  noto**, il pin buono è il 21. GPIO21 è anche il **LED utente** della XIAO →
  con la Sense montata lampeggia da solo ad ogni accesso alla card e non è
  usabile come spia di stato. Lo slot occupa tutto il bus SPI (ponticello
  **J3** sulla Sense per scollegarlo).
- **PIR HC-SR501**: VCC → pin **5V** (il modulo vuole 5V, l'uscita è 3,3V,
  sicura per il GPIO), GND → GND, OUT → **D0 (GPIO1)** (`PIN_PIR` nel `.ino`).
  Ponticello su **H** (retriggerabile), trimmer TIME al minimo. Dopo
  l'accensione il sensore dà falsi positivi: `PIR_WARMUP_MS` (60 s) li ignora.
- **GPIO liberi** per altre periferiche: D1–D5 (GPIO 2, 3, 4, 5, 6; 5/6 sono
  anche SDA/SCL di default). D8/D9/D10 sono occupati dalla microSD.
- **Microfono PDM** (CLK=GPIO42, DATA=GPIO41): presente sulla Sense ma **non
  usato** da questo template.

**Vincoli software da rispettare**:
- Il `WebServer` del core è **sincrono**: finché un client tiene aperto
  `/stream`, la scheda non risponde ad altro (OTA compreso). Lo stream si
  autolimita a 5 minuti e il ciclo chiama `app_pump()` ad ogni frame per tenere
  vivi PIR ed ESP-NOW. Dentro `app_pump()` **non** si chiama `net_loop()`:
  rientrare in `handleClient()` mentre si serve una richiesta rompe il server.
- `hub_begin()` va chiamata **dopo** `net_begin()`: il canale ESP-NOW dipende
  dall'AP a cui ci si è connessi (vedi
  [`libraries/EspNowLink/CLAUDE.md`](../../libraries/EspNowLink/CLAUDE.md) —
  l'hub va messo sullo stesso canale).
- La callback di ricezione ESP-NOW **accoda e basta** (coda FreeRTOS), i
  comandi si eseguono da `loop()`: stessa regola dei callback LVGL sull'hub.
- Ogni frame ottenuto dalla camera va **sempre** restituito
  (`camera_release()`), anche sui percorsi d'errore, o dopo pochi scatti non ci
  sono più buffer liberi.

**Dove scrivere la logica**: nel `.ino` (PIR, scatto, comandi dell'hub, nuove
periferiche in `loop()` senza bloccare a lungo). `camera.*`, `storage.*`,
`net_ota.*`, `web_ui.*`, `hub_link.*` sono boilerplate per compito.

