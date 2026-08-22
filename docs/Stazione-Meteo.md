# Stazione meteo e-ink — piano di lavoro

Stato al **2026-08-22**. Il pannello e-ink è arrivato il 2026-08-21 e
`projects/MeteoHub_S3/` — bring-up del solo pannello — è committato e
**funzionante su hardware**. Il primo sensore è stato saldato e cablato al nodo
il 2026-08-22: `projects/MeteoNode_C3/` per ora contiene **solo il bring-up del
sensore, con pagina web e OTA** — provato su hardware e funzionante — ma
niente ESP-NOW, né batteria, né deep sleep.

**OTA — usare la pagina `/update`, non `arduino-cli upload -p <ip>`.** Provate
entrambe il 2026-08-22 su `EnvNode_C3`: la POST multipart del `.bin` su
`/update` (campo del form `update`, basic-auth) ha funzionato, 1,2 MB in 29 s,
`HTTP 200 OK`, nodo ripartito e regolare. La strada `arduino-cli` invece
supera l'autenticazione e poi muore con `No response from device`: nel
protocollo ArduinoOTA è la **scheda** a ricollegarsi al PC per scaricare il
firmware, quindi serve una connessione in ingresso che il firewall di Windows
blocca. Da confermare, ma la pagina web risolve senza toccare il firewall.
Su `MeteoNode_C3` la stessa `arduino-cli` falliva ancora prima, in
autenticazione — quello resta inspiegato, e lì il `.bin` non è mai stato
caricato per davvero.

## Obiettivo

Misurare temperatura/umidità/pressione **dentro e fuori casa** e mostrare il
differenziale su un pannello e-ink, per sapere quando conviene aprire le
finestre per rinfrescare. Il pannello ospita anche altre pagine, la prima delle
quali è una galleria di immagini caricate via web.

```
nodi C3 (AHT20+BMP280, a batteria)  --ESP-NOW-->  hub S3 Sense
                                                    |-- pannello e-ink 4.2"
                                                    |-- microSD (log CSV)
                                                    +-- web UI + OTA
```

## Hardware

| Pezzo | Quantità | Stato |
|---|---|---|
| WeAct e-ink 4.2" 400×300 **bianco/nero** | 1 | arrivato il 2026-08-21 |
| XIAO ESP32-C3 (nodi sensore) | ≥ 2 | disponibili |
| XIAO ESP32-S3 Sense (hub) | 1 | disponibile — **la stessa che servirebbe a `projects/Timelapse_XIAO/`** |
| AHT20 + BMP280 (modulo combinato I2C) | ≥ 2 | in casa; il primo **saldato e cablato** al nodo il 2026-08-22 |
| 18650 3,7 V 3000 mAh nominali, con JST SM-2 | — | arrivate il 2026-08-19 |
| Connettori JST SM-2 maschio/femmina | — | disponibili |

I 3000 mAh delle celle economiche sono quasi sempre 1500–2200 reali:
dimensionare l'autonomia su quelli.

## Decisioni prese (con il perché)

1. **L'hub sta sempre su USB**, la 18650 è solo un UPS. Per ricevere ESP-NOW e
   servire la web UI la radio deve restare accesa: 60–100 mA continui, cioè
   20–30 ore a batteria. Il deep sleep va sui nodi, non sull'hub. Nota
   gradevole: l'e-ink è bistabile, quindi durante un blackout **il pannello
   continua a mostrare l'ultimo frame** anche a scheda spenta.
2. **Il dithering si fa nel browser**, non sull'ESP32. A bordo servirebbero
   decoder JPEG e PNG, ricampionamento a dimensione arbitraria, crop e
   Floyd–Steinberg su immagini da telefono di 12 megapixel. Nel browser sono
   poche decine di righe di canvas, l'anteprima è esatta, e il firmware si
   riduce a "ricevi 15.000 byte, scrivili su SD".
3. **Pannello solo bianco/nero.** La variante con il rosso raddoppia il buffer,
   non fa refresh parziale e ci mette ~15 s ad aggiornarsi.
4. **Il "quando aprire" non si decide sul solo delta di temperatura** ma
   confrontando l'**umidità assoluta** (g/m³) o il punto di rugiada tra dentro e
   fuori: se fuori è più fresco ma molto più umido, aprire peggiora le cose.
   Modulo `ventilation.h` header-only e puro, sullo stampo di
   `projects/EnvNode_C3/comfort.h`. **Serve isteresi**, o l'etichetta sfarfalla
   tra APRI e CHIUDI tutto il pomeriggio.
5. Il BMP280 regala gratis il **trend barometrico a 3 ore**, che è la vera
   previsione del tempo casalinga.

## Formato delle immagini — il contratto tra browser e firmware

```
400 x 300, 1 bit per pixel, NESSUN header: il file E' il framebuffer
riga  = 50 byte (400/8), righe = 300, totale = 15.000 byte esatti
MSB-first: il bit 7 del primo byte di ogni riga e' il pixel x = 0
1 = bianco, 0 = nero        (formato nativo della RAM del pannello)
percorso su SD: /images/<nome>.bin
nome: [A-Za-z0-9_-], max 24 caratteri
```

È il formato che `GxEPD2::writeImage()` spinge senza conversioni. Se al primo
test i colori risultassero scambiati si corregge con il flag `invert`, non
rifacendo nulla.

Conseguenza da ricordare: **non servono miniature**. La galleria web rilegge lo
stesso `.bin` e lo ridisegna su un canvas — niente seconda codifica, niente file
extra sulla card. 15 KB per immagine: il limite pratico non esiste.

## Fatto finora

