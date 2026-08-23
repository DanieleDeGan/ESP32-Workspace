# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Cos'è questo repository

Non è un singolo progetto ma un **workspace ESP32**: il posto dove vivono le
librerie condivise, un template riutilizzabile per ogni scheda usata, gli sketch
di esempio che le validano e i progetti reali già installati.

Quattro ruoli, quattro cartelle:

- **`libraries/`** — librerie Arduino condivise, una per periferica/funzione.
- **`starters/`** — un template per scheda. **Si copiano** per iniziare un
  progetto nuovo; non si modificano per un'applicazione specifica.
- **`examples/`** — sketch autosufficienti, da compilare e caricare così come
  sono. Servono a validare le librerie e a mostrare come si usano.
- **`projects/`** — applicazioni reali, già in funzione su hardware.

**L'organizzazione è per ruolo e, dentro, per scheda — non per modello di chip**,
ed è deliberato: `libraries/` è condivisa tra sketch che girano su chip diversi
(`EspNowLink` la usano sia gli hub S3 sia i nodi C3/XIAO), e metà degli esempi
gira su qualunque ESP32. Il chip è un attributo documentato per progetto — le
tabelle qui sotto e in `README.md` hanno la colonna "gira su" — non una cartella.

Per il dettaglio file-per-file (scopo, funzioni chiave, dipendenze, cosa non
toccare) vedi `docs/FILES.md`. Per il pinout/hardware della board AMOLED vedi
`docs/ESP32-S3-AMOLED-1.91-Guide.md`.

## Struttura

| Percorso | Ruolo |
|---|---|
| `libraries/` | librerie Arduino condivise (bus/display/touch/IMU/SD/comunicazione), vedi sotto |
| `starters/AMOLED_1.91_LVGL/` | template per interfacce LVGL (SquareLine Studio) sulla Waveshare **ESP32-S3-Touch-AMOLED-1.91** (AMOLED 536×240 SH8601, touch FT3168, IMU QMI8658) |
| `starters/AMOLED_1.91_LVGL/AMOLED_1.91_LVGL.ino` | sketch principale — `setup()`/`loop()`, qui va SOLO la logica applicativa |
| `starters/AMOLED_1.91_LVGL/lv_conf.h` | configurazione LVGL a livello di progetto |
| `starters/AMOLED_1.91_LVGL/build_opt.h` | flag di compilazione globali (vedi sotto) |
| `starters/AMOLED_1.91_LVGL/ui.h/.c` | stub segnaposto, sostituiti dall'export "UI Files" di SquareLine |
| `starters/C3_OLED_OTA/` | template **ESP32-C3 Supermini** + OLED SSD1306 I2C + **OTA**, self-contained (non usa `libraries/`) |
| `starters/C3_OLED_OTA/C3_OLED_OTA.ino` | `setup()`/`loop()` + disegno OLED — qui va la logica applicativa |
| `starters/C3_OLED_OTA/net_ota.h/.cpp` | boilerplate WiFi + ArduinoOTA + web server `/update` — di norma non si tocca |
| `starters/C3_OLED_OTA/secrets.h.example` | template delle credenziali: si copia in `secrets.h`, che è **gitignorato** (repo pubblico) |
| `starters/XIAO_S3_Camera/` | template nodo camera **Seeed XIAO ESP32-S3 Sense**: PIR → foto su microSD → notifica ESP-NOW, web UI, OTA |
| `starters/XIAO_S3_Camera/XIAO_S3_Camera.ino` | `setup()`/`loop()` + logica del PIR e dello scatto — qui va la logica applicativa |
| `starters/XIAO_S3_Camera/camera.h/.cpp` | camera OV2640/OV3660 (pin cablati sulla Sense), cattura e impostazioni |
| `starters/XIAO_S3_Camera/storage.h/.cpp` | microSD **SPI** della Sense: foto `IMG_*.JPG` + CSV degli eventi |
| `starters/XIAO_S3_Camera/net_ota.h/.cpp` | WiFi + ArduinoOTA + `/update`, gemello di quello del C3 — di norma non si tocca |
| `starters/XIAO_S3_Camera/web_ui.h/.cpp` | pagina di controllo + API HTTP (stream MJPEG, scatto, galleria) |
| `starters/XIAO_S3_Camera/hub_link.h/.cpp` | nodo ESP-NOW sopra `EspNowLink` (pairing, notifiche, comandi) |
| `starters/XIAO_S3_Camera/secrets.h.example` | come per il C3: si copia in `secrets.h`, **gitignorato** |
| `projects/EnvNode_C3/` | **progetto reale** (ESP32-C3): DHT11 + microSD SPI + dashboard web con grafici + orario NTP + OTA, e da `v4` anche **hub ESP-NOW** dei nodi a batteria — vedi sezione dedicata |
| `projects/Timelapse_XIAO/` | **progetto** (XIAO ESP32-S3 Sense): camera timelapse a intervallo, archivio per giorno su microSD, galleria web con riproduzione, NTP + OTA — vedi sezione dedicata |
| `projects/Timelapse_XIAO/Timelapse_XIAO.ino` | timer degli scatti, gestione dello spazio, impostazioni — qui va la logica applicativa |
| `projects/Timelapse_XIAO/storage.h/.cpp` | microSD SPI organizzata per giorno: `/timelapse/<giorno>/<ora>.JPG` + CSV giornaliero |
| `projects/Timelapse_XIAO/camera.h/.cpp` | copia di quello del nodo camera (stesso hardware, stessi pin) |
| `projects/Timelapse_XIAO/rtc_time.h/.cpp` | copia di quello di `EnvNode_C3`: stima da build-time, poi NTP |
| `projects/Timelapse_XIAO/net_ota.h/.cpp` | WiFi + ArduinoOTA + `/update`, con watchdog di riconnessione — di norma non si tocca |
| `projects/Timelapse_XIAO/web_ui.h/.cpp` | pagina di controllo, galleria/riproduzione e API HTTP |
| `examples/Orientation_IMU/` | demo autosufficiente: livella per veicolo basata sull'IMU onboard, UI costruita in codice (non SquareLine) |
| `examples/Link_Hub_Demo/` | demo lato hub di `EspNowLink`: schermo AMOLED, pairing/lista nodi associati |
| `examples/Link_Node_Demo/` | demo nodo sensore finto: solo Serial, nessuna dipendenza dai pin AMOLED, gira su qualunque board ESP32 |
| `examples/DHT11_SD_Logger/` | demo: DHT11 cablato su un GPIO libero, temperatura/umidità/conteggio campioni a schermo e log CSV ogni 60 s sulla microSD onboard (colonne `boot_id,n,secondi_da_accensione,temperatura_C,umidita_pct`) |
| `examples/Diag_Hub/` + `examples/Diag_Node/` | diagnostica ESP-NOW usa e getta su `esp_now.h` grezzo (nessuna libreria di questo repo): il nodo spara un contatore in broadcast, l'hub misura la perdita reale contando i buchi nel `seq`. In modalità Long Range, quindi **non** interoperabili con `EspNowLink` |
| `docs/FILES.md` | reference file-per-file di tutto il repo |
| `docs/ESP32-S3-AMOLED-1.91-Guide.md` | guida hardware/pinout della board AMOLED |
| `docs/*.pdf` | datasheet/reference (ESP32-S3, SH8601/RM67162, QMI8658, guida LVGL+SquareLine) — consultarli per dettagli di registro/timing, non riscriverne il contenuto nel codice |

