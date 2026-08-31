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
| `projects/EnvNode_C3/` | **progetto reale, ma la scheda è stata smantellata il 2026-08-31** (ESP32-C3): DHT11 + microSD SPI + dashboard web con grafici + orario NTP + OTA, e da `v4` anche **hub ESP-NOW** dei nodi a batteria. Resta come riferimento e come base da cui ripartire — vedi sezione dedicata |
| `projects/MeteoNode_C3/` | **progetto** (XIAO ESP32-C3, e lo stesso sketch anche su ESP32 "classico"): AHT20 + BMP280, previsione dal trend barometrico, storico 24 h in RAM, nodo ESP-NOW, e da `v9` **deep sleep** fra una misura e l'altra — vedi sezione dedicata |
| `projects/MeteoNode_C3/MeteoNode_C3.ino` | misura, previsione, ciclo di sonno e risveglio — qui va la logica applicativa |
| `projects/MeteoNode_C3/forecast.h` | trend barometrico a 3 ore con isteresi, header-only e puro |
| `projects/MeteoNode_C3/hub_link.h/.cpp` | nodo ESP-NOW sopra `EspNowLink`: canale, ripresa dell'hub dopo il sonno, invio delle misure |
| `projects/MeteoNode_C3/rtc_time.h/.cpp` | copia di quello di `EnvNode_C3`: stima da build-time, poi NTP |
| `projects/MeteoNode_C3/net_ota.h/.cpp` | WiFi + ArduinoOTA + `/update`, con watchdog di riconnessione — di norma non si tocca |
| `projects/MeteoNode_C3/web_ui.h/.cpp` | pagina di stato con grafici SVG, comandi e interruttore del deep sleep |
| `projects/MeteoHub_S3/` | **progetto** (XIAO ESP32-S3 Sense): hub della stazione meteo — riceve i nodi via ESP-NOW, li mostra su un pannello **e-ink WeAct 4.2\"** (SSD1683) e ne registra i CSV su microSD, con orario NTP, web UI e OTA — vedi sezione dedicata |
| `projects/MeteoHub_S3/MeteoHub_S3.ino` | pagine del pannello, tasto BOOT a due gesti, hub ESP-NOW, logging dei nodi — qui va la logica applicativa |
| `projects/MeteoHub_S3/pages.h/.cpp` | il modello delle pagine del pannello: elenco, rotazione, ore di silenzio, in NVS — non conosce il display |
| `projects/MeteoHub_S3/messages.h/.cpp` | il messaggio sul pannello: attivo in NVS, archivio su SD |
| `projects/MeteoHub_S3/remote_nodes.h/.cpp` | copia da `EnvNode_C3`: registro nodi, cadenza appresa, nodo muto, trend, NVS |
| `projects/MeteoHub_S3/sd_logger.h/.cpp` | copia da `EnvNode_C3` adattata alla microSD **SPI della Sense** (CS 21, bus condiviso con l'e-ink) |
| `projects/MeteoHub_S3/net_ota.h/.cpp` | WiFi + ArduinoOTA + `/update`, variante con `net_server()` condiviso |
| `projects/MeteoHub_S3/web_ui.h/.cpp` | pagina di stato dell'hub + API dei nodi, gli stessi endpoint di `EnvNode_C3` |
| `projects/MeteoHub_S3/secrets.h.example` | credenziali: si copia in `secrets.h`, **gitignorato** |
| `projects/MeteoHub_S3/www/dashboard.html` | dashboard personalizzata dell'hub: confronto fra nodi, storico dai CSV, pressione/trend, salute della rete. **Non compilata**: si carica sulla card da `/dashboard-upload` |
| `projects/MeteoHub_S3/www/dither.html` | ritaglio + dithering nel browser: produce i `.bin` da 15.000 byte e li manda all'hub. Da `v8` e' anche **servita dalla scheda** su `/immagini`, via `dither_page.h` generato con `www/gen_page.py` |
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
| `docs/Feature-Backlog.md` | **il taccuino delle cose da fare**: idee pronte, idee vecchie raccolte dagli altri documenti, e quelle valutate e scartate col perché. Da qui si pesca quando c'è voglia di aggiungere qualcosa |
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


### Deep sleep — quattro trappole trovate su hardware

Implementato per la prima volta il 2026-08-23 su `projects/MeteoNode_C3/`
(`v9`): il nodo si sveglia a timer, misura, manda un DATA ESP-NOW e torna a
dormire, senza mai accendere il WiFi. Le tre cose che sono costate una serata,
più una quarta che non è costata niente — e proprio per questo è la peggiore da
riconoscere:

**1. Il `seq` di `EspNowLink` deve attraversare il sonno.** È la peggiore, e ha
una sezione sua più sotto (vedi `EspNowLink`): il contatore vive in RAM, quindi
ogni risveglio ripartiva da `seq=0` e l'hub — che scarta un DATA con `seq`
uguale all'ultimo visto — ne accettava **uno** e ignorava tutti i successivi.

**2. Con il cavo USB attaccato il deep sleep non si può osservare.** Al
risveglio la porta CDC si riconnette e l'enumerazione dell'host resetta il
chip: nel log di boot si legge `rst:0x15 (USB_UART_CHIP_RESET)` invece di
`rst:0x5 (DSLEEP)`. Il nodo non completa mai un vero risveglio, riparte dal
percorso normale, e i tempi che si misurano sono quelli della veglia, non del
sonno. **Lo strumento con cui vorresti guardare il deep sleep è la cosa che te
lo impedisce** — stessa forma del monitor seriale in `setTxTimeoutMs(0)` qui
sopra. Per i log durante il sonno serve UART0 su un adattatore, non l'USB
nativo.

**3. Un alimentatore con auto-spegnimento taglia la corrente mentre il nodo
dorme.** Power bank e caricatori multipli si spengono sotto una soglia di
carico (50-100 mA); un C3 in deep sleep sta a decine di µA. Il nodo non si
risveglia più e sembra un firmware rotto — sul caricatore si vede l'uscita che
smette di segnare mentre le altre continuano. Rimedio da laboratorio: un carico
fittizio (è bastato un hub USB con un LED sempre acceso); rimedio vero: la
batteria sui pin BAT, che è comunque la destinazione del progetto.

**Come si distingue "non esegue codice" da "esegue e sbaglia"**: tenere
un'uscita di sicurezza che dopo N risvegli senza consegna riaccende WiFi e OTA
e riporta il nodo raggiungibile. Se non scatta mai, il codice non sta girando —
e si smette di cercare il guasto nella radio. E tenere i contatori dei risvegli
in **NVS, non in RTC memory**: la RTC memory la cancella il power-cycle, cioè
proprio l'operazione con cui si recupera un nodo che non torna, quindi la prova
sparisce esattamente quando serve. È stato il contatore `risvegli=19,
consegnati=19` a spostare il sospetto dal nodo all'hub.

**4. Un GPIO che alimenta qualcosa torna flottante nel sonno, e nessun dato lo
dice.** Se il VCC di un sensore passa da un pin (`MeteoNode_C3` lo fa apposta,
per poterlo power-ciclare), portarlo LOW prima di dormire **non basta**:
entrando nel deep sleep il pin perde lo stato di uscita, e attraverso i diodi
di protezione dei piedini il modulo resta "mezzo acceso". Serve
`gpio_hold_en((gpio_num_t)PIN)` + `gpio_deep_sleep_hold_en()` subito prima di
dormire, e il pin dev'essere **RTC-capable** (GPIO0-5 sul C3) perché l'hold lo
tiene il dominio RTC.

**E al risveglio l'hold va RILASCIATO** con `gpio_hold_dis()` +
`gpio_deep_sleep_hold_dis()` *prima* di ripilotare il pin, altrimenti
`pinMode()`/`digitalWrite()` non hanno alcun effetto e il sensore non si
riaccende più — che da fuori somiglia a una saldatura fredda. Va fatto nel
percorso di risveglio, non nel `setup()`: il `setup()` il risveglio non lo
esegue.

Perché è la trappola peggiore: **le altre tre si vedono nei dati** (pacchetti
mancanti, risvegli che non arrivano), questa no. Il nodo dorme, si sveglia,
misura bene e consegna tutto. `MeteoNode_C3` ha girato così per **20,4 ore, 1212
pacchetti, zero persi** e sembrava perfetto; il costo esce solo da un
amperometro in serie o da un'autonomia più corta del previsto. Corretto in `v10`
il 2026-08-24.

**Ordine di spegnimento prima di dormire** (preso da uno sketch già validato su
hardware, non inventato qui): `esp_now_deinit()` → `esp_wifi_stop()` →
`esp_wifi_deinit()` → `delay(100)` → `esp_sleep_enable_timer_wakeup()` →
`esp_deep_sleep_start()`.

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

### `streamFile()` e le risposte grosse — un client morto ferma la scheda

`WebServer::streamFile()` finisce in `NetworkClient::write(Stream&)`, che nel
core 3.3.10 e' scritto cosi':

```cpp
while (available) {
  toRead  = (available > 1360) ? 1360 : available;
  toWrite = stream.readBytes(buf, toRead);
  written += write(buf, toWrite);   // <-- il valore di ritorno NON viene guardato
  available = stream.available();
}
```

Due difetti che si sommano: il ritorno della `write()` e' ignorato, quindi il
ciclo prosegue fino a fine file anche se il client non prende piu' un byte; e
ogni `write()` aspetta che il socket torni scrivibile con dieci `select()` da
un secondo l'uno (`WIFI_CLIENT_MAX_WRITE_RETRY` x
`WIFI_CLIENT_SELECT_TIMEOUT_US`). Un client che smette di dare ACK **senza
chiudere il socket** — telefono che si addormenta, WiFi che cade, coperchio del
portatile — tiene quindi `loop()` dentro l'handler finche' non e' lo stack TCP
a rinunciare al peer. Sono minuti, e non e' un caso limite: e' il modo normale
in cui muore una pagina lasciata aperta.

