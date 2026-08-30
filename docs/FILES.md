# FILES.md — Dettaglio file per file

Reference completa di ogni file sorgente del repo: scopo, contenuto, dipendenze,
cosa NON toccare. Per il pinout/hardware della board AMOLED vedi
`docs/ESP32-S3-AMOLED-1.91-Guide.md`; per l'architettura d'insieme e i comandi di build
vedi `CLAUDE.md`. Qui il livello è quello del singolo file.

Il repo è un **workspace**, organizzato per ruolo: `libraries/` (codice
condiviso), `starters/` (un template per scheda, da copiare), `examples/`
(sketch da caricare così come sono), `projects/` (applicazioni reali già in
funzione), `docs/` (questo file, la guida hardware e i datasheet).

Il boilerplate hardware (display, touch, IMU, microSD, bus I2C) e di
comunicazione (ESP-NOW) vive in **sei** librerie Arduino condivise sotto
`libraries/` — una per periferica/funzione. Ogni sketch include solo quelle
che usa (`starters/AMOLED_1.91_LVGL/` e `examples/Orientation_IMU/` non toccano SD né ESP-NOW;
`examples/Link_Node_Demo/` include solo `EspNowLink` e non ha nemmeno LVGL),
senza copie duplicate del codice: un bug fix in una libreria vale per tutti
gli sketch che la usano. Restano invece deliberatamente per-sketch (non
condivisi) `lv_conf.h` e `build_opt.h`, perché sono configurazione di
progetto, non codice — vedi le rispettive sezioni sotto `starters/AMOLED_1.91_LVGL/`.

| Libreria | Usata da |
|---|---|
| `AMOLED191_Core` | tirata dentro da `AMOLED191_Touch`/`AMOLED191_IMU`, mai inclusa direttamente da uno sketch |
| `AMOLED191_Display` | `starters/AMOLED_1.91_LVGL/`, `Orientation_IMU`, `DHT11_SD_Logger`, `Link_Hub_Demo` |
| `AMOLED191_Touch` | `starters/AMOLED_1.91_LVGL/`, `Orientation_IMU`, `Link_Hub_Demo` |
| `AMOLED191_IMU` | `Orientation_IMU` |
| `AMOLED191_SD` | `DHT11_SD_Logger` |
| `EspNowLink` | `Link_Hub_Demo`, `Link_Node_Demo`, `starters/XIAO_S3_Camera/` |

**Il prefisso dice l'ambito**: le cinque `AMOLED191_*` conoscono i pin della
board AMOLED e fuori di lì non hanno senso. `EspNowLink` è l'unica che esce da
quella board — la usano anche i nodi (`starters/XIAO_S3_Camera/`), perché il
protocollo hub↔nodi deve essere lo stesso da entrambe le parti e duplicarlo
sarebbe il modo più sicuro di farlo divergere. Una libreria nuova segue la
stessa regola: legata a una scheda → prefisso della scheda, portabile → nome
funzionale.

Non usano nessuna di queste librerie: `examples/Diag_Hub/` e
`examples/Diag_Node/` (diagnostica ESP-NOW su `esp_now.h` grezzo), il template
**`starters/C3_OLED_OTA/`**, che è per un'altra scheda, e
**`projects/Timelapse_XIAO/`**, che è sulla stessa scheda del nodo camera ma
senza ESP-NOW. **`projects/EnvNode_C3/`** non ne usava nessuna fino a `v3`, ma
da `v4` include `EspNowLink` per fare da hub ai nodi a batteria: è su un'altra
scheda rispetto alla board AMOLED, ma `EspNowLink` non è legata a una scheda.
Vedi le rispettive sezioni.

---

## `libraries/` — le librerie Arduino condivise

### `AMOLED191_Core` — `AMOLED191_Core.h` / `AMOLED191_Core.c`

**Ruolo**: unica libreria che non esisteva prima del refactor in librerie.
Possiede il bring-up del bus I2C condiviso tra touch e IMU.

**Header (`.h`)**: costanti `AMOLED191_CORE_I2C_PORT`/`_SDA`/`_SCL`/`_CLOCK_HZ`
(I2C_NUM_0, GPIO40/39, 300kHz); `void Core_I2CBusInit(void)`.

**Implementazione (`.c`)**: `Core_I2CBusInit()` è **idempotente** (guardia
`static bool`): `i2c_param_config`+`i2c_driver_install` vengono eseguiti una
sola volta, indipendentemente da quale modulo la chiama per primo. Sia
`Touch_Init()` (`AMOLED191_Touch`) sia `imu_init()` (`AMOLED191_IMU`) la chiamano
internamente all'inizio — funzionano quindi in qualunque ordine, o anche uno
senza l'altro (prima di questo refactor, `imu_qmi8658.c` assumeva
implicitamente che `Touch_Init()` avesse già installato il driver I2C: un
IMU-senza-Touch avrebbe fallito silenziosamente).

**File `.c` non `.cpp`**: necessario perché l'inizializzatore designato
`.master.clk_speed = ...` di `i2c_config_t` (union anonima annidata, estensione
GNU) non è supportato dal compilatore in modalità C++; in C funziona come nel
resto del codice touch/IMU di origine.

**Da sapere**: la guardia di idempotenza non è protetta da mutex — sicura per
ogni uso reale in questo repo (`Touch_Init()`/`imu_init()` sono chiamate in
sequenza da `setup()`, single-threaded, prima che parta qualunque task). Se in
futuro un modulo la chiamasse da un task in background concorrente con
`setup()`, va aggiunta una protezione prima di riusarla in quel contesto.

---

### `AMOLED191_Display` — `AMOLED191_Display.h` / `.cpp` (+ `esp_lcd_sh8601.h`/`.c`)

**Ruolo**: modulo di porting — bus QSPI, pannello SH8601, LVGL, task di
rendering, mutex, in un'unica libreria. **Non si tocca** per lavoro
applicativo normale. Nessuna dipendenza (di compilazione né runtime) da
`AMOLED191_Touch`.

**API pubblica (`AMOLED191_Display.h`)**:
- `Display_Init(void)` — init completa, da chiamare una volta in `setup()`,
  prima di `Touch_Init()`/`Touch_RegisterLvglIndev()` se presenti.
- `lvgl_lock(int timeout_ms)` / `lvgl_unlock(void)` — mutex FreeRTOS
  obbligatorio attorno a ogni accesso LVGL fuori dal task di rendering.
  `timeout_ms = -1` = attesa infinita.
- `lcd_command(uint8_t cmd, const uint8_t *data, size_t len)` — invia un
  comando raw al pannello via QSPI, gestendo internamente il framing.
- `lcd_set_brightness(uint8_t level)` — scorciatoia per il registro `0x51`.
- `lcd_read_register(uint8_t cmd, uint8_t *data, size_t len)` — lettura
  registro via QSPI (opcode `0x03`), meno affidabile della scrittura su
  questo pannello.

**Implementazione (`AMOLED191_Display.cpp`, ex `lvgl_port.cpp`)**:
- Pin display: CS=6, PCLK=47, DATA0-3=18/7/48/5, RST=17 (QSPI, `SPI2_HOST`),
  risoluzione 536×240, 16 bit/pixel.
- `lcd_init_cmds[]`: sequenza di init del pannello SH8601 — Sleep Out (`0x11`),
  MADCTL landscape (`0x36`=`0xF0`, **valore confermato funzionante**, non usare
  `0x00`), pixel format RGB565 (`0x3A`=`0x55`), WRCTRLD/BCTRL abilitato
  (`0x53`=`0x20`, **necessario** perché il comando brightness `0x51` abbia
  effetto), finestra colonne/righe (`0x2A`/`0x2B`), Display On (`0x29`),
  brightness massima (`0x51`=`0xFF`).
- `lcd_command()`: costruisce manualmente il framing QSPI —
  `(0x02 << 24) | (cmd << 8)` — perché questo framing è applicato solo
  internamente dal driver vendor (`esp_lcd_sh8601.c`, funzione statica non
  esposta) per le sue operazioni interne; per comandi ad-hoc a runtime va
  replicato a mano.
- `lvgl_flush_cb()` / `flush_ready_cb()`: callback standard LVGL↔esp_lcd
  (draw bitmap / segnala frame consegnato).
- `lvgl_tick_cb()`: timer `esp_timer` periodico ogni 2ms, chiama `lv_tick_inc()`
  (coerente con `LV_TICK_CUSTOM 0` in `lv_conf.h` — il tick è manuale, non
  automatico).
- Buffer di disegno: doppio buffer DMA (`heap_caps_malloc(..., MALLOC_CAP_DMA)`),
  `LVGL_BUF_LINES = LCD_V_RES/4` = 60 righe ciascuno. Commento nel file: con WiFi
  attivo la RAM interna è preziosa, scendere a `/8` se serve liberare memoria.
- `lvgl_task()`: task FreeRTOS dedicato (stack 4KB, priorità 2) che in loop prende
  il lock, chiama `lv_timer_handler()`, rilascia il lock, e dorme per il delay
  ritornato (clampato tra `LVGL_TASK_MIN_DELAY_MS`=1 e `LVGL_TASK_MAX_DELAY_MS`=500).
- `Display_Init()`: sequenza completa — bus QSPI → panel IO → driver SH8601 →
  `lv_init()` → buffer/display driver → tick timer → mutex → avvio task di
  rendering. **Non chiama più `Touch_Init()`** (era l'unico punto di
  accoppiamento col touch nel vecchio `lvgl_port_init()` monolitico): il task
  viene creato come ultimo passo, quindi qualunque indev registrato *dopo*
  (es. da `Touch_RegisterLvglIndev()`) deve avvolgere la registrazione nel
  lock — vedi `AMOLED191_Touch`.

**Da sapere**: bus QSPI a 40MHz (`SH8601_PANEL_IO_QSPI_CONFIG`, `pclk_hz`), valore
prudente — i demo vendor arrivano fino a 75MHz ma tearing/corruzione vanno
indagati abbassando la frequenza prima di sospettare il codice di disegno.

**`esp_lcd_sh8601.h` / `.c`** — driver vendor Espressif/Waveshare per il
controller pannello SH8601 (usato per pilotare il RM67162 fisico — le due
sigle non coincidono, vedi `docs/ESP32-S3-AMOLED-1.91-Guide.md` §6.1), spostato
qui invariato. File di libreria, non applicativo: **non si modifica** se non
per bug fix (non c'è più una seconda copia da tenere allineata: prima del
refactor esisteva anche in `examples/Orientation_IMU/`, ora è un file solo).

Header pubblico: `sh8601_lcd_init_cmd_t`, `sh8601_vendor_config_t`,
`esp_lcd_new_panel_sh8601()`, macro `SH8601_PANEL_BUS_QSPI_CONFIG`/
`SH8601_PANEL_IO_QSPI_CONFIG` (precompilano i default per questo controller:
`lcd_cmd_bits=32`, `lcd_param_bits=8`, `quad_mode=true`). Implementazione:
`tx_param()`/`tx_color()` (helper statici col framing QSPI, opcode `0x02`/
`0x32`), `panel_sh8601_init()`, `panel_sh8601_draw_bitmap()` (CASET/RASET per
frame), mirror X supportato/mirror Y no, swap_xy non supportato. Zero
riferimenti a touch/I2C (driver puramente QSPI). SPDX Espressif,
Apache-2.0 — trattalo come libreria vendor upstream.

---

### `AMOLED191_Touch` — `AMOLED191_Touch.h` (+ `touch_bsp.c`, `touch_lvgl_indev.c`)

**Ruolo**: driver touch FT3168 su I2C, con wiring LVGL opzionale separato.
Piccolo, scritto ad-hoc (non un componente Espressif ufficiale).

**Header pubblico (`AMOLED191_Touch.h`)**, umbrella su `touch_bsp.h`:
- `Touch_Init(void)` — porta su il bus (`Core_I2CBusInit()`, libreria
  `AMOLED191_Core`) e configura il touch FT3168. Nessuna dipendenza da LVGL.
- `getTouch(uint16_t *x, uint16_t *y)` → `1` se c'è un tocco, `0` altrimenti.
- `Touch_RegisterLvglIndev(void)` — **opzionale**, da chiamare dopo
  `Display_Init()`: registra il touch come input device LVGL (pointer).

**`touch_bsp.c`** (hardware, LVGL-agnostico):
- FT3168 indirizzo `0x38`, letture via `Core_I2CBusInit()` per il bus (non
  installa più il driver I2C direttamente: prima del refactor era l'unico
  posto nel repo che lo faceva, ora quel ruolo è di `AMOLED191_Core`).
- `Touch_Init()`: chiama `Core_I2CBusInit()`, poi scrive `0x00` al registro
  `0x00` (modalità normale).
- `getTouch()`: legge registro `0x02` (numero di tocchi); se >0 legge 4 byte dal
  registro `0x03` — **Y nei primi 2 byte, X nei successivi 2** (ordine invertito
  rispetto a quanto ci si aspetterebbe), clampa a 536×240, poi fa
  `*y = 240 - *y` (flip verticale — necessario perché il controller conta Y al
  contrario rispetto al sistema di coordinate del pannello/LVGL).
- `I2C_writr_buff()` / `I2C_read_buff()`: helper I2C generici usati
  internamente (nome `writr` con refuso, non `write` — lasciato così, è
  cosmetico). La funzione morta `I2C_master_write_read_device()` (mai
  dichiarata in header, mai chiamata) presente nel vecchio `touch_bsp.c` è
  stata rimossa durante lo spostamento in libreria.

