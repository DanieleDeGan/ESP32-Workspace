# Workspace ESP32 — librerie condivise, starter, esempi, progetti

Il posto dove tengo tutto quello che riuso tra un progetto ESP32 e l'altro: le
librerie condivise, un template pronto per ogni scheda che uso, gli sketch di
esempio che le validano e i progetti veri, quelli installati e in funzione.

Non è un singolo progetto, e non è legato a un'applicazione particolare: è un
workspace. Uno starter si copia e diventa il punto di partenza del progetto
successivo, qualunque esso sia.

## Contenuto del repository

| Percorso | Ruolo |
|---|---|
| `libraries/` | le librerie Arduino condivise: una per periferica/funzione |
| `starters/` | un template riutilizzabile per scheda — **si copiano** per iniziare |
| `examples/` | sketch completi: si compilano e si caricano così come sono |
| `projects/` | le applicazioni reali, quelle davvero installate |
| `docs/` | reference file-per-file, guida hardware della board AMOLED, datasheet |

L'organizzazione è **per ruolo**, e dentro ciascun ruolo **per scheda**. Il
modello di chip non è una cartella: è un attributo documentato progetto per
progetto (colonna "gira su" nelle tabelle). Il motivo è pratico — le librerie
condivise sono usate da sketch che girano su chip diversi (`EspNowLink` la usano
sia la board S3 sia i nodi C3) e metà degli esempi gira su qualunque ESP32: una
divisione per chip o duplicherebbe le librerie o le lascerebbe comunque fuori.

### Le schede coperte

| Starter | Scheda | Cosa ci fai |
|---|---|---|
| `starters/AMOLED_1.91_LVGL/` | Waveshare ESP32-S3-Touch-AMOLED-1.91 | interfacce LVGL disegnate in SquareLine Studio (AMOLED 536×240 SH8601, touch FT3168, IMU QMI8658, microSD) |
| `starters/C3_OLED_OTA/` | ESP32-C3 Supermini + OLED 0.96" I2C | nodo con display SSD1306 e aggiornamento **OTA** via WiFi, self-contained |
| `starters/XIAO_S3_Camera/` | Seeed XIAO ESP32-S3 **Sense** | camera OV2640 + microSD: foto su movimento, web UI, OTA, notifica ESP-NOW |

| Progetto | Scheda | Cos'è |
|---|---|---|
| `projects/EnvNode_C3/` | ESP32-C3 Supermini | nodo ambientale in funzione: DHT11 + log su microSD + dashboard web con grafici + OTA, e hub ESP-NOW dei nodi a batteria |
| `projects/MeteoNode_C3/` | XIAO ESP32-C3 (e ESP32 "classico") | nodo meteo a batteria: AHT20 + BMP280, previsione dal trend barometrico, pagina con grafici SVG, nodo ESP-NOW e **deep sleep** fra una misura e l'altra |
| `projects/Timelapse_XIAO/` | Seeed XIAO ESP32-S3 **Sense** | camera timelapse: scatto a intervallo su microSD per giorno, galleria web con riproduzione, NTP + OTA |

## Le librerie condivise (`libraries/`)

Il boilerplate hardware vive in librerie Arduino locali, una per periferica, così
ogni sketch include solo quello che usa:

| Libreria | Ruolo | Gira su |
|---|---|---|
| `AMOLED191_Core` | bring-up idempotente del bus I2C condiviso | board AMOLED |
| `AMOLED191_Display` | pannello SH8601 (QSPI) + LVGL + task di rendering + mutex | board AMOLED |
| `AMOLED191_Touch` | driver touch FT3168, con wiring LVGL opzionale | board AMOLED |
| `AMOLED191_IMU` | driver IMU QMI8658 onboard | board AMOLED |
| `AMOLED191_SD` | microSD onboard (SDMMC 1 bit), orientata al logging testuale | board AMOLED |
| `EspNowLink` | comunicazione ESP-NOW hub↔nodi, indipendente da LVGL/display | **qualunque ESP32** |

Il prefisso dice a cosa sono legate: le cinque `AMOLED191_*` conoscono i pin di
quella board e solo lì hanno senso; `EspNowLink` non dipende da nessun
hardware specifico ed è inclusa sia dagli sketch hub (S3) sia dai nodi
(C3, XIAO). Sono tutte locali a questo repo (vedi setup sotto), non si
installano da Library Manager.