**Il guasto non somiglia alla sua causa, e punta lontano da se stesso.** Su
`EnvNode_C3` il 2026-08-24 alle 21:00:46 la scheda e' rimasta ferma **456 s**
(misurati: buco nel suo CSV locale, con `uptime` che esclude un riavvio). In
quella finestra non ha campionato il DHT11 **e non ha chiamato
`remote_loop()`**, quindi i DATA dei nodi ESP-NOW arrivavano alla radio e
nessuno li prelevava dal driver, che tiene solo l'ultimo: sei pacchetti di un
nodo e uno dell'altro persi. Nei log sembravano perdite radio, e la caccia
sarebbe partita da li'. **Un client andato via a meta' scaricamento fa un buco
nei dati di tutta la rete.**

L'endpoint piu' esposto non e' il download che si chiede a mano: e' quello che
la dashboard chiama **da sola** (`/api/giorno`), che in chunked encoding faceva
una `sendContent()` per riga — tre `write()` sul socket ogni ~25 byte di dati,
~4200 write per un giorno.

**Rimedio** (in `EnvNode_C3/web_ui.cpp`, `streamFileLimitato()` e
`giornoFlush()`): ci si ferma al primo chunk che il client non accetta per
intero — il controllo che manca al core — e comunque a fine budget
(`INVIO_BUDGET_MS`, 20 s: su una LAN un giorno di CSV vola). Dove si usa
`sendContent()`, che non dice quanto ha scritto, il client morto si riconosce
dal **tempo**, non dal ritorno. Il costo residuo e' UNA write bloccata (~10 s):
quel numero sta dentro il core e da li' non si abbassa.

**Attenzione**: se la risposta si interrompe, il JSON resta **tronco e non si
chiude**. E' deliberato — un array chiuso a meta' verrebbe letto come un giorno
con meno dati, cioe' un grafico sbagliato che sembra giusto; cosi' invece il
parse fallisce e si vede un errore, che e' la verita'.

**Corretto ovunque dal 2026-08-30**: `projects/Timelapse_XIAO/web_ui.cpp`
(foto e CSV) e `starters/XIAO_S3_Camera/web_ui.cpp` (foto) hanno ora lo stesso
`streamFileLimitato()`. Li' il difetto era **peggio**, non uguale: una foto da
300 kB sono 220 chunk, e la galleria ne carica decine per volta. Entrambi
espongono `invii_interrotti` su `/api/stato` — senza quel contatore il taglio
sarebbe invisibile, e il sintomo (scatti mancanti, PIR che sembra non
funzionare) punterebbe di nuovo lontano dalla causa.

**Se si scrive un handler nuovo che manda un file, usare `streamFileLimitato()`,
mai `streamFile()`.**

### Una scrittura su microSD che nessuno controlla e' un logger che mente

`File::write()` del core **non alza il writeError**: si limita a ritornare i
byte scritti. Quindi una `print()` su una card piena, sfilata o in errore torna
**0 senza lanciare niente**, e una funzione che non ne guarda il ritorno
risponde `true` lo stesso.

Il guasto che ne segue e' della famiglia peggiore: **il contatore sale e il file
non cresce**. Su `MeteoHub_S3` quel contatore (`righe_scritte`) e' proprio
quello che si usa per il controllo incrociato con i pacchetti dei nodi, quindi
la bugia toglieva valore all'unica verifica automatica che la rete ha — e la
lettura di un nodo a batteria, che vive **solo** nel CSV dell'hub, sarebbe
sparita mentre tutto diceva di andare bene.

Corretto il 2026-08-30 in `sd_log_sample()` e `sd_log_remote()` di
`projects/EnvNode_C3/` e `projects/MeteoHub_S3/`: si somma il ritorno delle
`print()` e si torna `false` se e' zero, cosi' i contatori non si muovono e
`sd_last_error()` lo dice.

**Il pattern giusto era gia' nel repo**: `sd_save_photo()` di
`projects/Timelapse_XIAO/storage.cpp` confrontava da sempre i byte scritti con
quelli attesi, e in piu' **cancella il file troncato** — meglio nessuna foto di
un JPEG a meta'. Era una delle copie ad avere ragione e le altre a non saperlo:
il rischio vero di tenere moduli gemelli allineati a mano.

### Aggiornare un nodo: un default nuovo vince su una chiave NVS mai scritta

Tutti gli sketch di questo repo tengono la configurazione con `Preferences`
(NVS) letta **con i default del firmware**:

```cpp
s_intervalloS = p.getULong("intervallo", INTERVALLO_DEFAULT_S);
```

Se quella chiave **non è mai stata scritta** — perché nessuno ha mai toccato
quel parametro dalla pagina — il valore in uso è il default del codice. Un
aggiornamento che cambia il default **cambia quindi il comportamento della
scheda senza che nessuno abbia chiesto niente**, e solo per i parametri che
l'utente non aveva mai impostato: quelli scritti davvero sopravvivono.

**Da fuori le due cose sono indistinguibili.** `/api/stato` riporta
`intervallo_s: 60` sia quando quel 60 è una scelta salvata, sia quando è solo
il default di quel firmware; non c'è modo, via rete, di sapere quali
impostazioni siano davvero in NVS — cioè quali sopravvivranno al prossimo OTA.

Trovato il 2026-08-26 aggiornando `MeteoNode_C3` da `v5` a `v11` sul nodo a
muro: l'intervallo di misura è passato da 60 a 300 s da solo, perché fra `v5` e
`v10` è cambiato `INTERVALLO_DEFAULT_S` (300 s è la cadenza pensata per il nodo
a **batteria**). L'altitudine, impostata davvero dalla pagina, è invece rimasta.

**Regola operativa**: dopo un OTA su una scheda in funzione, rileggere
`/api/stato` e **riscrivere esplicitamente** i parametri che devono restare come
sono — la riscrittura crea la chiave e mette il valore al riparo dai salti di
versione successivi. Vale la pena farlo anche quando il valore *sembra* giusto:
è l'unico modo di trasformare un default in una scelta.

Dove pesa di più, oltre all'intervallo: `sleep` in `projects/MeteoNode_C3/`
(un default cambiato metterebbe a dormire un nodo alimentato da USB, o terrebbe
sveglio uno a batteria) e `attivo` in `projects/Timelapse_XIAO/`, che oggi ha
default `true`: se diventasse `false`, una scheda che non l'ha mai salvato
smetterebbe di scattare dopo un aggiornamento, in silenzio. Lo stesso schema è
in `projects/EnvNode_C3/settings.cpp` (nome, intervallo, banda comfort, fuso).

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

Presente in **tutti** gli sketch del repo dal 2026-08-30: prima mancava nei
sei `examples/` e in `starters/AMOLED_1.91_LVGL/`, che sono i piu' esposti
proprio perche' si usano col monitor aperto — e il caso cattivo non e' "non
c'e' mai stato un monitor" ma "c'e' stato e se n'e' andato". `DHT11_SD_Logger`
era il piu' a rischio: resta acceso a registrare per ore.

Va messo in ogni sketch nuovo per queste board.

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

**Il `seq` è anche un filtro anti-doppioni, e con un nodo che dorme diventa una
trappola.** L'hub scarta un DATA il cui `seq` è **uguale** all'ultimo visto
(`remote_nodes.cpp`: `if (r->hasData && dato.seq == r->seq) continue;`). Il
contatore però vive in RAM dentro `link_node.cpp`, quindi un nodo che si
risveglia da deep sleep riparte da zero ad ogni ciclo: il primo DATA passa
(zero è diverso dall'ultimo seq della veglia) e **tutti i successivi vengono
buttati come doppioni**. Per questo esistono `Link_Node_SetSeq()` /
`Link_Node_GetSeq()`: il nodo conserva il seq in RTC memory e lo rimette dopo
ogni risveglio.

Il guasto è **silenzioso da entrambe le parti**, come quello del registro peer
qui sopra e per lo stesso motivo: l'ACK di ESP-NOW è di livello radio e arriva
comunque, quindi il nodo conta i propri invii come riusciti mentre l'hub non
mostra niente. Misurato il 2026-08-23: **19 risvegli, 19 invii confermati dal
nodo, uno solo visto dall'hub**. Si è cercato il guasto nel timer, nella radio,
nell'alimentazione e nell'USB; era una riga di deduplica.

**`Link_Node_ResumeWithHub(mac)`** serve allo stesso scenario: un nodo che si
sveglia non sa più di essere associato e rifarebbe il pairing (un HELLO ogni
2 s, più l'attesa che l'hub accodi il WELCOME dal suo `loop()`), che su un nodo
sveglio pochi secondi per volta è la parte più lunga e più incerta del ciclo,
tutta a radio accesa. Conservando il MAC dell'hub si registra il peer e si passa
dritti al DATA.

**Il canale si puo' cambiare a caldo** (da `v12` del nodo meteo, 2026-08-27):
`Link_SetChannel(ch)` sposta la radio **e riallinea i peer gia' registrati**
(itera il registro del driver con `esp_now_fetch_peer`/`esp_now_mod_peer`, cosi'
non deve sapere se gira su un hub o su un nodo). Prima il canale si sceglieva
solo a `Link_InitEx()`.

**Vietata a chi e' connesso a un access point**: la radio e' una sola e il
canale lo detta l'AP, quindi cambiarlo fa cadere la connessione. Serve a un
nodo che sta su ESP-NOW e basta — tipicamente uno a batteria, con il WiFi
spento, che si e' portato il canale in RTC memory.

Insieme c'e' `Link_Node_ResendLast(tentativi, timeout)`, che rimanda l'ultimo
DATA **senza incrementare il `seq`**. Non e' un dettaglio: `Link_Node_SendData()`
il seq lo incrementa ad ogni chiamata, quindi riprovare lo stesso campione su
piu' canali produrrebbe salti di numerazione, e l'hub li conta come pacchetti
persi sulla tratta radio — **buchi inventati dentro il registro che serve
proprio a contare i buchi veri**.

**Spostare un nodo da un hub a un altro: prima lo si DIMENTICA sul vecchio.**
Un hub che ha gia' il nodo nel registro gli rimanda il WELCOME anche a finestra
di pairing **chiusa** (`link_peer.cpp`: un HELLO da un peer noto mette
`welcomePending = true`, senza guardare il pairing). E' deliberato — serve a
non lasciare bloccato un nodo che si e' riavviato — ma vuol dire che a un HELLO
in broadcast rispondono **tutti** gli hub che quel nodo lo conoscono, e se lo
prende il primo che risponde.

Ordine giusto: **1)** `POST /api/nodi/dimentica?mac=…` sul vecchio hub,
**2)** finestra di pairing aperta sul nuovo, **3)** power-cycle del nodo. Il
power-cycle non e' evitabile: il MAC dell'hub sta in RTC memory e finche' e' li'
il nodo riprende con quello senza mandare HELLO. Un riavvio software non basta,
perche' la RTC memory sopravvive.

