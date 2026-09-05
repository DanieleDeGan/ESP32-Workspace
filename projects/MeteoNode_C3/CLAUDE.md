# MeteoNode_C3 — nodo meteo a batteria (progetto reale)

Guida di progetto: si carica quando si lavora sui file di questa cartella. Per
l'architettura del workspace, i comandi di build e le regole valide su tutte le
schede vedi `CLAUDE.md` alla radice; per le trappole hardware gia' pagate (USB
CDC, deep sleep, OTA, scritture su SD, default NVS) `docs/Trappole-Hardware.md`.

## I file di questa cartella

| File | Ruolo |
|---|---|
| `MeteoNode_C3.ino` | misura, previsione, ciclo di sonno e risveglio — qui va la logica applicativa |
| `forecast.h` | trend barometrico a 3 ore con isteresi, header-only e puro |
| `hub_link.h/.cpp` | nodo ESP-NOW sopra `EspNowLink`: canale, ripresa dell'hub dopo il sonno, invio delle misure |
| `rtc_time.h/.cpp` | copia di quello di `EnvNode_C3`: stima da build-time, poi NTP |
| `net_ota.h/.cpp` | WiFi + ArduinoOTA + `/update`, con watchdog di riconnessione — di norma non si tocca |
| `web_ui.h/.cpp` | pagina di stato con grafici SVG, comandi e interruttore del deep sleep |


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
- **Il trend del nodo legge una griglia che puo' essere sistematicamente
  disallineata**, e per una settimana lo e' stata: lo storico ha slot da
  **120 s**, il trend cerca quello a **tre ore**, e con l'intervallo a 300 s si
  riempiono solo le fasi {0, 2} modulo 5. `aggiornaTrend()` gira **mentre si
  misura**, dove `back = 0` e' l'ultimo slot gia' chiuso: lo slot cercato sta
  quindi **91** posizioni indietro, `91 % 5 = 1`, e le fasi cercate {4, 1} sono
  **disgiunte** da quelle piene. Risultato: `delta_3h` non compariva **mai** —
  199 misure su 199 — mentre la pagina diceva "servono tre ore di storico" con
  lo storico pieno. Corretto in `v18` (2026-09-04) con `histVicino()`, che
  accetta il pieno piu' vicino entro **+/- 3 slot** ed espone la finestra
  davvero usata in `delta_3h_finestra_s`.
  - **Il difetto era invisibile per tre motivi che vale la pena riconoscere**:
    il trend autorevole si calcola **sull'hub** (che ha slot da 10 min, tutti
    pieni, e infatti funziona); il messaggio d'errore era **plausibile**, cioe'
    identico a quello di un nodo appena acceso; ed e' nato il 26/08 quando
    l'intervallo e' passato da 60 a 300 s **da solo**, per la trappola del
    default NVS (`docs/Trappole-Hardware.md`) — a 60 s ogni slot era pieno e funzionava. **Una
    modifica non voluta di un parametro ne ha rotto un altro, altrove.**
  - **La regola generale**: una griglia a passo fisso letta a offset fisso non
    sbaglia ogni tanto, **non funziona mai**. Il campanello e' il rapporto non
    intero fra passo della griglia e cadenza di chi la riempie (300/120 = 2,5).
    Dove le due cose sono **configurabili separatamente**, la coincidenza va
    verificata, non data per buona.
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
  per sapere dove parlare. Le quattro trappole del sonno stanno in
  [`docs/Trappole-Hardware.md`](../../docs/Trappole-Hardware.md): leggerle
  **prima** di metterci le mani, a partire dal `seq`.
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

## Aggiungere un nodo nuovo alla rete

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

## La ricerca del canale (da `v12`, 2026-08-27)

L'access point cambia canale da solo, per scansare le reti dei vicini. Chi e'
connesso lo segue riassociandosi; **un nodo che dorme no**: si porta il canale
in RTC memory e diventa muto senza accorgersene, perche' l'unico sintomo e'
l'ACK che non arriva. Prima l'unica reazione era la piu' cara: cinque risvegli
muti, poi riavvio e cinque minuti di WiFi acceso.

Ora, se il DATA non viene consegnato, `hub_scan_channels()` prova gli altri
canali — **1, 6, 11 per primi**, poi gli altri dieci — con **un solo tentativo
per canale** e 200 ms di timeout, rimandando lo STESSO messaggio
(`Link_Node_ResendLast`, vedi
[`libraries/EspNowLink/CLAUDE.md`](../../libraries/EspNowLink/CLAUDE.md): il `seq` non deve avanzare). Al primo che
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