Le uniche dipendenze **esterne**:

- **LVGL 8.3.x** — per ogni sketch con schermo sulla board AMOLED.
- **DHT sensor library** (Adafruit) + **Adafruit Unified Sensor** — per
  `examples/DHT11_SD_Logger/` e `projects/EnvNode_C3/`.
- **Adafruit SSD1306** (tira dentro GFX e BusIO) — per `starters/C3_OLED_OTA/`
  e `projects/EnvNode_C3/`.
- **Niente** per `starters/XIAO_S3_Camera/`: il driver della camera è già dentro
  il core ESP32.

### Collegare le librerie condivise (una tantum)

L'IDE cerca le librerie in `Documents/Arduino/libraries/`, non dentro questo
repo. Per fargliele vedere senza copiarle a mano (e senza perdere gli
aggiornamenti quando le modifichi), crea una junction per ciascuna — su Windows
non serve essere amministratore né attivare la "Developer Mode" (necessaria
invece per i symlink veri):

```powershell
$repo = "<percorso di questo repo>\libraries"
$dest = "$env:USERPROFILE\Documents\Arduino\libraries"
foreach ($name in "AMOLED191_Core","AMOLED191_Display","AMOLED191_Touch","AMOLED191_IMU","AMOLED191_SD","EspNowLink") {
  New-Item -ItemType Junction -Path "$dest\$name" -Target "$repo\$name"
}
```

Dopo, riavvia l'IDE: `Sketch > Include Library` deve elencarle tutte e sei.
In alternativa, per compilare da riga di comando senza junction, usa
`arduino-cli compile --libraries libraries ...` dalla radice del repo
(vedi `CLAUDE.md`).

## Avviare un nuovo progetto da uno starter

1. **Copia** la cartella dello starter, per esempio `starters/AMOLED_1.91_LVGL/`
   in `projects/MioProgetto/`.
2. **Rinomina** lo sketch in `MioProgetto.ino`: il nome del `.ino` DEVE
   coincidere con quello della cartella (vincolo di Arduino).
3. **Librerie condivise**: se la copia resta dentro questo repo non serve altro,
   `libraries/` alla radice vale per tutti. Se invece il progetto diventa un
   repo indipendente altrove sul disco, copia (o crea una junction verso) anche
   le librerie che usa: senza, non compila.
4. **Compila e carica così com'è** prima di toccare qualsiasi cosa: è il modo
   più veloce per confermare che hardware e toolchain sono a posto.
5. Poi entra nel merito (UI, sensori, rete).

Per gli starter con `secrets.h.example` (C3 e XIAO) c'è un passo in più: copiare
il file in `secrets.h` e riempirlo — vedi le rispettive sezioni.

---

# Board AMOLED — `starters/AMOLED_1.91_LVGL`

Waveshare ESP32-S3-Touch-AMOLED-1.91. Tutto il boilerplate di basso livello è
nelle librerie `AMOLED191_*`: nello sketch resta solo la tua logica.

| File | Ruolo |
|------|-------|
| `AMOLED_1.91_LVGL.ino` | sketch principale: `setup`/`loop`, la tua logica (UI, WiFi, SD, sensori) |
| `lv_conf.h` | configurazione LVGL **a livello di progetto** |
| `build_opt.h` | flag di compilazione (vedi sotto) |
| `ui.h` / `ui.c` | **stub** segnaposto: vengono sostituiti dall'export di SquareLine |

## Impostazioni Arduino IDE (Tools)

- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
- PSRAM: **OPI PSRAM**
- USB CDC On Boot: **Enabled**
- CPU Frequency: **240 MHz**

## build_opt.h

Contiene due define passati globalmente al compilatore:

```
-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE
```

- `LV_CONF_INCLUDE_SIMPLE` → LVGL cerca `lv_conf.h` lungo il percorso di include
  (quindi quello di progetto, in questa cartella) invece del percorso relativo fisso.
- `LV_LVGL_H_INCLUDE_SIMPLE` → i file generati da SquareLine includono `lvgl.h` in
  modo "semplice"; senza questo, dopo l'export si ottengono errori `lvgl.h: No such file`.

## Workflow SquareLine Studio

