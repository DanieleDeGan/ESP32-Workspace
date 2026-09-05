# EnvNode_C3 — nodo ambientale con dashboard (progetto reale, hardware smantellato)

Guida di progetto: si carica quando si lavora sui file di questa cartella. Per
l'architettura del workspace, i comandi di build e le regole valide su tutte le
schede vedi `CLAUDE.md` alla radice; per le trappole hardware gia' pagate (USB
CDC, deep sleep, OTA, scritture su SD, default NVS) `docs/Trappole-Hardware.md`.


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

## Ruolo secondario: hub ESP-NOW dei nodi a batteria (da `v4`, 2026-08-23; concluso)

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
- **La cadenza si impara SOLO dai DATA consecutivi** (`seq == precedente + 1`),
  corretto in `v44` il 2026-09-03. Non basta che il `seq` cresca: con un
  pacchetto perso il delta è due periodi, e la media mobile a peso 1/4 se lo
  porta dentro — da 300 s si passa a **375**, e servono otto pacchetti per
  rientrare. Nel frattempo si sposta tutto quello che dipende dalla cadenza:
  `sogliaMuto` (2,5 × intervallo, quindi **tre minuti di ritardo** nel
  dichiarare morto un nodo che lo è davvero), `nodoInRitardo()` e
  `cadenzaNodiMs()`, che decide ogni quanto si ridisegna il pannello. Un delta
  misurato a cavallo di un buco non è un periodo rumoroso da mediare: **è il
  periodo di un'altra grandezza.** Il commento sopra quel codice diceva già la
  cosa giusta — la guardia che c'era (`seq` crescente) esclude i **riavvii**,
  non i **buchi**. `/api/nodi` espone `intervallo_campioni`: se sta fermo
  mentre `persi` sale, la cadenza mostrata è l'ultima buona, non una stima
  presa dai buchi.
- **E nemmeno il PRIMO delta dopo un RIAVVIO del nodo** (`v48`, 2026-09-03): e'
  consecutivo nel `seq` ma non e' un periodo, perche' in mezzo c'e' il boot — e
  su un nodo a batteria pure la finestra di veglia da 5 minuti. **Misurato**: il
  primo DATA dopo l'aggiornamento del nodo a batteria e' arrivato **671 s** dopo
  il precedente invece di 300, e la media mobile lo ha portato a **393 s** di
  cadenza appresa con la soglia del muto a **1012 invece di 780**. E' lo stesso
  difetto dei buchi visto dall'altro lato: `riavvii++` arma un flag e il delta
  successivo si scarta, **uno solo**.
  - **Provato sul campo** riavviando apposta il nodo a muro: `pacch` 2→3 con
    `seq` 1→2 consecutivo e `camp` **fermo a 0** (il delta saltato), poi
    `camp` 1 con `intervallo` **300 al primo campione** — cioe' giusto subito,
    invece di partire da 393 e ricadere in quaranta minuti.
- **Il salto di `seq` ha un tetto** (`PERSI_SALTO_MAX`, 1000). Il `seq`
  attraversa il deep sleep passando dalla RTC memory: un valore sporco letto da
  lì diventerebbe qualche milione di "pacchetti persi" **permanenti**, cioè un
  contatore avvelenato da un pacchetto solo. Il pacchetto **non** si scarta (il
  dato è buono, è la numerazione a essere strana) e il salto si conta a parte
  in `seq_assurdi`, che compare anche in `/api/salute` — o si sostituirebbe un
  numero sbagliato con un silenzio.
- **I timer del ritardo si tengono per MAC, non per posizione.**
  `remote_forget()` **compatta** il registro e i suoi array paralleli; quelli
  che stanno **fuori** dal modulo, nello sketch, nessuno li compatta. Vale come
  regola generale: uno stato che sopravvive fra una chiamata e l'altra va
  indicizzato per MAC; una lettura immediata consumata nello stesso giro (come
  `statTemp()`) può restare per indice.
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