`projects/MeteoHub_S3/www/dither.html` — pagina di ritaglio + dithering che gira
nel browser, senza dipendenze esterne. Ritaglio con pan/zoom nel riquadro
400×300, mipmap a dimezzamenti successivi per non aliasare le riduzioni forti,
luminosità/contrasto/gamma via LUT a 256 voci, Floyd–Steinberg (con serpentina)
/ Atkinson / Bayer 8×8 / soglia secca, inversione, anteprima 1:1 con simulazione
della resa e-ink, export del `.bin` e rilettura di un `.bin` per verifica.
Render completo misurato **13,4 ms**.

Verificato eseguendola davvero: 15.000 byte esatti, bianco `0xFF` / nero `0x00`,
MSB-first, giro `pack`/`unpack` identico, i quattro algoritmi funzionanti e
distinti, rotazione, inversione, sanificazione del nome file.
**Non provati**: il click su Scarica, il drag&drop, una foto vera con
orientamento EXIF.

`projects/MeteoHub_S3/MeteoHub_S3.ino` — **bring-up del pannello e-ink**,
scritto e **provato sull'hardware vero il 2026-08-21**: il pannello disegna, la
classe è quella giusta, il refresh parziale funziona. 431 KB di flash (12% della
partizione app), 38 KB di RAM globale. Girato sulla XIAO S3 **senza la scheda di
espansione Sense**, quindi il bus SPI era tutto dell'e-ink: la convivenza con la
microSD resta da provare. Non c'è
nulla della stazione meteo: né ESP-NOW, né SD, né web UI, né OTA. Fa quattro
cose in fila e cronometra tutto su Serial:

1. stampa quello che il driver dichiara di sé (dimensioni dopo la rotazione,
   `hasFastPartialUpdate`, tempi nominali);
2. schermata Adafruit_GFX: cornice, quattro tacche **diverse** agli angoli
   (così una rotazione o uno specchio non possono sembrare giusti), diagonale,
   tre corpi di testo, barre di retino 100/50/25/6%;
3. schermata costruita a mano **nel formato del progetto** — 15.000 byte, 1 bpp,
   MSB-first, 1 = bianco — e spinta con `drawImage()`. Verifica il contratto con
   `dither.html` prima che esista una riga di web UI: se i bit fossero
   impacchettati al contrario il righello a passo 8 px scivola, se il passo riga
   non fosse 50 byte la diagonale si spezza;
4. cinque refresh parziali cronometrati; poi a regime un contatore ogni 20 s,
   con un refresh completo ogni 10 — la politica antighosting del piano,
   accelerata per vederla lavorare in pochi minuti.

**Verifica visiva fatta sul pannello** (foto delle pagine, 2026-08-21):

- **orientamento giusto, nessuno specchio**: le quattro tacche d'angolo sono
  ognuna al suo posto (pieno in alto a sinistra, vuoto in alto a destra,
  barretta in basso a sinistra, cerchio in basso a destra) e la diagonale va da
  angolo ad angolo senza scalini;
- **il contratto del formato immagine e' confermato sul vetro**: righello a
  passo 8 px attaccato alla cornice sinistra (quindi MSB-first: il bit 7 e'
  davvero il pixel x = 0), diagonale continua su tutta la larghezza (quindi il
  passo riga e' 50 byte), scacchiera a 1 px che rende grigio uniforme e
  scacchiera a 2 px nettamente piu' grossa, nero pieno e bianco pieno
  affiancati con buon contrasto. `dither.html` puo' produrre `.bin` senza
  correzioni: niente `invert`, niente `mirror_y`;
- **le quattro densita' di retino si distinguono tutte** (100/50/25/6%), quindi
  il dithering delle pagine immagine ha senso su questo pannello;
- i font Adafruit GFX sono leggibili fino al corpo piccolo (`FreeMonoBold9pt`);
  `U8g2_for_Adafruit_GFX` continua a non servire.

**Misure vere, dal log della scheda** (contano piu' dei tempi nominali del
driver, che dichiara 1200/400 ms):

| Operazione | Tempo |
|---|---|
| refresh completo del pannello | **2197 ms** |
| refresh parziale | **562 ms** |
| pagina immagine intera: 15.000 byte + refresh (`drawImage`) | **737 ms** |
| aggiornamento del riquadro contatore, con `hibernate()` | **827 ms** |

Da cui due cose che cambiano il disegno delle pagine:

- **il parziale costa uguale qualunque sia la finestra** (l'SSD1683 fa comunque
  una passata su tutto il pannello): aggiornare un numerino costa quanto
  aggiornare mezza pagina. Non ha senso spezzettare la pagina in tante
  finestrelle per risparmiare — conviene un solo parziale che riscrive tutta la
  zona dei valori;
- **una pagina immagine costa 737 ms**, cioe' meno di un refresh completo: il
  formato del progetto passa senza conversioni, come previsto.

**Trappola trovata sull'hardware — la `Serial` che blocca.** Con il cavo USB
staccato (o collegato ma senza nessuno che legga la porta) il buffer del CDC si
riempie e ogni `printf` aspetta il proprio timeout: un aggiornamento da 827 ms
e' diventato **10.639 ms**, quasi tutti passati dentro le diagnostiche interne
di GxEPD2 (`_Update_Full`, `_PowerOff`, abilitate dal primo parametro di
`display.init()`). Rimedio, in cima a `setup()`:

```cpp
Serial.setTxTimeoutMs(0);   // se nessuno ascolta, il log si butta
```

**Riguarda anche gli altri progetti del repo**: `EnvNode_C3` e `Timelapse_XIAO`
stanno accesi per settimane senza cavo USB e nessuno dei due lo fa — da
verificare se ne soffrono (li' non c'e' GxEPD2, ma il meccanismo e' lo stesso).

**Tasto BOOT** (GPIO0): avanza di una pagina. Le pagine sono cinque — geometria,
formato-progetto, contatore, foto, bianca — e dopo l'ultima si ricomincia, cosi'
smettere di premere lascia sempre il pannello pulito. L'elenco di pagine con la
loro funzione di disegno e' in piccolo l'astrazione di Fase 6; il cambio pagina
e' sempre un refresh completo.

**Catena `dither.html` → pannello chiusa** (2026-08-21): una foto vera passata
dalla pagina (ritaglio, rotazione, Floyd-Steinberg a serpentina), esportata col
pulsante **Scarica — che ha prodotto 15.000 byte esatti** — e incollata da uno
script in `foto_prova.h`, si vede sul pannello **senza toccare un byte**: nessun
`invert`, nessun `mirror_y`, nessuna riga di conversione nel firmware. Il
percorso e' identico a quello finale, cambia solo da dove arrivano i byte (oggi
la flash, domani `/images/<nome>.bin` sulla SD). L'header **e' solo per la
prova**: 15 KB di flash per immagine non e' una strada percorribile, sparisce
quando ci sara' la microSD.