1. **Compila e carica lo starter così com'è**: all'avvio compare "Starter pronto"
   centrato. Conferma che display, touch e LVGL funzionano (la UI è ancora lo stub).
2. In **SquareLine Studio** crea un nuovo progetto:
   - Risoluzione **536 × 240**, profondità colore **16 bit**.
   - Versione LVGL **8.3.x** (deve combaciare con `lv_conf.h` e la libreria installata).
3. **Esporta solo i file UI** ("Export UI Files") con percorso di export = la cartella
   dello sketch. I file `ui.h` / `ui.c` generati **sovrascrivono gli stub**; arrivano
   anche `ui_helpers.*`, `ui_events.*`, gli screen e gli asset.
   - **Non** usare il `.ino` né il driver TFT_eSPI generati da SquareLine: il display
     è già gestito da `AMOLED191_Display`. Si tengono solo i file `ui_*`.
4. **Ricompila.** Se l'IDE non vede i file appena aggiunti, chiudilo e riaprilo (la
   cache di build conserva lo stato precedente).

## Dove scrivere la tua logica

- **Eventi dei widget**: per ogni evento "Call function" definito in SquareLine, scrivi
  il corpo in `ui_events.c`. Questo file **non viene sovrascritto** ai re-export.
- **Aggiornamenti UI dalla tua logica** (`loop`, task sensori, callback WiFi): la UI gira
  in un task dedicato, quindi avvolgi SEMPRE gli accessi a oggetti LVGL in
  `lvgl_lock(-1)` … `lvgl_unlock()`.
- **Dentro un callback di evento LVGL**: NON prendere il lock (ce l'hai già) e tieni il
  callback corto; il lavoro lento (SD, rete) va deferito al `loop()`/a un task.

## Note hardware

- **microSD**: dipende dalla revisione della scheda, e le due non sono
  intercambiabili.
  - **V2** (schede attuali, quello che implementa `AMOLED191_SD`): **SDMMC a 1 bit**,
    CLK=GPIO9, CMD=GPIO42, D0=GPIO8. **Nessun pin in comune col bus QSPI del
    pannello**, quindi SD e LVGL convivono senza arbitraggio e senza lock.
  - **V1** (schede vecchie): SD su SPI3 con CLK=GPIO47, cioè lo stesso pin del
    PCLK del display. Lì i due sono fisicamente sullo stesso clock e servirebbe
    condividere l'host SPI2. **Non supportata** da `AMOLED191_SD`: se
    `SDCard_Init()` fallisce con una card sicuramente buona e FAT32, la scheda è
    probabilmente una V1.

  Non c'è modo di distinguerle a runtime (anche l'esempio ufficiale Waveshare le
  seleziona a compile-time). Schede ≤ 64 GB, FAT32: le exFAT non montano, e la
  libreria non le formatta di sua iniziativa.
- **I2C**: touch e IMU QMI8658 sono sullo stesso bus (SDA=GPIO40, SCL=GPIO39;
  indirizzi 0x38 e 0x6B). `Core_I2CBusInit()` (libreria `AMOLED191_Core`) lo porta
  su in modo idempotente: sia il touch sia l'IMU la richiamano internamente,
  quindi funzionano in qualunque ordine. Non aprire un bus indipendente sugli
  stessi pin.
- **GPIO liberi** per periferiche tue: 2, 4, 10–16, 21, 38. Evita 26 e 33–37
  (PSRAM octal). Il **GPIO3** è elettricamente libero ma è un pin di strapping
  (JTAG): deve restare flottante al reset, quindi non usarlo per niente che
  tenga la linea alta o bassa all'accensione — un modulo sensore con pull-up a
  bordo, per esempio, la tiene alta.
- Sulla versione senza header a pettine (SKU 28596) i GPIO liberi sono piazzole
  da saldare, non un connettore: scegli il pin anche in base a quale riesci a
  raggiungere fisicamente.

Pinout completo e dettagli di registro: `docs/ESP32-S3-AMOLED-1.91-Guide.md`.

---

# Nodo con OLED e OTA — `starters/C3_OLED_OTA`

ESP32-C3 Supermini + OLED 0.96" I2C. Non ha niente in comune con la board
AMOLED: usa **Adafruit SSD1306 + GFX** e i moduli del core ESP32, non le
`libraries/` di questo repo, e si può spostare altrove così com'è.

