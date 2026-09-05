# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Questo file è l'indice, non la documentazione completa.** Qui c'è quello che
vale ovunque: com'è organizzato il repo, come si compila ogni scheda, e le
regole che un guasto su hardware ha già fatto pagare. La guida di ogni progetto,
starter e libreria vive **accanto al codice**, in un `CLAUDE.md` dentro la sua
cartella, e si carica quando si lavora su quei file — l'elenco è nella tabella
qui sotto. Se stai per mettere mano a un progetto e non hai il suo `CLAUDE.md`
in contesto, **aprilo prima**: contiene i motivi delle scelte che stai per
toccare.

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
gira su qualunque ESP32. Il chip è un attributo documentato per progetto — sta nella descrizione di
ogni riga della tabella qui sotto, e in `README.md` — non una cartella.

Per il dettaglio file-per-file (scopo, funzioni chiave, dipendenze, cosa non
toccare) vedi `docs/FILES.md`. Per il pinout/hardware della board AMOLED vedi
`docs/ESP32-S3-AMOLED-1.91-Guide.md`.

## Struttura

| Percorso | Ruolo | Guida |
|---|---|---|
| `libraries/` | librerie Arduino condivise (bus/display/touch/IMU/SD/comunicazione) | sezione qui sotto |
| `libraries/EspNowLink/` | protocollo ESP-NOW hub↔nodi, identico sui due lati del collegamento | [`libraries/EspNowLink/CLAUDE.md`](libraries/EspNowLink/CLAUDE.md) |
| `starters/AMOLED_1.91_LVGL/` | template LVGL (SquareLine Studio) sulla Waveshare **ESP32-S3-Touch-AMOLED-1.91** (AMOLED 536×240 SH8601, touch FT3168, IMU QMI8658) | [`starters/AMOLED_1.91_LVGL/CLAUDE.md`](starters/AMOLED_1.91_LVGL/CLAUDE.md) |
| `starters/C3_OLED_OTA/` | template **ESP32-C3 Supermini** + OLED SSD1306 I2C + **OTA**, self-contained (non usa `libraries/`) | [`starters/C3_OLED_OTA/CLAUDE.md`](starters/C3_OLED_OTA/CLAUDE.md) |
| `starters/XIAO_S3_Camera/` | template nodo camera **Seeed XIAO ESP32-S3 Sense**: PIR → foto su microSD → notifica ESP-NOW, web UI, OTA | [`starters/XIAO_S3_Camera/CLAUDE.md`](starters/XIAO_S3_Camera/CLAUDE.md) |
| `projects/MeteoHub_S3/` | **progetto** (XIAO ESP32-S3 Sense): hub della stazione meteo — nodi via ESP-NOW, pannello **e-ink WeAct 4.2"** (SSD1683), CSV su microSD, NTP, web UI, OTA | [`projects/MeteoHub_S3/CLAUDE.md`](projects/MeteoHub_S3/CLAUDE.md) |
| `projects/MeteoNode_C3/` | **progetto** (XIAO ESP32-C3, e lo stesso sketch su ESP32 "classico"): AHT20 + BMP280, trend barometrico, nodo ESP-NOW, **deep sleep** | [`projects/MeteoNode_C3/CLAUDE.md`](projects/MeteoNode_C3/CLAUDE.md) |
| `projects/Timelapse_XIAO/` | **progetto** (XIAO ESP32-S3 Sense): camera timelapse a intervallo, archivio per giorno su microSD, galleria web, NTP + OTA | [`projects/Timelapse_XIAO/CLAUDE.md`](projects/Timelapse_XIAO/CLAUDE.md) |
| `projects/EnvNode_C3/` | **hardware smantellato il 2026-08-31** (ESP32-C3): DHT11 + microSD SPI + dashboard + hub ESP-NOW. Resta come riferimento: è la copia da cui sono nati `remote_nodes`, `sd_logger`, `rtc_time`, `forecast.h` | [`projects/EnvNode_C3/CLAUDE.md`](projects/EnvNode_C3/CLAUDE.md) |
| `examples/Orientation_IMU/` | demo autosufficiente: livella per veicolo basata sull'IMU onboard, UI costruita in codice (non SquareLine) | — |
| `examples/Link_Hub_Demo/` | demo lato hub di `EspNowLink`: schermo AMOLED, pairing/lista nodi associati | — |
| `examples/Link_Node_Demo/` | demo nodo sensore finto: solo Serial, nessuna dipendenza dai pin AMOLED, gira su qualunque board ESP32 | — |
| `examples/DHT11_SD_Logger/` | demo: DHT11 su un GPIO libero, valori a schermo e log CSV ogni 60 s sulla microSD onboard (colonne `boot_id,n,secondi_da_accensione,temperatura_C,umidita_pct`) | — |
| `examples/Diag_Hub/` + `examples/Diag_Node/` | diagnostica ESP-NOW usa e getta su `esp_now.h` grezzo (nessuna libreria di questo repo): il nodo spara un contatore in broadcast, l'hub conta i buchi nel `seq`. In Long Range, quindi **non** interoperabili con `EspNowLink` | — |