**`touch_lvgl_indev.c`** (LVGL glue, nuovo in questo refactor):
- `Touch_RegisterLvglIndev()`: crea e registra un `lv_indev_drv_t` di tipo
  `LV_INDEV_TYPE_POINTER` con `read_cb` che chiama `getTouch()`. Avvolge
  `lv_indev_drv_register()` in `lvgl_lock(-1)`/`lvgl_unlock()` (da
  `AMOLED191_Display.h`) perché, a differenza del vecchio `lvgl_port_init()`
  monolitico (dove la registrazione avveniva prima che il task di rendering
  esistesse), ora può essere chiamata dopo che `Display_Init()` ha già
  avviato `lvgl_task` — senza il lock ci sarebbe una race tra
  `lv_indev_drv_register()` e `lv_timer_handler()` che scorre le stesse liste
  interne di LVGL.
- Questo è l'unico punto di accoppiamento tra `AMOLED191_Touch` e
  `AMOLED191_Display` (dipende da `lvgl_lock`/`lvgl_unlock` e da `lvgl.h`):
  `touch_bsp.c` da solo resta completamente LVGL-agnostico.

**Da sapere**: nessun uso dell'interrupt touch (GPIO41, vedi Guide.md) — questo
driver fa polling puro tramite il `read_cb` registrato in LVGL. `TP_RST` non
è gestito qui perché è fisicamente legato al reset del pannello (vedi Guide.md §7).

---

### `AMOLED191_IMU` — `AMOLED191_IMU.h` / `.cpp`

**Ruolo**: mini-driver per l'IMU onboard QMI8658 (solo accelerometro, non
gyro), usato oggi solo da `examples/Orientation_IMU/` ma disponibile a
qualunque sketch includa la libreria.

**Header**: `imu_init(void)` → `false` se il chip non risponde;
`imu_read_accel(float *ax, float *ay, float *az)` → valori in g.

**Implementazione**:
- Bus I2C **condiviso** col touch — `imu_init()` chiama `Core_I2CBusInit()`
  come primo passo (libreria `AMOLED191_Core`), quindi funziona indipendentemente
  dal fatto che `Touch_Init()` sia stato chiamato prima, dopo o per niente
  (prima del refactor assumeva implicitamente che `Touch_Init()` avesse già
  installato il bus: un IMU-senza-Touch avrebbe fallito silenziosamente,
  indistinguibile da "chip non trovato" — bug risolto con questo spostamento).