Il motivo per cui esiste è l'**OTA**: una scheda montata dentro qualcosa non la
raggiungi più col cavo. La carichi via USB una volta sola, poi la aggiorni via
WiFi in due modi — da Arduino IDE (compare come porta di rete) o da browser,
caricando il `.bin` su `http://<hostname>.local/update`.

## Prima di compilare: le credenziali

`secrets.h` **non è nel repository** ed è escluso dal `.gitignore`, perché
contiene la password della tua rete WiFi e questo repo è pubblico. Versionato
c'è solo il template:

```
cd starters/C3_OLED_OTA
copy secrets.h.example secrets.h      # Windows
cp   secrets.h.example secrets.h      # bash
```

Poi apri `secrets.h` e riempi SSID, password WiFi, hostname mDNS e le due
password OTA. **Se un giorno ti accorgi di aver committato credenziali vere,
cambia la password della rete**: riscrivere la storia di git non basta, una volta
pubblicata va considerata compromessa.

## Impostazioni Arduino IDE (Tools)

- Board: **ESP32C3 Dev Module**
- USB CDC On Boot: **Enabled** (senza, la `Serial` finisce sui pin UART0 e sulla
  porta USB vedi solo il log di boot della ROM)
- Flash Size: **4MB**
- Partition Scheme: **Minimal SPIFFS (1.9MB APP with OTA)** — consigliato. Va
  bene anche *Default 4MB with spiffs*, ma con questo sketch è già all'84%.
  **Mai** *Huge APP (3MB No OTA)*: senza partizione OTA l'aggiornamento via rete
  smette di funzionare.

## Cablaggio

| OLED | ESP32-C3 Supermini |
|------|--------------------|
| VCC | 3V3 |
| GND | GND |
| SDA | **GPIO5** |
| SCL | **GPIO6** |

I pin sono scelti per non toccare il LED onboard (GPIO8, attivo LOW, fa da
heartbeat) né il tasto BOOT (GPIO9). Se il tuo modulo è cablato diversamente,
cambia `PIN_SDA`/`PIN_SCL` in cima al `.ino`. Indirizzo I2C tipico `0x3C`,
alcuni moduli `0x3D`. Non toccare GPIO18/19: sono l'USB nativo.

## Primo avvio e aggiornamenti

1. Installa **Adafruit SSD1306** da Library Manager (tira dentro GFX e BusIO).
2. Compila `secrets.h` come sopra e carica **via USB**. A schermo compaiono l'IP
   e l'indirizzo `…/update`.
3. **Da Arduino IDE**: in *Tools > Port* compare una porta di rete; selezionala e
   premi Upload (se hai impostato `OTA_PASSWORD`, te la chiede).
4. **Da browser**: *Sketch > Export Compiled Binary*, poi apri
   `http://<hostname>.local/update`, inserisci `WEB_OTA_USER`/`WEB_OTA_PASS` e
   carica il `.bin`. La barra a schermo mostra l'avanzamento e a fine upload la
   scheda si riavvia.

`<hostname>.local` funziona via mDNS: su Windows richiede Bonjour installato, in
alternativa usa direttamente l'IP. L'autenticazione di `/update` e ArduinoOTA è
pensata per una **LAN fidata**: non esporre la scheda su Internet.

Se al boot il WiFi non è raggiungibile lo sketch continua comunque e ritenta in
background, ma per un OTA pulito conviene che la rete ci sia già all'avvio.

---

# Nodo camera — `starters/XIAO_S3_Camera`

Seeed XIAO ESP32-S3 **Sense** (camera + microSD sulla scheda di espansione).
Quando il PIR vede qualcosa muoversi:

```
PIR HC-SR501 -> foto -> JPEG sulla microSD -> notifica ESP-NOW all'hub
```