**Risultato sul vetro** (seconda passata, verificata in foto): la scena si legge
— testo, struttura e mezzitoni distinguibili — e l'**orientamento e' giusto**,
confermato guardando il pannello. Resta un pulviscolo sul fondo chiaro invece di
bianco pulito: si toglie alzando ancora (bri 30, gamma 1,35 → ~18-20% di nero),
al prezzo dei dettagli piu' tenui. Scelta di gusto per soggetto: conviene con
molto cielo o molto muro, non sui ritratti.

**Taratura del dithering — la lezione della prima prova.** A impostazioni
neutre la foto e' uscita col **52% di pixel neri** e sul pannello era una
poltiglia grigia: il fondo chiaro della scena diventava un retino a meta'
densita' invece che carta bianca. L'e-ink comprime la scala (il bianco e' un
grigio chiaro, il nero un carbone), quindi una conversione *fedele* rende
piatta. Rifatta con **luminosita' +20, contrasto +12, gamma 1,22 → 27,9% di
nero**. Regola pratica da cui partire: per una scena in prevalenza chiara si
punta al **25-30% di nero**, e si guarda quel numero, non l'anteprima a
schermo — il monitor mente sul contrasto, il conteggio no.

**Gotcha di `dither.html` da ricordare** quando la si integrera' nella web UI
dell'hub (Fase 5): `render()` si coalescia su `requestAnimationFrame` e alza un
flag `pending` che solo il frame successivo riabbassa. In un tab **non visibile**
quel frame non arriva, quindi il flag resta alzato e ogni render successivo esce
subito: la pagina sembra viva (i cursori si muovono, le etichette cambiano) ma
non ricalcola piu' niente, e un export fatto in quello stato salva l'**ultimo
render riuscito**, non quello che si crede. Con un utente vero davanti si
ricompone da solo appena il tab torna in primo piano, quindi non e' urgente, ma
va saputo: **il numero di nero e' l'unico modo di accorgersene**.

Restano non provati di `dither.html`: il **drag&drop** (l'immagine e' stata
passata all'`<input type=file>`, che e' lo stesso gestore ma non la stessa
strada) e una foto con **orientamento EXIF** non normalizzato — quella usata
veniva da WhatsApp, che l'EXIF lo appiattisce.

**Effetto collaterale di `setTxTimeoutMs(0)`, da sapere**: il log seriale ora
esce solo se dall'altra parte c'e' un terminale vero che asserisce DTR. Un
lettore passivo della porta (uno script che apre la COM e legge senza alzare
DTR) non riceve niente — verificato, una cattura di 5 minuti e' tornata vuota.
Non e' un difetto: e' il prezzo di non far mai aspettare il firmware. Per
leggere il log si usa il monitor seriale dell'IDE.

Nient'altro esiste ancora: nessun altro sketch, nessun commit. Libreria **GxEPD2 1.6.9** installata via Library Manager il
2026-08-21; `U8g2_for_Adafruit_GFX` per ora non serve, i font `FreeSansBold*`
di Adafruit GFX bastano.

## Da fare

### Fase 1 — nodo `projects/MeteoNode_C3/`, su USB, senza deep sleep

Legge AHT20+BMP280 e manda un DATA ESP-NOW. Serve solo a validare sensore e
collegamento prima di aggiungere batteria e sleep.

**Fatto il 2026-08-22 — bring-up del sensore, con web UI.** Come per l'hub,
prima il bring-up e poi il resto: `MeteoNode_C3.ino` oggi accende il sensore,
legge il chip ID del BMP280, stampa le misure ogni 2 s e offre i comandi
`s`/`r`/`p` (scan I2C, power-cycle, spegni-accendi) **sia da seriale sia da
una pagina web** (`net_ota.*` + `web_ui.*` ripresi da `EnvNode_C3`), perche' a
batteria la Serial non c'e' e il test sarebbe cieco. Niente ESP-NOW, niente
deep sleep, partitore batteria non ancora cablato (`BATTERY_ADC_ENABLED 0`).
Serve a distinguere una saldatura fredda da un errore di firmware **prima**
che ci sia firmware da sbagliare.

**Provato su hardware il 2026-08-22**, tutto verificato: AHT20 a 0x38, BMP280
a **0x77** (non 0x76: il fallback nel codice serve), chip ID 0x58 quindi
BMP280 vero e non un BME280 travestito; misure plausibili e stabili
(~28 °C, ~50 %RH, ~1011 hPa) e coerenti con `EnvNode_C3` interrogato in
parallelo; alimentazione da GPIO confermata dalla pagina web (spegnendo, le
letture si congelano); WiFi a −39/−48 dBm sul **canale 6**, che e' gia' il
canale di default di `EspNowLink` — comodo, ma da fissare nel router come
dice la Fase 4.