### `libraries/` — le librerie Arduino condivise

Il boilerplate hardware (display, touch, IMU, microSD, bus I2C) vive in librerie
Arduino locali, una per periferica, così ogni sketch include solo quello che
usa:

| Libreria | Ruolo | API pubblica |
|---|---|---|
| `AMOLED191_Core` | bring-up idempotente del bus I2C condiviso | `Core_I2CBusInit()` |
| `AMOLED191_Display` | pannello SH8601 (QSPI) + LVGL + task di rendering + mutex — **non si tocca** | `Display_Init()`, `lvgl_lock()`/`lvgl_unlock()`, `lcd_command()`/`lcd_set_brightness()`/`lcd_read_register()` |
| `AMOLED191_Touch` | driver touch FT3168 su I2C, con wiring LVGL opzionale | `Touch_Init()`, `getTouch()`, `Touch_RegisterLvglIndev()` |
| `AMOLED191_IMU` | mini-driver IMU QMI8658 onboard | `imu_init()`, `imu_read_accel()` |
| `AMOLED191_SD` | microSD onboard (SDMMC 1 bit), orientata al logging testuale — nessun pin in comune col display | `SDCard_Init()`, `SDCard_AppendLine()`, `SDCard_WriteHeaderIfNew()`, `SDCard_IsMounted()`/`SDCard_LastError()`/`SDCard_SizeMB()`/`SDCard_Exists()` |
| `EspNowLink` | comunicazione ESP-NOW hub↔nodi, indipendente da LVGL/display e da una scheda specifica | `Link_Init()`, `Link_OnMessage()`, `Link_Node_*`, `Link_Hub_*` — vedi sezione dedicata sotto |

**Il prefisso è la documentazione**: le cinque `AMOLED191_*` conoscono i pin
della board AMOLED e fuori di lì non hanno senso; `EspNowLink` non dipende da
nessun hardware specifico ed è inclusa da entrambi i lati del collegamento
(hub S3 **o C3** — vedi `projects/EnvNode_C3/` —, nodi C3/XIAO) — il protocollo
deve essere identico su tutti. Una
libreria nuova che vale solo per una scheda prende il prefisso di quella
scheda; una portabile prende un nome funzionale.

Restano deliberatamente **per-sketch** (non condivisi) `lv_conf.h` e
`build_opt.h`, perché sono configurazione di progetto, non codice — vedi le
rispettive sezioni sotto.

Le librerie sono referenziate da `arduino-cli` con `--libraries libraries`
(vedi comandi sotto) e, per l'uso in Arduino IDE, tramite junction Windows già
create in `C:\Users\<utente>\Documents\Arduino\libraries\{AMOLED191_*,EspNowLink}`
che puntano alla cartella `libraries/` di questo repo (nessuna copia manuale,
Developer Mode non necessario: le junction non richiedono i permessi dei
symlink).

## Build / verifica

Progetto **Arduino** (niente PlatformIO/CMake). `arduino-cli` è installato in
`C:\Program Files\Arduino CLI\arduino-cli.exe` (non ancora nel PATH di default:
usare il percorso completo, o `arduino-cli` diretto se una nuova sessione shell ha
già raccolto il PATH aggiornato da winget). Usa la stessa cartella dati di Arduino
IDE (`Arduino15`), quindi vede già **ESP32 core 3.3.10** e libreria **lvgl 8.3.11**
(via Library Manager) — versione compatibile con quanto richiesto da
`lv_conf.h`/`build_opt.h` (LVGL 8.3.x).