e in qualunque momento, da `http://<hostname>.local/`, puoi guardare il video
live, scattare a mano, sfogliare/scaricare/cancellare le foto sulla card e
cambiare le impostazioni (sorveglianza armata o no, pausa tra gli scatti,
risoluzione, qualità JPEG, rotazione dell'immagine). L'aggiornamento è OTA come
sul C3.

A differenza del C3 **non è self-contained**: usa `libraries/EspNowLink`, cioè
lo stesso protocollo che parla l'hub. Se sposti la cartella fuori dal repo,
portati dietro anche quella libreria.

## Cablaggio del PIR

| HC-SR501 | XIAO ESP32-S3 Sense |
|---|---|
| VCC | **5V** (il modulo vuole 5V; la sua uscita resta a 3,3V, sicura) |
| GND | GND |
| OUT | **D0 (GPIO1)** |

Sul modulo: ponticello su **H** (retriggerabile) e trimmer **TIME** al minimo —
la pausa tra gli scatti la gestisce il firmware, non il sensore. Dopo
l'accensione il PIR dà falsi positivi per una decina di secondi: il firmware lo
ignora per il primo minuto.

Camera e microSD sono già cablate sulla scheda di espansione, non c'è niente da
collegare. Attenzione a due cose: il chip select della microSD è **GPIO21**
(lo schematico Seeed dice GPIO3, è un errore noto) ed è anche il LED utente,
quindi il LED lampeggia da solo quando si scrive sulla card; e lo slot occupa
tutto il bus SPI, quindi D8/D9/D10 non sono liberi. GPIO liberi per roba tua:
D1–D5.

## Impostazioni Arduino IDE (Tools)

- Board: **XIAO_ESP32S3** (non "ESP32S3 Dev Module")
- PSRAM: **OPI PSRAM** — obbligatoria, senza la camera non va oltre QVGA
- Partition Scheme: **Default 8MB with spiffs (3MB APP/1.5MB SPIFFS)**, che ha
  le partizioni OTA. **Mai** *Maximum APP (No OTA)*.
- USB CDC On Boot: **Enabled** — su questa board è già il default, al contrario
  delle altre schede del repo

Nessuna libreria da installare: il driver della camera è già dentro il core
ESP32.

## Prima di compilare: le credenziali

Come per il C3, `secrets.h` non è nel repository:

```
cd starters/XIAO_S3_Camera
copy secrets.h.example secrets.h      # Windows
cp   secrets.h.example secrets.h      # bash
```

Dentro ci sono rete WiFi, hostname mDNS, password OTA e le credenziali della
web UI — qui proteggono **tutta** la pagina, non solo `/update`: mostra le foto
della camera.

## Il canale ESP-NOW (leggi prima di stupirti)

ESP-NOW ha una radio sola, e questo nodo sta anche sul WiFi (gli servono web UI
e OTA): il canale glielo impone il router. Quindi **l'hub va inizializzato sullo
stesso canale del tuo access point**, con

```c
Link_InitEx(LINK_NODE_HUB, "Hub", canale_del_tuo_AP);   // invece di Link_Init(...)
```

Il canale su cui il nodo sta effettivamente parlando è scritto nella riga di
stato della web UI e sulla seriale all'avvio. Senza WiFi il nodo ripiega sul
canale fisso 6 e l'hub standard va bene com'è.

## Primo avvio

1. Carica **via USB** la prima volta, con le impostazioni qui sopra.
2. Sulla seriale (115200) compaiono PSRAM, esito camera e microSD, IP e canale.
3. Apri `http://<hostname>.local/` e prova *Anteprima* e *Scatta e salva*.
4. Metti l'hub in pairing: il nodo manda HELLO finché non viene accettato, poi
   la UI mostra "hub associato".
5. Da lì in avanti si aggiorna via OTA. **Ferma il video prima di aggiornare**:
   mentre lo stream è aperto la scheda non risponde ad altro.

---

# Esempi (`examples/`)

Non si copiano per iniziare un progetto — per quello ci sono gli starter. Si
aprono in Arduino IDE e si caricano così come sono. Tutti costruiscono la UI in
codice, nessuno usa SquareLine.

| Sketch | Cosa fa | Gira su |
|---|---|---|
| `Orientation_IMU` | livella per veicolo con l'IMU onboard | board AMOLED |
| `DHT11_SD_Logger` | temperatura/umidità a schermo + log CSV su microSD | board AMOLED |
| `Link_Hub_Demo` | hub ESP-NOW: lista dei nodi associati + pairing da touch | board AMOLED |
| `Link_Node_Demo` | nodo sensore finto, solo Serial | qualunque ESP32 |
| `Diag_Hub` / `Diag_Node` | diagnostica ESP-NOW grezza (misura i pacchetti persi) | qualunque ESP32 |

### `Orientation_IMU` — livella per veicolo

Vista dall'alto del mezzo con le 4 ruote: ogni ruota mostra di quanti cm va
rialzata per metterlo in piano (verde = ok, ambra = poco, rosso = molto),
più una bolla centrale e un pulsante CALIBRA che azzera la posizione corrente.

- **Imposta `TRACK_MM` e `WHEELBASE_MM`** in testa allo sketch sul tuo veicolo:
  i default sono di un Adria Matrix Axess 680 SP su Ducato Maxi. La direzione
  resta corretta comunque, ma i centimetri dipendono dalle dimensioni reali.
- Pitch/roll derivano dall'accelerometro (lo yaw non è osservabile dal solo
  accelerometro). Se sinistra/destra o avanti/dietro risultano invertiti, cambia
  i segni nelle due righe indicate nel `loop()`. Se l'IMU non risponde, prova
  l'indirizzo 0x6A in `AMOLED191_IMU.cpp` (default 0x6B).