I documenti in `docs/`:

| File | Ruolo |
|---|---|
| `docs/Trappole-Hardware.md` | **le sei lezioni pagate su hardware** (USB CDC, OTA, `streamFile()`, scritture su SD, default NVS, deep sleep): il riassunto è più sotto, qui c'è il perché lungo e il sintomo |
| `docs/FILES.md` | reference file-per-file di tutto il repo |
| `docs/Feature-Backlog.md` | **il taccuino delle cose da fare**: idee pronte, idee vecchie raccolte dagli altri documenti, e quelle valutate e scartate col perché. Da qui si pesca quando c'è voglia di aggiungere qualcosa |
| `docs/Stazione-Meteo.md` | il diario di lavoro della stazione meteo: ogni fase, cosa è stato misurato, cosa è andato storto |
| `docs/Proposte-2026-09-02.md` | analisi sistematica del codice di hub e nodi (`MeteoHub_S3` `v43`, `MeteoNode_C3` `v15`, `EspNowLink`) fatta partendo dai sorgenti invece che dalle idee: cinque difetti trovati, le proposte con conti e prove, l'ordine consigliato. Le voci sono **riassunte una riga l'una in `Feature-Backlog.md` (17-40)**, che resta il taccuino da cui si pesca; qui c'è il perché lungo |
| `docs/ESP32-S3-AMOLED-1.91-Guide.md` | guida hardware/pinout della board AMOLED |
| `docs/*.pdf` | datasheet/reference (ESP32-S3, SH8601/RM67162, QMI8658, guida LVGL+SquareLine) — consultarli per dettagli di registro/timing, non riscriverne il contenuto nel codice |

**Perché la documentazione è spezzata così**: un `CLAUDE.md` per cartella si
carica solo quando si lavora lì, quindi il costo di contesto lo paga chi lo usa
— l'hub e-ink da solo sono 56 kB che non servono a chi tocca il nodo camera.
Il rovescio: quel testo **non** è in contesto se la domanda è generica, ed è il
motivo per cui la tabella qui sopra li linka uno per uno. Chi documenta una
cosa nuova la scrive nella cartella a cui appartiene; qui va solo ciò che vale
per tutte le schede.

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
| `EspNowLink` | comunicazione ESP-NOW hub↔nodi, indipendente da LVGL/display e da una scheda specifica | `Link_Init()`, `Link_OnMessage()`, `Link_Node_*`, `Link_Hub_*` — protocollo, pairing e trappole in [`libraries/EspNowLink/CLAUDE.md`](libraries/EspNowLink/CLAUDE.md) |

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


## Regole che valgono su tutte le schede

Sei difetti trovati su hardware vero, ognuno costato da mezza giornata a una
serata. Qui c'è la regola in tre righe; il perché lungo, il sintomo e — la
parte che conta — **perché il sintomo non somiglia alla causa** stanno in
[`docs/Trappole-Hardware.md`](docs/Trappole-Hardware.md), da leggere **prima**
di mettere le mani su deep sleep, OTA, invio di file o logging su SD.