**Sbagliare l'ordine da' un guasto silenzioso e simmetrico**, osservato il
2026-08-27 spostando `MeteoNode` da `EnvNode_C3` a `MeteoHub_S3`: il nodo torna
sul vecchio hub, mentre il nuovo — che era in pairing — se lo mette in elenco e
resta a `pacchetti: 0`. Da un lato una lista con un nodo che non parla,
dall'altro un nodo convinto di essere associato: **nessuno dei due dice che
sta parlando con qualcun altro**, e la lettura giusta viene solo dal campo
`espnow_hub` del nodo confrontato col MAC dell'hub che ci si aspetta.

**Due punti di robustezza sistemati il 2026-08-30**, entrambi invisibili
nell'uso normale:

- **il registro dei peer aveva una finestra di race**. `hub_insert_peer()`
  controlla "pieno o gia' presente" e poi inserisce, ma fra le due cose il lock
  si rilascia per forza: `new` e `addPeer()` allocano memoria e chiamano il
  driver, e dentro una `portENTER_CRITICAL` (spinlock, interrupt disabilitati)
  non si possono fare. Le due strade che arrivano li' girano su **task
  diversi** — la scoperta radio sul task del driver WiFi, il ripristino da NVS
  in `loop()` — e si sovrappongono davvero all'avvio dell'hub, quando i peer
  salvati si rimettono mentre i nodi stanno gia' trasmettendo. Con il registro
  quasi pieno due inserimenti simultanei scrivevano **oltre la fine
  dell'array**. Ora il controllo si rifa' dentro la sezione critica finale e
  chi perde il ballottaggio viene disfatto.
- **il nome che arriva dalla radio non era garantito terminato**: sedici byte
  pieni sono un payload legittimo. `link_parse_message()` ora chiude sempre
  `name` con un NUL — nell'unico punto da cui ogni messaggio passa, cosi' vale
  anche per il codice applicativo che verra' scritto dopo.

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

**Aggiornamento del 2026-08-27 — il limite non si è ripresentato, e la prova
rimasta aperta è chiusa.** Rifatta esattamente la combinazione sospetta: hub
**ESP32-S3** (`projects/MeteoHub_S3/`, XIAO S3 Sense) e nodo ESP32 "classico"
(lo stesso DOIT DevKit v1, MAC `70:4B:CA:82:9E:70`), core 3.3.10, canale 1.
Power-cycle del nodo con la finestra di associazione aperta sull'hub:
HELLO → WELCOME → primo DATA unicast **consegnato al primo tentativo**
(`espnow_inviati: 1, espnow_falliti: 0`), nodo adottato in pochi secondi.

Quindi **la raccomandazione qui sopra non vale più con il core attuale**: la
combinazione hub S3 ↔ nodo classico funziona. Resta scritta perché il guasto
era reale quando è stato osservato, e sapere che *poteva* presentarsi aiuta a
riconoscerlo se tornasse: il sintomo era broadcast che passa e unicast no, cioè
un nodo che si vede annunciare e non si riesce ad associare.

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

## `projects/EnvNode_C3/` — nodo ambientale con dashboard (progetto reale, hardware smantellato)

**Scheda smantellata il 2026-08-31**: l'hardware non esiste più, e
`192.168.1.140` non è più l'indirizzo di nessuno. Il progetto resta qui **di
proposito** — è la base da cui ripartire se servirà di nuovo un nodo ambientale
o un secondo hub, ed è la copia di riferimento dei moduli (`rtc_time`,
`sd_logger`, `remote_nodes`, `forecast.h`, `web_ui`) da cui sono nati gli altri
progetti. **Tutto quello che segue descrive com'era quando girava**: resta vero
del codice, non di una scheda in funzione. Non cercarlo in rete e non
proporne un OTA.

Non è un template: era un'applicazione installata e in funzione, cresciuta dallo
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
| `remote_nodes.h/.cpp` | **hub ESP-NOW**: riceve i DATA dei nodi a batteria, ne tiene lo stato in RAM, li dichiara "muti" e da `v11` ne calcola **trend barometrico e previsione** (vedi sotto) |
| `forecast.h` | copia di quella del nodo: trend a 3 h con isteresi, header-only e pura. Da `v11` il calcolo autorevole sta **qui**, non sul nodo |
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
- **Un dato che non si sa datare non si registra** (`orario_registrabile()`, da
  `v10`): finché il primo sync NTP non è arrivato, il campione non va né su SD
  né nei min/max. Prima del boot l'orologio riporta l'ora di **compilazione**,
  che è identica ad ogni riavvio: nel CSV del 2026-08-23 sono rimaste dieci
  righe con lo stesso identico timestamp `10:48:06`, una per riavvio, e il
  minimo della giornata risultava misurato a un istante in cui nessuno aveva
  letto niente. La finestra di grazia è di **5 minuti**: oltre, si registra
  comunque con `fonte_ora=STIMA`, perché una scheda rimasta senza rete che
  smette di loggare per sempre sarebbe un guasto peggiore di un timestamp
  impreciso. Vale anche per i DATA dei nodi remoti, dove pesa di più: il CSV
  dell'hub è l'unico posto dove quella lettura esiste.
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
- **Il tempo di giro si misura** (da `v12`): `/api/stato` riporta
  `loop_max_ms`, `loop_max_dove` (`web`/`nodi`/`trend`/`bottone`/`campione`/
  `oled`), `loop_max_ora`, `loop_lenti` e `invii_interrotti`. Serve a
  distinguere **"si è riavviata"** da **"è rimasta ferma dentro una chiamata"**:
  nei CSV le due cose hanno lo stesso identico aspetto — un buco — e
  distinguerle il 2026-08-25 è costato un'indagine incrociando `uptime`, log
  locale e log dei nodi. Sta in RAM di proposito: il buco nel CSV lo data già
  da sé, e scrivere in NVS dentro il giro sarebbe un costo continuo per un
  evento raro. La fase `web` **non** si misura durante un OTA: sono decine di
  secondi legittimi che coprirebbero per sempre il massimo vero.
- **`web_ui` non deve** duplicare stato: legge `settings_get()`, `sd_logger.*`,
  `rtc_time.*`, `comfort_eval()` direttamente; i ganci `app_*()` implementati
  nel `.ino` coprono solo letture correnti e min/max dall'ultimo avvio.

### Ruolo secondario: hub ESP-NOW dei nodi a batteria (da `v4`, 2026-08-23; concluso)

**Ruolo finito**: `MeteoHub_S3` esiste dal 2026-08-27 e da allora i nodi sono
suoi; con lo smantellamento della scheda (31/08) l'hub della casa è **uno solo**.
Quanto segue resta scritto perché il codice è ancora qui e `MeteoHub_S3` ne usa
le copie — `remote_nodes.*`, `forecast.h`, `sd_logger.*` sono nati in questo
progetto, e i motivi delle loro scelte sono spiegati qui.