### `DHT11_SD_Logger` — sensore a schermo e log su microSD

Legge un DHT11 ogni 60 s (`SAMPLE_PERIOD_MS`), mostra temperatura, umidità e
numero di campioni, e accoda ogni lettura valida a `/dht11_log.csv` sulla card.

- **Cablaggio**: modulo a 3 pin, DATA su GPIO2 (`DHT_DATA_PIN`, cambiabile). I
  moduli a 3 pin hanno già il pull-up da 10k a bordo; con un sensore nudo a 4
  pin va aggiunto (4.7k–10k verso 3V3).
- **Senza card funziona lo stesso**: valori a schermo, avviso in rosso, e ritenta
  il mount ogni 30 s — puoi infilarla a scheda accesa.
- Colonne del CSV: `boot_id,n,secondi_da_accensione,temperatura_C,umidita_pct`.
  La scheda non ha un RTC tamponato, quindi l'unico riferimento temporale onesto
  è "secondi da accensione", che riparte da zero ad ogni avvio; `boot_id` è un
  contatore in NVS che distingue una run dall'altra dentro lo stesso file, ed è
  mostrato anche a schermo in alto a sinistra.
- Provato in continuo per 66 ore: 3966 campioni, nessuna lettura persa e file
  integro anche estraendo la card senza spegnere (ogni riga viene aperta,
  scritta e chiusa singolarmente).
- La versione "cresciuta" di questo esempio, con orario vero e dashboard, è
  `projects/EnvNode_C3`.

### `Link_Hub_Demo` + `Link_Node_Demo` — la rete ESP-NOW

Da provare in coppia, su due schede: `Link_Hub_Demo` sulla AMOLED,
`Link_Node_Demo` su qualunque altro ESP32 (non serve schermo — è il punto:
un nodo sensore vero, in genere, non ne ha uno).

1. Carica l'hub sulla board AMOLED e il nodo sull'altra.
2. Sull'hub premi **ASSOCIA NUOVO NODO**: fuori da quella finestra i nodi
   sconosciuti vengono ignorati.
3. Accendi il nodo: deve comparire in una riga entro pochi secondi, con un
   valore finto che si aggiorna ogni ~5 s.

I nodi associati vivono **solo in RAM**: al riavvio dell'hub vanno riassociati.
Per nuovi nodi preferisci varianti recenti (S2/S3/C3/C6): con un ESP32 "classico"
il pairing unicast è risultato lento e inaffidabile su hardware reale.

### `Diag_Hub` / `Diag_Node` — diagnostica ESP-NOW

Coppia usa e getta, scritta per misurare quanti pacchetti si perdono davvero sul
canale. Nessuna libreria di questo repo, nessun pairing, nessun retry: il nodo
spara un contatore in broadcast ogni 500 ms e l'hub conta i buchi nella sequenza,
stampando un riepilogo ogni 5 s (ricevuti, persi, duplicati, RSSI).

Girano in modalità Long Range, quindi **non** si parlano con `EspNowLink`:
servono a misurare il canale, non a interoperare.

---

# Progetti (`projects/`)

Applicazioni vere, non template: hanno una storia di deploy e vanno lette come
esempio di "come viene fuori un progetto finito partendo da uno starter".