**Aggiunto lo stesso giorno (firmware `v2`), provato su hardware a batteria**:

- **Previsione dal trend barometrico a 3 ore** (`forecast.h`, header-only e
  puro come `comfort.h`): pressione riportata al livello del mare, trend
  classificato con le soglie convenzionali (0,5 / 1,6 / 3,5 / 6,0 hPa) e
  isteresi per non far sfarfallare l'etichetta, testo di previsione che
  combina trend e valore assoluto. Finché non ci sono tre ore di storico dice
  "raccolgo dati" invece di inventare.
- **Intervallo di misurazione impostabile da pagina** (2–3600 s, default 60),
  persistito in NVS. La prima lettura si fa subito all'avvio e non dopo un
  intervallo, o con l'intervallo a un'ora il nodo sembrerebbe rotto per
  un'ora dopo ogni riavvio.
- **Storico 24 h in RAM e tre grafici SVG** disegnati a mano nella pagina,
  senza librerie esterne. Griglia fissa da 2 minuti, 720 slot, ~4,3 kB; ogni
  slot è la media delle misure cadute nella finestra, gli slot senza dati
  restano buchi visibili nella linea invece di essere interpolati.
  `/api/storico` è servito **a blocchi**, non come una String unica da 10 kB.
  **Differenza da `EnvNode_C3` da tenere a mente**: là i grafici vengono dai
  CSV sulla microSD, qui non c'è microSD e lo storico si azzera a ogni
  riavvio. I dati storici veri li terrà l'hub, quando ci sarà l'ESP-NOW.
- **Altitudine impostabile e calibrabile**: si inserisce la pressione al
  livello del mare letta da un bollettino e il firmware ricava la quota dalla
  pressione che sta misurando. Serve solo a rendere il numero confrontabile
  coi bollettini — il trend, e quindi la previsione, non dipende
  dall'altitudine. Default 40 m per Trieste: **è una stima, va calibrata**.
  Il partitore della batteria non è ancora cablato.

Due cose imparate, che valgono per tutto il repo:

- **`Serial.setTxTimeoutMs(0)` e' obbligatorio sulle board con USB nativa**
  (XIAO C3/S3). `Serial` non e' una UART: e' la CDC, e senza un host che
  svuoti il buffer le `print()` BLOCCANO fino a un timeout interno,
  fermando `loop()` — quindi web server, OTA e letture. Il sintomo non
  somiglia alla causa: da rete la pagina moriva dopo ogni comando che stampa
  molte righe, mentre col monitor seriale collegato le stesse operazioni
  erano istantanee. **Vale anche per `Timelapse_XIAO`, `XIAO_S3_Camera` e
  `MeteoHub_S3`**, che stanno accesi per giorni senza cavo: da verificare.
- **La scansione I2C non va nell'init** ma solo su richiesta, e `Wire`
  vuole `setTimeOut(10)` (il default e' 50 ms per indirizzo vuoto).
  Init misurato: 251 ms.

- `value[0]` = °C, `value[1]` = %RH, `value[2]` = hPa — i tre float di
  `link_message_t` bastano esatti. `battery_mv` valorizzato.
- Valutare un `link_node_type_t` nuovo **in coda** all'enum (aggiungere in fondo
  è retrocompatibile), oppure riusare `LINK_NODE_SENSOR_TEMPERATURE`.
- **Pin (XIAO ESP32-C3)** — cablaggio reale, saldato il 2026-08-22:

  | Filo del modulo | Pin | GPIO |
  |---|---|---|
  | SCL | D2 | 4 |
  | VCC (**commutato**, vedi Fase 4) | D3 | 5 |
  | SDA | D4 | 6 |
  | GND | GND | — |

  SCL sta su **D2 e non sul D5/GPIO7 di default**: sull'ESP32-C3 l'I2C passa
  dalla GPIO matrix, quindi i pin si scelgono dove conviene e `Wire.begin()` li
  vuole espliciti. D2/GPIO4 non è di strapping (lo sono GPIO2/8/9) ed è
  RTC-capable come serve alla Fase 4. Resta libero D5/GPIO7. Il VCC **non** va
  al pad 5V: è alimentato solo dalla USB e a batteria è morto.
- Partitore batteria su **D1/GPIO3**, tenuto libero apposta; mai su GPIO2/8/9
  (strapping) e mai su ADC2 (inutilizzabile con il WiFi acceso): 2×1 MΩ + 100 nF.
- **Collegare l'antenna esterna** del C3. Senza, da fuori attraverso un muro non
  arriva niente.
- **Dissaldare il LED di alimentazione** del modulo sensore se ce l'ha: 2 mA
  sempre accesi svuotano la 18650 in tre settimane.
- Compilazione. Finché è solo il bring-up è self-contained, **niente**
  `--libraries`; quando arriverà l'ESP-NOW lo vorrà, a differenza di
  `EnvNode_C3`, perché userà `EspNowLink`:
  ```
  arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32C3:PartitionScheme=min_spiffs" projects/MeteoNode_C3
  ```
  **Attenzione al CDC**: la board è `XIAO_ESP32C3`, non `esp32c3` generica, e
  come sulla XIAO S3 ha `CDCOnBoot` già Enabled di default — nel FQBN
  `CDCOnBoot=cdc` significherebbe *Disabled*. Qui non va messo nulla.
- Librerie esterne: **Adafruit AHTX0** + **Adafruit BMP280 Library** (+ Adafruit
  Unified Sensor, Adafruit BusIO). Installate il 2026-08-22.