Compilazione di verifica da riga di comando (FQBN con le stesse opzioni del
Tools menu di Arduino IDE):

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,CPUFreq=240,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" --libraries libraries starters/AMOLED_1.91_LVGL
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,CPUFreq=240,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" --libraries libraries examples/Orientation_IMU
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,CPUFreq=240,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" --libraries libraries examples/DHT11_SD_Logger
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,CPUFreq=240,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" --libraries libraries examples/Link_Hub_Demo
```

`--libraries libraries` (percorso relativo, da eseguire dalla radice del repo)
fa risolvere a `arduino-cli` le librerie locali in `libraries/`. Non è
strettamente necessario su questa macchina — esistono anche le junction verso
`Documents/Arduino/libraries/` (vedi sopra), che arduino-cli/l'IDE trovano
comunque di default — ma è il modo esplicito e portabile di puntarci, utile
anche su un'altra macchina senza junction già create.

Equivalente via Arduino IDE (Tools menu), board AMOLED:
- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
- PSRAM: **OPI PSRAM**
- USB CDC On Boot: **Enabled**
- CPU Frequency: **240 MHz**

### Sketch per XIAO ESP32-S3 Sense (FQBN diverso)

`starters/XIAO_S3_Camera/` è un'altra scheda (variante XIAO, 8 MB flash, PSRAM
obbligatoria per la camera) ma **vuole** `--libraries libraries`, perché usa
`EspNowLink`:

```
arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB" --libraries libraries starters/XIAO_S3_Camera
```

`projects/Timelapse_XIAO/` gira sulla stessa scheda ma **non** vuole
`--libraries libraries`: non usa ESP-NOW, quindi è self-contained.

```
arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB" projects/Timelapse_XIAO
```

Equivalente Arduino IDE: Board **XIAO_ESP32S3**, PSRAM **OPI PSRAM**
(obbligatoria: senza, la camera non va oltre QVGA), Partition Scheme **Default
8MB with spiffs (3MB APP/1.5MB SPIFFS)** — ha le partizioni OTA, mai *Maximum
APP (No OTA)*.

**Attenzione al CDC**: su questa board `CDCOnBoot` è **già Enabled di default**
e nel FQBN il valore `CDCOnBoot=cdc` significa *Disabled* — cioè l'opposto
delle altre schede del repo, dove `CDCOnBoot=cdc` va aggiunto per avere la
`Serial` sull'USB. Qui non va messo nulla.

### Sketch per ESP32-C3 (FQBN diverso)

`starters/C3_OLED_OTA/` e `projects/EnvNode_C3/` sono per un chip diverso,
quindi hanno un loro FQBN. Lo starter è self-contained e **non** vuole
`--libraries libraries`; `EnvNode_C3` invece lo vuole **da `v4`** (2026-08-23),
perché da lì fa anche da hub ESP-NOW e include `libraries/EspNowLink` — non le
librerie della board AMOLED, che nessuno dei due usa:

```
arduino-cli compile --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs" starters/C3_OLED_OTA
arduino-cli compile --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs" --libraries libraries projects/EnvNode_C3
```

Equivalente Arduino IDE: Board **ESP32C3 Dev Module**, USB CDC On Boot
**Enabled**, Flash **4MB**, Partition Scheme **Minimal SPIFFS (1.9MB APP with
OTA)** — consigliato; va bene anche *Default 4MB with spiffs*, ma con questi
sketch è già molto pieno. **Serve una partizione con OTA**: mai *Huge APP (3MB
No OTA)*, o l'aggiornamento via rete non funziona più.

`examples/Link_Node_Demo/`, `examples/Diag_Node/` e `examples/Diag_Hub/` girano
su qualunque ESP32: per un C3 usa `esp32:esp32:esp32c3:CDCOnBoot=cdc`. **Senza
`CDCOnBoot=cdc` la `Serial` dello sketch finisce sui pin UART0** e sulla porta
USB si vede solo il log di boot della ROM, non lo sketch — errore facile da
scambiare per "lo sketch non parte".

### `Update.abort()` nel caso `UPLOAD_FILE_ABORTED` — obbligatorio

In `net_ota.cpp` il gestore dell'upload deve chiamare `Update.abort()` quando
un caricamento si interrompe:

```cpp
case UPLOAD_FILE_ABORTED:
  s_updateInProgress = false;
  Update.abort();          // <-- senza questo la scheda non si aggiorna piu'
  break;
```

Senza, l'oggetto `Update` resta "in corso" per sempre dopo il primo upload
caduto a metà, e **ogni tentativo successivo fallisce in silenzio**:
`Update.begin()` torna false, le `write()` non scrivono niente, `end()` dà
errore e la pagina risponde `500 Aggiornamento fallito` anche con un `.bin`
perfettamente valido. L'unico modo di uscirne è riavviare la scheda — cioè
esattamente la cosa che via rete non si può fare.

**Il sintomo non somiglia alla causa**: sembra un firmware corrotto o una
partizione sbagliata, e si perde tempo a controllare quelle. Il segnale da
riconoscere è che il trasferimento arriva **al 100%** e solo allora torna 500.

Trovato sul serio il 2026-08-23 su `MeteoNode_C3`, dove un primo upload caduto
a 512 KB aveva reso il nodo impossibile da aggiornare via rete (e spiega
retroattivamente perché su quel nodo l'OTA non era mai riuscito). Corretto in
`projects/EnvNode_C3/`, `projects/MeteoNode_C3/`, `starters/C3_OLED_OTA/` e
`starters/XIAO_S3_Camera/`; `projects/Timelapse_XIAO/` ce l'aveva già —
era la correzione fatta nella copia più recente e mai riportata indietro.

### `Serial.setTxTimeoutMs(0)` — obbligatorio su C3 e S3

Su queste schede la `Serial` dell'USB **non è una UART ma la CDC del chip**. Se
il PC ha riconosciuto la porta e nessuno la sta leggendo, il buffer si riempie e
ogni `print()` **blocca** fino a un timeout interno — e finché blocca, `loop()` è
fermo, quindi web server, OTA, sensori e timer con lui. Rimedio, subito dopo
`Serial.begin()`:

```cpp
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);   // se nessuno ascolta, il log si butta
#endif
```

Il `#if` serve perché con *USB CDC On Boot: Disabled* la `Serial` torna a essere
una UART, che quel metodo non ce l'ha e non ne ha bisogno (verificato: entrambe
le configurazioni compilano).

**Il sintomo non somiglia alla causa** ed è costato una mezza giornata il
2026-08-22 su `MeteoNode_C3`: da rete la pagina moriva dopo ogni comando che
stampa molte righe insieme, mentre **con il monitor seriale collegato le stesse
identiche operazioni erano istantanee**. È proprio quell'asimmetria — "da
seriale va, da rete no" — il segnale da riconoscere, perché il monitor aperto
non è lo strumento con cui si osserva il problema: è la cosa che lo fa sparire.
Il caso peggiore non è "non c'è mai stato un monitor" ma "c'è stato e se n'è
andato"; un nodo alimentato da un caricatore non se ne accorge (senza pin dati
la porta non viene mai riconosciuta), ma basta collegarlo a un PC.

Presente in tutti gli sketch del repo che restano accesi senza cavo. Va messo
anche in ogni sketch nuovo per queste board.

Per caricare su scheda reale (non solo verificare) serve `--upload -p <porta_seriale>`,
non testato da qui in quanto richiede la scheda collegata. Le schede con OTA si
caricano via USB solo la prima volta: poi si aggiornano via rete (vedi
`net_ota.*`).

Dipendenze esterne (Library Manager):
- **LVGL 8.3.x** — serve a ogni sketch con schermo sulla board AMOLED.
- **DHT sensor library** (Adafruit) + **Adafruit Unified Sensor** — per
  `examples/DHT11_SD_Logger/` e `projects/EnvNode_C3/`. Stesso sensore letto
  allo stesso modo su tutte le schede; non è stato scritto un driver locale
  apposta.