`EnvNode_C3` era l'unica scheda di casa sempre accesa, con orologio NTP, microSD e
web UI: finché non è esistito `MeteoHub_S3` faceva **anche** da hub per i nodi a
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
- **I valori dei nodi remoti si loggano su SD** in `/nodi/<NOME>/AAAA-MM-GG.csv`
  (colonne `ts_iso,ts_unix,fonte_ora,mac,seq,temp_c,hum_pct,press_hpa,batt_mv`),
  con elenco e download da `/nodi` (`/api/nodi/giorni`, `/api/nodi/scarica`).
  - **Cartella separata da `/logs`, non una sottocartella**: dentro `/logs` c'è
    una scansione che si aspetta solo file. E i due log sono anche
    concettualmente diversi — `/logs` è quello che questa scheda *misura*,
    `/nodi` è quello che le viene *raccontato*.
  - **Cartella per nome, MAC in colonna.** Sulla card i nomi si devono poter
    leggere; ma il nome può cambiare (basta riprogrammare un nodo) mentre
    l'identità vera è il MAC, quindi ogni riga se lo porta dietro e le righe
    vecchie restano attribuibili anche dopo una rinomina.
  - **Rinominare un nodo NON rinomina la sua cartella** (verificato il
    2026-08-24: in tutto il progetto non esiste una sola `rename`). L'hub crea
    `/nodi/<NOME_NUOVO>/` alla prima scrittura e ci scrive da lì in poi;
    `/nodi/<NOME_VECCHIO>/` resta intatta con lo storico fino a quel momento.
    Il nodo **non** si sdoppia in elenco — per l'hub l'identità è il MAC — ma
    il pulsante "Registri su SD" mostra solo la cartella del nome corrente,
    quindi lo storico vecchio **sparisce dalla vista pur essendo ancora sulla
    card**. Si riprende a mano, perché gli endpoint accettano un nome
    qualsiasi e non solo quello dei nodi registrati:
    `/api/nodi/giorni?nodo=<vecchio>` e
    `/api/nodi/scarica?nodo=<vecchio>&d=AAAA-MM-GG`.
    - Scelta consapevole, non svista: le cartelle vecchie restano come
      archivio. Il caso si presenta comunque di rado, e da `MeteoNode_C3`
      `v11` un nodo senza nome esplicito si chiama `Meteo-XXXXXX` (dal MAC),
      quindi la sua cartella è stabile per costruzione.
    - **Se un giorno la si volesse spostare**: `remote_nodes.cpp` rileva già
      il cambio (stampa "il nodo X ora si presenta come Y" e alza `s_dirty`),
      quindi basta esporre una callback tipo `remote_on_rename(vecchio,
      nuovo)` e agganciarci nel `.ino` una `SD.rename()` — il modulo non deve
      conoscere la SD, stessa regola di `remote_on_data()`. Due casi da
      gestire: la cartella di destinazione che **esiste già** (nome riciclato
      da un'altra scheda: lì i due storici andrebbero fusi, non sovrascritti)
      e la **SD assente** proprio nel momento del cambio, che lascerebbe il
      rinominare a metà.
  - **Un valore non finito diventa un campo vuoto, non uno zero**: nel grafico
    dev'essere un buco, non una misura che nessuno ha fatto. `seq` c'è apposta
    perché i salti si vedano: su una tratta radio il pacchetto perso è un dato.
  - **L'elenco dei registri si carica solo su richiesta** (pulsante "Registri su
    SD"), mai durante il polling: elencare i file è una scansione della card, e
    farla ogni 2 s toglierebbe tempo a campionamento, scrittura e OTA.
  - `remote_nodes` **non** conosce `sd_logger`: espone `remote_on_data()` e
    l'incollatura sta nel `.ino`. È il modulo che verrà copiato su
    `MeteoHub_S3`, che avrà uno storage diverso, e legarlo qui alla SD di
    `EnvNode_C3` vorrebbe dire doverlo scucire al trapianto.
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
- **Il trend barometrico si calcola qui, non sul nodo** (da `v11`, 2026-08-24).
  La previsione di `forecast.h` vuole la pressione di tre ore fa, e un nodo a
  batteria non ce l'ha e non potrà mai averla: il deep sleep gli azzera la RAM
  ad ogni risveglio, quindi la sua pagina resta per sempre su "raccolgo dati:
  servono tre ore di storico". L'hub è già sempre acceso ed è già il posto dove
  quei dati arrivano.
  - **Lo storico è un anello di slot da 10 minuti** (20 slot, ~160 byte per
    nodo), non i 720 slot da 2 minuti che il nodo teneva per i suoi grafici: al
    trend non serve quella risoluzione. Gli slot sono ancorati all'orologio e
    non ai pacchetti, perché la cadenza la decidono i nodi — un anello a numero
    fisso di *campioni* coprirebbe tre ore o tre minuti a seconda di come è
    configurato chi trasmette.
  - **Lo storico si ricostruisce dai CSV su SD al primo sync NTP dopo il boot**
    (`seedForecastDaSD()` nel `.ino`, che legge solo la **coda** dei file).
    Senza, ogni riavvio — e ogni OTA — costerebbe tre ore di "non ancora noto",
    cioè lo stesso guasto che si sta togliendo al nodo, spostato di una scheda.
    Va fatto dopo il sync perché lì i timestamp non datano una riga: compongono
    il **nome del file** da aprire.
  - **`remote_seed_begin()` prima di seminare**: i DATA veri arrivano anche
    prima che il seeding parta (aspetta NTP, i nodi no), e l'anello rifiuta i
    campioni fuori ordine — senza l'azzeramento il seeding girerebbe senza
    errori e senza seminare niente.
  - **L'altitudine è una sola per tutti i nodi** (`/api/nodi/altitudine`,
    NVS a chiave separata dal blob del registro, per non doverne migrare il
    formato): serve solo a riportare al livello del mare la pressione, che i
    nodi trasmettono **grezza**. Il trend non ne dipende — è una differenza, e
    l'offset si cancella.
  - **Solo per pressioni plausibili** (800..1100 hPa): `value[2]` ha significati
    diversi per tipo di nodo, e senza quel controllo il terzo canale di un
    attuatore diventerebbe una previsione del tempo.
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

## `projects/MeteoHub_S3/` — hub della stazione meteo (progetto reale)

Cresciuto dal bring-up del pannello e-ink, oggi è l'hub vero della stazione:

```
nodi ESP-NOW -> hub S3 -> pannello e-ink 4.2" + CSV su microSD + web UI/OTA
```

Le cinque pagine di prova del bring-up sono ancora in coda a quella dei nodi:
servono a distinguere un guasto del pannello da un guasto della radio, che
senza di loro si somiglierebbero (schermo che non cambia).

**Vincoli e scelte da conoscere**:

- **Il pannello e la microSD condividono il bus SPI** (SCK GPIO7/D8, MOSI
  GPIO9/D10; l'e-ink non usa MISO, gli basta un CS separato). Due conseguenze:
  il CS della card (**GPIO21**) va pilotato **ALTO prima di toccare il bus**,
  anche quando la card non si monta, e `sd_begin()` qui **non chiama
  `SPI.begin()`** — lo fa il `.ino` per il display, sugli stessi pin.
  Verificato su hardware il 2026-08-27: card montata (14,9 GB) e pannello che
  disegna, insieme, senza arbitraggio. Regge perché tutto gira in `loop()`:
  **se un giorno qualcosa passasse su un task proprio, quel bus andrebbe
  protetto**.
- **Il canale ESP-NOW è `ESPNOW_LINK_CHANNEL_CURRENT` (0), mai un numero.**
  Da quando c'è il WiFi la scheda sta su un AP e il canale lo impone il router:
  forzarlo chiamerebbe `esp_wifi_set_channel()` su una STA connessa. Prima
  della Fase 3 qui c'era un numero fisso, perché senza WiFi non c'era nessuno
  a imporlo. `remote_begin()` va chiamata **dopo** `net_begin()`.
- **La finestra di associazione NON si apre da sola all'avvio**, al contrario
  di `EnvNode_C3`: `remote_begin()` la apre e il `setup()` la richiude subito.
  Un nodo tiene un hub solo e lo adotta il primo che risponde al suo HELLO, e
  i nodi veri fanno HELLO ad ogni power-cycle. I nodi già noti stanno in NVS e
  rientrano comunque. Si apre da `/` o tenendo premuto **BOOT** (1,2 s) per 2
  minuti; BOOT breve cambia pagina.
- **La dashboard personalizzata su SD c'è anche qui** (da `v3`, 2026-08-28),
  identica a quella di `EnvNode_C3`: `/` serve `/www/dashboard.html` dalla card
  se esiste, `/dashboard-upload` la carica, `/dashboard-ripristina` torna al
  default. Le funzioni `sd_*_dashboard()` erano già in `sd_logger.cpp` (che è
  una copia di quello del C3) e stavano lì inutilizzate: è costato solo
  l'aggancio. **La pagina di upload sta sempre in PROGMEM**, mai sulla card che
  può sostituire — altrimenti una dashboard rotta chiuderebbe fuori proprio chi
  deve rimpiazzarla, e qui il rientro sarebbe andare a staccare la microSD.
  Vale la stessa avvertenza del C3: il file nel repo è **solo un sorgente**, la
  scheda serve la copia sulla card e ricompilare il firmware non la cambia.
- **La diagnostica sta sul pannello, non sulla seriale.** Il piede della pagina
  NODI porta IP e spazio libero della card, e in negativo `SD NON MONTATA`.
  Serve perché il log di boot di questa scheda **non è osservabile via USB**:
  ogni cattura si ferma a 256 byte (il buffer TX della CDC), l'host finisce di
  enumerare la porta un paio di secondi dopo il reset e `setTxTimeoutMs(0)`
  butta il resto. Da fuori sembra un `setup()` che si interrompe a metà.
- **Un DATA che arriva prima del primo sync NTP non si registra**
  (`orario_registrabile()`, finestra di grazia 5 minuti): il CSV di un nodo
  remoto è l'unico posto dove quella lettura esiste, e una riga datata con
  l'ora di compilazione — identica ad ogni riavvio — non è un dato salvato ma
  un dato falsificato. Il buco si vede comunque dal salto di `seq`.
- **Lo storico del trend si ricostruisce dai CSV** al primo sync NTP
  (`seedForecastDaSD()`), leggendo solo la **coda** dei file: senza, ogni
  riavvio — e ogni OTA — costerebbe tre ore di previsione "non ancora nota".
- **Le pagine sono un elenco, non un enum** (da `v4`, 2026-08-28, `pages.*`).
  Ogni slot ha tipo, `attiva`, `durata_s` e un parametro; la configurazione sta
  in NVS (namespace `hubpag`, un blob solo con un magic, come il registro dei
  nodi). Da qui discendono senza meccanismi propri le tre cose che si chiedono
  a un pannello: **«fissa una pagina» è "tutte le altre disattivate"**, «cambio
  automatico» è la rotazione, e la rotazione si ferma da sola quando resta una
  pagina attiva sola — lì non c'è nessun posto dove andare, e un refresh
  completo per tornare sulla stessa pagina sarebbe 2,2 s di lampeggio per
  niente. Se fossero tre stati separati potrebbero andare fuori sincrono fra
  loro; così no, perché sono lo stesso elenco letto in tre modi.
  - `pages.cpp` **non conosce il display**: dice quale pagina tocca, il `.ino`
    la disegna. È la stessa regola per cui `remote_nodes` non conosce la SD.
  - **La rotazione è spenta di default**, ed è una scelta d'uso, non una
    dimenticanza: un pannello che ruota fra sei pagine diventa un salvaschermo
    che nessuno legge, mentre il valore dell'e-ink è che l'informazione *sta
    lì* e la si guarda passando.
  - **Una pagina non si aggiunge due volte** (da `v20`): il grafico è unico, e
    la stessa immagine non entra in elenco due volte. Non era così — si poteva
    accumulare lo stesso grafico all'infinito. Sul pannello un doppione non si
    vede come un errore: si vede come una **rotazione che si inceppa**, perché
    mostra la stessa cosa due volte di fila pagando due refresh completi da
    2,2 s. Il rifiuto sta nel server (409) *e* nel pulsante, che si spegne: un
    pulsante premibile che non fa niente è il difetto tolto dal `postJson()`.
  - **Lo slot 0 è la pagina dei nodi e non si può togliere**: un elenco vuoto
    lascerebbe il pannello senza niente da mostrare e senza modo di rimediare
    dal tasto BOOT, che è l'unico comando quando la rete non c'è. Per lo stesso motivo,
    da `v12`, **non si può nemmeno spostare**: è il posto fisso da cui riparte
    il tasto.
  - **`PAGES_MAX` è 16 da `v12`, e alzarlo ancora richiede una migrazione.**
    `sizeof(PagBlob)` entra nel confronto che valida il blob NVS, quindi un
    numero diverso rende **irriconoscibile la configurazione salvata**: le
    pagine sparirebbero durante un OTA, in silenzio, con le immagini ancora
    sulla card. Il magic è passato da `PAG1` a `PAG2` e `pages_begin()` legge
    entrambi, convertendo una volta sola. **Guardare qui prima di toccare
    quella costante.**
  - **BOOT breve scorre anche le pagine escluse dalla rotazione.** Il tasto è
    la via di governo quando la rete è giù: non deve dipendere da come è
    configurata la rotazione.
- **Le pagine di prova del bring-up (geometria, formato, contatore, foto in
  flash) sono state tolte** in `v4`, con `foto_prova.h`. Servivano a distinguere
  un guasto del pannello da uno della radio; quella funzione la copre ora la
  pagina messaggio, che si vede o non si vede allo stesso modo — e in più dice
  qualcosa di utile quando funziona.
- **Il messaggio sta in due posti con due ruoli diversi** (`messages.*`), e non
  è la stessa informazione duplicata: in **NVS** il messaggio *attivo adesso*
  (~200 byte), perché è quello che sta sul pannello e deve tornare identico
  dopo un riavvio **anche con la card tolta**; su **SD**
  (`/messaggi/archivio.csv`) l'*archivio*, per riusare i messaggi ricorrenti
  senza consumare cicli di erase della NVS per tenere uno storico.
  - **Il testo va disegnato con `U8g2_for_Adafruit_GFX`** (dipendenza nuova da
    `v4`): i font Adafruit GFX sono ASCII puro e in italiano "perché"
    diventerebbe "perch?". U8g2 disegna UTF-8 sullo stesso canvas di GxEPD2,
    quindi le due strade convivono — font Adafruit per i numeroni della pagina
    nodi, U8g2 dove serve l'accento.
  - **Il corpo si sceglie da solo** provando 24/18/14/12/10 pt e tenendo il
    primo che entra nell'area, a capo compresi: un messaggio corto deve
    leggersi da lontano, uno lungo deve starci, e non c'è un corpo che faccia
    tutte e due le cose.
  - **Solo un messaggio `urgente` scavalca la pagina corrente.** Uno normale si
    vede alla prossima rotazione o andandoci a mano: se ogni bigliettino
    togliesse dallo schermo la pagina dei nodi, il pannello smetterebbe di
    essere quello per cui l'hub esiste.
  - Il pulsante **"manda al pannello" è esplicito apposta**: aggiornare mentre
    si scrive costerebbe un refresh da 2,2 s per carattere.
- **Un errore che il server gestisce non è un errore che l'utente vede**: in
  mezzo c'è un client che può ingoiarlo. Su `/pannello` il pulsante "Aggiungi"
  faceva `.then(r => r.json())` senza guardare `r.ok`, quindi il `507 non c'è
  più posto nell'elenco` — perfettamente corretto lato scheda — mandava la
  promise in eccezione e **il pulsante non faceva niente, in silenzio**. Con
  gli slot esauriti (erano 8 su 8) il sintomo era "la pagina è difficile da
  usare", che punta lontanissimo dalla causa. Da `v12` c'è `postJson()`, che
  mostra il testo della risposta. Vale per ogni `fetch` di questo repo: se non
  si guarda `r.ok`, il messaggio d'errore scritto con cura non arriva a
  nessuno.

- **Un byte che non è UTF-8 rende non parsabile l'INTERA risposta JSON**, ed è
  la stessa trappola del `NAN` emesso come `"nan"` su `EnvNode_C3`: la pagina
  resta vuota per colpa di un solo campo, e il guasto non somiglia alla causa.
  Trovato il 2026-08-28 mandando un messaggio da un client che parlava CP1252:
  un `0xF9` (la `ù`) è finito nell'archivio sulla card e `/api/messaggio` non
  si è più parsato — con dentro anche il messaggio *attivo*, che era sano.
  - **Si para in due punti, e servono entrambi.** All'ingresso
    (`handleApiMessaggioSet`) un testo non UTF-8 valido viene rifiutato con
    400, così non entra in NVS né nell'archivio, dove resterebbe per sempre.
    In uscita `appendJsonString()` sostituisce con `?` ogni sequenza malformata:
    è la rete per quello che è **già** stato scritto, e per i nomi dei nodi, che
    arrivano dalla radio e nessuno ha validato.
  - Il fatto che a corrompersi sia stato l'archivio e non il messaggio attivo è
    la parte istruttiva: **un dato vecchio e sbagliato in un elenco può togliere
    dallo schermo un dato nuovo e giusto**, perché il JSON è una risposta sola.
- **Le pagine immagine leggono `/images/<nome>.bin` dalla card** (da `v6`,
  2026-08-28), con il formato che `www/dither.html` produce dal 2026-08-21:
  400×300 a 1 bit, 50 byte per riga, **15.000 byte esatti**, bit a 1 = bianco.
  A bordo non si converte niente — nessun decoder JPEG/PNG, nessun dithering:
  quello lo ha fatto il browser, ed è tutto il motivo per cui la catena è fatta
  così.
  - **La lunghezza si controlla quando si carica, non quando si disegna.** Un
    file di dimensione diversa viene rifiutato con 400 **e cancellato dalla
    card**: scoperto al momento di disegnarlo si vedrebbe come una pagina
    sbilenca, che somiglia a un guasto del pannello invece che a un upload
    sbagliato. Provato il 2026-08-28 con un file da 9000 byte.
  - **Il buffer da 15 kB si prende e si rilascia ad ogni disegno** invece di
    tenerlo sempre: una pagina immagine può non esserci mai, e 15 kB fissi
    sarebbero il 6% della RAM tolti a chi lavora sempre.
  - **Se l'immagine manca, il pannello lo scrive.** Il percorso d'errore
    disegna "immagine non disponibile" con il path e, se il file c'è ma è
    storto, quanti byte ha: una pagina che resta com'era somiglia a un display
    rotto, e il log di boot di questa scheda non è leggibile via USB.
  - **Eliminare un'immagine NON toglie le pagine che la usano**, di proposito:
    l'utente può ricaricare lo stesso nome un minuto dopo, e ritrovarsi la
    pagina sparita senza averlo chiesto sarebbe peggio di vedere l'avviso.
  - **L'anteprima nel browser è 1:1 per costruzione**: la pagina `/pannello`
    scarica gli **stessi** 15.000 byte da `/api/immagini/scarica` e li
    ridisegna su canvas con lo stesso `unpack()` di `dither.html`. Non è una
    simulazione del pannello, è il suo contenuto. (L'anteprima di quello che
    l'hub sta disegnando *adesso* è un'altra cosa e non si può fare: vedi la
    nota su `getBuffer()` più sopra.)
- **`www/dither.html` manda direttamente alla scheda** (pulsante "Manda
  all'hub"): POST su `/api/immagini`. La pagina gira come **file locale**, quindi
  quel POST è cross-origin e l'hub monta il `CorsMiddleware` come `EnvNode_C3`
  — con `collectAllHeaders()`, senza il quale il middleware non vedrebbe mai
  l'header `Origin` e il preflight OPTIONS finirebbe sul 404 di default.
  L'autenticazione va nell'header `Authorization` scritto a mano: con origin
  `*` il browser non può usare le credenziali salvate.
- **`/immagini` e' `www/dither.html` servita dalla scheda** (da `v8`,
  2026-08-28): ritaglio, zoom, rotazione, luminosita', gamma e quattro
  algoritmi di dithering (Floyd-Steinberg, Atkinson, Bayer 8x8, soglia secca),
  con l'anteprima di come verra' davvero e il pulsante che manda il `.bin`
  all'hub. Il lavoro pesante resta nel browser: la scheda riceve 15.000 byte
  gia' impacchettati e non converte niente.
  - **La pagina NON e' una seconda copia**: `dither_page.h` (~31 kB in PROGMEM)
    si **rigenera** da `www/dither.html` con `python www/gen_page.py`, da
    rilanciare dopo ogni modifica e prima di ricompilare. Due copie a mano
    divergerebbero al primo ritocco, e la differenza si vedrebbe solo
    confrontando la pagina della scheda con quella aperta sul PC — cioe' quasi
    mai. Il file generato e' versionato apposta: chi clona compila senza
    eseguire niente.
  - **La stessa pagina vive in due modi** e cambia solo come manda il file:
    servita dalla scheda usa una richiesta **relativa e same-origin** (le
    credenziali le rimette il browser, quindi la riga con host e password si
    nasconde da sola); aperta da `file://` fa una richiesta **cross-origin**
    verso l'IP dell'hub con `Authorization` scritto a mano, perche' con origin
    `*` il browser non manda le credenziali salvate. E' `SERVITA_DA_SCHEDA`,
    una riga di JavaScript.
- **Le pagine dell'interfaccia si sostituiscono dalla card** (da `v18`): `/`,
  `/pannello`, `/immagini` e `/api` servono il file `/www/<nome>.html` se c'è,
  altrimenti quello nel firmware. Si gestisce da **`/pagine`**.
  - **Il motivo non è lo spazio**: le cinque pagine pesano 74 kB su una
    partizione piena al 41 %, e toglierle porterebbe al 39 %. Il motivo è
    **iterare senza OTA** — il 2026-08-30 sono serviti *cinque* aggiornamenti
    per dettagli grafici, e ogni riavvio si porta dietro il suo corredo (il
    pannello torna alla pagina dei nodi, i contatori si azzerano).
  - **Il fallback nel firmware resta sempre**, e per questo non si libera
    flash: è il prezzo, ed è quello giusto. Con tutto solo sulla card, una
    microSD che non monta significherebbe **nessuna interfaccia** e l'unico
    rientro sarebbe andare fisicamente alla scheda.
  - **`/pagine`, `/update` e gli upload NON sono sostituibili**, ed è la stessa
    regola della dashboard: una via di rientro che dipende da ciò da cui si
    sta rientrando non è una via di rientro.
  - **Il nome non arriva mai dalla rete come pezzo di path**: si cerca in una
    whitelist (`PAGINE_SOST`) e si usa la voce trovata. Niente path traversal
    da parare, e sulla card non finiscono file che nessuno serve.
  - **L'upload registra con quale firmware** la pagina è stata caricata
    (`/www/caricate.csv`), e `/pagine` avvisa quando quella versione non è più
    quella che gira. È il rischio nuovo che ci si prende spostando le pagine:
    prima firmware e pagine erano la stessa cosa e non potevano divergere.

- **La tabella delle rotte è una sola, e viene usata due volte** (da `v18`):
  da lì si registrano gli handler **e** si genera `/api/elenco`, che documenta
  l'interfaccia a chi si scrive le proprie pagine (impaginato su `/api`).
  - È ciò che permette alla pagina `/api` di stare sulla card **senza
    diventare bugiarda**: sulla card c'è solo l'impaginazione, i fatti li
    chiede al firmware ad ogni caricamento. Una rotta nuova compare nella
    documentazione perché è stata **registrata**, non perché qualcuno si è
    ricordato di scriverla.
  - È la stessa disciplina di `drawOra()` sul pannello: una funzione sola,
    perché due disegni dello stesso dato finirebbero per differire.
  - Le rotte con upload multipart vogliono due callback e non entrano nella
    forma della tabella: stanno lì con `handler == nullptr`, documentate, e si
    registrano a mano subito sotto.

- **Tutte le pagine portano lo STESSO piede di navigazione**: `/` &middot;
  `/pannello` &middot; `/immagini` &middot; `/pagine` &middot; `/api` &middot;
  `/update` (aggiornato in `v19`; prima c'era `/dashboard-upload`, che resta
  funzionante ma non è più la via consigliata — `/pagine` fa la stessa cosa per
  tutte le pagine). Vale anche per la dashboard sulla card e per la pagina
  `/update`, che sta in `net_ota.cpp`: sono **sette** posti da toccare insieme,
  ed è il prezzo di non avere un template condiviso. Non e' pignoleria
  estetica: `/` puo' essere sostituita da una dashboard personalizzata sulla
  card, e se quella non mette i link — o e' rotta — le altre pagine
  resterebbero raggiungibili solo digitando l'URL a memoria. Con il piede
  uniforme si continua a girare partendo da una qualunque.
- **La pagina GRAFICO** (da `v14`): la temperatura dei nodi nelle ultime 24 h,
  a piena pagina. E' un tipo di pagina come gli altri — si aggiunge, si toglie,
  entra nella rotazione.
  - **A piena pagina e non una sparkline dentro la pagina nodi**, ed e' la
    stessa regola gia' scritta per la fascia del messaggio: su e-ink il tempo e'
    la dimensione in piu', e per vedere tutto c'e' la rotazione, che alterna
    pagine intere e leggibili invece di comprimerne tre in 400x300. Un
    grafichino da 180x18 accanto ai numeri sarebbe stato un ornamento.
  - **Anello separato da quello del trend**: 48 slot da 30 minuti (24 h) contro
    20 slot da 10 minuti (3 h). Tenere la risoluzione fine per un giorno intero
    costerebbe dodici volte la memoria per una curva che a 344 px non potrebbe
    comunque mostrarla. **Niente timestamp per slot**: l'indice E' il tempo
    (`ts / 1800 % 48`), il che costa 101 byte per nodo invece di 288 — ma
    obbliga a **svuotare le celle scavalcate** quando si cambia slot, o dopo un
    giro dell'anello si leggerebbero i valori di ieri come se fossero di oggi.
  - **Lo storico si ricostruisce dai CSV** insieme a quello del trend, con la
    coda alzata da 64 a 128 kB: verificato sull'hardware il 2026-08-30, subito
    dopo l'OTA **48/48 mezz'ore per entrambi i nodi**, cioe' il grafico e' pieno
    al riavvio invece di impiegare un giorno a formarsi.
  - **Un buco non si attraversa con una retta**: un segmento sopra un'ora senza
    dati direbbe che la temperatura e' passata di li', che nessuno ha misurato.
    La linea si interrompe.
  - **Le curve si distinguono per tratto** (piena, tratteggiata, punteggiata),
    non per colore: su 1 bit e' l'unica differenza che sopravvive. Oltre tre
    nodi non si disegna: il bianco e nero non ha altri tratti leggibili.
  - **L'asse dei tempi porta l'ORA vera**, non "-24h/-12h/ora": un istante resta
    vero anche quando il pannello non si ridisegna da un pezzo. Stessa ragione
    per cui la pagina nodi mostra l'ora dell'ultimo pacchetto.
  - Si ridisegna **ogni mezz'ora**, cioe' quando entra un campione nuovo:
    ridisegnare piu' spesso mostrerebbe la stessa curva al prezzo di un refresh
    completo. Serve perche' con il grafico come unica pagina attiva la rotazione
    non scatta mai. Misurato: **2567 ms** (2,2 s di refresh completo piu' ~370
    ms di curve).
  - **`temp_campioni` in `/api/nodi`** dice quanti dei 48 slot hanno un
    campione. Senza, "la pagina e' comparsa" e "la pagina mostra qualcosa"
    sarebbero indistinguibili da remoto: il pannello non si puo' guardare da
    fuori.

- **`GET /api/salute` fa i controlli incrociati da sola** (da `v13`). Il
  controllo che vale e' **`pacchetti ricevuti == righe scritte + scartati per
  orario + scritture fallite`**, e regge perche' `remote_nodes` incrementa
  `pacchetti` e chiama la callback nello **stesso punto**: ad ogni pacchetto
  contato corrisponde esattamente un tentativo di scrittura, che finisce in uno
  dei tre contatori. Sono numeri tenuti da moduli che non si conoscono fra loro
  — `remote_nodes`, `sd_logger` e lo sketch — quindi **se non tornano, il guasto
  sta fra la radio e la card**, il tratto che nessun altro contatore guarda.
  - Perche' esiste: quella verifica si faceva a mano leggendo `/api/stato` e
    `/api/nodi` e sommando a occhio. Una diagnosi che serve davvero e che
    richiede una persona non viene fatta quasi mai.
  - **I contatori degli scarti sono la parte necessaria**, non un di piu': senza
    di loro "pacchetti diversi da righe" non distingue una perdita reale da uno
    scarto voluto (i DATA arrivati prima del primo sync NTP), e un controllo che
    si allarma da solo non lo guarda piu' nessuno.

- **`reset_reason` e `boot_count` anche sull'hub** (da `v13`, gia' su
  `MeteoNode_C3` da `v5`). Tutti gli altri contatori vivono in RAM e ripartono
  da zero: e' proprio questo che rende un riavvio **invisibile da remoto**,
  perche' da fuori si vede solo un hub che "ha registrato poco". Il 2026-08-30
  un `uptime` di 0,7 h ha richiesto di incrociare l'ora corrente, quella
  dell'ultimo OTA e i CSV dei nodi per concludere che non era successo niente:
  con questi due campi sarebbe stata una riga. `boot_count` sta in **NVS**,
  perche' deve sopravvivere proprio all'evento che misura.

- **Gli handler HTTP che toccherebbero il display accodano e basta**
  (`app_chiedi_pagina()` / `app_chiedi_refresh()`, eseguite dal `loop()`).
  Un refresh dentro un handler terrebbe fermo il WebServer — che è sincrono —
  e con lui l'OTA e il prelievo dei DATA dal driver ESP-NOW, che tiene solo
  l'ultimo pacchetto. È la stessa regola dei callback della radio: **la
  richiesta accoda, il loop lavora.**
- **L'anteprima 1:1 del pannello c'è, da `v22`** (2026-08-31), ed è il motivo
  per cui ogni disegno passa da una **tela** `GFXcanvas1` invece che dritto sul
  display: `pannello` riceve solo la tela finita, e da lui passano soltanto
  finestre, paging e refresh. `GET /api/pannello/anteprima` restituisce quei
  15.000 byte — **lo stesso formato dei `.bin`**, quindi il browser li disegna
  con l'`unpack()` che ha già e chi verifica da fuori può confrontarli **bit a
  bit** con l'immagine attesa (provato: l'anteprima di una pagina immagine è
  identica al file sulla card).
  - **Leggere il framebuffer di GxEPD2 non era la strada**, e non solo perché
    `_buffer` è `private` (`GxEPD2_BW.h:815`): la pagina immagine scriveva
    dritta al controller con `writeImage()`, **saltando quel framebuffer**, e
    un'anteprima presa da lì non avrebbe mai visto proprio le pagine che
    l'utente compone.
  - **`drawImage()`/`writeImage()` non vanno dentro `firstPage()/nextPage()`**:
    scrivono al controller e fanno il refresh da sé, quindi darebbero due
    refresh, il secondo col buffer ancora vuoto — **pannello bianco**. La tela
    si copia con `drawPixel` dentro il paging, così finestre e refresh restano
    esattamente come erano.
  - **Costo misurato**: refresh completo 2200 → **2630 ms**, parziale su tutta
    la pagina 980 → **1040 ms**, orologio **810 → 810 ms** (invariato, perché il
    ciclo gira solo sul rettangolo chiesto). I 430 ms del completo si
    toglierebbero con `writeImage()` fuori dal paging, ma quello tocca il
    tratto tela→vetro che l'anteprima **non** può verificare: va provato da chi
    il pannello lo sta guardando.
  - **Il limite da conoscere**: l'anteprima mostra ciò che l'hub **ha
    disegnato**, non i fotoni sul vetro. Copre "il contenuto è sbagliato", non
    "il display non risponde" — per quello restano `epd_refresh` e i tempi.
- **La fascia del messaggio sulla pagina nodi** (da `v11`, 2026-08-28,
  interruttore in `/pannello`): 70 px in fondo con il messaggio attivo, e
  compare **solo se un messaggio c'è** — acceso l'interruttore ma senza
  messaggi, la pagina resta identica a prima.
  - **È un baratto, non un miglioramento**, ed è per questo che lo decide
    l'utente: il corpo dei nodi scende da 232 a 162 px, quindi con due nodi si
    passa dal blocco comodo (24pt) a quello compatto (18pt). Si guadagna il
    messaggio sempre sotto gli occhi, si perde corpo sui numeri — e il corpo è
    la distanza da cui la pagina funziona.
  - **Immagine + altro invece NON si fa**, e non è una questione di gusto: il
    `.bin` è 400×300 a 1 bit **già retinato**, quindi rimpicciolirlo a bordo
    ricampiona un pattern e produce moiré. Se serve una foto con una scritta,
    la strada giusta è comporla **dentro l'immagine nel browser**
    (`www/dither.html` lavora già su un canvas): costo zero per il firmware e
    tipografia libera.
    - **Fatto da `v12`** (2026-08-30): `dither.html` ha il pannello del testo,
      con banda, alone o testo nudo. Il divieto qui sopra riguarda il
      **ricampionamento**, non l'accostamento — comporre alla dimensione finale
      è sempre stato lecito, ed è esattamente ciò che fa il browser.
    - **Il testo NON passa dal dithering**: si disegna su un canvas a parte, si
      legge a soglia secca e si stende sopra i bit già retinati. Una lettera
      ditherata perde i tratti sottili e a 400×300 diventa illeggibile.
    - **Non sostituisce il messaggio testuale** (`messages.*`, NVS, archivio,
      scadenza, urgenza, fascia): quello resta la via veloce, e sopravvive
      anche con la card tolta. Il biglietto illustrato è un file da 15 kB sulla
      card e non ha nessuna di quelle proprietà.
  - **Su e-ink il tempo è la dimensione in più**: per "vedere tutto" c'è la
    rotazione, che alterna pagine intere e leggibili invece di comprimerne tre
    in 400×300.
  - L'impostazione sta nel **byte `riservato`** che il blob NVS di `pages.cpp`
    aveva già: nessuna migrazione, le configurazioni salvate restano leggibili.
    Vale come esempio — quando si aggiunge un flag a un blob persistito,
    guardare prima se c'è del padding da spendere.
  - Cambiare l'interruttore **fa ridisegnare il pannello subito** (la web UI
    chiama `/api/pannello/refresh` dopo il salvataggio): cambia il layout, e
    senza resterebbe quello vecchio fino al refresh di cadenza.
- **Refresh del pannello — tre cadenze, non una**:

  | refresh | quando | area |
  |---|---|---|
  | orologio | ogni 60 s | solo il suo rettangolo (132x31) |
  | pagina, parziale | dato nuovo (min 120 s) o comunque ogni 5 min | tutta |
  | pagina, completo | ogni 10 parziali **oppure ogni ora** | tutta |

  Piu' un completo obbligatorio ad ogni cambio pagina (la precedente e' ancora
  nella memoria del controller e un parziale la lascerebbe sotto), e un
  parziale immediato al **primo** pacchetto di un nodo — senza, dopo un riavvio
  il pannello direbbe "in attesa del primo dato" per due minuti pur avendo gia'
  i valori. `hibernate()` dopo ognuno.

  **Il completo a tempo (ogni ora) e' la contropartita dell'orologio**: un
  rettangolo riscritto sessanta volte l'ora accumula un alone li' e solo li',
  che e' il modo peggiore in cui un e-ink invecchia. Prima il completo
  dipendeva solo dal conteggio dei parziali, quindi in una giornata senza
  novita' poteva non arrivare mai.

  **Tempi misurati su hardware**: completo **~2,2 s** (4,8 s il primo dopo
  l'accensione, che include il power-on del controller), parziale su tutta la
  pagina **~980 ms**, parziale sul solo orologio **~810 ms**.

  **Quel 810 contro 980 e' il numero da ricordare**: ridurre l'area a un decimo
  fa risparmiare il 17% del tempo, non il 90%. Il costo di un refresh e-ink e'
  dominato dalla sequenza di pilotaggio (power-on, waveform, attesa del BUSY,
  power-off), non dai byte mandati sull'SPI. La finestra piccola conviene lo
  stesso, ma **per il ghosting, non per la velocita'**: stressa solo i pixel che
  contiene. Conseguenza per il futuro: tre areette aggiornate al minuto
  costerebbero ~2,4 s, e converrebbe un solo parziale su tutta la pagina.

- **L'ora la disegna una funzione sola** (`drawOra()`), usata sia dal refresh
  piccolo sia da quello della pagina intera: se fossero due disegni distinti
  divergerebbero di qualche pixel, e l'ora "salterebbe" ad ogni refresh grande.

- **Layout adattivo**: fino a due nodi si usa il blocco comodo (temperatura a
  24pt, trend scritto per esteso), da tre in su quello compatto (18pt). Il
  pannello si legge da lontano, quindi il corpo del carattere non e' un vezzo
  grafico: e' la distanza a cui la pagina funziona.

- **Si mostra l'ORA dell'ultimo pacchetto, non da quanto tempo e' arrivato.** Un
  istante non invecchia: resta vero anche quando il pannello non si ridisegna
  da un pezzo, mentre un "38 s fa" diventa falso dopo trenta secondi — e su un
  e-ink che si aggiorna ogni due minuti sarebbe sbagliato quasi sempre.

- **Il trend e' una freccia disegnata, non una parola**: l'inclinazione segue i
  nove livelli di `forecast.h` (da -70 gradi per il crollo a +70 per la salita
  forte). Una parola va letta, un'inclinazione si vede da tre metri. Quando lo
  storico non basta si disegnano due trattini: "non lo so ancora" non deve
  somigliare a "stabile".
- **Testo centrato**: usare `drawCenter()`, che misura con `getTextBounds()`.
  Allineare a destra con un offset stimato a occhio taglia le stringhe larghe
  sul bordo sinistro, dove il cursore va a coordinate negative e Adafruit_GFX
  non protesta — successo davvero, e sul pannello si leggeva "ESSUN NODO".
- **Compilazione**: serve `--libraries libraries` (usa `EspNowLink`) e il FQBN
  della XIAO S3 **senza** `CDCOnBoot`, che su questa board è invertito.

**Dove scrivere la logica**: nel `.ino` (pagine, tasto, colla fra i moduli).
`remote_nodes.*`, `forecast.h`, `rtc_time.*`, `sd_logger.*`, `net_ota.*`,
`web_ui.*` sono boilerplate per compito — e i primi quattro sono **copie** di
`EnvNode_C3`, da tenere allineate a mano.

## `projects/MeteoNode_C3/` — nodo meteo a batteria (progetto reale)

Nodo della stazione meteo: **AHT20 + BMP280** su I2C, pagina web con tre grafici
SVG disegnati a mano, previsione dal trend barometrico a 3 ore (`forecast.h`),
OTA, e da `v9` **deep sleep** fra una misura e l'altra. Lo stesso sketch gira su
**XIAO ESP32-C3** e su un **ESP32 "classico"** (DOIT DevKit v1): pin, nome del
nodo e guardia della `Serial` si scelgono a compile-time dal tipo di chip.

**Vincoli e scelte da conoscere**:
- **Pin (XIAO C3)**: SCL su **D2/GPIO4** (non il D5 di default), VCC del sensore
  **commutato** su D3/GPIO5, SDA su D4/GPIO6; D1/GPIO3 lasciato libero per il
  partitore della batteria, **non ancora cablato**. Il BMP280 risponde a
  **0x77**, non a 0x76.
- **Il VCC del sensore passa da un GPIO** apposta: permette un power-cycle vero
  del modulo quando smette di rispondere (`sensorPower()`), ed è la stessa
  sequenza che serve al deep sleep — si spegne il sensore prima di dormire.
- **Niente microSD**: lo storico 24 h (720 slot da 2 min, ~4,3 kB) vive in RAM e
  si azzera ad ogni riavvio. I grafici storici veri stanno sull'hub, che riceve
  i DATA e li scrive su card — vedi `projects/EnvNode_C3/`.
- **La pressione si trasmette GREZZA**, non riportata al livello del mare: la
  correzione dipende dall'altitudine, che su questo nodo è ancora un default mai
  calibrato. Trasmettendo il valore corretto si scriverebbe un errore
  sistematico dentro lo storico dell'hub, per sempre; trasmettendo la misura,
  l'hub può applicare la quota giusta il giorno che la si conosce. Il trend —
  cioè la previsione — non cambia in nessuno dei due casi: è un offset costante.
- **Si trasmette anche una lettura fallita**, con NAN sui canali mancanti: "sono
  vivo ma il sensore non risponde" è un'informazione, il silenzio no — da fuori
  sarebbe indistinguibile da un nodo morto, che è proprio ciò che l'hub sta
  cercando di riconoscere.
- **Diagnostica del riavvio** (da `v5`): `reset_reason` (`SW` = OTA o watchdog
  WiFi, `PANIC`, `BROWNOUT`…), `boot_count` in NVS, e i contatori delle cadute
  WiFi. Serve perché tutti gli altri contatori vivono in RAM e ripartono da
  zero: da fuori un nodo appena riavviato e uno che legge male si somigliano
  molto. Nata dopo tre riavvii inspiegati in quattro ore, ricostruiti solo dai
  salti di `seq` nel log dell'hub.
- **Deep sleep** (da `v9`), spento di default e acceso dalla pagina: il nodo si
  sveglia a timer, misura, manda un DATA e ridorme **senza mai accendere il
  WiFi** — a ESP-NOW basta il canale, non l'associazione all'AP. Canale e MAC
  dell'hub stanno in RTC memory, così il risveglio non deve riaccendere la rete
  per sapere dove parlare. Le trappole sono nella sezione "Deep sleep" più
  sopra: leggerla **prima** di metterci le mani, a partire dal `seq`.
  - **Finestra di veglia di 5 minuti ad ogni accensione vera**, con WiFi e OTA:
    è la via di rientro, perché un nodo che dorme non risponde. Togliere e
    rimettere corrente deve bastare a riprenderlo.
  - **Uscita di sicurezza dopo cinque risvegli senza consegna**: riaccende tutto
    e torna raggiungibile da solo. Vale anche come strumento di diagnosi — se
    non scatta mai, il nodo non sta eseguendo codice, e si smette di cercare il
    guasto nella radio.
  - La durata del sonno **è l'intervallo di misura** già configurabile dalla
    pagina: un parametro solo, già persistito, invece di due che possono andare
    fuori sincrono.

### Aggiungere un nodo nuovo alla rete

Copia la cartella, rinomina il `.ino`, `secrets.h` con un `OTA_HOSTNAME`
diverso, carica via USB, **apri la finestra di associazione sull'hub** (pagina
`/nodi` o dashboard: l'hub accetta un MAC sconosciuto solo mentre è aperta, e
si apre da sola solo al suo avvio), accendi. Il resto è automatico: canale,
CSV, cadenza appresa, trend, rilevamento del nodo muto, persistenza in NVS.

**Il nome non va più inventato a mano** (da `v11`, 2026-08-24): se
`NODE_NAME_FISSO` è vuoto — ed è vuoto sulla XIAO C3, la board che si replica —
il nodo si chiama `Meteo-XXXXXX`, dalle ultime tre coppie del MAC letto
dall'eFuse con `esp_read_mac()` (non da `WiFi.macAddress()`: al risveglio dal
deep sleep il WiFi non è acceso). Dalla pagina si può dare un nome parlante,
che finisce in NVS e vince sul derivato.

**Perché conta più di un'etichetta**: sull'hub il nome è la **cartella** in cui
finisce il CSV di quel nodo (`sd_node_dir_name`), quindi due schede omonime
scrivono nello stesso file, mescolando letture di posti diversi. Restano
separabili solo dalla colonna `mac`, ma grafici, trend e download le vedono
mischiate. Il default dal MAC rende quella collisione impossibile per
dimenticanza.

**Limiti da conoscere**: massimo **8 nodi** (`REMOTE_MAX_NODES`), e il CSV
dell'hub ha colonne fisse `temp_c,hum_pct,press_hpa` — il contratto dei tre
float è quello del nodo meteo, un sensore di altro tipo richiede lavoro
sull'hub. E **rinominare un nodo già associato** non lo fa sparire (per l'hub
l'identità è il MAC, e l'anagrafica si aggiorna al primo messaggio), ma da lì
in poi il suo storico prosegue in una cartella nuova.

### La ricerca del canale (da `v12`, 2026-08-27)

L'access point cambia canale da solo, per scansare le reti dei vicini. Chi e'
connesso lo segue riassociandosi; **un nodo che dorme no**: si porta il canale
in RTC memory e diventa muto senza accorgersene, perche' l'unico sintomo e'
l'ACK che non arriva. Prima l'unica reazione era la piu' cara: cinque risvegli
muti, poi riavvio e cinque minuti di WiFi acceso.

Ora, se il DATA non viene consegnato, `hub_scan_channels()` prova gli altri
canali — **1, 6, 11 per primi**, poi gli altri dieci — con **un solo tentativo
per canale** e 200 ms di timeout, rimandando lo STESSO messaggio
(`Link_Node_ResendLast`, vedi sopra: il `seq` non deve avanzare). Al primo che
risponde salva il canale in RTC memory **e in NVS**, e torna a dormire.

| | rete di sicurezza | ricerca del canale |
|---|---|---|
| radio accesa | 5 min di WiFi | ~1 s (peggio: ~2,6 s) |
| carica spesa | ~7 mAh | ~0,03 mAh |
| dati persi | 25 minuti | nessuno, recupera nello stesso risveglio |

**Non sostituisce la rete di sicurezza**, che resta sotto: la ricerca risolve
il canale cambiato, non l'hub spento. Se nessun canale risponde si torna a
quello di partenza — non all'ultimo provato, che e' scelto a caso e per giunta
ha appena fallito.

**Il canale sta anche in NVS**, non solo in RTC memory: quella la cancella il
power-cycle, cioe' proprio l'operazione con cui si recupera un nodo che non
torna. Al boot senza AP si riparte dall'ultimo canale su cui l'hub ha davvero
risposto, invece che dal canale fisso della libreria.

**Come si prova senza aspettare l'AP**: `GET /api/comando?c=prova-canale` arma
**un** risveglio con un canale volutamente sbagliato. Serve perche' altrimenti
la funzione resta non verificata per settimane, e il momento in cui si scopre
che non va sarebbe esattamente quello in cui serviva. Il contatore
`scansioni_ok` (in NVS, visibile su `/api/stato` e in pagina) dice quante volte
l'hub e' stato ritrovato altrove: **zero non e' un guasto**, vuol dire che
l'access point non si e' mai spostato.

**Verificata sul campo il 2026-08-28**, e non da una prova armata: nelle 23,7 h
successive l'AP è passato **dal canale 1 al 13** per conto suo e il nodo a
batteria non ha perso un pacchetto — 285 campioni su 285, gap fra 299 e 302 s,
zero salti di `seq`, **zero interventi della rete di sicurezza** (nei tre giorni
prima erano cinque, uno ogni ~14 h). Il 13 **non** è fra i tre canali provati
per primi, quindi la scansione è arrivata agli altri dieci restando dentro lo
stesso risveglio: il caso peggiore della tabella qui sopra è reale, non teorico.

**La conferma si legge sui CSV dell'hub, non sul nodo.** `scansioni_ok` direbbe
quante volte la ricerca è servita, ma sta dentro una scheda che dorme e non ha
IP: si legge solo nei 5 minuti di veglia dopo un power-cycle — cioè dopo aver
azzerato in RTC memory proprio lo stato che si voleva guardare (il contatore in
NVS sopravvive). La prova che conta è l'assenza di buchi in
`/api/nodi/scarica`, che non richiede di toccare il nodo.

**Dove scrivere la logica**: nel `.ino` (misura, previsione, ciclo di sonno).
`forecast.h`, `hub_link.*`, `rtc_time.*`, `net_ota.*`, `web_ui.*` sono
boilerplate per compito.

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
- **Non si scatta finché l'orario non è sincronizzato** (`orario_registrabile()`,
  da `v3`), con la stessa finestra di grazia di 5 minuti di `EnvNode_C3`. Qui
  pesa più che altrove perché l'orario **è** il nome del file e la cartella che
  lo contiene: prima del primo sync NTP le foto finirebbero in
  `/timelapse/<giorno-di-build>/`, un giorno diverso da quello vero e già
  passato — e con `APP_FULL_RING`, che elimina il giorno **più vecchio**, quella
  cartella sarebbe la prima candidata alla cancellazione. Si perderebbero cioè
  proprio le foto appena scattate, e lo si scoprirebbe solo a card piena. Non
  c'è invece rischio di sovrascrittura: `sd_save_photo()` aggiunge già un
  suffisso `_N` se il nome esiste. Gli slot saltati per questo motivo finiscono
  in `s_skipped`, quindi la web UI li mostra.
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