- **Se il chip ID esce 0x60** il modulo monta un BME280, non un BMP280: ha anche
  l'umidità e vuole la libreria Adafruit BME280. Il bring-up lo dice a schermo.

### Fase 2 — hub `projects/MeteoHub_S3/`, pannello e-ink

Valori a schermo, niente SD/web ancora. Valida GxEPD2 e il refresh parziale.

- Libreria **GxEPD2** + Adafruit GFX; **U8g2_for_Adafruit_GFX** se servono font
  grandi decenti per i numeroni.
- **Pannello identificato** (2026-08-21, etichetta sul flat): è l'**E042A87**,
  controller **SSD1683** — confermato dal repo WeAct, che per il 4.2" B/N tiene
  `Doc/4.2 Inch Black&Write/英瑞达E042A87（BW）.pdf` +
  `4D2_BW_400X300_1683_UT From MCU.c` e il `Doc/SSD1683_Datasheet.PDF`. **Il
  refresh parziale c'è**, come il piano dava per scontato.
- **Classe GxEPD2: `GxEPD2_420_GDEY042T81`** (400x300, SSD1683). È quella usata
  dall'esempio ufficiale WeAct per questo modulo
  ([`Example/EpaperModuleTest_Arduino_ESP32S3`](https://github.com/WeActStudio/WeActStudio.EpaperModule)),
  quindi è provata sull'hardware vero, ed è il default da cui partire.
  L'alternativa è `GxEPD2_420_GYE042A87`, che in GxEPD2 corrisponde alla sigla
  del pannello alla lettera (`GYE042A87 … SSD1683 (HINK-E042-A07-FPC-A1)`):
  stesso controller, cambiano init e LUT del parziale. Terzo ripiego, se fosse
  un lotto vecchio: `GxEPD2_420` (GDEW042T2, UC8176, solo refresh completo).
  **Tenere la scelta dietro un `#define`** in cima allo sketch: cambiarla è una
  riga, e non si perde tempo se il ghosting del parziale non convince.
- L'esempio WeAct chiama `display.init(115200, true, 50, false)`: il **50** è la
  durata dell'impulso di reset in ms (il default di GxEPD2 è più corto). Se
  all'accensione il pannello sembrasse morto, è il primo parametro da guardare.
  Il flag `hasFastPartialUpdate` va interrogato a runtime, come fa l'esempio,
  invece di darlo per buono.
- 15.000 byte stanno in RAM interi: modalità full-buffer, niente paginazione.
- **Refresh**: parziale ogni 5–10 min, **completo ogni ora o al cambio pagina**,
  o il ghosting si accumula. `hibernate()` dopo ogni refresh.
- **Pin — l'e-ink condivide il bus SPI con la microSD della Sense** (l'e-ink non
  usa MISO, gli basta un CS separato):

  | Segnale | GPIO | Nota |
  |---|---|---|
  | SCK / CLK | 7 (D8) | condiviso con la SD |
  | MOSI / DIN | 9 (D10) | condiviso con la SD |
  | CS | 2 (D1) | dedicato |
  | DC | 3 (D2) | dedicato |
  | RST | 4 (D3) | dedicato — serve a `hibernate()` |
  | BUSY | 1 (D0) | dedicato |

  Restano liberi D4/D5 (GPIO5/6) per l'I2C e il **tasto BOOT (GPIO0)** per
  cambiare pagina: nessun bottone da aggiungere.
  **Se la condivisione SPI desse problemi**, ripiego: bus dedicato all'e-ink su
  D0–D5, rinunciando al sensore I2C locale sull'hub; GPIO43/44 (D6/D7) sono
  ulteriore margine, visto che la `Serial` passa dall'USB CDC.
- **Attenzione al CDC**: su XIAO S3 `CDCOnBoot` è già Enabled di default e nel
  FQBN `CDCOnBoot=cdc` significa *Disabled*. Qui non va messo nulla:
  ```
  arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB" --libraries libraries projects/MeteoHub_S3
  ```

### Fase 3 — SD, orario, web UI, OTA

Riuso quasi verbatim da `projects/EnvNode_C3/`: `sd_logger.*` (CSV con rotazione
giornaliera), `rtc_time.*`, `net_ota.*` (variante con `net_server()` condiviso),
`web_ui.*`. Più `ventilation.h` nuovo.

- Vale l'ordine obbligato di `EnvNode_C3`: `rtctime_begin(tz)` →
  `rtctime_seedFromBuild()` → (WiFi) → `rtctime_onWifiConnected()`, quest'ultima
  **a ogni riconnessione**, non solo la prima.
- Con 2–3 nodi ogni 5 minuti si fanno ~864 righe al giorno: qui diventa
  rilevante il backlog già annotato per la dashboard di `EnvNode_C3` (rollup
  giornaliero + decimazione lato server).
- Aggiungere il **watchdog di riconnessione WiFi** come in `EnvNode_C3` e
  `Timelapse_XIAO`: questa scheda sta accesa per settimane.

### Fase 4 — deep sleep e batteria sui nodi

Sveglia ogni ~5 min, legge, manda, si riaddormenta. Con ~44 µA dormendo e ~0,5 s
di radio a risveglio l'ordine di grandezza è **6–12 mesi** su una 18650 reale.

**Modifiche necessarie a `libraries/EspNowLink`** (retrocompatibili: la libreria
è condivisa col nodo camera e con le demo):

- il **nodo** deve memorizzare MAC dell'hub + canale nella **RTC memory**, che
  sopravvive al deep sleep, e mandare DATA diretto al risveglio, ricadendo su
  HELLO solo dopo N fallimenti. Oggi ripartirebbe da HELLO in broadcast ad ogni
  risveglio, con l'hub costretto a stare sempre in pairing mode;
- l'**hub** deve persistere il registro peer in **NVS**: oggi vive solo in RAM e
  un reboot perde tutti i nodi.

**Canale ESP-NOW**: l'hub sta sul WiFi del router, quindi il canale glielo impone
l'AP e tutti devono usare quello (`Link_InitEx` con il canale dell'AP, non
`Link_Init`). **Fissare il canale 2,4 GHz nel router**, altrimenti un giorno
cambia da solo e i nodi diventano muti.