- **Adafruit SSD1306** (tira dentro **Adafruit GFX** e **Adafruit BusIO**) —
  per `starters/C3_OLED_OTA/` e `projects/EnvNode_C3/`.
- **Niente** per `starters/XIAO_S3_Camera/`: il driver della camera
  (`esp_camera.h`) è bundled nel core ESP32
  (`tools/esp32s3-libs/<versione>/…/espressif__esp32-camera`), non è una
  libreria da installare. Tutto il resto (`SD`, `SPI`, `WebServer`,
  `ArduinoOTA`, `Preferences`) è core.

Più le librerie locali in `libraries/` — vedi la sezione `libraries/` sopra.

Non esiste una test suite automatica (è codice embedded legato all'hardware): la
verifica è "compila senza errori" + test manuale su scheda reale.

### `build_opt.h`

Contiene, passati globalmente al compilatore:
```
-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
```
- `LV_CONF_INCLUDE_SIMPLE` → LVGL cerca `lv_conf.h` lungo l'include path (quello di
  progetto) invece che a percorso relativo fisso.
- `LV_LVGL_H_INCLUDE_SIMPLE` → i file generati da SquareLine includono `lvgl.h` in
  modo "semplice"; senza questo, dopo l'export si ottengono errori `lvgl.h: No such
  file`.

## Architettura delle librerie della board AMOLED

Ordine di init in `setup()` (vedi `AMOLED_1.91_LVGL.ino`/`Orientation_IMU.ino`):

1. **`Display_Init()`** (`AMOLED191_Display`) — fa tutto l'init display in un colpo
   solo: bus QSPI → panel IO → driver SH8601 → `lv_init()` → buffer di disegno
   DMA doppi (`LVGL_BUF_LINES` righe ciascuno) → display driver → tick timer
   (`esp_timer`, 2ms) → mutex → **task FreeRTOS dedicato** che chiama
   `lv_timer_handler()` in loop. **Nessuna dipendenza dal touch.**
2. **`Touch_Init()`** (`AMOLED191_Touch`, opzionale) — chiama internamente
   `Core_I2CBusInit()` poi configura il touch FT3168.
3. **`Touch_RegisterLvglIndev()`** (`AMOLED191_Touch`, opzionale, dopo
   `Display_Init()`) — registra il touch come input device LVGL. Il task di
   rendering può già essere partito a questo punto (gira potenzialmente
   sull'altro core): la registrazione avvolge `lv_indev_drv_register()` in
   `lvgl_lock()`/`lvgl_unlock()` internamente, quindi è sicura da chiamare
   in qualunque momento dopo `Display_Init()`.
4. **`imu_init()`** (`AMOLED191_IMU`, opzionale) — chiama internamente
   `Core_I2CBusInit()` poi configura la IMU QMI8658. Funziona indipendentemente
   dal fatto che `Touch_Init()` sia stato chiamato prima, dopo, o per niente
   (entrambi passano dallo stesso `Core_I2CBusInit()` idempotente).
5. **`SDCard_Init()`** (`AMOLED191_SD`, opzionale, in qualunque ordine) — monta la
   microSD in SDMMC 1 bit. È l'unica init che può fallire per cause esterne
   (card assente/non FAT32), quindi ritorna `bool` ed è ri-chiamabile: gli
   sketch devono continuare a funzionare senza card, non fermarsi.

**Regola fondamentale di threading**: il rendering LVGL gira nel suo task. Qualunque
accesso a un oggetto LVGL da un contesto diverso (`loop()`, task sensori/WiFi propri,
callback) deve essere avvolto in `lvgl_lock(-1)` / `lvgl_unlock()` (esportate da
`AMOLED191_Display`). Dentro un callback di evento LVGL il lock è già acquisito: non
ri-prenderlo, e tenere il callback corto (lavoro lento come SD/rete va deferito a
`loop()`/un task).

`lcd_command()` / `lcd_set_brightness()` / `lcd_read_register()` (`AMOLED191_Display`)
parlano direttamente col pannello via QSPI (stesso bus del rendering): se chiamate a
runtime dal `loop()`/da un task, vanno anch'esse avvolte nel lock; non serve dentro
l'init o dentro una callback LVGL (lock già preso, o task non ancora avviato).

**Aggiungere un nuovo modulo in futuro**: segui lo stesso schema — libreria
propria in `libraries/<Nome>/`, `library.properties` senza campo `depends`
(l'unico meccanismo di risoluzione che conta con `--libraries` è l'`#include`
letterale, non quel campo), e se serve il bus I2C/QSPI condiviso passa da
`AMOLED191_Core`/i primitivi già esposti da `AMOLED191_Display` invece di
reinstallare bus propri. Sul nome vale la regola del prefisso: legato a una
scheda → prefisso della scheda; portabile → nome funzionale. Nessun modulo di
questo repo avvia un task FreeRTOS proprio a parte `AMOLED191_Display`:
valutalo solo per moduli con lavoro continuo in background, non per sensori a
lettura rapida come Touch/IMU, che restano volutamente passivi (letti da
`loop()`/dal task LVGL).

## `EspNowLink` — comunicazione ESP-NOW hub↔nodi

Libreria per reti di sensori/attuatori: una board fa da **hub** e riceve dati da
moduli indipendenti ("nodi") via **ESP-NOW** (scelto invece di MQTT/WiFi perché
alcuni nodi sono a batteria e non serve infrastruttura broker/AP). Costruita
sopra la libreria ufficiale `ESP_NOW`/`ESP_NOW_Peer` del core Arduino ESP32
(bundled in `.../packages/esp32/hardware/esp32/<versione>/libraries/ESP_NOW/`),
non su `esp_now.h` grezzo. **Indipendente da LVGL/`AMOLED191_Display`**: gira
anche su schede senza schermo (è il caso tipico di un nodo sensore reale).

**Protocollo** (`link_message_t`, 37 byte, ben sotto i 250 byte limite
ESP-NOW v1.0): `protocol_version`/`msg_type` (HELLO/WELCOME/DATA/COMMAND)/
`node_type` (temp/livello_acqua/batteria/attuatore/camera/hub, estensibile)/
`name`/`seq`/`battery_mv`/`value[3]`. Pairing dinamico: un nodo manda HELLO in
broadcast finché non associato; l'hub, solo mentre `Link_Hub_SetPairingMode(true)`,
accetta il primo HELLO sconosciuto e risponde con WELCOME (mai da dentro il
callback di ricezione — troppo lento, va accodato e inviato da
`Link_Hub_Poll()`, stessa regola dei callback LVGL). Il nodo poi manda DATA
in unicast; l'hub può mandare COMMAND allo stesso modo. Registro peer
**solo in RAM** (nessuna persistenza SD/NVS in questo giro — scelta
deliberata, non ancora implementata).