- **`Serial.setTxTimeoutMs(0)` dopo ogni `Serial.begin()`** (dentro
  `#if ARDUINO_USB_CDC_ON_BOOT`), su C3 e S3. Senza, con la porta riconosciuta
  dal PC e nessuno che legge, ogni `print()` **blocca** il `loop()` — e con lui
  web server, OTA, sensori e timer. Il segnale da riconoscere: «da seriale va,
  da rete no». Va messo in **ogni sketch nuovo** per queste board.
- **`Update.abort()` nel ramo `UPLOAD_FILE_ABORTED`** di `net_ota.cpp`: senza,
  dopo il primo upload caduto a metà la scheda **non si aggiorna più via rete**,
  in silenzio, e l'unica uscita è riavviarla di persona. Il segnale: il
  trasferimento arriva al 100% e solo lì risponde 500.
- **Mai `streamFile()` in un handler nuovo — usare `streamFileLimitato()`.** Il
  core ignora il valore di ritorno di `write()`, quindi un client che smette di
  dare ACK **senza chiudere il socket** (telefono che si addormenta, coperchio
  chiuso) tiene fermo il `loop()` per minuti. In quel buco i DATA dei nodi non
  vengono prelevati dal driver, che ne tiene **uno solo**: un client andato via
  a metà scaricamento fa un buco nei dati di tutta la rete.
- **Una scrittura su microSD che nessuno controlla è un logger che mente.**
  `File::write()` non alza il writeError e torna 0 su card piena o sfilata:
  sommare i byte scritti dalle `print()` e tornare `false` se sono zero, o il
  contatore sale mentre il file non cresce.
- **Un default nuovo vince su una chiave NVS mai scritta.** Cambiare un
  `*_DEFAULT` cambia il comportamento delle schede in funzione che quel
  parametro non l'hanno mai salvato, e da fuori le due cose sono
  indistinguibili. Dopo un OTA: rileggere `/api/stato` e **riscrivere
  esplicitamente** i valori che devono restare come sono.
- **Deep sleep: quattro trappole** — il `seq` di `EspNowLink` che deve
  attraversare il sonno, il cavo USB che impedisce di osservarlo, l'alimentatore
  che si autospegne sotto carico, e il GPIO che alimenta un sensore e torna
  flottante (serve `gpio_hold_en()` + `gpio_deep_sleep_hold_en()`, e il rilascio
  al risveglio). Le prime tre si vedono nei dati, la quarta no.

**Ordine di spegnimento prima di dormire** (preso da uno sketch già validato su
hardware, non inventato): `esp_now_deinit()` → `esp_wifi_stop()` →
`esp_wifi_deinit()` → `delay(100)` → `esp_sleep_enable_timer_wakeup()` →
`esp_deep_sleep_start()`.

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

**Sulla XIAO ESP32-C3 quel flag è però invertito**, esattamente come sulla
XIAO S3 e al contrario del C3 generico qui sopra. Lo dice `arduino-cli board
details --fqbn esp32:esp32:XIAO_ESP32C3`: `CDCOnBoot=default` → *Enabled* (ed è
il default della board), `CDCOnBoot=cdc` → *Disabled*. `projects/MeteoNode_C3/`
si compila quindi **senza** quel flag:

```
arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32C3:PartitionScheme=min_spiffs" --libraries libraries projects/MeteoNode_C3
```

Aggiungerlo "per avere la Serial sull'USB" ottiene l'opposto: la `Serial`
finisce su UART0 e sull'USB resta solo il log della ROM. Trovato il 2026-08-23
mentre si cercava di leggere i log di un deep sleep — e per un pezzo è sembrato
che lo sketch non partisse affatto.


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

Più le librerie locali in `libraries/` — vedi la sezione `libraries/` più sopra.

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
4. Poi procedere con l'export SquareLine — il workflow è in
   [`starters/AMOLED_1.91_LVGL/CLAUDE.md`](starters/AMOLED_1.91_LVGL/CLAUDE.md).

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