**Alimentare il sensore da un GPIO** — **deciso e saldato il 2026-08-22: VCC su
D3/GPIO5**, non al 3V3 fisso. L'LDO della basetta piu' l'eventuale LED piu' il
riposo dei sensori stanno sulle decine di µA, cioe' lo stesso ordine dei ~43 µA
di deep sleep della XIAO: spegnere il modulo fra una lettura e l'altra puo'
quasi dimezzare il consumo a riposo. AHT20 + BMP280 assorbono qualche centinaio
di µA con picchi sotto i 2 mA, quindi un GPIO basta e non serve un MOSFET.
I tre vincoli, e come sono stati risolti:

- **il pin dev'essere RTC-capable** (sul C3 sono GPIO0-5), altrimenti
  `gpio_hold_en()` + `gpio_deep_sleep_hold_en()` non lo possono tenere basso
  durante il sonno e i GPIO tornano flottanti. Scelto **D3/GPIO5** — non
  D1/GPIO3, riservato al partitore della batteria;
- **prima di dormire, SDA e SCL vanno messi a ingresso senza pull-up interno**:
  i pull-up dell'I2C stanno sulla basetta e si spengono con lei, quindi un pin
  dell'ESP32 che resta alto spinge corrente nei piedini di un chip non
  alimentato e, attraverso i diodi di protezione, ne alimenta parzialmente il
  VDD. Il sensore resta "mezzo acceso", consuma e non si resetta pulito. Da
  fuori sembra solo che il deep sleep consumi piu' del previsto. Il bring-up
  di Fase 1 lo fa gia' cosi' in `sensorPower(false)`;