**In pairing l'hub adotta anche un DATA unicast da un MAC sconosciuto**, non
solo un HELLO in broadcast. Serve perché il registro peer vive in RAM: dopo un
riavvio dell'hub il nodo **si crede ancora associato**, quindi non manda più
HELLO, e i suoi DATA verrebbero scartati — resterebbe invisibile per sempre.
Il guasto è **silenzioso da entrambe le parti**, ed è ciò che lo rende
cattivo: l'ACK di ESP-NOW è di livello radio e arriva comunque, quindi il nodo
continua a contare i propri invii come riusciti e la sua pagina dice che va
tutto bene, mentre l'hub non mostra niente. Osservato su hardware il
2026-08-23 fra `EnvNode_C3` e `MeteoNode_C3`: quindici DATA "consegnati", zero
nodi visti. Il DATA che fa scoprire il nodo viene anche conservato come prima
lettura, o andrebbe perso (`onReceive()` non è stato chiamato: il peer non
esisteva ancora). Finché il registro non è persistito, **tenere una finestra
di pairing aperta per qualche minuto ad ogni avvio dell'hub** è ciò che fa
rientrare i nodi già noti senza intervento.

**Scoperta bidirezionale**: `ESP_NOW.onNewPeer()` scatta per MAC *sorgente*
sconosciuto, quindi non solo l'hub deve gestirlo per gli HELLO — anche il
nodo deve gestirlo per il WELCOME dell'hub (sconosciuto finché non arriva).
`Link_Init()` registra il gestore giusto in base al ruolo internamente.

**Consegna affidabile**: `Link_Node_SendData()`/`Link_Hub_SendCommand()`
usano `LinkPeer::sendReliable()` — attendono la conferma di consegna
(`onSent`) e ritentano fino a 3 volte se non arriva entro il timeout, invece di
un fire-and-forget silenzioso. La sincronizzazione tra `onSent()` (gira sul
task del driver WiFi, tipicamente Core 0) e chi attende la conferma (chiamante
su `loop()`, tipicamente Core 1) usa un **semaforo FreeRTOS**
(`SemaphoreHandle_t`), non un `volatile bool`: un bool nudo non garantisce
visibilità tra core su un chip dual-core e faceva sì che il ritentativo non
vedesse mai la conferma in tempo, esaurendo sempre tutti i tentativi anche a
invio riuscito (bug reale trovato e corretto durante il test su hardware).

