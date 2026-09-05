# MeteoHub_S3 — hub della stazione meteo (progetto reale)

Guida di progetto: si carica quando si lavora sui file di questa cartella. Per
l'architettura del workspace, i comandi di build e le regole valide su tutte le
schede vedi `CLAUDE.md` alla radice; per le trappole hardware gia' pagate (USB
CDC, deep sleep, OTA, scritture su SD, default NVS) `docs/Trappole-Hardware.md`.

## I file di questa cartella

| File | Ruolo |
|---|---|
| `MeteoHub_S3.ino` | pagine del pannello, tasto BOOT a due gesti, hub ESP-NOW, logging dei nodi, riepiloghi — qui va la logica applicativa |
| `pages.h/.cpp` | il modello delle pagine del pannello: elenco, rotazione, ore di silenzio, in NVS — non conosce il display |
| `messages.h/.cpp` | il messaggio sul pannello: attivo in NVS, archivio su SD |
| `remote_nodes.h/.cpp` | copia da `EnvNode_C3`: registro nodi, cadenza appresa, nodo muto, trend, NVS |
| `sd_logger.h/.cpp` | copia da `EnvNode_C3` adattata alla microSD **SPI della Sense** (CS 21, bus condiviso con l'e-ink); più immagini, riepiloghi, diario e registro dei refresh |
| `rtc_time.h/.cpp` | copia da `EnvNode_C3`: stima da build-time, poi NTP |
| `forecast.h` | copia dal nodo: trend barometrico a 3 h con isteresi, header-only e puro. Il calcolo autorevole sta **qui**, non sul nodo |
| `daily.h` | aggregati di una giornata (min/max/media di T, RH, pressione, rugiada, più completezza e cadenza dedotta), header-only e puro |
| `meteo_calc.h` | quello che si ricava da T e RH: rugiada, umidità assoluta, humidex. Header-only e puro come `forecast.h` — sta **sull'hub** perché sono grandezze derivate, e un errore di formula spedito dal nodo finirebbe nello storico per sempre |
| `icone.h` | le icone 1 bit del pannello, **generate** da `tools/icone.py` — non si modificano a mano |
| `net_ota.h/.cpp` | WiFi + ArduinoOTA + `/update`, variante con `net_server()` condiviso |
| `web_ui.h/.cpp` | pagine servite dalla scheda, tabella delle rotte e API — gli stessi endpoint di `EnvNode_C3`, più quelli del pannello |
| `secrets.h.example` | credenziali: si copia in `secrets.h`, **gitignorato** |
| `www/dashboard.html` | dashboard personalizzata dell'hub: confronto fra nodi, storico dai CSV, pressione/trend, salute della rete. **Non compilata**: si carica sulla card da `/pagine` (o dal vecchio `/dashboard-upload`) |
| `www/analisi.html` | pagina di analisi dei riepiloghi: sei viste, grafici **ECharts** dal CDN con la riserva SVG scritta a mano. **Sorgente unica**, da cui `analisi_page.h` si rigenera con `python www/gen_page.py analisi` |
| `analisi_page.h` | generato dalla precedente, servito su `/analisi` — non si modifica a mano |
| `www/dither.html` | ritaglio + dithering nel browser: produce i `.bin` da 15.000 byte e li manda all'hub. Da `v8` è anche **servita dalla scheda** su `/immagini` |
| `dither_page.h` | generato dalla precedente, servito su `/immagini` — non si modifica a mano |
| `www/gen_page.py` | rigenera `analisi_page.h`/`dither_page.h` dal loro `.html`: `python www/gen_page.py analisi\|dither`. Un hook di Claude Code lo lancia da solo quando quei due file cambiano; a mano va rilanciato **prima** di ricompilare |
| `tools/controlla_piedi.py` | verifica che ogni pagina porti il piede di navigazione completo. Con `--host <ip>` controlla quelle che la **scheda** serve davvero e confronta `fw_caricata` col firmware che gira |
| `tools/larghezza_testo.py` | quanto è largo un testo sul pannello **prima** di disegnarlo: somma gli `xAdvance` dei glifi nei `.h` veri dei font |
| `tools/pannello_png.py` | scarica `/api/pannello/anteprima` e ne fa un PNG, senza dipendenze: il pannello si guarda da riga di comando |
| `tools/icone.py` | disegna le icone e le mostra a schermo per giudicarle; con `--c` genera `icone.h` |
| `tools/refresh_simula.py` | quanti refresh farebbe il pannello, rigiocando i CSV veri dei nodi |
| `tools/analisi.py` | cosa dicono davvero i CSV dei nodi: le analisi che a bordo non si possono fare |


Cresciuto dal bring-up del pannello e-ink, oggi è l'hub vero della stazione:

```
nodi ESP-NOW -> hub S3 -> pannello e-ink 4.2" + CSV su microSD + web UI/OTA
```

Il pannello ha **sei tipi di pagina** (`pages.h`): `PT_NODI` — quella per cui
l'hub esiste —, `PT_MESSAGGIO`, `PT_BIANCA` (pannello a riposo), `PT_IMMAGINE`
(un `.bin` da `/images`), `PT_GRAFICO` (24 ore di temperatura) e
`PT_DETTAGLIO` (tutto su un nodo solo). Le cinque pagine di prova del bring-up
sono state tolte in `v4`: servivano a distinguere un guasto del pannello da uno
della radio, e quel compito lo fa ora la pagina messaggio, che si vede o non si
vede allo stesso modo — e in più dice qualcosa di utile quando funziona.

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
  - **La fascia di silenzio si muove a quarti d'ora da `v43`** (2026-09-02),
    e i quarti stanno in **due chiavi NVS separate** (`silq_da`/`silq_a`), non
    nel blob: nel blob resta l'**ora piena**. È la regola generale da riusare
    quando si aggiunge risoluzione a un campo già persistito — cambiare la
    struttura ne cambia `sizeof`, e un ritorno al firmware precedente
    troverebbe un blob irriconoscibile, cioè **tutte le pagine sparite**.
    Così invece il firmware vecchio rilegge la stessa fascia arrotondata
    all'ora e non si accorge di niente. Stessa scelta già fatta per `silpag`.
    - **Le funzioni hanno un nome nuovo** (`pages_silenzio_da_q()`,
      `pages_set_silenzio_q()`): l'unità è cambiata sotto lo stesso tipo
      (`uint8_t`), quindi un chiamante rimasto indietro deve **smettere di
      compilare** invece di scambiare un `21` (le nove di sera) per un `21`
      (le cinque e un quarto del mattino).
    - Verso la rete si parla in `"HH:MM"` (`/api/pannello` lo emette così, e
      il POST lo accetta): un intero mentirebbe su tre valori su quattro. Un
      intero senza `:` resta l'**ora piena**, così i comandi già scritti a mano
      valgono ancora.
  - **L'immagine della notte può venire da TUTTA la card** (`v43`,
    `sil_pagina=253`), non solo dalle pagine in elenco, e **non consuma uno
    slot**: di notte non serve una pagina ma un file, e `screenImmagine()`
    disegna già per nome. Una pagina dedicata starebbe in elenco, dove la si
    può attivare nella rotazione, spostare o togliere da una schermata che
    della notte non parla — e i sedici slot si sono già esauriti una volta.
    - **Il nome sorteggiato va ESPOSTO** (`silenzio_immagine` in
      `/api/pannello`, e l'intestazione di `/pannello`): mentre quel file è a
      schermo, `corrente` descrive il modello delle pagine, che il file non lo
      conosce — sarebbe una risposta vera e fuorviante insieme. Per lo stesso
      motivo la badge «A SCHERMO» sparisce dall'elenco finché dura la notte.
    - **Al mattino si ridisegna solo se sul vetro c'è ancora l'immagine**:
      `showPage()` incrementa un contatore e il risveglio lo confronta. Se
      durante la notte si è premuto BOOT o chiesta una pagina dal web, quella
      è la pagina voluta, e riprendersela sarebbe un refresh in più per
      togliere all'utente quello che ha scelto.
    - **Card vuota o assente: non si disegna niente** e ci si ferma com'era.
      Un «immagine non disponibile» spegnerebbe la pagina dei nodi per tutta
      la notte in cambio di un messaggio che nessuno sta guardando.
    - **Due passate su `/images` invece di un elenco di nomi in RAM**
      (`sd_img_page()` conta, poi consegna l'n-esima): quante immagini ci
      siano lo decide la card, e un tetto in memoria sarebbe arbitrario e
      muto. Costa due scansioni al giorno. Il sorteggio evita l'immagine
      della notte prima — su un pannello «non è cambiato» e «è fermo» si
      somigliano troppo.
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
    l'hub sta disegnando *adesso* è un'altra cosa, e da `v22` si fa: è
    `GET /api/pannello/anteprima`, più sotto.)
  - **Nella galleria le anteprime sono miniature da 600 byte**
    (`GET /api/immagini/mini`, 80×60, la stessa immagine sottocampionata 5×):
    dodici anteprime piene sarebbero 180 kB su un web server **sincrono**, cioè
    altrettanto tempo in cui l'hub non preleva i DATA dei nodi dal driver, che
    ne tiene uno solo. Con le miniature sono 7,2 kB.
    - **Si calcolano ad ogni richiesta invece di tenerle sulla card**: sono
      pochi ms di lettura, mentre una miniatura salvata sarebbe un secondo file
      da creare, cancellare e tenere allineato all'originale — tre modi in più
      di andare fuori sincrono per risparmiare una cosa che non costa.
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
  `/pannello`, `/immagini`, `/api` e — da `v52` — `/analisi` servono il file
  `/www/<nome>.html` se c'è, altrimenti quello nel firmware. Sono le cinque voci
  di `PAGINE_SOST` in `web_ui.cpp`. Si gestisce da **`/pagine`**.
  - **Il motivo non è lo spazio**: le cinque pagine pesano 74 kB su una
    partizione che alla `v18` era piena al 41 %, e toglierle l'avrebbe portata
    al 39 % (alla `v57` si sta al **44 %**: 1.498.226 byte su 3.342.336, RAM
    globale 23 %). Il motivo è
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
  `/pannello` &middot; `/analisi` &middot; `/immagini` &middot; `/pagine` &middot;
  `/api` &middot;
  `/update` (`/analisi` aggiunta in `v52`; aggiornato in `v19`; prima c'era `/dashboard-upload`, che resta
  funzionante ma non è più la via consigliata — `/pagine` fa la stessa cosa per
  tutte le pagine). Vale anche per la dashboard sulla card e per la pagina
  `/update`, che sta in `net_ota.cpp`: sono **nove** pagine da toccare insieme —
  cinque in PROGMEM dentro `web_ui.cpp`, una in `net_ota.cpp` e le tre in
  `www/` — ed è il prezzo di non avere un template condiviso. Il conto lo fa
  `python tools/controlla_piedi.py`, che le elenca una per una. Non e' pignoleria
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

- **La pagina DETTAGLIO** (`PT_DETTAGLIO`, da `v23`): tutto quello che si sa di
  **un** nodo, una riga per valore, etichetta a sinistra e numero incolonnato a
  destra. Si aggiunge con `POST /api/pannello/aggiungi?tipo=dettaglio&param=NODO`.
  - **Nasce da un difetto della pagina nodi**: i valori derivati (rugiada,
    min/max, percepiti, acqua nell'aria) erano finiti tutti nel blocco comodo,
    e **otto numeri in 116 px non si leggono da tre metri** — si decifrano da
    vicino, uno per volta. In `v24` quella riga è stata tolta e i valori sono passati qui,
    dove ci sono 300 px per incolonnarli. È la regola già scritta per la fascia
    del messaggio e per il grafico: su e-ink il tempo è la dimensione in più.
  - **Il nodo si indica per NOME, non per indice**: gli indici si spostano
    quando un nodo viene dimenticato, e la pagina mostrerebbe un altro nodo
    senza dirlo. Stessa ragione per cui i timer del ritardo si tengono per MAC.
  - **Le grandezze derivate stanno in `meteo_calc.h`** — rugiada
    (Magnus-Tetens), umidità assoluta in g/m³, humidex — header-only e puro come
    `forecast.h`. **Si calcolano sull'hub e non sul nodo** perché si ricavano da
    T e RH, che il nodo trasmette già: calcolarle a bordo vorrebbe dire spedire
    numeri derivati al posto delle misure, e un errore di formula finirebbe
    dentro lo storico su card **per sempre**, mentre così bastano i CSV di ieri
    per rifare i conti. È la stessa ragione per cui la pressione viaggia grezza.
  - **Una riga che non c'è dice qualcosa**: sotto i 20 gradi l'humidex non
    esiste e la riga **non compare affatto**, invece di mostrare un numero senza
    significato. Un valore non finito si disegna `--`, mai zero.
  - **L'unità viaggia separata dal valore**: va in corpo più piccolo e, quando
    sono gradi, con il cerchietto disegnato (`drawGrado()`) al posto della
    lettera — i font Adafruit GFX sono ASCII puro e il grado non ce l'hanno.
    Scriverlo come `" C"` era l'unico punto in cui un'unità non somigliava a sé
    stessa.
  - **`drawFila()` mette in fila i pezzi che ci stanno e si ferma al primo che
    non entra**, e **l'ordine dell'array È la priorità**: quello che si perde è
    l'ultimo, mai il contrario. Serve perché il numero di voci cambia da solo —
    l'humidex sparisce sotto i 20 gradi, il delta a 3 ore manca finché lo
    storico non è pieno, un nodo senza pressione non ne ha affatto. Una riga
    scritta a lunghezza fissa era destinata a sovrapporsi in qualche
    combinazione, ed è successo davvero: `3h +1,percepiti 32`.
  - **Le icone** (`icone.h`, da `v24`) sono generate da `tools/icone.py`, che le
    **mostra a schermo prima di generarle**: a 14-20 px e 1 bit si vede la
    silhouette, non il dettaglio, quindi quelle che funzionano sono pochissime e
    l'unico modo di accorgersene è guardarle — non immaginarsele.

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

- **Il tempo di giro si misura anche qui** (da `v45`): `loop_max_ms`,
  `loop_max_dove`, `loop_max_ora`, `loop_lenti` in `/api/stato`, trapiantati da
  `EnvNode_C3`. Serve a distinguere **«si è riavviata»** da **«è rimasta ferma
  dentro una chiamata»**, che nei CSV hanno lo stesso identico aspetto — un
  buco — e che `reset_reason`/`boot_count` da soli non separano.
  - **Il pannello NON entra nel massimo**, ed è deliberato: 2,6 s lì sono
    normali e coprirebbero per sempre tutto il resto. Si misurano le quattro
    fasi che vengono **prima di qualunque disegno** (`web`, `nodi`, `seed`,
    `bottone`), che sono anche tutto ciò che nel `loop()` può bloccare senza
    doverlo; il disegno ha già `epd_ultimo_ms` e il registro dei refresh.
  - La fase `web` **non si misura durante un OTA**: sono decine di secondi
    legittimi. Serve la callback di progresso, che è anche quella che alimenta
    il watchdog.
  - Primo dato reale, 25 s dopo l'accensione: `loop_max_ms 1325`, `dove seed` —
    cioè `seedForecastDaSD()`, la ricostruzione dello storico dai CSV. Una
    volta per accensione, e non è un guasto: adesso però è un numero.

- **Il watchdog del loop è armato** (hub da `v45`, nodo da `v16`). Fino a lì
  `app_reset_reason()` traduceva `WDT_TASK`, `WDT_INT` e `WDT` **senza che
  nessuno potesse produrli**: una diagnosi scritta per un meccanismo che non
  esisteva. Un `loop()` piantato lasciava un pannello e-ink bistabile, nitido e
  non più vero, con tutta la diagnostica irraggiungibile insieme all'hub.
  - **Il baratto va conosciuto**: il core inizializza già il TWDT a 5 s con
    l'idle task di CPU0 iscritto, e il timeout è **uno solo** per tutto il
    TWDT. Portarlo a 60 s allunga anche quella protezione — si accetta, perché
    copre uno scenario in cui la radio è comunque morta, mentre si guadagna la
    protezione del `loop()`. L'idle di CPU0 resta **iscritto**
    (`idle_core_mask`): si allunga, non si spegne.
  - **60 s e non meno**: il caso legittimo più lungo è il budget di invio di un
    file (20 s) più una `write()` bloccata dentro il core (~10 s). Un timeout
    stretto trasformerebbe un download lento in un riavvio.
  - **La `idle_core_mask` si LEGGE dalle macro di `sdkconfig`, non si scrive a
    mano** (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0`/`_CPU1`). Passarne una
    sbagliata **disiscrive** gli idle task, togliendo in silenzio una
    protezione che c'era — e le due schede su cui gira lo stesso sketch di
    `MeteoNode_C3` sono configurate diversamente fra loro: sulla XIAO C3
    nessun idle è iscritto, sull'ESP32 classico c'è quello di CPU0. Un numero
    fisso sarebbe stato giusto su una e sbagliato sull'altra, senza nessun
    sintomo fino al giorno in cui serviva.
  - **Durante l'OTA si ALIMENTA, non si sospende** (dalla callback di
    progresso): così resta armato anche lì, e un aggiornamento che si pianta
    davvero viene comunque ripreso.
  - **Sul nodo vale di più**: un hub bloccato perde dati, un nodo bloccato non
    si riaddormenta e svuota una cella da 1500 mAh in **~21 ore** contro i mesi
    che dovrebbe durare — e la rete di sicurezza non aiuta, perché sta *dentro*
    il percorso del sonno. Lì i timeout sono 60 s in veglia e **20 s** nel ciclo
    di risveglio (non 10: il peggio legittimo è ~9 s, fra `PAIRING_MS` e ricerca
    del canale), **disarmato prima di `esp_deep_sleep_start()`**, che non torna.
  - **`wdt_armato` in `/api/stato`**, con avviso in `/api/salute` se è falso:
    un watchdog configurato male e uno giusto sono indistinguibili da fuori
    finché non serve, e il log di boot di questa scheda non è leggibile via USB.
    Prova che il task è **iscritto**, che è la metà che si sbaglia in silenzio.
  - **E l'altra metà si prova fabbricando il guasto**: `POST /api/prova/blocco`
    (da `v49`) blocca il `loop()` apposta senza alimentare il watchdog. Il
    comando si **accoda**, non si esegue nell'handler HTTP, o la risposta non
    partirebbe e il client resterebbe appeso senza sapere se è arrivato. Il
    tetto di 120 s serve al caso in cui la prova **fallisce**: senza, un numero
    sbagliato terrebbe ferma la scheda per ore.
    - **Eseguita il 2026-09-03, e riuscita**: blocco chiesto 90 s, watchdog
      scattato a **~60** (l'ha interrotto), 14 s di boot, **75 s** dal comando
      al ritorno in rete. `reset_reason` **WDT_TASK** e `boot_count` 54 → 55 —
      la prima volta che quella stringa compare da quando esiste il progetto.
      Tutto ciò che sta in NVS è sopravvissuto: registro nodi, pagine,
      impostazioni.
    - Il diario ha registrato **la prova e il suo esito** (`prova_blocco` alle
      13:02:54, `boot WDT_TASK` alle 13:04:10): le due funzioni del Blocco B si
      sono verificate a vicenda.
    - **Se il watchdog non funzionasse** il blocco durerebbe tutti i secondi
      chiesti e il codice scriverebbe `FALLITA` sulla seriale **e nel diario**:
      il risultato negativo non è un silenzio.

- **`GET /api/nodi/serie`** (da `v53`): la serie ORARIA concatenata su piu'
  giorni, **decimata a bordo** in cesti con media/min/max. E' cio' che rende
  possibile un grafico multi-giorno: sette giorni di CSV sono ~160 kB che
  uscirebbero da un server **sincrono** un chunk per volta, e in quel tempo
  l'hub non preleva i DATA dal driver (che ne tiene **uno** per nodo).
  Misurato: 4 giorni, 1015 righe, **395 ms** e **5,6 kB** invece di ~83 kB.
  Tetto di **14 giorni**, perche' ogni giorno e' una lettura dentro un handler.
  Un cesto vuoto e' `null`, mai uno zero.
- **`sd_read_remote_day()` e' l'UNICO posto dove si interpreta il CSV dei
  nodi** (da `v53`), e legge **a blocchi di 512 byte**: prima `leggiRiga()`
  chiamava `File::read()` un byte alla volta, e su SPI ogni chiamata attraversa
  il driver della card. Il riepilogo di un giorno a 60 s e' passato da
  **2125 ms a 367 ms**, con `loop_lenti` da 1 a 0. Lo stesso parser era
  copiato in due punti del `.ino`: una colonna aggiunta un domani avrebbe
  dovuto essere ricordata in entrambi.
- **`[hidden]` non nasconde niente se una classe imposta `display`.** Quella
  regola la mette il foglio di stile del **browser**, che perde contro
  qualunque regola d'autore: con `.barra{display:flex}` gli elementi marcati
  `hidden` **restano visibili**. Nella pagina di analisi il sintomo era «i
  selettori non mostrano i giorni» — si vedevano, vuoti, prima che qualcuno li
  riempisse, e in JavaScript non c'era niente da trovare. Ogni pagina che usa
  `hidden` deve avere `[hidden]{display:none!important}`, e `www/gen_page.py`
  ora lo verifica.
  - **Il controllo ha sbagliato al primo colpo, ed e' istruttivo**: cercava la
    stringa `[hidden]` e la trovava **dentro il commento che spiegava il
    difetto**. Ora toglie i commenti e pretende una *regola*. Un controllo che
    legge la documentazione invece del codice non controlla niente.
- **UNA PAGINA SOSTITUITA SULLA CARD NON LA AGGIORNA L'OTA.** E' la trappola
  che ha fatto sparire il link a `/analisi` dal piede della **home** anche dopo
  aver corretto tutti i sorgenti: `/` era servita dalla `dashboard.html` sulla
  **card**, caricata ai tempi della `v44`, e il file nel repo e' **solo un
  sorgente**. Ricompilare e ricaricare il firmware non tocca la card.
  - **Dopo ogni modifica al piede** (o a qualunque cosa in una pagina
    sostituibile) vanno **ricaricate le pagine che stanno sulla card**, da
    `/pagine` o con
    `curl -F "pagina=@www/dashboard.html" ".../api/pagine/carica?nome=dashboard"`.
  - **Il campo `fw_caricata` di `/api/pagine/elenco` lo diceva gia'** — quel
    campo esiste apposta — ma nessuno lo guardava. Ora lo guarda
    `controlla_piedi.py --host`.
- **`tools/controlla_piedi.py` verifica che ogni pagina porti il piede**, e
  serve perche' il difetto **a mano non si trova**: aggiungendo `/analisi` in
  `v52` la sostituzione e' stata fatta su un formato solo (`<nav>`) mentre nel
  firmware ce n'e' un secondo (`<p class="muted">`), e sono rimaste indietro
  **due pagine su nove, fra cui la home**. Le pagine dimenticate sono sempre
  quelle che non si aprono. Esce con codice 1, quindi si puo' mettere in un hook.
  - **Ha DUE modi, e servono entrambi.** Senza argomenti controlla i
    **sorgenti**; con `--host <ip>` scarica le pagine **come le serve la
    scheda** e confronta anche `fw_caricata` con il firmware che gira. La
    lezione del 2026-09-04: sui soli sorgenti diceva "tutte e nove a posto"
    mentre la home servita non aveva il link. **Quando i due modi divergono,
    ha ragione la scheda.**
- **La pagina `/analisi`** (da `v52`, 2026-09-04) legge i riepiloghi e li
  disegna: banda min/max con la media per temperatura, pressione e umidita',
  barre della completezza, record del periodo e tabella. **Una richiesta per
  nodo** invece di una per giorno — e' tutto il punto del riepilogo.
  - **I giorni incompleti restano visibili e SEGNATI** (cerchio nel grafico,
    riga arancione in tabella, avviso in testa), con soglia scegliibile. E'
    la regola che tiene in piedi la feature: toglierli farebbe sparire anche
    l'informazione che sono mancati, cioe' rifarebbe con la grafica l'errore
    che la colonna `completezza_pct` serve a evitare.
  - **Da `v57` i grafici li fa ECharts 5.5, scaricata dal CDN DAL BROWSER**
    (non dalla scheda: l'hub serve solo HTML e dati, e il suo web server e'
    sincrono). Misurato dalla LAN di casa: 1,03 MB in 0,69 s, poi in cache.
    - **I grafici SVG scritti a mano sono rimasti come RISERVA**: se la
      libreria non arriva entro 7 s la pagina disegna come prima e lo dice.
      Il caso cattivo non e' "niente rete" (li' l'errore e' immediato) ma
      "rete che c'e' e non risponde", e senza timeout la pagina resterebbe
      muta a fissare il vuoto.
    - **`connectNulls:false` non e' un dettaglio**: ECharts di default unisce
      i punti ATTRAVERSO un buco, cioe' disegna una linea dove nessuno ha
      misurato. E' la stessa bugia che il codice SVG evitava a mano.
    - **La decimazione resta a bordo** anche se la libreria reggerebbe 20.000
      punti: quei punti sarebbero 160 kB fuori da un server sincrono.
  - **Sei viste** (`v57`): per giorno, andamento con zoom, confronto fino a
    tre giorni, confronto fra nodi, **mappa oraria** (heatmap ora x giorno) e
    distribuzione con percentili.
  - **Tre viste da `v53`**: «per giorno» (dai riepiloghi), **«andamento
    continuo»** (la serie concatenata, banda min/max + media, linea che si
    **interrompe sui buchi** e asse con l'ora vera) e **«confronto giorni»**
    (due giornate sovrapposte sulle stesse 24 h, distinte per **tratto** e non
    per colore — stessa regola del pannello e-ink; la tabella delle differenze
    esce dai riepiloghi **gia' in RAM**, senza richieste in piu').
  - **Sostituibile dalla card** come le altre (`/www/analisi.html` via
    `/pagine`), ed e' il caso d'uso per cui quel meccanismo esiste: una
    pagina di grafici si itera molte volte, e ogni giro non deve costare un
    OTA con il riavvio che si porta dietro.
  - **La sorgente e' UNA**, `www/analisi.html`; `analisi_page.h` si rigenera
    con `python www/gen_page.py analisi` — parametrico da `v52`, prima
    serviva solo dither. **Un hook di Claude Code lo fa da solo** quando il
    file viene modificato; a mano va rilanciato prima di ricompilare.
- **Il riepilogo giornaliero** (da `v50`-`v51`, 2026-09-04):
  `/nodi/<NOME>/riepilogo.csv`, una riga per giorno **chiuso**, con
  `GET /api/nodi/riepilogo?nodo=X` per leggerla e
  `POST /api/nodi/riepilogo/rifai` per rifarla. Il calcolo sta in `daily.h`
  (puro), la colla nel `.ino`, il file in `sd_logger`.
  - **NON e' un timer a mezzanotte**, ed e' la scelta che lo rende affidabile:
    una riga prodotta da un timer sparisce per sempre se in quel minuto la
    scheda e' spenta, sta aggiornandosi o e' appena ripartita — e l'assenza di
    una riga non somiglia a un guasto. Il giorno si chiude **quando ci si
    accorge che ne e' cominciato uno nuovo**, recuperando quelli rimasti
    indietro: idempotente, e si rimette in pari da solo dopo ogni assenza.
  - **UN GIORNO PER GIRO**, a turno fra i nodi: un CSV sono ~23 kB dalla card,
    e farne diciotto di fila (il backfill iniziale) terrebbe fermo il `loop()`
    e con lui il prelievo dei DATA, di cui il driver tiene **uno solo** per
    nodo. Stessa regola del WELCOME uno per giro della `v44`.
  - **La cadenza si deduce DAL GIORNO STESSO** (mediana dei delta fra campioni
    consecutivi), mai da quella appresa dall'hub: quella e' la cadenza di
    adesso. Misurato qui — il nodo a muro stava a **60 s fino al 26/08** e a
    300 s dopo, quindi usare i 299 s di oggi per il 28/08 scriverebbe una
    completezza del **497 %**. Mediana e non media perche' un delta che
    scavalca un buco e' un multiplo del periodo (stessa ragione della `v44`).
  - **La completezza (`campioni/attesi`) e' la colonna che rende leggibili le
    altre**, e ha ripagato subito: il 31/08 sta al **75,4 %** e il 27/08 al
    24,6 %, giorni il cui minimo sembrava affidabile quanto tutti gli altri.
    **Sopra il 100 % ci si va e non si tappa** (la cadenza e' stimata: un
    secondo su 300 vale un campione al giorno), e `cadenza_s` sta nel CSV
    perche' altrimenti la completezza sarebbe un numero non verificabile.
  - **La rugiada si media campione per campione**, non si calcola dalle medie:
    e' non lineare. La differenza misurata e' pero' solo `+0,02..0,03 C` — la
    si fa giusta perche' non costa niente, non perche' cambi una decisione.
  - **`POST /api/nodi/riepilogo/rifai` e' la via di rientro**: una riga non
    viene mai riscritta, quindi senza di essa un giorno calcolato male
    resterebbe sbagliato per sempre e si dovrebbe smontare la card. Il lavoro
    non si fa nell'handler — si cancella e si lascia ricostruire dal `loop()`.
  - **Costo misurato**: `loop_max_ms` **367 ms** in fase `riep` sul giorno a
    60 s (1440 righe), sceso da 2125 con la lettura a blocchi della `v53`.
  - **E il CONTROLLO del cambio giorno costava piu' del calcolo.** Girava ad
    ogni giro di `loop()` e chiamava `rtctime_format()` (localtime_r +
    strftime) **prima** di guardare se c'era qualcosa da fare: **132 us a
    972 giri/s = il 12,8 % della CPU del loop**, misurato sull'hardware, per
    rispondere a una domanda che cambia risposta **una volta al giorno**. Da
    `v56` l'orologio si guarda ogni **10 s** quando non c'e' niente da
    chiudere: **2 us, lo 0,2 %**, e i giri/s sono saliti da 972 a 1000.
    - **La guardia vale SOLO quando non c'e' lavoro**: appena c'e' un giorno
      da chiudere si torna a passare ad ogni giro, cosi' gli arretrati si
      smaltiscono senza attese. Nel caso peggiore una giornata si chiude 10 s
      dopo mezzanotte, su 86400 che ha per farlo.
    - **`loop_giri_s` in `/api/stato` resta**: e' il denominatore di ogni
      ragionamento sul costo di una cosa fatta "ad ogni giro", e senza non si
      sarebbe potuto dire *quanto* costava. Serve anche da sintomo — se cala
      di colpo, qualcosa nel giro ha cominciato a bloccare.
- **Il diario degli eventi** (da `v45`): `/eventi/AAAA-MM.csv` sulla card,
  `GET /api/eventi`. Esiste perché tre indagini raccontate in
  `docs/Stazione-Meteo.md` sono state la ricostruzione a mano di un diario che
  nessuno teneva: i contatori in RAM dicono **quanto**, mai **quando**.
  - **È una diagnosi, non un log**: una riga per **transizione**, mai una per
    campione. `nodo_muto` si scrive quando il nodo *diventa* muto, non finché
    lo è; la card che rifiuta scrive una riga per serie e una quando riprende.
  - **Tetto per tipo** (dieci righe l'ora) con le soppressioni **dichiarate** in
    una riga a fine finestra: mai un silenzio.
  - **L'ora dev'essere vera** (`orario_registrabile()`). Il boot capita *prima*
    del primo sync NTP, quindi non si può datare quando succede: si tiene da
    parte e si scrive appena l'orologio è vero, portandosi dietro da quanti
    secondi la scheda è su.
  - Il dettaglio è testo libero e finisce in un CSV: virgole e a capo si
    sostituiscono con spazi invece di virgolettare, perché quel file lo legge
    anche un occhio umano.

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

  **Ogni refresh lascia una riga sulla card**, in `/epd/AAAA-MM.csv`
  (`ts_iso,motivo,tipo,ms`; motivo: `stato`/`valori`/`ghosting`/`pagina`/
  `silenzio`), e `GET /api/epd/registro?m=AAAA-MM` la restituisce.
  `GET /api/epd/totale` conta quanti ne ha fatti **davvero**, leggendo le righe
  di quei file — non un totale in NVS aggiornato ad ogni refresh: la flash
  interna ha cicli di erase finiti e lì dentro vivono le pagine e il registro
  dei nodi, mentre la card no. Il costo si sposta dal consumo continuo di una
  memoria che si logora al conteggio occasionale di un file, e quella rotta la
  chiama una persona ogni tanto, non il firmware ogni cinque minuti.

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

- **La firma dei valori deve descrivere CIO' CHE SI DISEGNA**, cifra per cifra
  (`firmaValori()`, corretta in `v41`). Ogni grandezza entra con i decimali con
  cui si mostra: temperatura e pressione a 1, **umidita' a 0** — sul pannello
  e' un intero, e fino a `v40` stava in firma a 0,1, quindi un 41,2 -> 41,3
  faceva un refresh che non cambiava un pixel. Era la voce che cambiava di piu':
  **601 volte su 771 pacchetti** nelle 24 h misurate il 2026-09-01.
  - **Vale anche al contrario**: la rugiada la disegna solo il blocco compatto e
    dipende dall'umidita' in modo continuo (41,2 e 41,7 sono lo stesso "41%" ma
    due rugiade diverse), quindi entra in firma **solo li'**. Min/max e delta a
    3 ore solo nel comodo. Il layout lo decide `nodiLayoutComodo()`, una
    funzione sola usata sia per disegnare sia per firmare: due condizioni
    copiate divergerebbero al primo ritocco, e **una firma che non corrisponde
    alla pagina si vede come refresh mancati, che nessun contatore segnala.**
  - **Quanto vale**: poco, ed e' misurato — 287 -> 282 refresh al giorno. Il
    motivo per farlo e' la coerenza, non il risparmio.

- **Il vero limite ai refresh e' la CADENZA, non la firma** (misurato il
  2026-09-01 rigiocando i CSV veri). Con due nodi a 299 s il massimo consentito
  e' **288 refresh al giorno** e se ne fanno **287**: il confronto dei valori ne
  evita *uno*, perche' con tre grandezze a quella risoluzione qualcosa cambia
  sempre. Chi volesse davvero ridurli deve alzare la cadenza minima dei refresh
  per i soli valori, non affinare la firma.
  - **`refresh_evitati` promette piu' di quanto misuri**: conta i *check* (uno
    ogni 5 s) che non hanno prodotto un refresh, non refresh risparmiati. Fra un
    pacchetto e l'altro lo stesso "niente di nuovo" viene contato ~60 volte, e
    un rapporto tipo 974:28 sembra un'efficienza enorme che non c'e'. Il numero
    onesto e' **refresh per pacchetto**: 28 su 56, e quel 50% lo fa la cadenza
    minima assorbendo il pacchetto del secondo nodo.
  - **Attenzione a leggere `epd_refresh` diviso `uptime`**: le ore di silenzio
    (21-7) stanno dentro l'uptime ma non producono refresh, quindi il tasso
    esce diviso per dieci. Il 2026-09-01 sembrava 1,5/h contro 12,2/h; erano
    15,7/h contro 12,2/h, cioe' nessun peggioramento.

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
  - Da `v38` accanto alla freccia c'e' il **numero** (`+1,2/3h`) e non piu' la
    parola: dicevano la stessa cosa, e la parola costava una riga intera. Sta
    ancora nella pagina dettaglio e nella web UI, dove c'e' spazio per
    leggerla.

- **La barra del giorno** (`drawRangeGiorno()`, da `v38`): minimo e massimo
  delle 24 h con un cursore dove sta la temperatura di adesso. E' l'unica
  informazione che alla pagina mancava davvero — **26,5 gradi con minimo 12 e
  con minimo 24 sono due giornate diverse, e il pannello le mostrava
  identiche**. Nessuna memoria nuova: legge l'anello dei 48 slot che la pagina
  grafico usa gia' (`statTemp()`).
  - **Non contraddice la regola contro le sparkline**: li' si vieta di
    comprimere una *curva* in un francobollo, e resta valido. Qui non si
    disegna un andamento ma **una posizione dentro un intervallo** — due tacche
    e un cursore, che a tre metri si leggono. Una curva a quella dimensione no.
  - Se lo storico non basta si scrive "in raccolta": una barra col cursore in
    mezzo direbbe "escursione nulla", che e' falso. Stessa regola dei due
    trattini del trend.

- **La testata del nodo e' nome + filetto, non piu' una barra nera piena**
  (da `v38`). Il nero pieno e' cio' che si vede da piu' lontano, ma e' anche
  cio' che **imprime il vetro**: le due barre da sole facevano meta'
  dell'inchiostro della pagina (22,2% di nero, misurato sull'anteprima; ora
  8,7%). Il nero risparmiato va all'unica cosa che deve gridare — il badge
  `MUTO` in negativo, che essendo rimasto il solo nero pieno si vede molto piu'
  di prima.

- **Un testo si MISURA prima di disegnarlo**, e da `v38` c'e' lo strumento:
  `python tools/larghezza_testo.py --riga3` somma gli `xAdvance` dei glifi nei
  `.h` veri dei font — lo stesso conto di `getTextBounds()` — e dice se una
  stringa invade quella accanto. **Il caso che sfugge non e' quello di oggi ma
  quello di gennaio**: `-10,5` sono 41 px dove `21,4` ne era 28, e con le
  coordinate stimate a occhio il minimo finiva sopra la barra alla prima gelata.
  Tre posizioni su quattro erano sbagliate e sono state corrette **prima**
  dell'OTA. Vale anche al contrario: `+12,3 hPa/3h` non ci sta (108 px, invade
  la freccia), ed e' il motivo per cui sul pannello si legge `+12,3/3h` —
  la controprova e' dentro lo script, o fra sei mesi qualcuno rimetterebbe
  l'unita' "che ci sta benissimo".
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