- **il sensore va reinizializzato ad ogni risveglio**, non solo al primo boot:
  accensione, ~100 ms di attesa (l'AHT20 lo chiede da datasheet), `Wire.begin()`,
  lettura. Se non e' scritto cosi' fin dall'inizio, il giorno che si aggiunge il
  deep sleep sembrera' che il sensore si sia rotto. Nel bring-up e' gia' la
  coppia `sensorPower(true)` + `sensorsBegin()`, esercitata dal comando `r`:
  quando arrivera' il deep sleep basta agganciarla al risveglio.

Il partitore della batteria invece conviene lasciarlo fisso: 2x1 MΩ sono ~1,7 µA,
non valgono la complicazione di commutarli.

**OTA "pull": il nodo che si aggiorna da solo.** Va fatto *insieme* al deep
sleep, non dopo, perché è il deep sleep a renderlo necessario: un nodo che
dorme è irraggiungibile per il 99% del tempo, quindi non gli si può più
**spingere** un firmware come si fa oggi con `/update`. Deve essere lui a
chiedere, al risveglio. Le due cose condividono anche la logica del risveglio,
quindi conviene scriverle insieme.

Meccanica, tutta già nel core ESP32 (classe `HTTPUpdate`): il nodo scarica un
manifest di poche centinaia di byte con versione e URL del `.bin`, lo confronta
con la propria, e se serve scarica e riavvia. `HTTPUpdate` sa anche mandare la
versione corrente in un header e ricevere un **304 Not Modified**, che rende il
controllo di routine quasi gratuito.

- **Dove sta il firmware: sull'hub.** `MeteoHub_S3` è sempre alimentato, ha la
  microSD e già serve una web UI: è il punto di distribuzione naturale. Si
  carica il `.bin` una volta lì e i nodi se lo prendono quando si svegliano.
  Niente NAS, niente servizi esterni. GitHub Releases vorrebbe HTTPS, quindi
  certificati e più flash: su una LAN fidata non ripaga.
- **Frequenza**: una volta al giorno, a un'ora fissa. Non a ogni risveglio — la
  radio accesa è la voce di consumo dominante, e un controllo inutile costa più
  dell'aggiornamento.
- **Mai aggiornare sotto soglia di batteria.** Un brownout a metà scrittura
  trasforma un nodo in giardino in un oggetto da recuperare col cacciavite.
  **Finché il partitore su D1/GPIO3 non è cablato il nodo non sa quanta
  batteria ha**, quindi non può prendere questa decisione: è un motivo in più
  per saldarlo prima di mettere i nodi a dormire.
- **Rollback.** Il caso da cui proteggersi non è il download fallito — quello è
  già gestito, il firmware nuovo semplicemente non viene attivato. È il caso
  cattivo: firmware che si installa, parte, e **non si connette al WiFi**. Vivo
  ma irraggiungibile, e nessun OTA lo salva più. Contromisura: contatore al
  boot, se dopo N tentativi la rete non c'è si torna alla partizione
  precedente.
- **Serve uno schema di versione confrontabile.** Oggi `FW_VERSION` è la
  stringa `"v2"`: va bene per un umano che legge la pagina, ma un confronto
  automatico direbbe che `"v10"` è minore di `"v9"`. Serve un intero monotono o
  una data tipo `2026082201`.

**Sicurezza delle celle**:

- **verificare la polarità del JST col tester** prima di collegarlo ai pad B+/B−
  del XIAO: le celle prewired arrivano cablate in entrambi i versi e
  l'inversione fa fuori la scheda;
- usare celle **protette** o un modulo di protezione in linea (DW01+8205A): il
  caricabatterie a bordo del XIAO protegge dalla sovraccarica, non dalla scarica
  profonda;
- **cutoff firmware**: sotto ~3,3 V, deep sleep permanente;
- non caricare una cella sotto 0 °C. Il nodo esterno non viene caricato in loco:
  con il JST staccabile si porta dentro la cella e si carica a parte.

**Nodo esterno**: in ombra, in un contenitore ventilato (una schermatura tipo
Stevenson, o almeno sotto una gronda esposta a nord). Al sole la lettura non
significa niente. Attenzione alla condensa; il freddo riduce la capacità della
cella.

### Fase 5 — pagine immagine

Le scelte di interfaccia (modello delle pagine, impostazioni, messaggi,
anteprima) stanno nella sezione **Web UI dell'hub** piu' sotto, che e' una bozza
aperta: leggerla prima di implementare.

- endpoint di upload che accetta esattamente 15.000 byte e scrive
  `/images/<nome>.bin`, con la stessa regola difensiva di `sd_name_is_safe()`;
- galleria web che rilegge i `.bin` e li ridisegna su canvas (il codice
  `unpack()`/`paint()` è già in `dither.html`);
- integrare `dither.html` nella web UI dell'hub, sostituendo il download del
  file con un POST diretto alla scheda;
- pagina "immagine" sul pannello ed eventuale slideshow. **Non ogni pochi
  secondi**: il refresh completo lampeggia e consuma. Ordine dei minuti.

### Fase 6 — pagine extra

Qui si realizza il modello delle pagine descritto in **Web UI dell'hub**: elenco
di pagine con tipo/layout/attiva/durata, e i tre comandi dell'interfaccia
(rotazione, durata, fissa una pagina) che ne discendono senza meccanismi propri.

Astrazione "pagina" (elenco + callback di disegno), cambio con il tasto BOOT
oppure da web. Il cambio pagina è sempre un refresh completo.

## Web UI dell'hub — specifica in lavorazione

> **Work in progress.** Questa sezione è una bozza aperta, scritta il 2026-08-21
> discutendone a voce: niente qui è deciso in via definitiva e ci si tornerà
> sopra. Serve a fissare le scelte mentre sono fresche e a far vedere quali
> conseguenze hanno sul firmware, non a chiudere il discorso.

**Dipende tutto dalla Fase 3**: oggi l'hub non ha né WiFi, né web server, né SD
montata. `www/dither.html` è una pagina a sé che gira nel browser e produce un
`.bin`; non è ancora "la web UI". Nessuna delle funzioni qui sotto può esistere
prima che l'hub stia in rete e sappia leggere e scrivere la card.

### Il modello delle pagine è la spina dorsale

Tutto quello che si vuole dall'interfaccia — scegliere layout, ruotare le
pagine, fissarne una, mostrare messaggi — è **la stessa struttura**: un elenco
di pagine, ognuna con

| campo | significato |
|---|---|
| `tipo` | valori / grafico / nodi / immagine / messaggi |
| `layout` | quale disposizione, fra le poche previste per quel tipo |
| `attiva` | partecipa alla rotazione sì/no |
| `durata_s` | per quanto resta a schermo quando ruota |
| `parametri` | dipendono dal tipo (quale immagine, quale nodo, ecc.) |

Conseguenza da non perdere di vista: **«fissa una singola pagina» non è una
funzione a sé**, è "tutte le altre disattivate". Stessa cosa per «cambio pagina
automatico»: è la rotazione che si ferma quando resta una sola pagina attiva.
Una struttura sola, tre comandi nell'interfaccia — non tre meccanismi nel
firmware.

### Layout: pochi e fissi, nessun editor

Per ogni tipo di pagina si prevedono **3-4 disposizioni fisse**, scelte da un
menu. Per i valori, per esempio: "un numerone", "dentro/fuori a due colonne",
"con grafico in basso".

Un editor visuale (trascina i riquadri, ridimensiona) è mesi di lavoro per una
cosa che si usa tre volte e poi non si tocca più. Se un giorno servisse davvero
una disposizione fuori standard, la strada corta esiste già ed è un'altra: si
compone l'immagine nel browser e la si manda come pagina immagine.

### Rotazione: i vincoli vengono dalle misure, non dai gusti

- Ogni cambio pagina è un **refresh completo: 2197 ms misurati**, e lampeggia.
  Sotto il minuto non ha senso. Default proposto: **5-10 minuti**.
- **Ore di silenzio**: niente rotazione di notte. Nessuno guarda, e ogni refresh
  risparmiato è consumo e usura in meno.
- **Pulsante "refresh completo adesso"** nella web UI, per togliere il ghosting
  quando si accumula senza aspettare il ciclo.
- Il **tasto BOOT continua a cambiare pagina**: se la rete cade, il pannello
  deve restare governabile a mano. Vale come principio generale — la web UI è un
  telecomando comodo, non l'unico modo di usare l'oggetto.

### La striscia del verdetto su ogni pagina

Proposta: la riga "conviene aprire le finestre / no" con dentro e fuori sta
**su tutte le pagine**, foto e messaggi compresi.

Costa zero, e la ragione è una misura: **il refresh parziale costa uguale
qualunque sia la finestra** (562 ms, l'SSD1683 fa comunque una passata su tutto
il pannello). Non c'è nessun risparmio nel tenere la striscia fuori dalle
pagine, e c'è un guadagno evidente nel non far sparire mai l'unica informazione
per cui il progetto esiste.

### Pagina messaggi

Il bigliettino sul frigo, scritto dal telefono. Modello di un messaggio:

```
testo       max ~200 caratteri, UTF-8
creato      timestamp
scadenza    quando sparisce da solo (o "mai")
priorita'   normale | urgente
```

Un messaggio **urgente** porta il pannello su quella pagina subito, scavalcando
la rotazione: è la differenza fra una lavagnetta e un modo per lasciare un
avviso a chi torna a casa.

Tre dettagli che si scoprirebbero solo implementando:

- **Gli accenti.** I font Adafruit GFX sono ASCII puro: "perché" diventa
  "perch?". Serve **`U8g2_for_Adafruit_GFX`**, che gestisce UTF-8 — la libreria
  che fino a ieri avevo escluso perché per i numeri non serviva. Con l'italiano
  serve.
- **Corpo automatico**: messaggio corto → font grande, lungo → font piccolo con
  a capo automatico. Si fa provando 24/18/12/9 pt con `getTextBounds()` e
  tenendo il più grande che ci sta.
- **Un pulsante "manda al pannello" esplicito**, non aggiornamento mentre si
  scrive: ogni carattere costerebbe un refresh da 2,2 s.

### Anteprima 1:1 di quello che c'è sul pannello

L'hub espone i suoi 15.000 byte su un endpoint, il browser li ridisegna su un
canvas — il codice `unpack()`/`paint()` **esiste già** in `dither.html`.

Costa pochissimo ed è la funzione che rende tutto il resto usabile: comporre una
pagina, controllare un messaggio o verificare un layout da un'altra stanza,
senza andare a guardare il pannello. Dopo i messaggi, è la cosa che
consiglierei di fare per prima.

### Pagine che consiglio di prevedere fin dall'inizio

- **Stato dei nodi**: chi è vivo, ultimo contatto, batteria. Con nodi a batteria
  sparsi per casa, *accorgersi* che uno è morto è metà del progetto: senza,
  guardi un numero fermo da tre giorni credendolo vero. Vale sia come pagina sul
  pannello sia come schermata web.
- **Grafico**: temperatura delle ultime 24 h e trend barometrico a 3 ore del
  BMP280. È la previsione del tempo casalinga, e su e-ink si legge benissimo.

### Dove vivono le cose

| cosa | dove | perché |
|---|---|---|
| impostazioni (rotazione, durate, ore di silenzio, layout scelti) | **NVS** | poche decine di byte, devono sopravvivere al riavvio anche senza card |
| elenco delle pagine e loro parametri | **NVS** | idem; è configurazione, non dati |
| immagini `.bin` | **SD**, `/images/<nome>.bin` | 15 KB l'una, non stanno in flash |
| messaggi | **SD**, più l'ultimo attivo in NVS | così un messaggio urgente si rivede anche se la card viene tolta |
| storico per i grafici | **SD**, CSV giornaliero | come `EnvNode_C3` |

Attenzione ai **cicli di scrittura della NVS**: vale la lezione di
`EnvNode_C3` — mai scrivere ad ogni cambio, solo quando l'utente conferma
un'impostazione.

### Vincolo che condiziona il codice: il WebServer è sincrono

Come sul nodo camera (`starters/XIAO_S3_Camera/`): finché si serve una
richiesta, la scheda non fa altro. Quindi handler **corti**, nessun lavoro lungo
dentro — un refresh del pannello da 2,2 s non si fa dentro un handler HTTP, si
mette in coda e lo esegue il `loop()`. È la stessa regola dei callback ESP-NOW e
di quelli LVGL sull'altra scheda: **la richiesta accoda, il loop lavora.**

Per fortuna qui non c'è niente di tempo reale: un pannello che risponde mezzo
secondo dopo va benissimo.

### L'opinione di fondo

Il rischio di questo elenco di funzioni non è tecnico, è d'uso: **un pannello
che ruota fra sei pagine diventa un salvaschermo che nessuno legge**. Il valore
dell'e-ink è che l'informazione *sta lì* e la si guarda passando.

Quindi: **rotazione spenta di default**, una pagina primaria quasi sempre a
schermo (il verdetto con dentro/fuori), le altre come scelta deliberata. La
rotazione automatica resta disponibile, ma come opzione, non come modo normale
di funzionare.

### Da decidere, quando ci si torna sopra

1. I messaggi si disegnano **come testo a bordo** (leggeri, aggiornabili,
   ricercabili) o **come immagine composta nel browser** (tipografia libera, ma
   15 KB l'uno e non modificabili dalla scheda)? La proposta è testo, con
   l'immagine come strada già disponibile per i casi speciali.
2. Quante pagine immagine si tengono sulla card e con quale politica quando si
   riempie — la stessa scelta già fatta in `Timelapse_XIAO` (stop o ring).
3. Serve un'autenticazione sulla web UI oltre alla basic-auth di `/update`?
   Vale la stessa premessa degli altri sketch: **LAN fidata**, non Internet.
4. La rotazione deve tornare da sola alla pagina primaria dopo un po' che
   nessuno tocca niente (tipo "screensaver al contrario")?

## Domande aperte

1. ~~**Gli AHT20+BMP280 sono già in casa?**~~ Sì: il primo è saldato e cablato
   dal 2026-08-22, la Fase 1 è partita dal bring-up del sensore.
2. Quanti nodi in totale, ogni quanto trasmettono, che autonomia si vuole.
3. Dove viene montato l'hub, e se gli si attacca un AHT20/BMP280 sull'I2C per
   fare **anche** da sensore interno, risparmiando un nodo. In quel caso il
   sensore va su ~20 cm di cavo, lontano dalla scheda: la S3 in WiFi si
   autoriscalda di qualche grado e falserebbe la lettura.
4. Conferma dei nomi delle cartelle `MeteoHub_S3` / `MeteoNode_C3`.
5. La XIAO S3 Sense è la stessa che serve a `projects/Timelapse_XIAO/`: se quel
   progetto deve restare montabile, ne serve una seconda.