**Canale e convivenza col WiFi**: `Link_Init()` forza il canale fisso
`ESPNOW_LINK_CHANNEL` (6) e presuppone che nessuno sia connesso a un access
point — il caso normale di una rete di nodi a batteria. Un nodo che sta
**anche** su una rete WiFi (il nodo camera `starters/XIAO_S3_Camera/`, che ha
web UI e OTA) non può scegliere il canale: glielo impone il router. Per quel
caso c'è `Link_InitEx(tipo, nome, canale)` con `ESPNOW_LINK_CHANNEL_CURRENT`
(0), che non tocca il canale e registra i peer con channel 0 ("quello
corrente"). Conseguenza da non dimenticare: la radio è una sola, quindi **anche
l'hub va inizializzato sul canale di quell'AP** (`Link_InitEx(LINK_NODE_HUB,
"Hub", canale_AP)`), altrimenti i due non si sentono. Se il router cambia canale
da solo, il nodo lo segue al riavvio e l'hub no.

**Limite noto**: l'unicast ESP-NOW tra un hub ESP32-S3 e un nodo ESP32
"classico" (Xtensa D0WD) è risultato inaffidabile/lento ad associarsi su
hardware reale (broadcast sempre ok, WELCOME/unicast spesso perso), coerente
con un'issue nota e irrisolta nell'ecosistema arduino-esp32
([espressif/arduino-esp32#10895](https://github.com/espressif/arduino-esp32/issues/10895)).
Con nodi ESP32-C3 il pairing è immediato e affidabile.

**Aggiornamento del 2026-08-23 — il limite è dell'hub S3, non del chip
classico.** Provato un nodo ESP32 "classico" (DOIT DevKit v1, Xtensa D0WD)
contro un hub **ESP32-C3** (`projects/EnvNode_C3/`): pairing **immediato**,
DATA unicast consegnati, zero pacchetti persi. Quindi la raccomandazione va
letta al contrario di come era scritta: il problema sta nella combinazione con
un **hub S3**, e un ESP32 classico è un nodo perfettamente valido se l'hub è un
C3. Resta aperto se lo stesso valga per un hub S3 con le versioni attuali del
core — quella prova non è stata rifatta.

## `starters/C3_OLED_OTA/` — ESP32-C3 Supermini + OLED + OTA

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

## `starters/XIAO_S3_Camera/` — nodo camera (XIAO ESP32-S3 Sense)

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
  dall'AP a cui ci si è connessi (vedi la sezione `EspNowLink` sopra —
  l'hub va messo sullo stesso canale).
- La callback di ricezione ESP-NOW **accoda e basta** (coda FreeRTOS), i
  comandi si eseguono da `loop()`: stessa regola dei callback LVGL sull'hub.
- Ogni frame ottenuto dalla camera va **sempre** restituito
  (`camera_release()`), anche sui percorsi d'errore, o dopo pochi scatti non ci
  sono più buffer liberi.

**Dove scrivere la logica**: nel `.ino` (PIR, scatto, comandi dell'hub, nuove
periferiche in `loop()` senza bloccare a lungo). `camera.*`, `storage.*`,
`net_ota.*`, `web_ui.*`, `hub_link.*` sono boilerplate per compito.

## `projects/EnvNode_C3/` — nodo ambientale con dashboard (progetto reale)

Non è un template: è un'applicazione installata e in funzione, cresciuta dallo
starter `C3_OLED_OTA`. Vale come esempio di dove si arriva partendo da uno
starter, e i suoi moduli sono i primi candidati da riusare in un nodo nuovo.

DHT11 + microSD **SPI (modulo HW-125**, non la SDMMC della board AMOLED) + OLED
SSD1306 + dashboard web con grafici + orario NTP + OTA. Moduli:

| File | Ruolo |
|---|---|
| `EnvNode_C3.ino` | campionamento DHT11, media mobile per l'OLED, pagine OLED, `loop()` |
| `comfort.h` | indice di comfort a soglie configurabili, header-only e puro |
| `settings.h/.cpp` | parametri utente persistiti in NVS (nome nodo, intervallo, banda comfort, fuso) |
| `rtc_time.h/.cpp` | orario: stima da `__DATE__`/`__TIME__` al boot, poi NTP quando c'è rete |
| `sd_logger.h/.cpp` | CSV con rotazione giornaliera in `/logs/YYYY-MM-DD.csv`, contatori in RAM+NVS |
| `net_ota.h/.cpp` | gemello di quello del C3, variante "server condiviso": espone `net_server()` |
| `web_ui.h/.cpp` | dashboard + API JSON registrate sul WebServer di `net_ota` |
| `remote_nodes.h/.cpp` | **hub ESP-NOW**: riceve i DATA dei nodi a batteria, ne tiene lo stato in RAM e li dichiara "muti" (vedi sotto) |
| `www/dashboard.html` | dashboard personalizzata, **non compilata**: sorgente della pagina da caricare su SD via `/dashboard-upload` |

**Vincoli e scelte da conoscere**:
- **Pin**: OLED SDA=GPIO5/SCL=GPIO6, DHT11 su GPIO0, HW-125 con CS=GPIO1
  SCK=GPIO4 MISO=GPIO3 MOSI=GPIO7, tasto BOOT (GPIO9) per cambiare pagina OLED,
  LED GPIO8 come heartbeat.
- **Niente RTC tamponato** (scelta confermata: nessun DS3231): l'orologio va
  seminato prima del WiFi, altrimenti riparte dal 1970. Ordine obbligato in
  `setup()`: `rtctime_begin(tz)` → `rtctime_seedFromBuild()` → (WiFi) →
  `rtctime_onWifiConnected()`, quest'ultima **a ogni riconnessione**, non solo
  la prima. Ogni riga del CSV porta la colonna `fonte_ora` (`NTP` o `STIMA`).
- **CSV**: `ts_iso,ts_unix,fonte_ora,temp_c,hum_pct`. Ogni scrittura
  apre/scrive/chiude: un distacco di corrente perde al più una riga.
- **Contatori**: `sd_record_count_total()/today()` stanno in RAM e il totale va
  in NVS a intervalli, mai a ogni scrittura (cicli di erase). Non scansionare la
  SD per rispondere a quelle funzioni.
- **La dashboard** può essere sovrascritta caricando un `/www/dashboard.html`
  sulla SD; `/dashboard-upload` serve **sempre** la versione in PROGMEM, ed è la
  via di recupero se quella caricata a mano è rotta. Il sorgente di quella in
  uso è versionato in `www/dashboard.html`, ma è **solo** un sorgente: dopo
  averlo modificato va ri-caricato a mano da `/dashboard-upload`, perché il nodo
  serve la copia sulla SD e ricompilare il firmware non cambia nulla.
- **`web_ui` non deve** duplicare stato: legge `settings_get()`, `sd_logger.*`,
  `rtc_time.*`, `comfort_eval()` direttamente; i ganci `app_*()` implementati
  nel `.ino` coprono solo letture correnti e min/max dall'ultimo avvio.

### Ruolo secondario: hub ESP-NOW dei nodi a batteria (da `v4`, 2026-08-23)

`EnvNode_C3` è l'unica scheda di casa sempre accesa, con orologio NTP, microSD e
web UI: finché non esiste `MeteoHub_S3` fa **anche** da hub per i nodi a
batteria della stazione meteo (`remote_nodes.h/.cpp`, pagina `/nodi`, API
`/api/nodi` e `/api/pairing`). Non è una comodità: un nodo in deep sleep perde
la RAM ad ogni risveglio, quindi il suo storico **non può** stare su di lui.

- **Canale: `ESPNOW_LINK_CHANNEL_CURRENT` (0), mai un numero esplicito.** Questa
  scheda è connessa a un AP, quindi il canale lo impone il router; passare un
  numero a `Link_InitEx()` chiamerebbe `esp_wifi_set_channel()` su una STA
  connessa. Con lo 0 i peer sono registrati sul "canale corrente" e seguono
  l'AP da soli — anche se il WiFi si connette *dopo* `remote_begin()`. È il
  motivo per cui l'ESP-NOW funziona senza toccare il router. Conseguenza: **i
  nodi devono stare sul canale dell'AP**, e `/api/nodi` lo riporta apposta,
  perché un nodo che dorme senza WiFi dovrà impostarlo esplicitamente.
- **Pairing a finestra, non interruttore**, e **si riapre da sola per 5 minuti
  ad ogni avvio**, per i nodi non ancora in elenco.
- **Il registro è persistito in NVS** (namespace `envnodi`, un blob solo), e si
  salvano **soltanto MAC, tipo e nome**. Il MAC è l'identità vera: è bruciato
  nel chip e sopravvive ai riflash. Al boot i nodi vengono rimessi nel driver
  con `Link_Hub_AddPeer()`, quindi i loro DATA arrivano **anche a finestra di
  pairing chiusa** — è ciò che rende affidabile un riavvio dell'hub.
  - **I valori NON si persistono**, di proposito: dopo un riavvio l'hub
    mostrerebbe come "attuale" una lettura vecchia di giorni, cioè esattamente
    il guasto che il rilevamento del nodo muto serve a evitare. Un nodo
    ripristinato riparte da "in attesa del primo DATA" (`pacchetti` a 0) e si
    riempie al primo pacchetto vero. Nemmeno i contatori si salvano: sono "da
    quando questa scheda è accesa", e persisterli vorrebbe dire scrivere in NVS
    ad ogni pacchetto.
  - Si scrive **solo quando il registro cambia** (nodo nuovo o dimenticato):
    una manciata di scritture nella vita dell'hub.
- **"Dimentica nodo"** (`POST /api/nodi/dimentica?mac=…`, pulsante su `/nodi` e
  in dashboard) toglie il nodo da libreria, RAM e NVS. Serve perché l'identità
  è il MAC: **sostituendo una scheda, quella vecchia resterebbe in elenco per
  sempre come nodo muto**, cioè un allarme falso permanente. Attenzione al
  rovescio: se il nodo dimenticato è ancora acceso e si crede associato non
  manda più HELLO, quindi per riprenderlo serve una finestra di pairing aperta
  (o un suo riavvio).
- **"Nodo muto" con soglia osservata, non configurata**: il nodo decide la
  propria cadenza dalla sua pagina, e duplicare quel valore qui sarebbe solo un
  modo per andare fuori sincrono. Si misura l'intervallo fra un DATA e il
  successivo (media mobile, delta assurdi scartati) e si dichiara muto dopo
  ~2,5 intervalli. **Finché i nodi non hanno il partitore della batteria,
  questa è l'unica diagnostica esistente**: `battery_mv` arriva 0.
- `remote_nodes.cpp` **polla** `Link_Hub_GetPeerInfo()` da `loop()` invece di
  registrare una `Link_OnMessage()`: la concorrenza col task del driver WiFi
  sta già dentro `EspNowLink`, e così non ce n'è di nostra da sincronizzare.
- **La pagina `/nodi` è in PROGMEM e volutamente separata dalla dashboard**:
  quella vera sta sulla SD e va ricaricata a mano dopo ogni modifica, quindi
  una funzione nuova che vive nel firmware resta raggiungibile comunque. Stessa
  logica di `/dashboard-upload`. La dashboard personalizzata ha in più la card
  "Nodi remoti" (`www/dashboard.html`, caricata sulla SD il 2026-08-23), che
  rilegge `/api/nodi` **un giro su tre** — il WebServer è sincrono, e i
  contatori `nodi`/`nodi_online`/`pairing` di `/api/stato` bastano a forzare
  una rilettura immediata quando qualcosa cambia davvero.
- **Un float non finito va emesso come `null`**: `String(NAN, 2)` produce
  `"nan"`, che non è JSON valido e farebbe fallire il parse dell'*intera*
  risposta — pagina vuota per colpa di un solo sensore guasto su un solo nodo.
  E i NaN arrivano davvero, perché un nodo che non riesce a leggere il sensore
  trasmette lo stesso.

## `projects/Timelapse_XIAO/` — camera timelapse con galleria web

Cresciuto da `starters/XIAO_S3_Camera/`, stessa scheda, scatto comandato da un
**timer** invece che dal PIR:

```
timer -> scatto -> /timelapse/<AAAA-MM-GG>/<HHMMSS>.JPG  ->  galleria web
```

**Cosa cambia rispetto allo starter da cui nasce**:
- **niente PIR e niente ESP-NOW**, quindi niente `hub_link.*` e **nessuna
  dipendenza da `libraries/`**: la cartella si sposta fuori dal repo così com'è
  (a differenza del nodo camera, che si porta dietro `EspNowLink`);
- `storage.*` è riscritto: non un progressivo `IMG_%05lu` in un'unica cartella,
  ma **una cartella per giorno** e il nome = ora locale dello scatto. I nomi
  sono a lunghezza fissa con zeri iniziali, quindi ordinarli alfabeticamente
  equivale a ordinarli nel tempo — la web UI non deve leggere le date dal
  filesystem, che `SD.h` non espone in modo affidabile su tutte le versioni del
  core;
- c'è `rtc_time.*` (copia da `EnvNode_C3`, modulo puro): senza orario vero i
  nomi non significano niente. Vale lo stesso ordine obbligato in `setup()`:
  `rtctime_begin(TZ_POSIX)` → `rtctime_seedFromBuild()` → (WiFi) →
  `rtctime_onWifiConnected()`, **a ogni riconnessione** — qui la riconnessione
  la segnala `net_takeReconnectedFlag()`;
- `net_ota.*` ha in più il **watchdog di riconnessione WiFi** (ritento a 30 s,
  re-init dello stack a 5 min, riavvio a 30 min), come su `EnvNode_C3`: sta
  acceso per settimane.

**Scelte da conoscere prima di metterci le mani**:
- **Gli slot sono allineati all'orologio**, non "ultimo scatto + intervallo":
  il prossimo istante è un multiplo dell'intervallo contato sull'epoch. Il conto
  si rifà ad ogni giro, quindi il primo sync NTP (che sposta l'orologio anche di
  ore) o uno stream MJPEG lungo **non** lasciano una coda di scatti arretrati da
  recuperare tutti insieme: gli slot persi si saltano e basta (contati in
  `s_skipped`).
- **Durante lo stream non si scatta**: `app_pump()` — chiamata ad ogni frame dal
  ciclo dello stream, dove il web server è fermo — riallinea soltanto il timer.
  La camera sta già servendo frame allo stream e una scrittura su SD lo
  bloccherebbe. Come nello starter, dentro `app_pump()` **non** si chiama
  `net_loop()`.
- **Spazio esaurito**: due politiche, in NVS. `APP_FULL_STOP` (default) smette
  di scattare; `APP_FULL_RING` elimina il **giorno più vecchio** — mai quello in
  corso, o con una card piccola si finirebbe a cancellare le foto di un'ora fa.
  La soglia è in MB liberi (`min_liberi`, default 200).
- **Il CSV giornaliero registra anche gli scatti falliti** (`esito` diverso da
  `ok`): in un timelapse il buco nella sequenza è proprio ciò che si vuole
  spiegare. Colonne: `ts_iso,boot_id,fonte_ora,sorgente,file,byte,esito`.
  `sd_delete_day()` cancella le foto ma **lascia** il CSV.
- **Niente miniature sulla card**: la galleria mostra i JPEG a piena
  risoluzione rimpiccioliti dal browser (`loading=lazy`). Generarle costerebbe
  una seconda codifica per scatto e il doppio dello spazio; il prezzo è che una
  giornata da migliaia di scatti fa una pagina pesante.
- **Risoluzione e qualità non sono persistite** (a differenza di intervallo,
  finestra oraria e politica dello spazio): dopo un riavvio la camera riparte
  dai default di `camera.cpp`. Se servisse, il posto giusto è `settings_save()`
  nel `.ino`, non `camera.cpp`.
- Il **fuso orario** è una costante di compilazione (`TZ_POSIX` nel `.ino`), non
  un'impostazione da web come in `EnvNode_C3`: questa scheda non viaggia.

**Dove scrivere la logica**: nel `.ino` (timer, scatto, spazio, nuove
periferiche in `loop()` senza bloccare a lungo). `camera.*`, `storage.*`,
`rtc_time.*`, `net_ota.*`, `web_ui.*` sono boilerplate per compito. GPIO liberi
per aggiunte: D1–D5 (GPIO 2, 3, 4, 5, 6) — D8/D9/D10 e GPIO21 sono la microSD.

## Workflow SquareLine Studio (per `starters/AMOLED_1.91_LVGL/`)

1. Nuovo progetto SquareLine: risoluzione **536×240**, colore **16 bit**, LVGL
   **8.3.x** (deve combaciare con `lv_conf.h` e la libreria installata).
2. **"Export UI Files"** con percorso di export = la cartella dello sketch. Questo
   sovrascrive `ui.h`/`ui.c` e porta anche `ui_helpers.*`, `ui_events.*`, screen e
   asset.
3. **Non** usare il `.ino` né il driver TFT_eSPI generati da SquareLine: il display
   è già gestito da `AMOLED191_Display`. Si tengono solo i file `ui_*`.
4. Il corpo degli eventi "Call function" definiti in SquareLine va in `ui_events.c`
   (non viene sovrascritto ai re-export).
5. Se l'IDE non vede i file appena aggiunti dopo un export, chiuderlo e riaprirlo (la
   build cache mantiene lo stato precedente).

## Avviare un nuovo progetto da uno starter

Regola generale, valida per tutti: si **copia** lo starter (di solito in
`projects/`), si rinomina il `.ino` come la cartella (vincolo Arduino), si
compila e carica **prima** di toccare qualsiasi cosa, poi si entra nel merito.

Partendo da `starters/AMOLED_1.91_LVGL/` (board AMOLED):

1. Copiare la cartella in `projects/MioProgetto/` e rinominare lo sketch in
   `MioProgetto.ino`.
2. **Portabilità delle librerie**: se resta dentro questo repo non serve altro —
   condivide già `libraries/` alla radice. Se invece diventa un progetto/repo
   indipendente altrove sul disco, copia (o crea una junction verso) anche la
   cartella `libraries/` accanto ad esso: senza, non compila, perché
   `AMOLED191_Display`/`AMOLED191_Touch`/`AMOLED191_IMU`/`AMOLED191_Core` non
   sono incluse dentro la cartella dello sketch.
3. Compilare e caricare così com'è, per confermare che display/touch/LVGL
   funzionino (compare "Starter pronto" centrato).
4. Poi procedere con l'export SquareLine come sopra.

Partendo da `starters/XIAO_S3_Camera/` (nodo camera):

1. Copiare la cartella e rinominare il `.ino`. Se la copia resta dentro il repo
   non serve altro; se esce, portarsi dietro anche `libraries/EspNowLink`.
2. Copiare `secrets.h.example` in `secrets.h` e riempirlo. La regola di
   `.gitignore` è un pattern (`secrets.h`), quindi copre anche le copie nuove,
   dentro e fuori dal repo.
3. Cambiare `NODE_NAME` nel `.ino`: è il nome con cui il nodo si presenta
   all'hub, e due nodi con lo stesso nome sono indistinguibili nella lista.
4. Caricare via USB la prima volta, poi si aggiorna via OTA.

Partendo da `starters/C3_OLED_OTA/` (nodo C3):

1. Copiare la cartella e rinominare il `.ino`. Nessuna dipendenza da
   `libraries/` da portarsi dietro: lo starter è self-contained, si può spostare
   ovunque.
2. Copiare `secrets.h.example` in `secrets.h` e riempirlo.
3. Caricare via USB la prima volta, poi si aggiorna via OTA.
4. Per un nodo che deve anche loggare e mostrare grafici, guardare prima
   `projects/EnvNode_C3/`: quella strada è già stata fatta.

## Hardware: vincoli di pinout (scheda Waveshare ESP32-S3-Touch-AMOLED-1.91)

- **Display QSPI**: CS=GPIO6, PCLK=GPIO47, DATA0-3=GPIO18/7/48/5, RST=GPIO17.
- **microSD**: dipende dalla revisione della scheda, e le due non sono
  intercambiabili (l'esempio ufficiale Waveshare `04_SD_Card` le seleziona a
  compile-time con `#ifdef VersionControl_V2`, non c'è modo di distinguerle a
  runtime):
  - **V2** (schede attuali, quello che implementa `AMOLED191_SD`): **SDMMC a 1 bit**,
    CLK=GPIO9, CMD=GPIO42, D0=GPIO8. Nessun pin in comune col bus QSPI del
    pannello → SD e LVGL convivono senza arbitraggio e senza lock.
  - **V1** (schede vecchie): SD su SPI3_HOST con CLK=**GPIO47**, cioè lo stesso
    pin del PCLK del display → i due sono fisicamente sullo stesso clock e
    servirebbe condividere l'host SPI2. Non supportata.
  Attenzione: su V2 il GPIO9 porta anche il **TE** del pannello — non abilitare
  il comando 0x35, o entra in conflitto col clock della card. Schede ≤ 64 GB,
  FAT32 (le exFAT non montano; `AMOLED191_SD` non le formatta di sua iniziativa).
- **I2C condiviso** tra touch FT3168 (addr `0x38`) e IMU onboard QMI8658
  (addr `0x6B`, fallback `0x6A`): SDA=GPIO40, SCL=GPIO39. `Core_I2CBusInit()`
  (libreria `AMOLED191_Core`) installa il driver I2C in modo idempotente: sia
  `Touch_Init()` (`AMOLED191_Touch`) sia `imu_init()` (`AMOLED191_IMU`) la chiamano
  internamente, quindi funzionano in qualunque ordine, anche uno senza l'altro.
- **GPIO liberi** per periferiche custom: 2, 3, 4, 10–16, 21, 38. Evitare 26 e 33–37
  (riservati alla PSRAM octal).

## Dove scrivere la logica applicativa

- Eventi dei widget SquareLine → `ui_events.c`.
- Aggiornamenti UI da `loop()`/task sensori/callback WiFi → sempre dentro
  `lvgl_lock(-1)` … `lvgl_unlock()`.
- Dentro una callback di evento LVGL → niente lock (già preso), callback corta,
  lavoro lento deferito.