- Indirizzo `0x6B` (commento: "se WHO_AM_I fallisce, prova 0x6A" — **non
  implementato come fallback automatico nel codice**, è solo una nota per debug
  manuale; vedi anche la nota di verifica in `docs/ESP32-S3-AMOLED-1.91-Guide.md` §8,
  che invece indica 0x6B come l'unico indirizzo atteso su questa board).
- `imu_init()`: legge `WHO_AM_I` (registro `0x00`, atteso `0x05`), poi scrive
  `CTRL1=0x40` (auto-increment per lettura a blocco — **valore diverso** da
  quello suggerito nella guida hardware, `0x60`; non ri-verificato a livello di
  bit contro il datasheet, ma questo è il valore usato dalla demo funzionante),
  `CTRL2=0x05` (accelerometro ±2g, ODR ~250Hz), `CTRL7=0x01` (abilita solo
  l'accelerometro), poi 20ms di attesa.
- `imu_read_accel()`: legge 6 byte da `0x35` (`QMI_AX_L`), converte i 3
  `int16_t` con sensibilità `1/16384` g/LSB (fondo scala ±2g).

**Da sapere**: nessuna gestione degli interrupt IMU (GPIO45/46) — solo polling
dal `loop()` dello sketch che la usa. Nessun task dedicato: per design, resta
un modulo passivo (vedi `CLAUDE.md`, sezione architettura).

---

### `AMOLED191_SD` — `AMOLED191_SD.h` / `.cpp`

**Ruolo**: microSD onboard in **SDMMC a 1 bit**, con una API volutamente
minuscola e orientata a un solo caso d'uso: accodare righe di testo (log CSV).
Sottile strato sopra `SD_MMC` del core Arduino, non su `esp_vfs_fat_*` grezzo.

**API pubblica**:
- `SDCard_Init(void)` → `bool` — **è l'unica init del repo che può fallire per
  cause esterne** (card assente, non FAT32, scheda V1), quindi ritorna un esito
  invece di essere `void` come le altre. Idempotente e ri-tentabile: se già
  montata ritorna subito `true`, e se il mount fallisce lascia tutto pulito, così
  lo sketch può richiamarla più tardi (card infilata a board accesa).
- `SDCard_IsMounted(void)`, `SDCard_LastError(void)` (stringa breve in italiano,
  pensata per finire direttamente a schermo), `SDCard_SizeMB(void)`,
  `SDCard_Exists(const char *path)`.
- `SDCard_AppendLine(const char *path, const char *line)` — accoda una riga e il
  newline, creando il file se manca.
- `SDCard_WriteHeaderIfNew(const char *path, const char *header)` — scrive
  l'intestazione **solo se il file non esiste**, per non ripeterla ad ogni
  riavvio; ritorna `true` anche quando non c'era niente da fare.

**Implementazione**:
- Pin V2 fissi (`SD_PIN_CLK 9`, `SD_PIN_CMD 42`, `SD_PIN_D0 8`), non
  configurabili di proposito: sono saldati sulla scheda, non una scelta di
  progetto.
- `SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)` —
  `mode1bit=true`; `format_if_mount_failed=**false**` (formattare la card di
  qualcuno perché "il mount non riesce" non è un comportamento accettabile);
  20MHz invece del massimo, perché in 1 bit la frequenza alta è la prima causa
  di card che montano "a volte".
- `SDCard_AppendLine()` apre, scrive e **richiude** il file ad ogni riga. Costa
  qualche millisecondo in più di un handle tenuto aperto, ma se salta
  l'alimentazione — su un impianto a 12V succede — si perde al massimo l'ultima riga
  invece dell'intero file. Verificato sul campo: in una run di 66 h di
  `DHT11_SD_Logger` la card è stata estratta senza spegnere pulito e il CSV era
  integro fino all'ultimo byte, 3966 righe su 3966.

**Da sapere**:
- **Nessun `lvgl_lock()` attorno a queste chiamate**: la SDMMC è un controller a
  sé e non condivide pin né bus col QSPI del pannello (unico caso, in questo
  repo, di periferica che convive col rendering senza arbitraggio). Sono però
  bloccanti per decine di ms: da `loop()`/da un task tuo, **mai** da dentro una
  callback di evento LVGL.
- **Solo schede V2.** Sulle V1 la SD sta su SPI3 con CLK=GPIO47, lo stesso pin
  del PCLK del display: non basta cambiare i `#define`, servirebbe condividere
  l'host SPI2 riscrivendo anche `AMOLED191_Display`. Non c'è modo di distinguere le
  revisioni a runtime (l'esempio ufficiale Waveshare le seleziona a compile-time
  con `#ifdef VersionControl_V2`) — se `SDCard_Init()` fallisce con una card
  sicuramente buona e FAT32, la scheda è probabilmente una V1.
- Su V2 il GPIO9 porta **anche il TE (tearing effect) del pannello**. Resta
  inattivo finché non gli si manda il comando `0x35`, che la sequenza di init in
  `AMOLED191_Display.cpp` non manda: non abilitarlo, o va in conflitto col clock
  della card.
- Nessuna API di lettura/enumerazione: si può solo scrivere e chiedere se un
  path esiste. Conseguenza pratica in `AMOLED191_SD.cpp:92`:
  `SDCard_WriteHeaderIfNew()` controlla l'**esistenza** del file, non che
  l'intestazione combaci — cambiando il formato di un CSV già presente su una
  card, le righe nuove si accodano in silenzio sotto un header vecchio (vedi la
  nota in testa a `DHT11_SD_Logger.ino`).

---

### `EspNowLink` — `EspNowLink.h` (+ `link_peer.h/.cpp`, `link_node.cpp`, `link_hub.cpp`)

**Ruolo**: livello di comunicazione **ESP-NOW** hub↔nodi per reti di sensori/attuatori.
Scelto invece di MQTT/WiFi perché alcuni nodi sono a batteria e non serve
infrastruttura broker/AP. **Indipendente da LVGL e da `AMOLED191_Display`**: gira
anche su una scheda senza schermo, che è il caso tipico di un nodo sensore vero
(vedi `examples/Link_Node_Demo`). Costruita sopra la libreria ufficiale
`ESP_NOW`/`ESP_NOW_Peer` bundled nel core Arduino ESP32
(`.../packages/esp32/hardware/esp32/<versione>/libraries/ESP_NOW/`), non su
`esp_now.h` grezzo: quella gestisce già peer, canale e scoperta di mittenti
sconosciuti.

**`EspNowLink.h`** — unico header pubblico:
- `ESPNOW_LINK_CHANNEL` (6) — canale WiFi fisso, **deve** essere lo stesso su
  hub e nodi. Vale per i dispositivi **non** connessi a un access point (il caso
  normale di una rete di nodi a batteria).
- `ESPNOW_LINK_CHANNEL_CURRENT` (0) — "usa il canale attuale, non toccarlo": da
  passare a `Link_InitEx()` quando il dispositivo è **anche** connesso a un AP,
  dove il canale lo detta il router e un `esp_wifi_set_channel()` farebbe cadere
  la connessione. È il caso del nodo camera `starters/XIAO_S3_Camera/`.
- `link_node_type_t` (UNKNOWN/HUB/SENSOR_TEMPERATURE/SENSOR_WATER_LEVEL/
  SENSOR_BATTERY/ACTUATOR/**CAMERA**) e `link_msg_type_t`
  (HELLO/WELCOME/DATA/COMMAND).
  Aggiungere tipi **in coda** non rompe la compatibilità: sul wire è un `uint8_t`.
- `link_message_t` — payload unico da **37 byte**: `protocol_version`,
  `msg_type`, `node_type` (del mittente), `name[16]`, `seq`, `battery_mv`
  (0 = alimentazione fissa), `value[3]` (significato dipendente dal tipo).
  Molto sotto i 250 byte di `ESP_NOW_MAX_DATA_LEN` v1.0 — limite scelto
  deliberatamente al posto dei 1470 della v2.0 per restare compatibili con
  qualunque chip ESP32 finisca a fare da nodo.
- `Link_Init(self_type, self_name)` (una sola volta in `setup()`; decide il
  ruolo), `Link_OnMessage(cb)`.
- `Link_InitEx(self_type, self_name, channel)` — stessa cosa con il canale
  esplicito; `Link_Init()` è il wrapper che passa `ESPNOW_LINK_CHANNEL`. Il
  canale scelto finisce in `g_link_channel` (interno) ed è quello con cui
  vengono registrati **tutti** i peer, hub e nodi. Attenzione: ESP-NOW ha una
  radio sola, quindi passare `ESPNOW_LINK_CHANNEL_CURRENT` su un nodo connesso
  al WiFi obbliga a inizializzare **anche l'hub** sul canale di quell'AP
  (`Link_InitEx(LINK_NODE_HUB, "Hub", canale_AP)`), altrimenti i due non si
  sentono.
- Ruolo nodo: `Link_Node_Poll()`, `Link_Node_IsPaired()`, `Link_Node_SendData()`.
- Ruolo nodo, per chi **dorme**: `Link_Node_ResumeWithHub()` riprende con un hub
  già noto saltando HELLO/WELCOME (il MAC se lo conserva il nodo in RTC memory),
  e `Link_Node_SetSeq()`/`Link_Node_GetSeq()` portano il contatore di sequenza
  attraverso il deep sleep. **Il secondo non è opzionale**: l'hub scarta un DATA
  il cui `seq` è uguale all'ultimo visto, e un nodo che riparte da zero ad ogni
  risveglio viene sentito una volta sola e poi ignorato per sempre — mentre dalla
  sua parte tutto sembra a posto, perché l'ACK di ESP-NOW è di livello radio e
  arriva comunque.

- Ruolo hub: `Link_Hub_Poll()`, `Link_Hub_SetPairingMode()`,
  `Link_Hub_GetPeerCount()`, `Link_Hub_GetPeerInfo()`, `Link_Hub_SendCommand()`.
- Chiamare le `Link_Node_*` dopo essersi inizializzati come hub (o viceversa)
  non fa nulla: il ruolo è deciso una volta sola da `Link_Init()`.

**`link_peer.cpp`** — parte comune ai due ruoli:
- `Link_Init()`/`Link_InitEx()`: `WIFI_STA` con attesa di `WiFi.STA.started()`
  **a timeout 5 s**
  (evita l'hang infinito se il driver non parte) → `ESP_NOW.begin()` → e solo
  **dopo** `esp_wifi_set_channel()` (saltata del tutto se il canale richiesto è
  `ESPNOW_LINK_CHANNEL_CURRENT`). L'ordine non è cosmetico: un
  `WiFi.setChannel()` chiamato prima viene ignorato in silenzio su alcune
  combinazioni di chip, e il frame esce sul canale sbagliato — l'invio locale
  sembra riuscito ma `onSent()` è sempre `false`. Poi `esp_wifi_set_protocol()`
  con lo stesso bitmask 11B|11G|11N su entrambi i lati (i default variano tra
  generazioni di chip) e `esp_wifi_set_ps(WIFI_PS_NONE)` (il modem-sleep fa
  perdere unicast mentre il broadcast passa lo stesso).
- `link_parse_message()`: valida lunghezza esatta e `protocol_version`, e copia
  **sempre con `memcpy`** in una struct locale — mai un cast diretto del buffer
  ricevuto, che arriva senza garanzie di allineamento (da cui anche il
  `__attribute__((packed))` sulla struct, necessario e non solo prudente).
- `class LinkPeer : public ESP_NOW_Peer` — un peer (il nodo visto dall'hub, o
  l'hub visto dal nodo). `onReceive()` è generico rispetto al ruolo: valida,
  aggiorna `lastSeenMs`/`lastData`, e se riceve un HELLO da un peer **già noto**
  rialza `welcomePending` (un nodo che si riavvia perde il pairing mentre l'hub
  non lo dimentica mai — senza questo resterebbe in attesa per sempre, perché
  `onNewPeer` non riscatta per un MAC già registrato).
- `sendReliable()`: invia, attende la conferma di `onSent()` e ritenta, con
  backoff crescente + **jitter casuale** (`30 + attempt*40 + random(0,60)` ms) —
  un ritardo fisso rifarebbe collidere due nodi che hanno fallito nello stesso
  istante.
- **La sincronizzazione tra `onSent()` (task del driver WiFi, tipicamente Core 0)
  e chi attende la conferma (`loop()`, tipicamente Core 1) usa un semaforo
  FreeRTOS, non un `volatile bool`**: `volatile` impedisce solo il riordino del
  compilatore, non garantisce la visibilità tra core su un dual-core. Con un bool
  nudo il ritentativo non vedeva mai la conferma in tempo ed esauriva sempre
  tutti i tentativi anche a invio riuscito — bug reale trovato su hardware.

**`link_node.cpp`** — ruolo nodo: HELLO in broadcast ogni
`LINK_HELLO_INTERVAL_MS` (2000) finché non associato, poi DATA in unicast con
`sendReliable()`. `node_on_new_peer()` accetta il WELCOME dell'hub (scarta
qualunque altro `msg_type`/`node_type`) e si ferma al primo: non ci si
"ri-accoppia" con un secondo hub.

**`link_hub.cpp`** — ruolo hub:
- `hub_on_new_peer()` accetta un MAC sconosciuto solo mentre
  `Link_Hub_SetPairingMode(true)` è attivo, e in **due** casi: un `HELLO` in
  broadcast (nodo che cerca un hub) oppure un `DATA` in **unicast** (nodo che
  si crede già associato, ma che l'hub ha perso a un riavvio — il registro sta
  in RAM). Senza il secondo caso quel nodo resterebbe invisibile per sempre,
  in modo silenzioso da entrambe le parti: l'ACK di ESP-NOW è di livello radio,
  quindi il nodo conta i propri invii come riusciti mentre l'hub non vede
  niente. Nel caso `DATA` il messaggio viene anche salvato in `lastData`, o la
  prima lettura andrebbe persa (`onReceive()` non è stato chiamato per quel
  pacchetto: il peer non esisteva ancora).
- Il WELCOME **non parte mai da dentro il callback di ricezione** (gira nel task
  del driver WiFi e va tenuto breve, stessa regola dei callback LVGL): viene
  accodato con `welcomePending` e inviato da `Link_Hub_Poll()`, dal `loop()`. Il
  flag viene pulito **solo a invio riuscito**, così un fallimento transitorio si
  ritenta al giro dopo invece di bruciare la finestra di pairing di quel nodo.
- Registro peer in array statico da `ESP_NOW_MAX_TOTAL_PEER_NUM`, **solo in RAM**
  (nessuna persistenza SD/NVS: scelta deliberata, non ancora implementata — ad
  ogni riavvio dell'hub i nodi vanno riassociati).
- Scritto dal callback di ricezione e letto da `loop()`/task LVGL: concorrenza
  vera, protetta da `portMUX_TYPE` — a differenza di `Core_I2CBusInit()`, che è
  chiamata solo in sequenza da `setup()` e non ha bisogno di lock.

**Da sapere**:
- **Scoperta bidirezionale**: `ESP_NOW.onNewPeer()` scatta per MAC *sorgente*
  sconosciuto, quindi non è una cosa del solo hub — anche il nodo deve gestirlo,
  perché l'hub gli è sconosciuto finché non arriva il WELCOME. `Link_Init()`
  registra il gestore giusto in base al ruolo.
- **Niente RSSI nella callback applicativa**: la libreria ESP_NOW ufficiale lo
  espone solo in `onNewPeer()`, non nel dispatch `onReceive()` dei peer già
  aggiunti. Fornirlo sarebbe disponibile per il solo primissimo messaggio di
  pairing e non per i DATA successivi — incoerenza evitata di proposito.
- **Limite noto sull'hardware**: l'unicast tra hub ESP32-S3 e nodo ESP32
  "classico" (Xtensa D0WD) è risultato inaffidabile/lento ad associarsi
  (broadcast sempre ok, WELCOME/unicast spesso perso), coerente con
  [espressif/arduino-esp32#10895](https://github.com/espressif/arduino-esp32/issues/10895).

- **`Link_SetChannel(ch)` / `Link_GetChannel()`** (2026-08-27): cambio di canale
  a caldo, con riallineamento dei peer gia' registrati. **Solo per dispositivi
  non connessi a un AP** — la radio e' una sola e il canale lo detta l'access
  point. Serve al nodo a batteria che si risveglia sul canale sbagliato.
- **`Link_Node_ResendLast(tentativi, timeout)`**: rimanda l'ultimo DATA senza
  toccare il `seq`. Riprovare con `Link_Node_SendData()` incrementerebbe il
  contatore ad ogni canale, e l'hub leggerebbe quei salti come perdite radio.
  Con nodi ESP32-C3 il pairing è immediato. Per nuovi nodi preferire S2/S3/C3/C6.
- **Quanto blocca**: `sendReliable()` è sincrona, con default `max_attempts=3` e
  `ack_timeout_ms=300`. Il caso peggiore (tre tentativi tutti in timeout) è
  3×300 ms di attesa più i due backoff intermedi (max 90 e 130 ms) ≈ **1,1 s**,
  ereditato da `Link_Node_SendData()`, `Link_Hub_SendCommand()` e da
  `Link_Hub_Poll()` per ogni WELCOME accodato. Alzare il timeout si paga tutto
  sul `loop()` del chiamante: durante la campagna di test ESP-NOW era stato
  portato a 1000 ms (≈ 3,1 s di blocco) e quello era, da solo, la causa dei
  "ritardi" che si stavano indagando.

---

### `library.properties` (tutte e sei le librerie)

Nessun campo `depends`: la risoluzione delle dipendenze che conta con
`--libraries`/le junction locali è quella basata sugli `#include` letterali
nel codice, scansionati ricorsivamente da arduino-cli — `depends` è
consultato solo da Library Manager/`arduino-cli lib install` per librerie
da registry, quindi sarebbe comunque inerte qui. `category`/`architectures=esp32`
seguono le convenzioni Arduino standard.

---

## `starters/AMOLED_1.91_LVGL/` — il template della board AMOLED

### `AMOLED_1.91_LVGL.ino`

**Ruolo**: sketch principale, punto di ingresso Arduino (`setup()`/`loop()`). È
l'unico file pensato per essere riscritto ad ogni nuovo progetto copiato dal
template.

- `setup()`: chiama `Display_Init()`, poi `Touch_Init()`/
  `Touch_RegisterLvglIndev()` (commenta queste due righe per un progetto senza
  touch), poi `ui_init()` sotto `lvgl_lock()` (crea gli oggetti LVGL), poi
  lascia spazio per WiFi/SD/sensori (commentati come placeholder:
  `#include <WiFi.h>` / `<SD.h>`).
- `loop()`: esempio di pattern periodico (`millis()` ogni 1000ms) che legge un
  sensore fittizio (`read_sensor()`, ritorna sempre `23.5f` — da sostituire) e
  aggiornerebbe una label LVGL sotto lock (riga commentata,
  `lv_label_set_text(ui_LabelTemp, buf)`, perché lo stub non ha ancora quell'oggetto).
  Chiude con `delay(5)`.
- Commento in testa ribadisce la regola del lock e le impostazioni Tools richieste
  (Board, Flash, Partizione, PSRAM, USB CDC).

**Dipendenze**: `AMOLED191_Display.h`, `AMOLED191_Touch.h` (libreria), `ui.h`.
**Da sapere**: il nome del file **deve** coincidere col nome della cartella
(vincolo Arduino) — è il primo blocco quando si copia il template. Copiando
`starters/AMOLED_1.91_LVGL/` fuori da questo repo, serve portare/collegare anche `libraries/`
(vedi `CLAUDE.md`, "Avviare un nuovo progetto").

---

### `lv_conf.h`

**Ruolo**: configurazione LVGL **a livello di progetto** (letta grazie a
`LV_CONF_INCLUDE_SIMPLE` in `build_opt.h`). È sostanzialmente il template
default di LVGL 8.3.11 con pochi valori adattati a questa board — **non è stata
sfoltita** per risparmiare RAM/flash: quasi tutti i widget ed extra sono
abilitati (`=1`). Resta deliberatamente per-sketch (non spostata in libreria):
ogni progetto deve poter tunare la propria config LVGL indipendentemente
(es. abilitare `LV_USE_FS_FATFS` per caricare immagini da SD in un futuro
esempio, senza toccare gli altri).

Valori significativi/non-default:
- `LV_COLOR_DEPTH 16` + `LV_COLOR_16_SWAP 1` — RGB565 con byte-swap, richiesto
  dall'interfaccia QSPI di questo pannello.
- `LV_TICK_CUSTOM 0` — il tick NON è automatico: coerente con
  `lvgl_tick_cb()`/`lv_tick_inc()` chiamato a mano ogni 2ms in
  `AMOLED191_Display.cpp`.
- `LV_MEM_SIZE (48*1024)` — 48KB di heap interno LVGL (allocazione statica, RAM
  interna, non PSRAM). Separato dai buffer di disegno DMA allocati a parte in
  `AMOLED191_Display.cpp`.
- `LV_DISP_DEF_REFR_PERIOD` / `LV_INDEV_DEF_READ_PERIOD` = 30ms — periodo interno
  di LVGL per invalidazione/lettura input; il task di rendering vero e proprio
  (`lvgl_task` in `AMOLED191_Display.cpp`) ha la sua logica di delay indipendente.
- `LV_FONT_DEFAULT &lv_font_montserrat_14`, con Montserrat 12/14/16 abilitati
  (altre taglie disattivate).
- `LV_USE_THEME_DEFAULT 1`, `LV_THEME_DEFAULT_DARK 0` → tema chiaro di default
  finché una UI SquareLine non lo sovrascrive esplicitamente.
- `LV_USE_FS_*` tutti a `0` — nessun filesystem collegato a LVGL. Se in futuro si
  vuole caricare immagini/font da microSD via widget LVGL (`lv_img_set_src` con
  path), va abilitato e configurato `LV_USE_FS_FATFS` (driver più comune per SD
  su ESP32) — non è ancora stato fatto in questo template.
- `LV_BUILD_EXAMPLES 1`, `LV_USE_DEMO_WIDGETS 1`, `LV_USE_DEMO_MUSIC 1` — compila
  anche le demo integrate di LVGL nella libreria (non richiamate da nessun file
  di questo progetto, quindi codice morto a meno di chiamarle esplicitamente;
  occupano flash ma con margine ampio — vedi `CLAUDE.md` per i numeri di
  compilazione).

**Da sapere**: se si cambia `LV_COLOR_DEPTH`/`LV_COLOR_16_SWAP` bisogna verificare
che combaci ancora con `bits_per_pixel`/`rgb_ele_order` passati a
`esp_lcd_new_panel_sh8601()` in `AMOLED191_Display.cpp`, e con le impostazioni del
progetto SquareLine (deve restare 16 bit).

---

### `build_opt.h`

**Ruolo**: singola riga di flag globali passati dall'IDE Arduino al compilatore
per **tutti** i file dello sketch (inclusi quelli delle librerie in `libraries/`
pullate dentro la build):

```
-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
```

- `LV_CONF_INCLUDE_SIMPLE`: LVGL cerca `lv_conf.h` lungo l'include path (quello di
  progetto) invece che a percorso relativo fisso — è quello che fa funzionare il
  `lv_conf.h` di questa cartella.
- `LV_LVGL_H_INCLUDE_SIMPLE`: i file generati da SquareLine includono `lvgl.h` in
  modo "semplice" (`#include "lvgl.h"` invece di percorso relativo) — senza
  questo, dopo un export SquareLine si ottengono errori `lvgl.h: No such file`.

Resta per-sketch come `lv_conf.h`, per lo stesso motivo (ogni progetto deve
poter avere le proprie flag di build).

**Da sapere**: riconosciuto solo da Arduino IDE (meccanismo `build_opt.h`
specifico dei sketch), non da `arduino-cli` in automatico a meno che il file sia
nella cartella dello sketch — è lì, quindi `arduino-cli compile` lo applica
correttamente (verificato: compilazione riuscita con le librerie in
`libraries/`, vedi `CLAUDE.md`).

---

### `ui.h` / `ui.c`

**Ruolo**: **stub segnaposto**, destinato a essere interamente sovrascritto
dall'export "Export UI Files" di SquareLine Studio.

- `ui.h`: dichiara solo `void ui_init(void)` — lo stesso contratto che genera
  SquareLine, così il resto del progetto (`AMOLED_1.91_LVGL.ino`) non deve cambiare quando
  arriva l'export vero.
- `ui.c`: crea una singola label centrata con testo "Starter pronto" — serve solo
  a confermare che display/touch/LVGL funzionano prima di disegnare la UI reale.

**Da sapere**: dopo un export SquareLine, questi due file vengono sostituiti (non
uniti) — arrivano anche `ui_helpers.*`, `ui_events.*`, gli screen e gli asset.
`ui_events.c` (quando esiste) **non** viene sovrascritto ai re-export successivi,
quindi è il posto giusto per la logica degli eventi widget.

---

## `starters/C3_OLED_OTA/` — il template ESP32-C3 + OLED + OTA

Template per i **nodi** con display piccolo e aggiornamento via rete. **Non
condivide niente** con la board AMOLED: nessuna libreria di `libraries/`, nessun LVGL,
nessun `lv_conf.h`/`build_opt.h`. Usa Adafruit SSD1306+GFX e i moduli del core
(`WiFi`, `ESPmDNS`, `ArduinoOTA`, `WebServer`, `Update`, `Wire`). È self-contained
per scelta: si può spostare fuori dal repo e continua a compilare.

FQBN diverso dal resto del repo:
`esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`, **senza**
`--libraries libraries`.

### `C3_OLED_OTA.ino`

**Ruolo**: `setup`/`loop` + disegno OLED. È il file in cui va la logica
applicativa del nodo.

- Pin in cima come `static constexpr`: `PIN_SDA`=5, `PIN_SCL`=6 (scelti per **non**
  toccare `PIN_LED`=8, il LED blu onboard attivo LOW, né il tasto BOOT su GPIO9),
  `OLED_ADDR`=`0x3C`, `OLED_RST`=`-1` (moduli a 4 pin, nessuna linea di reset).
- `Wire.begin(PIN_SDA, PIN_SCL)` con `periphBegin=false` nella init dell'OLED,
  per non farsi sovrascrivere i pin appena passati.
- `drawStatus()`: schermata a riposo (hostname, `FW_VERSION`, stato WiFi, IP,
  indirizzo `/update`) ridisegnata ~4 fps dal `loop()`, più una pallina che
  rimbalza nella fascia bassa — serve a vedere a colpo d'occhio che il firmware
  gira e non è piantato.
- `onOtaProgress()`: registrata con `net_setOtaProgressCb()` **prima** di
  `net_begin()`, disegna la barra di avanzamento durante un update. `otaActive`
  sospende il disegno normale mentre l'update è in corso.

### `net_ota.h` / `net_ota.cpp`

**Ruolo**: tutto il boilerplate di rete e OTA, isolato dal `.ino`. Di norma non
si tocca.

**API pubblica** (`net_ota.h`, 31 righe):
- `net_setOtaProgressCb(cb)` — callback opzionale `(int percent, const char *what)`
  per il feedback a schermo. `percent` è 0..100, oppure **-1 se la dimensione è
  ignota** (upload web senza `Content-Length`). Va impostata **prima** di
  `net_begin()`.
- `net_begin()` — una volta in `setup()`, dopo `Serial` e dopo l'init del display
  se vuoi vedere l'avanzamento: connette il WiFi (bloccante, timeout 15 s, poi
  ritenta in background), avvia `ArduinoOTA` (che porta su anche mDNS con
  `OTA_HOSTNAME`) e il web server con `/` (form) e `/update`
  (POST multipart → `Update`).
- `net_loop()` — **a ogni giro** di `loop()`: `ArduinoOTA.handle()` +
  `server.handleClient()`. Se il `loop()` blocca a lungo, l'OTA smette di
  rispondere: è il vincolo principale da rispettare aggiungendo logica.
- `net_isConnected()`, `net_ip()` — stato per la UI.

**Da sapere**: l'autenticazione (password ArduinoOTA + basic-auth su `/update`)
è dimensionata per una **LAN fidata**, non per esporre la scheda su Internet.

### `secrets.h.example` → `secrets.h`

**Ruolo**: credenziali WiFi (`WIFI_SSID`/`WIFI_PASSWORD`), hostname mDNS
(`OTA_HOSTNAME`) e password OTA (`OTA_PASSWORD`, `WEB_OTA_USER`/`WEB_OTA_PASS`).

Versionato c'è **solo il `.example`** con i segnaposto: `secrets.h` è escluso dal
`.gitignore` di radice perché questo repository è **pubblico**. Si copia il
template e si riempie.

**Da sapere**: la regola nel `.gitignore` è il **pattern** `secrets.h`, non un
percorso: vale per qualunque cartella, quindi copiare lo starter altrove nel repo
non scopre il file. Fuori dal repo, invece, serve un `.gitignore` con la stessa
riga. Se credenziali vere finiscono committate su un repo pubblico, la
risposta giusta è **cambiare la password della rete**, non riscrivere la storia
di git: una volta pubblicata va considerata compromessa.

---

## `starters/XIAO_S3_Camera/` — il template nodo camera (XIAO ESP32-S3 Sense)

Template di **nodo camera con sensore di movimento**. La catena è: PIR HC-SR501
→ scatto → JPEG su microSD → messaggio DATA all'hub via ESP-NOW, più una web UI
per guardare/scattare/scaricare e l'OTA come sul C3.

A differenza di `starters/C3_OLED_OTA/` **non è self-contained**: usa
`libraries/EspNowLink` (il protocollo dell'hub, che non ha senso duplicare).
Tutto il resto viene dal core ESP32 — `esp_camera` (bundled: non serve nessuna
libreria dal Library Manager), `SD`+`SPI`, `WiFi`, `ESPmDNS`, `ArduinoOTA`,
`WebServer`, `Update`, `Preferences`.

FQBN: `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB`, **con**
`--libraries libraries`. Nota: su questa board `CDCOnBoot` è **già Enabled di
default** e il valore `CDCOnBoot=cdc` significa *Disabled* — l'opposto delle
altre schede del repo, dove va aggiunto esplicitamente.

### `XIAO_S3_Camera.ino`

**Ruolo**: la logica applicativa. È il file che si modifica.

- Configurazione in cima: `FW_VERSION` (cambiala ad ogni build, la web UI la
  mostra), `NODE_NAME` (max 15 caratteri, è il nome con cui il nodo si presenta
  all'hub), `PIN_PIR` = `D0` (GPIO1), `PIR_WARMUP_MS` (60 s), `COOLDOWN_S_DEFAULT`
  (30 s).
- `pir_isr()` — ISR `IRAM_ATTR` su fronte di salita: alza un `volatile bool` e
  basta. Il timestamp lo prende `loop()`, così nell'ISR non serve nemmeno
  `millis()`. L'interrupt (invece del polling) serve perché uno scatto+notifica
  può trattenere `loop()` per qualche secondo e un impulso del PIR andrebbe
  perso.
- `do_capture(sorgente, notify_hub, ...)` — l'unico punto in cui si scatta:
  `camera_grab_fresh()` → `sd_save_photo()` → `camera_release()` (sempre, anche
  se il salvataggio fallisce: i frame non restituiti finiscono i buffer) →
  eventuale `hub_notify_motion()` → riga nel CSV. L'indice della foto mandato
  all'hub si ricava dal nome (`IMG_00042.JPG` → 42).
- `handle_motion()` — scarta l'evento durante il riscaldamento del PIR o a
  sorveglianza disarmata, applica la pausa `cooldown` (senza, una persona che si
  muove riempie la card in pochi minuti), poi scatta **notificando l'hub**.
- `on_hub_command()` — ARM/DISARM/CAPTURE ricevuti dall'hub, già nel contesto di
  `loop()` (l'accodamento lo fa `hub_link`).
- Ganci `app_*()` dichiarati in `web_ui.h` e implementati qui: la web UI non sa
  niente di PIR/NVS, chiede a queste.
- `app_pump()` — chiamata dal ciclo dello stream MJPEG. **Non chiama
  `net_loop()`**: rientrare in `handleClient()` mentre si sta già servendo una
  richiesta rompe il web server.

### `camera.h` / `camera.cpp`

**Ruolo**: sensore OV2640/OV3660 della Sense. I pin sono copia fedele di
`CAMERA_MODEL_XIAO_ESP32S3` in `camera_pins.h` del core (XCLK=10, SIOD=40,
SIOC=39, D0-D7=15/17/18/16/14/12/11/48, VSYNC=38, HREF=47, PCLK=13) e **non
sono negoziabili**: sono cablati sulla scheda di espansione.

- `camera_begin()` — JPEG, doppio buffer in PSRAM, `CAMERA_GRAB_LATEST`. Senza
  PSRAM ripiega su QVGA/buffer singolo in DRAM e lo dice su Serial invece di
  fallire. Applica la correzione standard per l'OV3660 (montato su una parte
  delle Sense: esce capovolto e slavato).
- `camera_grab()` (stream, veloce) e `camera_grab_fresh()` (foto: scarta
  `CAM_STALE_FRAMES`=2 frame, così lo scatto non è l'immagine di un attimo prima
  e l'esposizione si è assestata). Ogni frame ottenuto va restituito con
  `camera_release()`.
- Risoluzione esposta come **indice in una tabella interna** (QVGA→UXGA), non
  come valore grezzo di `framesize_t`: la web UI non dipende dalla numerazione
  dell'enum di esp32-camera.
- `camera_set_quality()` accetta 10..40 — nella convenzione di esp32-camera
  valori **più bassi = immagine migliore** (sotto 10 si rischiano frame
  troncati).

### `storage.h` / `storage.cpp`

**Ruolo**: microSD della Sense. **Non** usa `AMOLED191_SD`: quella è per la SDMMC
della board AMOLED, qui la card è su **SPI** (CS=GPIO21, SCK/MISO/MOSI=7/8/9 dai
default della variante).

- Foto in `/foto/IMG_<progressivo>.JPG`, progressivo in **NVS** (non riparte da
  zero ad ogni riavvio); se il nome esiste già — card sostituita — avanza invece
  di sovrascrivere. Un JPEG scritto a metà viene cancellato: meglio nessun file
  che un file corrotto.
- CSV `/foto/eventi.csv`, colonne
  `boot_id,n,secondi_da_accensione,sorgente,file,byte,notifica_hub`. `boot_id`
  è un contatore NVS, stessa idea di `DHT11_SD_Logger`: senza orologio, "secondi
  da accensione" da solo non distingue due run nello stesso file.
- Apre/scrive/chiude ad ogni riga, come `AMOLED191_SD`: più lento, ma togliere la
  card o l'alimentazione non lascia dati in sospeso.
- `sd_name_is_safe()` — da usare **sempre** sui nomi che arrivano dal web (nega
  `/`, `\` e `..`), altrimenti l'URL apre l'intera card.
- `sd_begin()` è ri-chiamabile: senza card lo sketch deve continuare a
  funzionare (web UI e notifiche restano attive, solo senza salvataggio).

**Da sapere**: GPIO21 è anche il **LED utente** della XIAO — con la Sense montata
lampeggia da solo ad ogni accesso alla card e non è usabile come spia. E lo slot
occupa l'intero bus SPI (sulla Sense c'è il ponticello **J3** per scollegarlo).
Lo schematico Seeed riporta il CS su GPIO3: è un errore noto, il pin buono è 21.

### `net_ota.h` / `net_ota.cpp`

**Ruolo**: gemello di `starters/C3_OLED_OTA/net_ota.*` (WiFi + ArduinoOTA + `/update`), con
tre differenze:
- **non** registra la rotta `/` — la home è la web UI della camera;
- espone `net_server()`, così `web_ui.cpp` aggiunge le proprie rotte allo stesso
  `WebServer` (registrarle dopo `server.begin()` è lecito);
- espone `net_webAuthOk()` (basic-auth condivisa), `net_rssi()` e
  `net_channel()` — quest'ultimo è il canale che l'ESP-NOW è costretto a seguire.

### `web_ui.h` / `web_ui.cpp`

**Ruolo**: pagina di controllo (PROGMEM, self-contained, nessuna CDN) e API:
`/`, `/stream`, `/snapshot.jpg`, `/api/scatta`, `/api/stato`, `/api/config`,
`/api/foto`, `/foto?f=`, `/api/elimina`. Tutte dietro la stessa basic-auth di
`/update`.

- **Lo stream MJPEG scrive la risposta a mano sul socket** invece di usare
  `server.send()`: a lunghezza sconosciuta il `WebServer` passerebbe al chunked
  encoding, che corromperebbe il multipart.
- Il `WebServer` del core è **sincrono**: finché un client tiene aperto
  `/stream` la scheda non risponde ad altro, OTA compreso. Per questo lo stream
  si autolimita a `WEB_STREAM_MAX_MS` (5 minuti) — una scheda del browser
  dimenticata aperta non deve rendere il nodo inaggiornabile — la pagina lo
  tiene spento di default e sospende il polling di stato mentre è attivo.
- Nel ciclo dello stream viene chiamata `app_pump()` ad ogni frame: PIR ed
  ESP-NOW restano vivi mentre il web server è occupato.

### `hub_link.h` / `hub_link.cpp`

**Ruolo**: il nodo visto dall'hub. Sottile strato sopra `EspNowLink`.

- `hub_begin()` va chiamata **dopo** `net_begin()`: guarda se il WiFi è connesso
  e sceglie il canale di conseguenza (`ESPNOW_LINK_CHANNEL_CURRENT` se siamo su
  un AP, il canale fisso 6 se non c'è rete). **L'hub va inizializzato sullo
  stesso canale dell'AP** con `Link_InitEx()` — è il punto fragile: se il router
  cambia canale da solo, il nodo lo segue al riavvio e l'hub no.
- La callback di ricezione **accoda e basta** (`xQueueSend`), i comandi vengono
  eseguiti da `hub_loop()` nel contesto di `loop()`: stessa regola dei callback
  LVGL sull'hub. È una coda FreeRTOS e non un flag `volatile` per lo stesso
  motivo del semaforo in `LinkPeer` (visibilità tra core).
- `hub_notify_motion()` manda un DATA con `value[0]`=n. evento,
  `value[1]`=indice della foto (-1 se non salvata), `value[2]`=1 se è finita su
  SD. È **bloccante** (conferma + ritentativi): se l'hub non risponde può
  trattenere `loop()` fino a ~1,1 s — vedi "quanto blocca" nella sezione
  `EspNowLink`.

### `secrets.h.example` → `secrets.h`

**Ruolo**: come per il C3 — `WIFI_SSID`/`WIFI_PASSWORD`, `OTA_HOSTNAME`,
`OTA_PASSWORD`, più `WEB_USER`/`WEB_PASS` (qui proteggono **tutta** la UI, non
solo `/update`: la pagina mostra le foto della camera). Versionato c'è solo il
`.example`; il file vero è coperto dal pattern `secrets.h` nel `.gitignore`, con
le stesse note viste per il C3.

---

## `examples/` — sketch di esempio

Sei cartelle sketch indipendenti dagli starter: non si copiano per iniziare un
progetto (per quello c'è `starters/`), si compilano e caricano così come sono.
Nessuna contiene copie locali del codice hardware: quelle che ne hanno bisogno
includono le librerie di `libraries/` come qualunque altro sketch, le due
diagnostiche non ne usano nessuna. Quelle con schermo hanno i propri
`build_opt.h`/`lv_conf.h` (per-sketch per design, vedi sopra); quelle senza LVGL
non ne hanno bisogno e infatti non li hanno.

| Sketch | Schermo | Librerie del repo | Gira su |
|---|---|---|---|
| `Orientation_IMU` | sì | Display, Touch, IMU (+Core) | solo questa board |
| `DHT11_SD_Logger` | sì | Display, SD | solo questa board |
| `Link_Hub_Demo` | sì | Display, Touch, Link (+Core) | solo questa board |
| `Link_Node_Demo` | no | Link | qualunque ESP32 |
| `Diag_Hub` | no | nessuna | qualunque ESP32 |
| `Diag_Node` | no | nessuna | qualunque ESP32 (pensato per C3) |

---

### `Orientation_IMU/Orientation_IMU.ino`

**Ruolo**: sketch completo e funzionante — livella per veicolo basata sull'IMU
onboard. UI costruita interamente in codice (non SquareLine), utile come esempio
di pattern alternativo a `ui.c`/SquareLine.

- Dimensioni del veicolo hardcoded in testa (`TRACK_MM`, `WHEELBASE_MM` — Adria
  Matrix Axess 680 SP / Fiat Ducato Maxi) — **da adattare** per un altro veicolo; la
  direzione (segni di roll/pitch) resta corretta comunque, solo i centimetri
  dipendono dalle dimensioni reali.
- `leveling_ui_create()`: costruisce la UI sotto lock in `setup()` — pianta
  del veicolo con 4 "ruote" colorate (verde/ambra/rosso secondo il rialzo),
  bolla centrale che si sposta, pannello testo con pitch/roll, bottone CALIBRA.
- `leveling_update(pitch, roll)`: converte pitch/roll in cm di rialzo per ruota
  (trigonometria su `TRACK_MM`/`WHEELBASE_MM`), aggiorna colori/testi/posizione
  bolla.
- `loop()`: legge l'IMU ogni 20ms (media di 8 campioni per ridurre il rumore),
  filtro esponenziale lento (costante 0.05) su pitch/roll filtrati, aggiorna la
  UI ogni 300ms sotto lock. Calibrazione (`s_calibrate_req`, impostato dal
  bottone) azzera l'offset corrente.
- Roll/pitch calcolati da sola accelerazione (`atan2f`) — **nessuno yaw**
  possibile con solo accelerometro; se gli assi risultano invertiti sul tuo
  mezzo, i punti da modificare sono commentati esplicitamente nel `loop()`.
- `setup()`: `Display_Init()` → `Touch_Init()`/`Touch_RegisterLvglIndev()` →
  `leveling_ui_create()` sotto lock → `imu_init()`.

**Dipendenze**: `lvgl.h`, `AMOLED191_Display.h`, `AMOLED191_Touch.h`, `AMOLED191_IMU.h`.

---

### `DHT11_SD_Logger/DHT11_SD_Logger.ino`

**Ruolo**: primo sketch del repo che scrive su microSD — temperatura/umidità da
un DHT11 a schermo (tre "card": temperatura, umidità, numero di campioni) e una
riga di CSV per lettura valida. **Nessun touch**: è un logger, non si tocca.

- **Cablaggio**: modulo DHT11 a 3 pin su `DHT_DATA_PIN` (GPIO2 di default). I
  moduli a 3 pin hanno già il pull-up da 10k a bordo; con un sensore nudo a 4
  pin va aggiunto (4.7k–10k verso 3V3). **Non usare GPIO3** (strapping JTAG,
  deve restare flottante al reset e il modulo tiene DATA in pull-up) né 26 e
  33–37 (PSRAM). Sulla versione senza header a pettine (SKU 28596) i GPIO liberi
  sono piazzole da saldare: scegli il pin in base a quale riesci a raggiungere.
- **Cadenza**: `SAMPLE_PERIOD_MS` (60000). Il valore compare **solo** lì:
  l'etichetta a schermo e la riga di `setup()` sulla seriale sono generate da
  quel define, non scritte a mano.
- **CSV** `/dht11_log.csv`, colonne
  `boot_id,n,secondi_da_accensione,temperatura_C,umidita_pct`. La scheda non ha
  un RTC tamponato, quindi l'unico riferimento temporale onesto è "secondi da
  accensione" — che però riparte da zero ad ogni avvio, insieme a `n`, in un file
  che invece sopravvive ai riavvii. Da qui `boot_id`: un contatore in **NVS**
  (`Preferences`, namespace `dht11log`) incrementato da `boot_id_next()` ad ogni
  accensione e condiviso da tutte le righe di quella run. È mostrato anche nel
  titolo a schermo (`(avvio #N)`), così si sa quali righe del file sta scrivendo
  la scheda che si ha davanti. Ritorna 0 se la NVS non si apre: sentinella
  riconoscibile, non un conteggio sballato.
- **Senza microSD funziona lo stesso**: valori a schermo, riga di stato in rosso
  con `SDCard_LastError()`, e ritenta il mount ogni `SD_RETRY_PERIOD_MS` (30 s)
  così la card si può infilare a scheda accesa.
- `take_sample()` non gira sotto `lvgl_lock`: la lettura del DHT11 tiene le
  interruzioni disabilitate (il protocollo si misura al microsecondo) e la
  scrittura SD blocca per decine di ms — bloccare anche il rendering per tutto
  quel tempo non servirebbe a niente. Il lock si prende **solo alla fine**,
  dentro `ui_refresh()`.
- `loop()`: la finestra di campionamento riparte da `millis()` **alla fine** di
  ogni campione, così la barra di avanzamento riflette la cadenza reale invece di
  accumulare ritardo se una scrittura è andata lunga. Il prezzo è una deriva di
  ~34 ms a campione (durata di `take_sample()` + granularità del `delay(5)`);
  se un giorno servisse una cadenza assoluta, si cambia in
  `win_start_ms += win_span_ms`.

**Dipendenze**: `DHT.h` (Adafruit "DHT sensor library" + "Adafruit Unified
Sensor", da Library Manager — le stesse usate dai nodi ESP32-C3 di questo repo
(vedi `projects/EnvNode_C3`), così il DHT11 si legge allo stesso modo ovunque),
`Preferences.h` (bundled nel core), `lvgl.h`, `AMOLED191_Display.h`, `AMOLED191_SD.h`.

**Da sapere**: validato sul campo con una run di **66 h** (3966 campioni a 60 s,
zero letture fallite, zero scritture fallite, nessun buco nella sequenza). Il
formato CSV è cambiato dopo quella run (l'aggiunta di `boot_id`): su una card
che contiene ancora un log a 4 colonne, `SDCard_WriteHeaderIfNew()` non se ne
accorge — controlla solo se il file esiste — e le righe nuove si accodano a 5
campi sotto un'intestazione che ne dichiara 4. Rinomina o cancella il vecchio
file prima di riusare quella card.

---

### `Link_Hub_Demo/Link_Hub_Demo.ino`

**Ruolo**: valida `EspNowLink` dal lato hub, quello che ha lo schermo.
Mostra fino a `MAX_ROWS` (6) nodi associati con nome, tipo, ultimo
valore e "visto N s fa", più un bottone touch che accende/spegne la modalità
pairing. UI scritta a mano nello stesso stile di `Orientation_IMU`, non
SquareLine.

- Le 6 righe sono **pre-create nascoste** in `hub_ui_create()` e poi
  mostrate/nascoste a runtime, invece di creare e distruggere oggetti LVGL ogni
  volta che cambia il numero di nodi: niente allocazioni nel `loop()`.
- `pairing_toggle_cb()` è una callback di evento LVGL: **niente lock** (già
  preso) e corta — chiama solo `Link_Hub_SetPairingMode()` e aggiorna due
  etichette.
- `loop()`: `Link_Hub_Poll()` ad ogni giro (è lì che partono i WELCOME accodati),
  aggiornamento UI ogni 500 ms sotto `lvgl_lock`.
- Una riga mostra `--` finché `last_data.protocol_version` non combacia, cioè
  finché da quel nodo non è arrivato un vero DATA: un nodo appena associato
  compare subito, senza inventare un valore.

**Dipendenze**: `lvgl.h`, `AMOLED191_Display.h`, `AMOLED191_Touch.h`, `EspNowLink.h`.

**Da sapere**: da provare in coppia con `Link_Node_Demo` su una seconda scheda —
attiva il pairing qui, accendi il nodo, deve comparire una riga entro pochi
secondi con un valore che si aggiorna ogni ~5 s.

---

### `Link_Node_Demo/Link_Node_Demo.ino`

**Ruolo**: nodo sensore finto, **solo Serial** — 65 righe, nessuna dipendenza da
display/touch/pin della board AMOLED, gira su qualunque ESP32. È proprio il
punto: un nodo sensore vero, in genere, uno schermo non ce l'ha, e questo
sketch dimostra che `EspNowLink` non trascina dentro LVGL. Nessun `lv_conf.h`
né `build_opt.h` in cartella, per lo stesso motivo.

- Manda una temperatura finta (20.0–30.0 °C derivata da `millis()`) ogni ~5 s
  una volta associato.
- L'intervallo ha **jitter casuale** (±250 ms): con più nodi identici sullo
  stesso hub, un periodo fisso può farli convergere a trasmettere nello stesso
  istante man mano che i clock derivano, e a quel punto collidono ad ogni ciclo.
- `on_message()` logga il WELCOME (col MAC dell'hub) e gli eventuali COMMAND.

**Dipendenze**: `EspNowLink.h`.

---

### `Diag_Hub/` + `Diag_Node/` — diagnostica ESP-NOW

**Ruolo**: coppia di sketch **usa e getta**, scritti per misurare il tasso di
perdita reale dei pacchetti quando il pairing di `EspNowLink` si è rivelato
inaffidabile su certe combinazioni di chip. Deliberatamente al livello più basso
possibile: `esp_now.h` grezzo, **nessuna libreria di questo repo**, nessun
pairing, nessun peer unicast, nessun retry — solo broadcast di un contatore. Non
sono demo del sistema e non condividono nulla col resto del codice:
`diag_packet_t` (12 byte, `static_assert` sulla dimensione) è definita a mano e
identica nei due file, ed è tutto ciò che li lega.

- `Diag_Node` spara un pacchetto ogni `SEND_INTERVAL_MS` (500) in broadcast;
  `NODE_ID` va cambiato se se ne accendono più di uno. `boot_count` in
  `RTC_DATA_ATTR` + `esp_reset_reason()` servono a non contare un brownout come
  "pacchetti persi".
- `Diag_Hub` conta i **buchi nel numero di sequenza** (= perdita reale),
  i duplicati, RSSI last/min, e i drop di coda; riepilogo ogni 5 s. Distingue un
  nodo ripartito (salto all'indietro > 100) da un vero duplicato/riordino MAC.
- **La recv callback non stampa e non lavora**: copia il pacchetto in una coda
  FreeRTOS e basta, tutto il parsing e la stampa avvengono in `loop()`. È la
  stessa disciplina che in `EspNowLink` tiene i WELCOME fuori dal callback, qui
  applicata perché la seriale nel contesto radio fa perdere i pacchetti
  successivi.
- **Attenzione**: entrambi impostano `WIFI_PROTOCOL_LR` (Long Range), che è
  proprietario Espressif e **diverso** dal bitmask 11B|11G|11N di
  `EspNowLink`. I due mondi non si parlano: un Diag_Node non viene visto da un
  `Link_Hub_Demo` e viceversa. È voluto — servono a misurare il canale, non a
  interoperare.

**Da sapere**: i commenti di entrambi rimandano ai paragrafi (§1, §2, §4, §6,
§7, §8, §10) di un documento di analisi ESP-NOW che **non è in questo repo** —
i riferimenti restano leggibili come struttura del ragionamento (canale fisso
dopo l'init, callback corta, seq/dup, struct packed, contatori) ma il documento
va cercato altrove.

---

## `projects/` — le applicazioni reali

Qui non ci sono template né demo: sono firmware installati e in funzione. Si
leggono per due motivi — capire dove si arriva partendo da uno starter, e
riusarne i moduli (che sono già scritti per essere staccabili) in un nodo nuovo.

| Progetto | Scheda | Nato da |
|---|---|---|
| `EnvNode_C3` | ESP32-C3 Supermini | `starters/C3_OLED_OTA` |
| `MeteoNode_C3` | XIAO ESP32-C3 (e ESP32 "classico") | moduli da `EnvNode_C3` |
| `Timelapse_XIAO` | Seeed XIAO ESP32-S3 Sense | `starters/XIAO_S3_Camera` |

---

### `projects/EnvNode_C3/` — nodo ambientale con dashboard

**Ruolo**: nodo di monitoraggio temperatura/umidità con log storico e interfaccia
web. Cresciuto dallo starter C3 aggiungendo, nell'ordine: sensore, storage,
orario vero, dashboard, configurazione persistente e — da `v4` — il ruolo di
**hub ESP-NOW** per i nodi a batteria della stazione meteo. ~2700 righe in 8
moduli.

**Catena**: DHT11 → media mobile (OLED) e dato grezzo (CSV) → riga in
`/logs/YYYY-MM-DD.csv` sulla microSD → grafici e download dalla dashboard web.

**Seconda catena** (da `v4`): nodo a batteria → DATA ESP-NOW → `remote_nodes` →
pagina `/nodi` e `/api/nodi`. Nessun log su SD dei valori remoti, per ora.

**Hardware** (diverso dallo starter, che ha solo l'OLED): OLED SSD1306 su
SDA=GPIO5/SCL=GPIO6, DHT11 su GPIO0, microSD su modulo **HW-125 in SPI**
(CS=GPIO1, SCK=GPIO4, MISO=GPIO3, MOSI=GPIO7) — non la SDMMC della board AMOLED,
quindi `AMOLED191_SD` qui non c'entra nulla. Tasto BOOT (GPIO9) per cambiare
pagina OLED, LED GPIO8 come heartbeat.

**Dipendenze**: `Adafruit SSD1306`+`GFX`, `DHT sensor library`+`Adafruit Unified
Sensor` (Library Manager); `WiFi`, `ESPmDNS`, `ArduinoOTA`, `WebServer`,
`Update`, `SD`, `SPI`, `Preferences`, `time.h` (core); e **`EspNowLink` di
`libraries/`** da `v4` — quindi la compilazione ora vuole `--libraries
libraries`, che fino a `v3` non serviva. FQBN:
`esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`.

#### `EnvNode_C3.ino`

Logica applicativa: campionamento DHT11 all'intervallo di `settings_get()`,
media mobile delle ultime letture (contro il rumore del sensore) per OLED e
dashboard, min/max dall'ultimo avvio sul dato **grezzo**, tre pagine OLED
(`PAGE_NOW`, `PAGE_MINMAX`, `PAGE_SYSTEM`) che ruotano da sole o col tasto BOOT,
disegno a ~2 Hz. Implementa i ganci `app_*()` che `web_ui` chiama per leggere lo
stato che vive qui.

**Da sapere**: né la lettura DHT11 (interrupt disabilitati, protocollo al
microsecondo) né la scrittura SD (decine di ms) sono veloci, ma qui non c'è un
lock di rendering da tenere corto come sugli sketch LVGL — il `loop()`
semplicemente aspetta.

#### `comfort.h`

Indice di comfort **header-only e puro** (nessuno stato, nessun side effect), così
lo chiamano sia il `.ino` sia `web_ui.cpp` senza dipendenze incrociate. Punteggio
0..100 a soglie: banda ideale configurabile `[tMin,tMax]×[hMin,hMax]` con
penalità lineare oltre i bordi, saturata a 6 °C / 30 punti di RH.

**Da sapere**: niente Heat Index né PMV/PPD, che pretenderebbero una precisione
che il DHT11 (±2 °C, ±5% RH) non ha. Gli span di saturazione sono larghi apposta:
un campione rumoroso al limite della tolleranza sposta il punteggio di ~16 punti,
non abbastanza da far saltare l'etichetta da un estremo all'altro. Lo smoothing,
se serve, va fatto **prima** di chiamare `comfort_eval()`.

#### `settings.h` / `settings.cpp`

Parametri utente persistiti in NVS (`Preferences`): nome nodo, intervallo di log
(5..3600 s), rotazione pagine OLED, banda di comfort, stringa POSIX TZ. Un solo
punto di verità: `.ino`, `sd_logger` e `web_ui` leggono tutti da
`settings_get()`; scrivono solo `web_ui` (dalla dashboard) e il `.ino`. Ogni
setter valida, persiste e aggiorna la copia in RAM, o ritorna `false` senza
toccare nulla.

**Da sapere**: `settings_set_tz()` **non** riapplica il fuso al clock di sistema
(questo modulo non dipende da `rtc_time`): chi la chiama deve richiamare anche
`rtctime_begin(tz)`, o il cambio ha effetto solo al riavvio.

#### `rtc_time.h` / `rtc_time.cpp`

Orario del nodo senza RTC tamponato (scelta confermata: niente DS3231). L'orologio
di sistema va seminato prima del WiFi, altrimenti riparte da epoch 0. Ordine
obbligato in `setup()`: `rtctime_begin(tz)` → `rtctime_seedFromBuild()` → (WiFi)
→ `rtctime_onWifiConnected()`. `rtctime_source()` ritorna `"NTP"` o `"STIMA"` e
finisce sia nella UI sia nel CSV, così si sa sempre quanto fidarsi di un
timestamp.

**Da sapere**: `rtctime_begin()` **deve** precedere `seedFromBuild()`, perché
`mktime()` interpreta la data di build come ora locale nel fuso corrente.
`onWifiConnected()` va richiamata a **ogni** riconnessione, non solo la prima.
Approssimazione accettata: `__TIME__` è l'ora della macchina che ha compilato,
non UTC — errore di qualche ora al massimo, corretto al primo sync NTP. Due
server NTP configurati, così se uno non risponde si prosegue.

#### `sd_logger.h` / `sd_logger.cpp`

microSD SPI e log CSV con **rotazione giornaliera**: `/logs/YYYY-MM-DD.csv`,
intestazione `ts_iso,ts_unix,fonte_ora,temp_c,hum_pct`. Espone anche
l'enumerazione dei giorni disponibili e la lettura riga-per-riga via callback,
usata dall'API web per rispondere in streaming senza mai caricare un file intero
in RAM. `sd_begin()` è ri-chiamabile: senza card il nodo continua a funzionare,
solo senza logging.

**Da sapere**: stessa disciplina di `AMOLED191_SD` e di `storage.cpp` del nodo
camera — ogni scrittura apre/scrive/chiude, mai un handle tenuto aperto. I
contatori `sd_record_count_total()/today()` vivono in RAM e il totale va in NVS
**a intervalli**, non ad ogni scrittura: un contatore che si riscrive ogni pochi
secondi per anni consumerebbe cicli di erase per niente. Un crash tra due flush
perde al più qualche unità del contatore mostrato in UI; il CSV non è mai a
rischio. Non scansionare la SD per rispondere a quelle due funzioni.

#### `net_ota.h` / `net_ota.cpp`

Gemello del `net_ota` dello starter C3, in variante **"server condiviso"**:
espone `net_server()`, così `web_ui.cpp` registra le proprie rotte sullo stesso
`WebServer` e la home `/` può essere la dashboard invece della pagina di stato.
`/update` resta qui.

**Da sapere**: contiene un **watchdog di riconnessione WiFi** — se la
connessione cade, ritenta a intervalli (`WIFI_RETRY_MS`, 30 s) invece di restare
offline fino al riavvio; la finestra di un OTA in corso è esclusa, il watchdog
non tocca la connessione mentre si aggiorna.

#### `web_ui.h` / `web_ui.cpp`

Dashboard (HTML/CSS/JS inline, nessuna CDN) e API JSON: stato corrente, elenco
giorni, campioni di un giorno in streaming, download/eliminazione del CSV,
lettura e scrittura della configurazione. Tutto dietro la stessa basic-auth di
`/update`.

**Da sapere**: la dashboard può essere sostituita caricando un
`/www/dashboard.html` sulla SD, ma `/dashboard-upload` serve **sempre** la
versione in PROGMEM — è la via di recupero se quella caricata a mano è rotta;
`/dashboard-ripristina` cancella il file e si torna al default. `web_ui` non
duplica stato: legge direttamente `settings_get()`, `sd_logger.*`, `rtc_time.*`
e `comfort_eval()`; i ganci `app_*()` implementati nel `.ino` coprono solo le
letture correnti e i min/max dall'ultimo avvio, che vivono lì.

Da `v4` ci sono anche `/nodi` (pagina, PROGMEM), `/api/nodi` e `/api/pairing`,
che leggono `remote_nodes.*`. **La pagina dei nodi è deliberatamente separata
dalla dashboard**: quella vera sta sulla SD e va ricaricata a mano dopo ogni
modifica, quindi una funzione nuova che vive nel firmware resta raggiungibile
anche con una `dashboard.html` vecchia o rotta. Stessa logica di
`/dashboard-upload`. In `/api/stato` sono comparsi `nodi`, `nodi_online` e
`pairing`, per quando la dashboard verrà aggiornata.

**Trappola, e non è di questo file ma del core** (da `v12`): niente
`streamFile()` nudo, e niente `sendContent()` una riga per volta. Gli invii
passano da `streamFileLimitato()` e `giornoFlush()`, che si fermano appena il
client smette di accettare dati — il controllo che `NetworkClient::write()` non
fa — e comunque a fine budget (`INVIO_BUDGET_MS`). Senza, un client andato via
a metà scaricamento tiene `loop()` dentro l'handler per minuti: misurati 456 s
il 2026-08-24, e in quella finestra spariscono anche i pacchetti dei nodi
ESP-NOW, perché `remote_loop()` non gira e il driver tiene solo l'ultimo DATA.
Meccanismo completo in `CLAUDE.md`. Conseguenza voluta: una risposta interrotta
resta **JSON tronco e non chiuso**, così il parse fallisce invece di consegnare
un grafico con meno dati che sembra giusto.

**Trappola**: i float dei nodi remoti passano da `appendJsonFloat()`, che emette
`null` per NaN/infinito. `String(NAN, 2)` produce `"nan"`, che non è JSON valido
e farebbe fallire il parse dell'**intera** risposta — pagina vuota per colpa di
un solo sensore guasto su un solo nodo. E i NaN arrivano davvero: un nodo che
non riesce a leggere il proprio sensore trasmette lo stesso.

#### `remote_nodes.h` / `remote_nodes.cpp`

Ruolo hub ESP-NOW (da `v4`): riceve i DATA dei nodi a batteria, ne tiene lo
stato in RAM (valori, cadenza, perdita di pacchetti, riavvii), li dichiara
"muti" quando smettono di parlare e da `v11` ne calcola **trend barometrico e
previsione**. Sopra `libraries/EspNowLink`. Niente log su
SD e niente disegno: `web_ui` legge da qui, come già fa con gli altri moduli.

**Da sapere**:

- **canale `ESPNOW_LINK_CHANNEL_CURRENT` (0)**, mai un numero esplicito: questa
  scheda sta su un AP, e forzare il canale su una STA connessa non serve o fa
  danni. Con lo 0 i peer seguono l'AP da soli, anche se il WiFi si connette
  dopo `remote_begin()`. È il motivo per cui funziona senza toccare il router —
  al prezzo che tutti i nodi devono stare sul canale dell'AP, che `/api/nodi`
  riporta apposta;
- **si polla `Link_Hub_GetPeerInfo()` da `loop()`** invece di registrare una
  `Link_OnMessage()`: la concorrenza col task del driver WiFi sta già dentro
  `EspNowLink` (portMUX), così non ce n'è di propria da sincronizzare. Il poll
  gira ogni millisecondi contro trasmissioni ogni minuti, e comunque un DATA
  nuovo si riconosce dal salto di `seq`;
- **il registro è persistito in NVS** (namespace `envnodi`, blob unico) con
  **solo MAC, tipo e nome**: il MAC è l'identità vera, bruciata nel chip. Al
  boot i nodi tornano nel driver via `Link_Hub_AddPeer()`, quindi i loro DATA
  passano **anche a pairing chiuso**. I valori non si salvano di proposito
  (mostrare una lettura vecchia come attuale è il guasto da evitare) e nemmeno
  i contatori, che sono "da quando la scheda è accesa"; si scrive solo quando
  il registro cambia, mai ad ogni pacchetto;
- **la finestra di pairing si riapre da sola per 5 minuti ad ogni avvio**, ora
  solo per i nodi non ancora in elenco;
- `remote_forget()` (pulsante "Dimentica") toglie il nodo da libreria, RAM e
  NVS: serve quando si sostituisce una scheda, o il MAC vecchio resterebbe in
  elenco per sempre come nodo muto;
- **niente dipendenza da `sd_logger`**: il modulo espone `remote_on_data()`, e
  il `.ino` ci aggancia `sd_log_remote()`. È scritto per essere copiato su
  `MeteoHub_S3`, che avrà uno storage diverso;
- **una rinomina non sposta lo storico**: la cartella su SD è `/nodi/<NOME>/` e
  nessuno la rinomina, quindi dopo un cambio nome l'hub scrive in una cartella
  nuova e la vecchia resta come archivio — visibile solo interrogando gli
  endpoint con il nome vecchio (`/api/nodi/giorni?nodo=…`). Il modulo rileva
  già il cambio: se un giorno si vorrà spostare anche i file, il gancio è una
  callback verso il `.ino`, non una `SD.rename()` qui dentro. Dettagli e casi
  limite in `CLAUDE.md`;
- **il trend barometrico si calcola qui** (`forecast.h`, arrivato dal nodo):
  storico a **slot da 10 minuti**, 20 per nodo (~160 byte) — non i 720 slot da
  2 minuti del nodo, che servivano ai suoi grafici. Gli slot sono ancorati
  all'orologio e non ai pacchetti, perché la cadenza la decidono i nodi. Si
  calcola solo per pressioni **plausibili** (800..1100 hPa): `value[2]` ha
  significati diversi per tipo di nodo. `remote_seed_begin()` +
  `remote_seed_pressure()` servono al `.ino` per rimettere in RAM le tre ore
  dopo un riavvio, leggendole dai CSV — e vanno chiamate in quest'ordine,
  perché l'anello rifiuta i campioni fuori ordine e senza l'azzeramento il
  seeding girerebbe a vuoto senza dare errore. **`s_hist` è un array parallelo
  a `s_nodi`**: `remote_forget()` deve compattarli insieme, o dopo un
  "dimentica" ogni nodo eredita lo storico del vicino;
- **una sola altitudine per tutti i nodi** (`remote_set_altitude_m()`, chiave
  NVS separata dal blob del registro per non doverne migrare il formato):
  serve solo a riportare al livello del mare la pressione, che i nodi
  trasmettono grezza. Il trend non ne dipende, è una differenza;
- **la soglia di "muto" è osservata, non configurata**: si misura l'intervallo
  fra un DATA e il successivo (media mobile; delta a cavallo di un riavvio del
  nodo, o più lunghi di 6 h, non entrano nella media) e si dichiara muto dopo
  ~2,5 intervalli, con clamp a [90 s, 2 h]. Il nodo decide la propria cadenza
  dalla sua pagina, e duplicare quel valore qui sarebbe solo un modo per andare
  fuori sincrono;
- l'istante di ricezione è quello che registra la libreria, non `millis()` al
  momento del poll: fra il callback e il giro di `loop()` può esserci una
  scrittura su SD o una richiesta HTTP lenta, e un timestamp spostato in avanti
  falserebbe sia la cadenza appresa sia l'ora mostrata in pagina.

#### `www/dashboard.html`

Dashboard personalizzata, **non compilata nel firmware**: è il sorgente della
pagina che si carica da `/dashboard-upload` e che finisce in
`/www/dashboard.html` sulla microSD, dove `GET /` la preferisce a quella in
PROGMEM. Consuma solo le API di `web_ui.cpp`, quindi non richiede modifiche al
firmware. Rispetto alla dashboard di default aggiunge confronto fra due giorni
sovrapposti, statistiche giornaliere (medie, escursione, % di tempo in comfort,
copertura del log), storico multi-giorno, sparkline nelle tile e canvas
scalati sul `devicePixelRatio`.

**Da sapere**:
- Il file nel repo è **solo il sorgente**: modificarlo non aggiorna il nodo, e
  nemmeno ricompilare il firmware serve a qualcosa. Va ri-caricato a mano da
  `/dashboard-upload`.
- Deve restare **self-contained** (CSS/JS inline, nessuna richiesta esterna): il
  nodo non fa da proxy, e una risorsa remota sarebbe irraggiungibile da una LAN
  senza Internet.
- Le richieste al nodo sono **sequenziali per scelta**, mai `Promise.all`: il
  `WebServer` del core è sincrono e serve un client alla volta. Vale soprattutto
  per lo storico multi-giorno, che oggi fa una richiesta per giorno.
- La % di tempo in comfort è pesata sul tempo, e la soglia oltre cui un salto è
  considerato lacuna si ricava dalla **mediana degli intervalli presenti nel
  file**, non da `settings.logIntervalS`: i CSV vecchi conservano il passo con
  cui furono scritti, quindi legarla alla configurazione corrente faceva
  scartare per intero ogni giornata registrata a un passo diverso.
- La formula del comfort è duplicata in JS (stessa di `comfort.h`): serve a
  ricalcolare lo storico nel browser quando si cambia la banda, senza rete e
  senza costo per il nodo. Se `comfort.h` cambia, va allineata anche qui.

---

### `projects/MeteoNode_C3/` — nodo meteo a batteria

**Ruolo**: nodo della stazione meteo. Legge **AHT20 + BMP280** su I2C, serve una
pagina di stato con tre grafici SVG disegnati a mano, calcola la previsione dal
trend barometrico a 3 ore, manda le misure all'hub via ESP-NOW e — da `v9` —
dorme fra una misura e l'altra. Lo stesso sketch gira su **XIAO ESP32-C3** e su
un **ESP32 "classico"** (DOIT DevKit v1), scelti con `#ifdef` sul tipo di chip.

Compilazione (FQBN diverso per le due schede, e **senza** `CDCOnBoot=cdc` sulla
XIAO C3, dove quel flag significa *Disabled* — vedi `CLAUDE.md`):

```
arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32C3:PartitionScheme=min_spiffs" --libraries libraries projects/MeteoNode_C3
arduino-cli compile --fqbn "esp32:esp32:esp32doit-devkit-v1" --libraries libraries projects/MeteoNode_C3
```

#### `MeteoNode_C3.ino`

Misura, previsione, comandi da seriale e da web, e il ciclo di deep sleep.
Da conoscere:

- `sensorPower(bool)` — il VDD del sensore passa da un GPIO (D3/GPIO5), così si
  può fare un power-cycle vero del modulo quando smette di rispondere. È anche
  la sequenza usata prima di dormire, e da `v10` è la **coppia** che regge
  l'hold del pin nel deep sleep: `vaiADormire()` chiama `gpio_hold_en()` dopo
  averlo messo basso (o entrando nel sonno tornerebbe flottante e il modulo
  resterebbe mezzo alimentato), `sensorPower(true)` chiama `gpio_hold_dis()`
  prima di ripilotarlo — senza quel rilascio il pin resta inchiodato e il
  sensore non si riaccende più. Il pin è RTC-capable apposta.
- `nodeName()` / `app_set_nome()` — il nome con cui il nodo si presenta. Tre
  livelli: NVS (impostato dalla pagina) → `NODE_NAME_FISSO` se non vuoto →
  derivato dal MAC (`Meteo-XXXXXX`). Il MAC si legge con `esp_read_mac()`
  dall'eFuse e non con `WiFi.macAddress()`, perché al risveglio dal deep sleep
  il WiFi non è acceso. Non è un'etichetta: sull'hub quel nome diventa la
  **cartella** del CSV, quindi due schede omonime scrivono nello stesso file.
  `NODE_NAME_FISSO` resta valorizzato solo sull'ESP32 "classico", che è un
  esemplare unico già installato con il suo storico; sulla XIAO C3 è vuoto,
  perché è la board che si replica.
- `bootDiagBegin()` / `app_reset_reason()` / `app_boot_count()` — perché la
  scheda è ripartita e quante volte (il contatore è in NVS). Tutti gli altri
  contatori vivono in RAM e ripartono da zero, quindi senza questi un riavvio è
  invisibile da remoto.
- `cicloRisveglio()` / `vaiADormire()` — il percorso corto del risveglio: niente
  WiFi né web, solo sensore, ESP-NOW e via. Lo stato che deve attraversare il
  sonno (canale, MAC dell'hub, **seq**, orario già sincronizzato) sta in
  `RTC_DATA_ATTR`; i contatori dei risvegli stanno invece in **NVS**, perché la
  RTC memory la cancella il power-cycle — cioè proprio l'operazione con cui si
  recupera un nodo che non torna.
- L'uscita di sicurezza dopo cinque risvegli senza consegna, che riaccende WiFi
  e OTA. Vale anche come diagnosi: se non scatta, il nodo non sta girando.

#### `forecast.h`

Header-only e puro: trend barometrico a 3 ore con isteresi, e il testo della
previsione. Nessuna dipendenza da hardware o rete, quindi si sposta di peso —
ed è **spostato sull'hub** il 2026-08-24 (`EnvNode_C3` `v11`), perché la RAM di
un nodo che dorme si azzera ad ogni risveglio e lo storico non può stare su di
lui.

Il file resta anche qui, e non è una svista: sul gemello che gira su ESP32
"classico", alimentato a muro e senza deep sleep, il calcolo locale funziona e
la sua pagina lo mostra. Su un nodo a batteria è invece inerte — la sua
previsione dirà "raccolgo dati: servono tre ore di storico" per sempre, e
quella vera va letta dall'hub.

#### `hub_link.h` / `hub_link.cpp`

Strato sottile sopra `libraries/EspNowLink`, gemello di quello del nodo camera.
`hub_begin()` sceglie il canale (quello dell'AP se connessi, altrimenti il fisso
della libreria); `hub_begin_ex()` accetta un canale esplicito e `hub_resume()`
riprende con un hub già noto — servono entrambi al risveglio, dove non c'è
nessun AP a cui chiedere. `hub_seq_set()`/`hub_seq_get()` portano il contatore
di sequenza attraverso il sonno: **senza, l'hub scarta tutti i DATA come
doppioni**.

#### `rtc_time.h` / `.cpp`, `net_ota.h` / `.cpp`, `web_ui.h` / `.cpp`

Copie dei moduli di `projects/EnvNode_C3/`, con lo stesso contratto: ordine
obbligato dell'orario in `setup()`, watchdog di riconnessione WiFi, server
condiviso via `net_server()`. In `web_ui` ci sono in più l'interruttore del deep
sleep e i contatori dei risvegli (`risvegli` / `risvegli_ok`, separati apposta:
è l'unico modo di distinguere un nodo che non si sveglia da uno che si sveglia
senza farsi sentire).

#### `secrets.h.example` → `secrets.h`

Come negli altri progetti, gitignorato. `OTA_HOSTNAME` dipende dal chip, così
le due schede che girano lo stesso sketch non si contendono lo stesso nome mDNS.

### `projects/Timelapse_XIAO/` — camera timelapse con galleria web

**Ruolo**: camera che scatta a intervallo regolare e conserva l'archivio
ordinato per giorno sulla microSD, con una web UI per inquadrare, configurare,
sfogliare e riprodurre la sequenza. Cresciuto da `starters/XIAO_S3_Camera`
sostituendo il PIR con un timer e il progressivo dei file con data/ora.

**Catena**: timer → `camera_grab_fresh()` → `/timelapse/AAAA-MM-GG/HHMMSS.JPG`
→ riga nel CSV del giorno → galleria/riproduzione dal browser.

**Hardware**: nessun cablaggio oltre l'alimentazione USB. Camera e microSD sono
quelle della scheda Sense (stessi pin dello starter: SD su SPI, CS=GPIO21).
GPIO liberi per aggiunte: D1–D5 (GPIO 2, 3, 4, 5, 6).

**Dipendenze**: solo core ESP32 (`esp_camera` bundled, `SD`, `SPI`, `WebServer`,
`ArduinoOTA`, `Preferences`, `time.h`). **Nessuna** libreria di `libraries/` e
nessuna da Library Manager: a differenza del nodo camera non usa ESP-NOW, quindi
la cartella è spostabile fuori dal repo così com'è. FQBN:
`esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB`.

#### `Timelapse_XIAO.ino`

Logica applicativa: impostazioni persistite in NVS (attivo, intervallo, finestra
oraria, politica dello spazio, MB minimi liberi), timer degli scatti, scatto
(`do_capture()`), gestione dello spazio (`ensureSpace()`), ganci `app_*()` per
`web_ui`. Rilancia il sync NTP quando `net_takeReconnectedFlag()` segnala una
riconnessione e ritenta il mount della card ogni 30 s se manca.

**Da sapere**:
- `nextSlot()` allinea gli scatti all'**orologio** (multiplo dell'intervallo
  sull'epoch), non all'ultimo scatto: con intervallo 60 le foto cadono al
  secondo `:00`. Il prossimo slot si ricalcola **prima** dello scatto e ad ogni
  giro, quindi un salto dell'orologio (primo sync NTP) o una pausa lunga non
  producono una raffica di recuperi — gli slot persi si contano in `s_skipped`
  e si saltano.
- `app_pump()`, chiamata ad ogni frame dal ciclo dello stream MJPEG, **non
  scatta**: riallinea solo il timer. La camera sta già servendo lo stream e una
  scrittura su SD lo bloccherebbe. Come nello starter, lì dentro non si chiama
  `net_loop()`.
- `inWindow()` con `inizio == fine` vale "sempre"; `inizio > fine` è una
  finestra a cavallo della mezzanotte.
- Sotto `EPOCH_PLAUSIBLE` (2020-01-01) non si scatta: senza un orario credibile
  si creerebbero cartelle datate 1970.

#### `camera.h` / `camera.cpp`

Copia identica di quelli di `starters/XIAO_S3_Camera` (stesso hardware, stessi
pin, stessa tabella di risoluzioni). Se si corregge un bug qui, va corretto
anche là: sono due copie, non un modulo condiviso — è la stessa scelta fatta per
`net_ota.*` fra gli starter.

#### `storage.h` / `storage.cpp`

microSD SPI organizzata **per giorno**: foto in `/timelapse/AAAA-MM-GG/` col
nome `HHMMSS.JPG` (ora locale), CSV in `/timelapse/log/AAAA-MM-GG.csv` con
colonne `ts_iso,boot_id,fonte_ora,sorgente,file,byte,esito`. Espone elenco
giorni/foto, apertura e cancellazione (foto, intero giorno, CSV in download),
occupazione della card e contatori.

**Da sapere**:
- I nomi a lunghezza fissa con zeri iniziali rendono l'ordine alfabetico uguale
  all'ordine temporale: la web UI non legge mai le date dal filesystem, che
  `SD.h` non espone in modo affidabile su tutte le versioni del core.
- `sd_photos_today()` è un contatore in **RAM**: la directory del giorno si
  scandisce una volta sola, al primo scatto del giorno (o dopo il boot), non ad
  ogni richiesta di stato.
- `SD.usedBytes()` percorre la FAT, quindi occupazione e spazio libero sono
  **cachati 15 s** (`USAGE_TTL_MS`) e invalidati ad ogni scrittura/eliminazione:
  la web UI chiede lo stato ogni 3 s.
- `sd_delete_day()` rimuove i file uno alla volta riaprendo la directory ad ogni
  giro (non si cancella mentre la si sta iterando) e poi fa `rmdir`; il CSV del
  giorno **resta**, è il registro di cosa è successo.
- Ogni nome che arriva dal web passa da `sd_day_is_valid()` +
  `sd_name_is_safe()`: senza, l'URL aprirebbe qualunque file della card.
- Un secondo scatto nello stesso secondo (manuale sopra l'automatico) prende un
  suffisso `_1`, non sovrascrive.

#### `rtc_time.h` / `rtc_time.cpp`

Copia del modulo di `EnvNode_C3` (codice puro, non dipende dalla scheda), con il
commento adattato. Stesso ordine obbligato in `setup()`: `rtctime_begin(tz)` →
`rtctime_seedFromBuild()` → (WiFi) → `rtctime_onWifiConnected()`, quest'ultima a
**ogni** riconnessione. Il fuso qui è la costante `TZ_POSIX` nel `.ino`, non
un'impostazione da web: questa scheda non viaggia.

#### `net_ota.h` / `net_ota.cpp`

Gemello di quelli degli altri nodi (WiFi + ArduinoOTA + `/update`, la `/` la
registra `web_ui`), con in più il **watchdog di riconnessione WiFi** di
`EnvNode_C3` — ritento a 30 s, re-init completo dello stack a 5 min, riavvio
della scheda a 30 min — e `net_takeReconnectedFlag()`, che lo sketch consuma per
rilanciare NTP. `s_updateInProgress` impedisce al watchdog di toccare la
connessione mentre un OTA sta scrivendo la partizione. Di norma non si tocca.

#### `web_ui.h` / `web_ui.cpp`

Pagina di controllo in PROGMEM (self-contained, nessuna CDN) e API HTTP:
`/stream`, `/snapshot.jpg`, `/api/scatta`, `/api/stato`, `/api/config`,
`/api/giorni`, `/api/foto`, `/foto`, `/log`, `/api/elimina`,
`/api/elimina-giorno`. Tutto dietro la basic-auth di `net_ota`. La UI legge
orario, SD e camera direttamente dai rispettivi moduli e chiede al `.ino` solo
ciò che vive lì (ganci `app_*()`).

**Da sapere**:
- La galleria mostra i JPEG a piena risoluzione rimpiccioliti dal browser
  (`loading=lazy`): **niente miniature sulla card**, costerebbero una seconda
  codifica per scatto e il doppio dello spazio. Su una giornata da migliaia di
  scatti la pagina resta pesante — è il compromesso scelto.
- Il player è puro JS: scarica una foto alla volta e precarica la successiva, e
  a 20 fps il WiFi non ce la fa. Per il montaggio vero si copia la cartella
  dalla microSD.
- `/foto` risponde con `Cache-Control: max-age=86400`: i file non cambiano mai,
  e senza cache il player riscaricherebbe ogni fotogramma ad ogni passaggio.
- I campi del form non vengono sovrascritti dal polling dello stato mentre sono
  a fuoco (`set()` in pagina), altrimenti scrivere un valore diventerebbe una
  lotta con l'aggiornamento ogni 3 s.
- Lo stream tiene fermo il web server (server sincrono): dura al massimo
  `WEB_STREAM_MAX_MS` (5 min) e durante quel tempo gli scatti si saltano.

#### `secrets.h.example` → `secrets.h`

Come per gli altri nodi: `secrets.h` è gitignorato (pattern globale), qui c'è
solo il template. Tenere un `OTA_HOSTNAME` **diverso** da quello del nodo
camera, o le due schede si contendono lo stesso nome mDNS.

---

### `projects/MeteoHub_S3/` — hub della stazione meteo (XIAO ESP32-S3 Sense)

Riceve i DATA dei nodi via ESP-NOW, li mostra su un pannello **e-ink WeAct
4.2" 400x300** (SSD1683) e ne registra i CSV su microSD, con orario NTP, web UI
e OTA (Fase 3 chiusa il 2026-08-27). Da `v4` (2026-08-28) le pagine del
pannello sono un **elenco configurabile** (`pages.*`) invece che un enum
fisso, e fra i tipi c'è il **messaggio** scritto dalla web UI. Le cinque
pagine di prova del bring-up sono state tolte nella stessa occasione: la
loro funzione diagnostica la copre ora la pagina messaggio, che sul
pannello si vede o non si vede allo stesso modo.

| File | Ruolo |
|---|---|
| `MeteoHub_S3.ino` | pagine del pannello, tasto BOOT, hub ESP-NOW, logging dei nodi — qui va la logica applicativa |
| `pages.h/.cpp` | il **modello delle pagine**: elenco, attiva/durata, rotazione, ore di silenzio, tutto in NVS. Non conosce il display: dice quale pagina tocca, il `.ino` la disegna |
| `messages.h/.cpp` | il messaggio del pannello: quello **attivo** in NVS (sopravvive senza card), l'**archivio** su SD in `/messaggi/archivio.csv` |
| `sd_logger.h/.cpp` | copia da `EnvNode_C3` adattata alla microSD SPI della Sense: CS 21, nessuna `SPI.begin()` propria |
| `net_ota.h/.cpp` | WiFi + ArduinoOTA + `/update` + watchdog di riconnessione, con `net_server()` condiviso |
| `web_ui.h/.cpp` | pagina di stato dell'hub e API dei nodi: gli stessi endpoint di `EnvNode_C3` |
| `secrets.h.example` | si copia in `secrets.h` (gitignorato): WiFi, `OTA_HOSTNAME` `meteohub-s3`, credenziali web |
| `remote_nodes.h/.cpp` | copia da `EnvNode_C3`: registro nodi, cadenza appresa, nodo muto, trend, NVS |
| `forecast.h` | copia da `EnvNode_C3`: trend a 3 h con isteresi, header-only e pura |
| `rtc_time.h/.cpp` | copia da `EnvNode_C3`: stima da build-time, poi NTP. Il primo sync fa anche partire `seedForecastDaSD()`, che ricostruisce il trend leggendo la coda dei CSV |
| `www/dither.html` | ritaglio + dithering nel browser: la **sorgente unica** della pagina di composizione. Si apre da disco per lavorarci, e da `v8` e' anche servita dall'hub su `/immagini` |
| `www/gen_page.py` | rigenera `dither_page.h` da `dither.html`. **Da rilanciare dopo ogni modifica alla pagina**, prima di ricompilare |
| `dither_page.h` | **GENERATO**, non si modifica a mano: la pagina in PROGMEM (~38 kB) servita su `/immagini`. Da `v12` include anche la composizione del testo sopra la foto |

**Cose da sapere prima di metterci le mani**:

- **Il pannello e la microSD condividono il bus SPI** (SCK GPIO7/D8, MOSI
  GPIO9/D10; l'e-ink non usa MISO). Il CS della card (**GPIO21**) va pilotato
  **ALTO prima di toccare il bus**, anche quando la card non si monta:
  flottante, la card puo' rispondere insieme al pannello. Pin dedicati
  dell'e-ink: CS GPIO2/D1, DC GPIO3/D2, RST GPIO4/D3, BUSY GPIO1/D0.
- **Il canale ESP-NOW e' `ESPNOW_LINK_CHANNEL_CURRENT` (0), mai un numero.**
  L'overload `remote_begin(nome, canale)` e' nato qui quando la scheda non
  aveva ancora il WiFi e doveva scegliersi il canale da sola; **da quando sta
  su un AP (Fase 3, 2026-08-27) quel numero non si passa piu'**, perche' il
  canale lo impone il router e forzarlo chiamerebbe `esp_wifi_set_channel()`
  su una STA connessa. `remote_begin()` va chiamata **dopo** `net_begin()`.
- **La finestra di associazione non si apre da sola all'avvio**, al contrario di
  `EnvNode_C3`: `remote_begin()` la apre e il `setup()` la richiude subito. Un
  nodo tiene un hub solo e lo adotta il primo che risponde al suo HELLO, quindi
  un hub di sviluppo in pairing sul canale di casa si porterebbe via un nodo
  vero — e con lui il suo log su SD, che qui non c'e'. Si apre a mano tenendo
  premuto **BOOT** (1,2 s), per 2 minuti; BOOT breve cambia pagina.
- **La diagnostica sta sul pannello, non sulla seriale**: il piede della pagina
  NODI porta IP e spazio libero della card, e in negativo `SD NON MONTATA`.
  Serve perche' il log di boot di questa scheda **non e' osservabile via USB**:
  la cattura si ferma a 256 byte (il buffer TX della CDC), l'host finisce di
  enumerare la porta un paio di secondi dopo il reset e con
  `Serial.setTxTimeoutMs(0)` il resto viene buttato.
- **`GET /api/salute`** (da `v13`) fa i controlli incrociati che prima si
  facevano a mano leggendo due endpoint: il principale e' **pacchetti ricevuti
  == righe scritte + scartati per orario + scritture fallite**. Sono contatori
  tenuti da moduli che non si conoscono fra loro, quindi se non tornano il
  guasto sta **fra la radio e la card**, dove nessun altro contatore guarda.
- **Refresh**: tre cadenze — orologio ogni 60 s su finestra piccola, pagina in
  parziale ad ogni dato nuovo (min 120 s) e comunque ogni 5 min, completo ogni
  10 parziali **o ogni ora**. Piu' un completo ad ogni cambio pagina e un
  parziale immediato al primo pacchetto di un nodo. `hibernate()` dopo ognuno.
  Misurati: completo ~2,2 s, parziale intero ~980 ms, solo orologio ~810 ms —
  **l'area conta poco, il tempo lo detta la waveform**.
- **Testo centrato**: usare `drawCenter()`, che misura con `getTextBounds()`.
  Allineare a destra con un offset stimato a occhio taglia le stringhe larghe
  sul bordo sinistro, dove il cursore va a coordinate negative e Adafruit_GFX
  non protesta.
- **Serve `--libraries libraries`** (usa `EspNowLink`), e il FQBN della XIAO S3
  **senza** `CDCOnBoot`: su questa board quel flag e' invertito.

## File a livello repository

### `README.md`

Introduzione e istruzioni d'uso del workspace in italiano: cosa contiene ogni
cartella, tabella delle schede coperte, setup Arduino IDE (incluso il
collegamento delle librerie condivise via junction), spiegazione `build_opt.h`,
procedura "avvia un nuovo progetto da uno starter", una sezione per ciascuno dei
tre starter, descrizione di tutti e sei gli esempi e dei progetti reali. È il
documento rivolto a un umano che apre il repo per la prima volta, quindi resta a
livello "cosa fa / come lo provo": il dettaglio per file è qui, non lì.

### `CLAUDE.md`

Guida operativa per lavorare sul repo con Claude Code: comandi di build/verifica
(incluso `arduino-cli` con `--libraries`), architettura delle librerie
condivise in `libraries/`, workflow SquareLine, vincoli hardware, convenzioni
(dove scrivere la logica, gestione del lock). Non ripete il dettaglio
file-per-file (quello è qui, in `docs/FILES.md`) né il pinout completo (quello è in
`docs/ESP32-S3-AMOLED-1.91-Guide.md`).

### `docs/ESP32-S3-AMOLED-1.91-Guide.md`

Guida di riferimento hardware/board-level (non file-per-file): identità scheda,
pinout completo, pin da non toccare, toolchain, flashing/boot mode, dettagli
display/touch/IMU/SD/batteria/WiFi, checklist di errori comuni. Cross-verificata
col codice di questo repo (vedi commit `b8a7fd8`): gli snippet ora riflettono i
valori confermati funzionanti (framing QSPI, MADCTL, WRCTRLD, flip Y touch), con
note esplicite dove non è stato possibile verificare con certezza (revisione SD
V1/V2, valore CTRL1 IMU). I riferimenti a `touch_bsp.c`/`esp_lcd_sh8601` in
questa guida citano l'esempio vendor Waveshare esterno usato come riferimento
quando la guida è stata scritta, non i file di questo repo (spostati in
`libraries/` da questo refactor) — non necessita aggiornamenti per questo.

### `.gitignore`

Esclude artefatti di build Arduino (`build/`, `*.bin`, `*.elf`, `*.map`), file di
sistema Windows/macOS, `.vscode/`, `.claude/settings.local.json` (permessi
locali di Claude Code per questa macchina/sessione, non da condividere) e
**`secrets.h`** (credenziali WiFi/OTA reali — il repository è pubblico).

**Da sapere**: la regola su `secrets.h` è un **pattern**, non un percorso: una
riga `secrets.h` in `.gitignore` vale per qualunque cartella, quindi copre anche
le copie future di uno starter, dentro e fuori dal repo. Prima erano tre percorsi
letterali (uno per sketch), che smettevano di coprire il file appena lo sketch
veniva spostato o copiato — errore facile e silenzioso su un repo pubblico.
Versionati restano solo i `secrets.h.example`.

### `.claude/settings.local.json`

Configurazione locale di Claude Code (permessi Bash consentiti in questa
sessione) — **non versionata** (vedi `.gitignore`), specifica di questa macchina.

### PDF di riferimento (`docs/`)

`ESP32-S3-AMOLED-1.91.pdf` (schema board), `esp32-s3_datasheet_en.pdf`,
`esp32-s3_technical_reference_manual_en.pdf`, `RM67162.pdf`, `QMI8658C_datasheet_rev_0.9.pdf`,
`Guida_LVGL_SquareLine_ESP32S3_AMOLED.pdf` — datasheet e guide sorgente da cui è
stata derivata `docs/ESP32-S3-AMOLED-1.91-Guide.md`. Consultarli per dettagli di
registro/timing non coperti dalla guida; non riscriverne il contenuto nel codice
o in altra documentazione.