### `EnvNode_C3` — nodo ambientale con dashboard

Cresciuto dallo starter `C3_OLED_OTA`, aggiungendo tutto quello che serviva
davvero:

- **DHT11** campionato a intervallo configurabile, con media mobile per l'OLED e
  dato grezzo nel log;
- **microSD su modulo SPI HW-125** (non la SDMMC della board AMOLED): CSV con
  **rotazione giornaliera** in `/logs/YYYY-MM-DD.csv`;
- **orario vero**: nessun RTC tamponato a bordo, quindi l'ora viene stimata al
  boot dalla data di compilazione e poi corretta via **NTP** appena c'è rete —
  ogni riga del CSV porta la colonna `fonte_ora` (`NTP` o `STIMA`), così si sa
  sempre quanto fidarsi di un timestamp;
- **dashboard web** con grafici, download/eliminazione dei log per giorno e
  pagina di configurazione (nome nodo, intervallo, banda di comfort, fuso
  orario), servita dallo stesso `WebServer` dell'OTA;
- **indice di comfort** a soglie configurabili (`comfort.h`): niente Heat Index
  o PMV, che pretenderebbero una precisione che il DHT11 non ha;
- **hub ESP-NOW** (da `v4`): riceve i dati dei nodi a batteria della stazione
  meteo, li mostra su `/nodi` e segnala quelli che hanno smesso di trasmettere.
  Finche' `MeteoHub_S3` non e' pronto, e' questa la scheda che tiene lo storico
  dei nodi che dormono — la loro RAM si azzera ad ogni risveglio;
- **OTA** come sullo starter.

Cablaggio: OLED su SDA=GPIO5/SCL=GPIO6, DHT11 su GPIO0, HW-125 su CS=GPIO1
SCK=GPIO4 MISO=GPIO3 MOSI=GPIO7, tasto BOOT (GPIO9) per cambiare pagina OLED.
Partition Scheme **Minimal SPIFFS**: serve la partizione OTA. Da `v4` la
compilazione vuole `--libraries libraries` (usa `EspNowLink`), che prima non
serviva.

`secrets.h` vale la stessa regola degli starter: copia `secrets.h.example` e
riempilo, il file vero non entra nel repo.

### `Timelapse_XIAO` — camera timelapse con galleria web

Nato dallo starter `XIAO_S3_Camera`, con lo scatto comandato da un **timer**
invece che dal PIR:

- **scatto a intervallo** configurabile (default 60 s), allineato all'orologio
  — con intervallo 60 le foto cadono al secondo `:00` di ogni minuto, non
  "60 secondi dopo l'ultima";
- **finestra oraria** giornaliera opzionale (es. 07 → 20: di notte non scatta,
  e una finestra che scavalca la mezzanotte è ammessa);
- **archivio per giorno** sulla microSD: `/timelapse/2026-08-14/143000.JPG`, più
  un CSV al giorno in `/timelapse/log/` con una riga per scatto, **anche per
  quelli falliti** — in un timelapse il buco nella sequenza è l'informazione
  interessante;
- **galleria web** che sfoglia un giorno alla volta e lo **riproduce come un
  filmato** (play/pausa, 2–20 fps, cursore), con download ed eliminazione della
  singola foto o dell'intera giornata;
- **gestione dello spazio**: sotto la soglia di MB liberi o si ferma (default) o
  elimina il giorno più vecchio come un buffer circolare — mai quello in corso;
- **orario vero** via NTP (`rtc_time.*`, lo stesso modulo di `EnvNode_C3`): qui
  non è un lusso, i nomi delle cartelle e dei file *sono* la data e l'ora;
- **OTA** e watchdog di riconnessione WiFi, perché resta acceso per settimane.

Niente PIR e niente ESP-NOW: non dipende da `libraries/` ed è spostabile fuori
dal repo così com'è. L'unico cablaggio è l'alimentazione USB.

`secrets.h`: stessa regola, copia `secrets.h.example` e riempilo. Tieni un
`OTA_HOSTNAME` diverso da quello del nodo camera, o i due si pestano i piedi su
mDNS.

---

## Dettaglio file per file

`docs/FILES.md` documenta ogni file del repo: scopo, funzioni chiave,
dipendenze e cosa conviene non toccare.
