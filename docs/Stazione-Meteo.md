# Stazione meteo e-ink — piano di lavoro

## Aggiornamento del 2026-08-30 (4) — `v14`: il grafico a 24 ore, e la salute in dashboard

**La dashboard su card mostra `/api/salute`** (nessuna modifica al firmware,
solo `www/dashboard.html` ricaricata): una pill sempre visibile e una card con
i problemi in chiaro, che **compare solo quando ce n'e' almeno uno** — come la
fascia del messaggio sul pannello. Una dashboard che cambia aspetto per dire
"va tutto bene" insegna a ignorare quell'area. Quando il conto non torna,
sotto l'elenco compaiono i numeri: e' li' che si legge dove guardare.

**La pagina GRAFICO sul pannello**: temperatura dei nodi nelle ultime 24 ore, a
piena pagina, come tipo di pagina fra gli altri.

Le scelte che contano, tutte gia' implicite in regole scritte prima:

- **piena pagina, non una sparkline dentro la pagina nodi**: su e-ink il tempo
  e' la dimensione in piu', e la rotazione alterna pagine intere e leggibili
  invece di comprimerne tre in 400x300;
- **anello separato** da quello del trend (48 slot da 30 min contro 20 da 10),
  perche' le due cose hanno finestre e scopi diversi. Senza timestamp per slot:
  l'indice e' il tempo, 101 byte per nodo invece di 288 — al prezzo di dover
  **svuotare le celle scavalcate**, o dopo un giro si legge ieri come oggi;
- **i buchi non si attraversano**: una retta sopra un'ora senza dati direbbe che
  la temperatura e' passata di li', cosa che nessuno ha misurato;
- **le curve si distinguono per tratto**, non per colore.

**Verificato sull'hardware** subito dopo l'OTA: **48/48 mezz'ore per entrambi i
nodi**, cioe' il seeding dai CSV riempie il grafico al riavvio invece di
lasciarlo formare in un giorno. Refresh misurato **2567 ms**. `temp_campioni`
su `/api/nodi` esiste apposta per poterlo verificare da remoto: il pannello da
fuori non si vede, e senza quel numero "la pagina e' comparsa" e "la pagina
mostra qualcosa" sarebbero la stessa cosa.

## Aggiornamento del 2026-08-30 (3) — `v13`: ispezione del repo, e la scheda che si controlla da sola

Passata completa su tutto il repository: compilazione dei **13 sketch**,
confronto delle copie dei moduli gemelli, caccia ai difetti noti e non ancora
corretti. Quello che ne e' uscito.

**Tre difetti veri, corretti:**

1. **`streamFile()` grezza in `Timelapse_XIAO` e `XIAO_S3_Camera`.** Era gia'
   documentato in CLAUDE.md come "dove NON e' ancora corretto", con la nota che
   li' e' **peggio**: una foto da 300 kB sono 220 chunk e la galleria ne carica
   decine. Ora hanno lo stesso `streamFileLimitato()` di `EnvNode_C3`, e
   `invii_interrotti` su `/api/stato` — senza quel contatore il taglio sarebbe
   invisibile.
2. **Le scritture su microSD non erano verificate.** `File::write()` non alza il
   writeError del core: una `print()` su card piena o sfilata torna 0 senza
   lanciare niente, e `sd_log_sample()` / `sd_log_remote()` rispondevano `true`
   lo stesso. **Il contatore saliva e il file non cresceva** — e su questo hub
   quel contatore e' proprio quello del controllo incrociato. Il pattern giusto
   era gia' nel repo, in `sd_save_photo()` del timelapse, che confronta i byte
   scritti e **cancella il file troncato**: una copia aveva ragione e le altre
   non lo sapevano.
3. **Due punti di robustezza in `EspNowLink`**: una finestra di race nel
   registro dei peer (controllo e inserimento in due sezioni critiche separate,
   con scrittura oltre l'array se il registro e' pieno) e il nome ricevuto dalla
   radio che non era garantito terminato.

**Piu' la guardia `Serial.setTxTimeoutMs(0)` nei sei `examples/` e nello
starter AMOLED**, dove mancava: sono i piu' esposti proprio perche' si usano
col monitor aperto, e il caso cattivo non e' "non c'e' mai stato un monitor" ma
"c'e' stato e se n'e' andato".

**Due aggiunte all'hub, entrambe nate da qualcosa successo oggi:**

- **`GET /api/salute`**: i controlli incrociati fatti dalla scheda invece che a
  mano. Il principale e' *pacchetti == righe + scartati + fallite*, che regge
  perche' ad ogni pacchetto contato corrisponde esattamente un tentativo di
  scrittura. Insieme arrivano i contatori degli scarti, senza i quali il
  confronto si allarmerebbe per motivi legittimi — e un controllo che grida al
  lupo non lo guarda piu' nessuno.
- **`reset_reason` e `boot_count`**, che il nodo ha da `v5` e l'hub no. Oggi un
  `uptime` di 0,7 h ha richiesto di incrociare tre fonti per concludere che era
  semplicemente l'OTA di poco prima.

**Un dato migliore del previsto**: l'OTA delle 08:51 **non e' costato un solo
pacchetto** al nodo a muro — 574 righe consecutive, nessun buco oltre i 90 s.
La stima di "un pacchetto perso per riavvio", scritta il 29/08, era pessimista.

Tutti e 13 gli sketch compilano dopo le modifiche.

## Aggiornamento del 2026-08-30 (2) — `v12`: il testo sopra le foto, e un elenco solo

Due richieste dell'utente, e una terza cosa trovata mentre si guardava.

**Il biglietto si compone nel browser** (`www/dither.html`, quindi anche
`/immagini` sulla scheda). Nuovo pannello con testo, quattro caratteri, corpo,
altezza, e tre modi di staccarlo dallo sfondo: banda piena, alone attorno alle
lettere, o nudo sulla foto.

Il punto tecnico e' che **il testo non passa dal dithering**: si disegna su un
canvas a parte, si legge a soglia secca e si stende sopra i bit gia' retinati.
Una lettera ditherata perde i tratti sottili, e a 400x300 su un pannello a 1 bit
diventa illeggibile — e' lo stesso motivo per cui non si rimpicciolisce a bordo
un `.bin` gia' retinato, visto dall'altro lato.

**Cosi' la regola "immagine + altro non si fa" resta vera dov'e' vera.** Il
divieto in CLAUDE.md riguarda il RICAMPIONAMENTO, non l'accostamento: comporre
alla dimensione finale, dove il ricampionamento non esiste, e' sempre stato
lecito. Il firmware infatti non ha imparato niente di nuovo — un biglietto e'
un'immagine come le altre e passa da `/api/immagini`.

**Il messaggio testuale NON e' stato toccato** (`messages.*` non e' nemmeno fra
i file modificati): restano NVS, archivio, scadenza, urgenza e la fascia sulla
pagina nodi. Sono due strade con due mestieri — il bigliettino scritto in dieci
secondi che sopravvive senza card, e il biglietto illustrato preparato con calma.

**`/pannello` ha un elenco solo.** Prima ogni immagine compariva due volte: in
cima come slot (solo testo) e in fondo nella galleria (con l'anteprima) — e
quella che mostrava la figura era proprio quella da cui non si governava la
pagina. Ora l'anteprima sta nell'elenco, e sotto restano solo le immagini della
card **non ancora** in uso. In piu': riordino con le frecce (lo slot 0 resta
ancorato, ed e' il posto fisso da cui riparte il tasto BOOT) e il conteggio dei
posti liberi.

**Il difetto trovato per caso, ed e' quello che rendeva la pagina "difficile da
usare"**: gli slot erano **8 su 8** e il pulsante "Aggiungi" rispondeva 507
correttamente, ma il JavaScript faceva `.then(r => r.json())` senza guardare
`r.ok`. Su una risposta d'errore in `text/plain` la promise andava in eccezione
e **il pulsante non faceva niente, in silenzio**. Lo schema era su tutti i
pulsanti della pagina: ora c'e' `postJson()`, che mostra il testo del server.
Da ricordare come forma: *un errore gestito dal server non e' un errore
mostrato all'utente* — in mezzo c'e' un client che puo' ingoiarlo.

**`PAGES_MAX` da 8 a 16, con migrazione `PAG1` -> `PAG2`.** La validita' del
blob NVS si controlla anche sulla lunghezza, quindi allungare l'elenco lo
avrebbe reso irriconoscibile: le cinque pagine immagine sarebbero sparite
durante l'OTA, in silenzio, con le immagini ancora sulla card. Il blob vecchio
viene riconosciuto, copiato nei primi otto slot e riscritto nel formato nuovo
una volta sola.

**Caricato e verificato sull'hardware** (OTA, 1,38 MB in **8,1 s**):

| verifica | esito |
|---|---|
| migrazione NVS | 8 pagine su 8 **identiche** (tipo, param, attiva, durata), rotazione/silenzio/fascia invariati |
| slot | 8 su **16**, 8 liberi |
| riordino su/giu' | funziona nei due versi, configurazione riportata identica |
| slot 0 | rifiutato con **409** e un messaggio leggibile, come previsto |
| nodi dopo il riavvio | tutti e due consegnano entro un minuto, `persi: 0` |
| `righe_scritte` | 2, cioe' la somma dei pacchetti dei due nodi |

**Quello che NON e' stato verificato**: la resa visiva. L'estensione Chrome non
era connessa, quindi la tipografia del biglietto (interlinea, margini della
banda, scelta dei caratteri) e' ragionata e provata **solo** con nove controlli
in node sui bit — banda, alone, inversione, testo vuoto, blocco ai bordi, testo
lunghissimo — non guardata a occhio.

## Aggiornamento del 2026-08-30 — il quarto segmento di scarica, il primo pulito

Verifica di salute di hub e nodi, e una lettura al multimetro. **La cella non è
mai stata ricaricata dal 23/08**, quindi i quattro segmenti sono una curva sola:

| | seg. 1 (`v9`) | seg. 2 (`v10`) | seg. 3 (`v10`) | **seg. 4 (`v13`)** |
|---|---|---|---|---|
| cadenza | 60 s | 300 s | 300 s | **300 s** |
| finestra | 20,41 h | 24,93 h | 23,43 h | **~59 h** |
| interventi della rete di sicurezza | 0 | 1 | 2 | **0** |
| batteria (multimetro, a riposo) | 4,18 → 4,12 V | 4,12 → 4,10 V | 4,10 → 4,08 V | **4,07 → 4,06 V** |
| caduta oraria | −2,94 mV/h | −0,80 mV/h | −0,85 mV/h | **−0,17 mV/h** |

In totale **4,18 → 4,06 V in 6,4 giorni**. Il segmento 4 è il primo dopo la
Fase 9, cioè il primo **senza un solo riavvio**: la pendenza cala di cinque
volte, che è il verso giusto e quantifica quanto pesavano quegli episodi
(~6,7 mAh l'uno, più di un giorno di funzionamento regolare). Vale però la
stessa avvertenza di sempre, anzi più forte: **10 mV sono UNA cifra sul
multimetro**, quindi è un indizio concorde, non una misura.

**L'estrapolazione non si può fare, e conviene vedere di quanto sbaglierebbe.**
Da 4,06 V a 3,9 V mancano 160 mV, che valgono **8 giorni** con la pendenza del
segmento 3 e **39 giorni** con quella del segmento 4: un fattore cinque fra due
letture entrambe difendibili. Sul plateau della LiPo la tensione non è una
misura di carica, e nessun altro punto preso quassù lo cambierà — il dato utile
arriverà **sotto i 3,9 V**, dove la curva torna a pendere. Fino ad allora
l'unico strumento vero resta il partitore su D1/GPIO3 (`BATTERY_ADC_ENABLED 0`,
componenti non ancora arrivati).

**Il resto della rete, alle 07:00**: hub `v11` con uptime 32,6 h (nessun riavvio
dal 28/08 22:26), AP tornato sul **canale 1** dopo essere stato sul 13, card
14,9 GB, heap 222 kB, `invii_interrotti: 0`. Nodo a muro `v13`: `errors: 0` su
1260 letture, **0 invii ESP-NOW falliti**, `espnow_canale: 1` — cioè il campo
corretto il 29/08 ora segue davvero la radio. Nodo a batteria: 390 pacchetti,
`persi: 0`.

CSV del 29-30/08: gap **299-301 s** per il nodo a batteria e **59-61 s** per
quello a muro, zero campi vuoti, zero righe con `fonte_ora != NTP`. I due soli
reset di `seq` sono voluti (OTA del nodo a muro il 29/08 alle 10:02, power-cycle
del nodo a batteria alle 15:55). **Nessuna traccia della rete di sicurezza**
nemmeno con l'AP che si è spostato di nuovo: la Fase 9 regge alla seconda prova
sul campo.

Un controllo incrociato che vale la pena rifare ad ogni verifica:
`righe_scritte` dell'hub (2346) è **esattamente** la somma dei `pacchetti` dei
due nodi (1956 + 390). Sono contatori tenuti da moduli diversi — se un giorno
non tornassero, il guasto starebbe fra la radio e la scrittura su card.

## Aggiornamento del 2026-08-28 — la Fase 9 regge 24 ore, e l'AP si è spostato davvero

Prima finestra lunga dopo la Fase 9, letta dai CSV dell'hub
(`/api/nodi/scarica`), **2026-08-27 19:08 → 2026-08-28 18:49, 23,7 ore**:

| | prima (24-27 ago) | questa finestra |
|---|---|---|
| episodi della rete di sicurezza | 5 in 3 giorni (~1,6 attesi in 24 h) | **0** |
| salti di `seq` / riavvii | ricorrenti, `seq` che ripartiva da 1 | **0** |
| campioni consegnati | con buchi da 25 minuti | **285 su 285** |

Distribuzione dei gap fra un DATA e il successivo: 299 s ×5, 300 s ×213,
301 s ×65, 302 s ×1. **Niente fuori da ±2 s.**

**La prova non è passiva: il caso è arrivato da solo.** Il 27/08 l'hub stava
sul **canale 1**; oggi `/api/stato` dell'hub dice **canale 13**. L'access point
si è spostato dentro la finestra, e il nodo a muro lo conferma da un'altra
angolazione (`wifi_drops: 2`, cadute da 5 s — cioè riassociazioni, non
disconnessioni vere). È **esattamente** lo scenario che il 25/08 aveva
ammutolito il nodo per 25 minuti, e stavolta è passato senza perdere un
campione.

Da notare: **13 non è fra i tre canali provati per primi** (1, 6, 11). La
scansione ha dovuto arrivare agli altri dieci e ce l'ha fatta comunque dentro
lo stesso risveglio — il caso peggiore della tabella qui sotto (~2,6 s di
radio) non è teorico, è successo.

**Quindi la diagnosi era giusta**: il guasto era il canale in RTC memory contro
un AP che se lo sposta. Non la batteria (la cella misurava 4,07 V, già escluso
il 27/08), non la portata, non l'hub. Una causa in meno, verificata invece che
supposta.

**Cosa resta non misurato**: `scansioni_ok`, cioè **quante volte** la ricerca è
servita in queste 24 ore. Il contatore c'è ed è in NVS, ma sta dentro un nodo
che dorme e non ha IP — si legge solo nei 5 minuti di veglia dopo un
power-cycle. I CSV dicono già la cosa che conta (nessun buco), quindi non vale
un power-cycle apposta: si leggerà alla prossima occasione in cui il nodo è
sveglio comunque.

**Il nodo a muro** (`MeteoEsp32`, `v11`) nella stessa finestra: 1487 campioni a
60 s, **un solo salto di `seq`** (41 → 44, il 27/08 alle 18:16:44, nei minuti
della migrazione di hub), poi 25 ore filate. `espnow_falliti: 9` su 1510 invii,
tutti recuperati dai ritentativi di `sendReliable()` — lato hub `persi: 0`.

**L'hub** (`v2`): uptime 23,2 h, RSSI −45 dBm (l'antenna esterna si sente),
1666 righe scritte su card, `invii_interrotti: 0`, heap 228 kB, 1378 refresh
del pannello.

**Conseguenza per la misura di scarica**: cade la riserva annotata il
2026-08-26 (*«finché il canale non è risolto, una misura di scarica misura
anche i riavvii»*). Da questa finestra in poi un segmento di scarica è pulito —
un riavvio della rete di sicurezza costava ~6,7 mAh contro i ~6,5 mAh/giorno
del funzionamento regolare, cioè più di un giorno intero ciascuno, e non se ne
vedeva traccia sul multimetro.

## Aggiornamento del 2026-08-27 (4) - Fase 9 fatta: il nodo cerca il canale

Scritta, compilata su tutti e sei gli sketch che usano `EspNowLink`, e caricata
sul nodo a batteria via OTA (`v10` -> `v12`, 1,2 MB in 7,4 s, impostazioni
sopravvissute: `sleep` true, intervallo 300 s, altitudine 29 m).

**Nella libreria condivisa**: `Link_SetChannel(ch)` sposta la radio e riallinea
i peer gia' registrati, iterando il registro del driver invece dei peer dei due
ruoli - cosi' non deve sapere se gira su un hub o su un nodo, e prende anche il
peer broadcast. Ed e' documentata come **vietata a chi e' connesso a un AP**.

**`Link_Node_ResendLast()` e' l'aggiunta che non era nel piano**, ed e' quella
che ha cambiato il disegno: `Link_Node_SendData()` incrementa il `seq` ad ogni
chiamata, quindi riprovare lo stesso campione su tre canali avrebbe prodotto
tre numeri di sequenza. L'hub li avrebbe letti come pacchetti persi sulla
tratta radio: **buchi inventati dentro il registro che serve a contare i buchi
veri**, e per giunta proprio nei momenti in cui si sta indagando su una
perdita. Il rinvio manda lo stesso identico messaggio.

**Nel nodo**: `hub_scan_channels()` prova 1, 6, 11 e poi gli altri dieci, un
tentativo per canale, 200 ms di timeout. Al primo che risponde salva il canale
in RTC memory e in NVS. Se nessuno risponde si torna al canale di partenza -
non all'ultimo provato, che e' scelto a caso e ha appena fallito. La rete di
sicurezza dei cinque risvegli muti resta sotto: la ricerca risolve il canale
cambiato, non l'hub spento.

**Il canale ora sta anche in NVS**: la RTC memory la cancella il power-cycle,
cioe' l'operazione con cui si recupera un nodo che non torna. Al boot senza AP
si riparte dall'ultimo canale buono invece che dal canale fisso della libreria.

**E' stato aggiunto un modo di provarla**: `GET /api/comando?c=prova-canale`
arma UN risveglio con il canale volutamente sbagliato. Senza, la Fase 9 sarebbe
rimasta non verificata finche' l'access point non si fosse spostato da solo -
giorni o settimane - e il momento della scoperta sarebbe stato esattamente
quello in cui serviva funzionante. Resta come strumento diagnostico.

**Contatore `scansioni_ok`** (NVS, su `/api/stato` e in pagina, riga "Hub
ritrovato su altro canale"): misura quanto spesso l'AP si sposta sotto il naso
di un nodo che dorme. **Zero non e' un guasto.**

### Provata sul campo, e funziona (2026-08-27, 19:08)

Prova armata dalla pagina, nodo addormentato **sul canale 11** con l'hub sul
**canale 1**. Al risveglio delle 19:07 il DATA e' arrivato lo stesso:

```
19:08:03  pacchetti 6 -> 7, seq 2
/nodi/Meteo-7EAE0C/2026-08-27.csv  ->  NTP, 27.49 C, 58.78 %, 1014.00 hPa
```

Con il firmware di prima quel pacchetto sarebbe andato perso, e con lui altri
quattro prima che la rete di sicurezza riavviasse la scheda: 25 minuti di buco
e ~7 mAh. Qui il campione **non e' stato perso**, ed e' stato recuperato dentro
lo stesso risveglio.

**Riserva, per onesta'**: e' la prova del risultato (il DATA e' arrivato dove
non doveva arrivare), non la lettura del contatore. `scansioni_ok` dovrebbe
essere a 1 e `canale_noto` a 1, ma stanno dentro un nodo che dorme. Si leggono
al prossimo power-cycle. Il sabotaggio e' codice deterministico e il flag vive
in RTC memory, che il deep sleep conserva, quindi "non e' stato sabotato
affatto" e' molto improbabile - ma finche' quel numero non si legge resta
un'inferenza, non una misura.

### Effetto collaterale dell'aggiornamento: il nodo si e' rinominato

Passando da `v10` a `v12` il nodo a batteria e' diventato **`Meteo-7EAE0C`**
invece di `MeteoNode`. E' la trappola gia' documentata in CLAUDE.md - **un
default nuovo vince su una chiave NVS mai scritta**: quel nome era il default
del firmware, non una scelta salvata, e da `v11` il default e' "derivato dal
MAC".

Sull'hub il nodo **non si e' sdoppiato** (l'identita' e' il MAC), ma i suoi CSV
proseguono in `/nodi/Meteo-7EAE0C/` mentre lo storico fino a oggi resta in
`/nodi/MeteoNode/`. Decisione presa: **si lascia cosi' fino alla messa in
servizio**, quando i nodi prenderanno nomi parlanti - e quei nomi andranno
**scritti dalla pagina**, perche' solo cosi' finiscono in NVS e nessun
aggiornamento futuro potra' piu' cambiarli da sotto.

## Aggiornamento del 2026-08-27 (3) - migrare un nodo: prima si dimentica

Trovato spostando il nodo a batteria dall'hub C3 a quello S3, e non e' ovvio:
**un hub che ha gia' il nodo in registro risponde al suo HELLO anche a finestra
di associazione CHIUSA.** Sta in `link_peer.cpp`, ed e' deliberato:

```cpp
} else if (msg.msg_type == LINK_MSG_HELLO) {
    // Un peer gia' noto che manda di nuovo HELLO ha perso il proprio stato
    // di pairing (es. riavvio) ... rimandagli il WELCOME
    welcomePending = true;
}
```

Serve a non lasciare bloccato un nodo che si e' riavviato. Ma a un HELLO in
broadcast rispondono quindi TUTTI gli hub che quel nodo lo conoscono, e vince
chi arriva primo: al primo tentativo ha vinto EnvNode_C3, che lo aveva in NVS
dal 23 agosto. Il nodo e' tornato da lui, mentre l'hub S3 - che era in pairing -
se lo era messo in elenco senza ricevere un solo DATA (vanno in unicast
all'altro). **Dai due lati la cosa si legge in modo opposto**, ed e' il solito
guasto silenzioso: l'S3 mostra un nodo, il nodo dice di essere associato, e
nessuno dei due dice che parlano con qualcun altro.

**Ordine giusto per migrare un nodo**: 1) dimenticarlo sul vecchio hub, 2)
aprire la finestra sul nuovo, 3) power-cycle del nodo. Il power-cycle serve
comunque: il MAC dell'hub sta in RTC memory, e senza toglierlo di li' il nodo
non rifa' HELLO.

**Nota sulla batteria** (misurata a riposo, scollegata: 4,07 V). Dopo i tre
segmenti di scarica (4,18 / 4,12 / 4,10 / 4,08) la cella e' ancora intorno al
75-80%: **la pista "tensione che cede sul picco della radio" per i cinque
risvegli muti si sgonfia**, e resta in piedi quella del canale in RTC memory
contro un AP che se lo cambia - coerente col fatto che il riavvio dell'uscita
di sicurezza risolve, visto che riaccende il WiFi e riassocia. Il che vuol dire
anche che **cambiare hub non lo curera'**: la cura e' la Fase 9.

Contatori letti dal nodo mentre era sveglio: risvegli 31, consegnati 20,
boot_count 24, fw v10, sleep attivo, intervallo 300 s.

**Esito, al secondo tentativo con l'ordine giusto**: nodo dimenticato su
EnvNode_C3, finestra aperta sull'S3, power-cycle. Il nodo si e' associato
all'hub nuovo (`espnow_hub: E0:72:A1:F9:A2:B0`, boot_count 25, POWERON), e il
primo DATA e' finito in `/nodi/MeteoNode/2026-08-27.csv` con `fonte_ora=NTP` e
`seq 1`. Finestra di associazione richiusa subito dopo.

**La stazione e' ora tutta su `MeteoHub_S3`**: due nodi, entrambi online,
registrati su microSD, con web UI e OTA. `EnvNode_C3` e' tornato a fare solo il
nodo ambientale (`nodi: 0`) e non risponde piu' agli HELLO, non avendo piu'
nessuno in registro: **il rischio di riprendersi un nodo per sbaglio si e'
chiuso da se'**.

**Lo storico dei due nodi resta diviso in due**: fino al 27 agosto sulla card di
`EnvNode_C3` (`/nodi/MeteoEsp32/`, `/nodi/MeteoNode/`), da qui in poi su quella
dell'S3. Non e' stato unito: servirebbe un lettore di schede, e le due cartelle
si leggono benissimo separate. Chi guardera' quei grafici fra un mese deve pero'
saperlo.

**Il trend riparte da zero** su entrambi i nodi (tre ore prima di dire qualcosa
di sensato): `seedForecastDaSD()` ricostruisce dai CSV di QUESTA card, che oggi
comincia adesso.

## Aggiornamento del 2026-08-27 (2) — Fase 3 chiusa: SD, NTP, web UI, OTA

Tutto provato su hardware nella stessa sessione, con la scheda ancora al cavo.

**La microSD e l'e-ink convivono sul bus SPI.** Era il rischio rimasto in
sospeso dal bring-up (fatto senza la scheda Sense, quindi con il bus tutto del
pannello). Con la Sense montata: card montata, 14.901 MB liberi su 14.902, e il
pannello continua a disegnare. Due accortezze, entrambe nel codice: il CS della
card (GPIO21) alzato **prima** di toccare il bus, e `sd_begin()` che **non**
chiama `SPI.begin()` — lo ha già fatto il `.ino` per il display, sugli stessi
pin. Regge perché tutto gira in `loop()`; con un task proprio quel bus andrebbe
protetto.

**Quello che ora funziona**, misurato: WiFi (192.168.1.73, canale 1, RSSI -78),
NTP, pagina di stato con i nodi, `/update`, CSV dei nodi su card. Il primo DATA
del DOIT è finito in `/nodi/MeteoEsp32/2026-08-27.csv` con `fonte_ora=NTP`,
seq 32, e i valori giusti. **Lo storico del DOIT, interrotto stamattina con la
migrazione, riprende da qui.**

**L'OTA funziona su questa scheda**: 1.263.312 byte in **6,5 s** (193 kB/s),
risposta `OK`, riavvio, e dopo il riavvio SD montata, NTP sincronizzato e il
nodo rientrato **da solo** dal registro in NVS, a finestra di associazione
chiusa. Da adesso l'hub si aggiorna via rete.

**Canale ESP-NOW: si torna a `ESPNOW_LINK_CHANNEL_CURRENT` (0).** Prima della
Fase 3 qui c'era il numero fisso 1, perché senza WiFi nessuno imponeva il
canale; ora la scheda sta su un AP e forzarlo chiamerebbe
`esp_wifi_set_channel()` su una STA connessa. `remote_begin()` va dopo
`net_begin()`.

**La finestra di associazione resta chiusa all'avvio**, al contrario di
`EnvNode_C3`, e la pagina lo dice: i nodi noti stanno in NVS e rientrano
comunque, mentre un hub in ascolto si porterebbe via il primo nodo che si
riavvia in casa.

**Refresh del pannello, misurati**: completo ~2,2 s (4,8 s il primo dopo
l'accensione, che include il power-on del controller), parziale ~1 s.

**Cosa resta**: `ventilation.h` e il confronto dentro/fuori (serve un secondo
nodo posizionato fuori), le pagine immagine (Fase 5) e le pagine extra
(Fase 6). E il nodo a batteria è ancora su `EnvNode_C3`: ha senso spostarlo
qui adesso che questo hub registra e si aggiorna da solo, ma va fatto con un
power-cycle e la finestra aperta — e chiude anche il doppio hub acceso.

## Aggiornamento del 2026-08-27 — l'hub S3 riceve, e il DOIT ci si e' trasferito

**`MeteoHub_S3` non e' piu' solo il bring-up del pannello: e' un hub.** Trapiantati
da `projects/EnvNode_C3/` senza modifiche `remote_nodes.*`, `forecast.h` e
`rtc_time.*` (unica aggiunta: un overload `remote_begin(nome, canale)`, perche'
questa scheda non sta ancora su un AP e il canale deve sceglierselo). Sopra ci
sono la **pagina 1/6 NODI** — nome, temperatura in grande, umidita', pressione,
eta' del dato, trend, badge MUTO in negativo — e due cose nuove:

- **Il tasto BOOT fa due gesti**: breve cambia pagina, lungo (1,2 s) apre o
  chiude la finestra di associazione per 2 minuti. Finche' non c'e' la Fase 3
  quel tasto e' l'unica interfaccia dell'hub.
- **La finestra NON si apre da sola all'avvio**, al contrario di `EnvNode_C3`.
  Un nodo tiene un hub solo e lo adotta il primo che risponde al suo HELLO; il
  nodo a batteria fa HELLO ad ogni power-cycle e si riavvia da solo circa una
  volta ogni 14 ore, mentre `EnvNode_C3` ha la finestra normalmente chiusa. Un
  hub di sviluppo lasciato in pairing sul canale di casa se lo porterebbe via
  insieme al log su SD, che qui ancora non esiste.

**Il DOIT (`MeteoEsp32`) e' passato all'hub S3.** Power-cycle con la finestra
aperta: `boot_count` 10 → 11, `reset_reason` POWERON, `espnow_hub` da
`AC:A7:04:BF:11:48` (EnvNode_C3) a `E0:72:A1:F9:A2:B0` (la XIAO S3), primo DATA
`inviati: 1, falliti: 0`.

**Cade il limite noto dell'unicast hub-S3 ↔ nodo classico** (issue
arduino-esp32 #10895): rifatta esattamente quella combinazione, il pairing e'
stato immediato. Con il core 3.3.10 non si ripresenta. Aggiornato anche
CLAUDE.md, dove la raccomandazione diceva il contrario.

**Il log di boot di questa scheda non e' osservabile via USB.** Ogni cattura si
ferma a **256 byte esatti** — il buffer TX della CDC. All'apertura del monitor
la scheda si resetta, l'host completa l'enumerazione un paio di secondi dopo, e
con `Serial.setTxTimeoutMs(0)` tutto quello che eccede il buffer viene buttato:
le righe dell'hub, stampate subito dopo quelle del display, non arrivano mai.
Sembra un setup che si interrompe a meta'. E' la stessa forma delle altre
trappole di questo progetto — lo strumento con cui si vorrebbe guardare e' cio'
che impedisce di vedere — e il rimedio e' lo stesso: **la diagnostica sta sul
pannello**, non sulla seriale. Il piede della pagina NODI dice "canale 1" se la
radio e' su e "ESP-NOW NON ATTIVO" se l'init e' fallita, cosi' le due cose non
sono piu' la stessa pagina vuota.

**Quello che questo aggiornamento costa, e va chiuso presto**: da adesso i dati
del DOIT arrivano a una scheda che **non li salva** — niente microSD montata,
niente orario NTP, niente web UI ne' OTA. Il suo storico in
`/nodi/MeteoEsp32/*.csv` si ferma qui. La **Fase 3 diventa la priorita'**, e
finche' non e' fatta il nodo a batteria resta dov'e', su `EnvNode_C3`, che
continua a loggarlo.

Da fare subito dopo: **dimenticare `MeteoEsp32` su `EnvNode_C3`**
(`POST /api/nodi/dimentica?mac=70:4B:CA:82:9E:70`), o restera' in elenco come
nodo muto per sempre, cioe' un allarme falso permanente.

## Aggiornamento del 2026-08-26 — il terzo segmento, due riavvii in più, e la co-locazione finita

**Segmento 3 della misura di scarica, chiuso.** Stessa cella, `v10`, cadenza
300 s, letture al multimetro alle ~21:00 del 25 e alle ~20:30 del 26:

| | segmento 1 (`v9`) | segmento 2 (`v10`) | segmento 3 (`v10`) |
|---|---|---|---|
| cadenza | 60 s | 300 s | 300 s |
| hold sul VCC del sensore | no | sì | sì |
| finestra | 20,41 h | 24,93 h | **23,43 h** |
| pacchetti attesi / scritti | 1212 / 1212 | 294 / 294 | **282 / 272** |
| batteria (multimetro, a riposo) | 4,18 → 4,12 V | 4,12 → 4,10 V | **4,10 → 4,08 V** |
| caduta oraria | −2,94 mV/h | −0,80 mV/h | **−0,85 mV/h** |

La pendenza ripete quella del segmento 2, ma vale ancora l'avvertenza del 25 —
20 mV sono due cifre sul multimetro, quindi i due segmenti sono
**indistinguibili, non confermati**. Il fatto nuovo è un altro: il segmento 3
**non è pulito**.

**La rete di sicurezza dei cinque risvegli muti è scattata altre due volte.**
Letto dai CSV dell'hub (`/nodi/MeteoNode/`), la firma è identica ogni volta:
1508 s di silenzio (5 cicli da 300 s), `seq` che riparte da 1, poi altri ~10
minuti prima del primo DATA.

| silenzio da | riavvio (`seq` → 1) | dati persi |
|---|---|---|
| 2026-08-25 00:13 | 00:38 | 25,1 min (già annotato il 25) |
| 2026-08-25 21:14 | 21:39 | 25,1 min — **nuovo** |
| 2026-08-26 00:14 | 00:39 | 25,1 min — **nuovo** |

In totale, nel segmento 3: 4 buchi, 8 cicli persi, 70 minuti di dati mancanti su
23,43 h. **Il gemello a muro non ha un buco in nessuna delle tre finestre**,
quindi l'hub riceveva ed era il nodo a non essere sentito: stessa diagnosi del
25 (il canale in RTC memory contro un AP che se lo cambia da solo), ora su tre
occorrenze in due giorni. Non è un caso raro da manuale, è la normalità di
questa rete — la **Fase 9** smette di essere un'idea di comodo.

**Quanto costa un riavvio, in corrente.** Il funzionamento regolare a 300 s sta
sui ~6,5 mAh/giorno (44 µA dormendo = 1,06 mAh, più 288 risvegli da 0,68 s a
~100 mA = 5,4 mAh). Un intervento della rete di sicurezza accende WiFi e OTA per
la finestra di veglia da 5 minuti: a ~80 mA sono **~6,7 mAh**, cioè **più di un
giorno intero di funzionamento normale in un colpo solo**. I due eventi dentro
il segmento 3 valgono ~13 mAh contro i ~6,3 mAh consumati dal ciclo vero: **il
grosso della scarica osservata potrebbe non essere il deep sleep, ma i
riavvii.** Le correnti sono stime da datasheet mai misurate su questa scheda e
la capacità reale della cella è ignota, quindi il numero è buono entro un
fattore due — ma l'ordine di grandezza regge, e ribalta la lettura della
tabella qui sopra.

**Perché non si vede nel multimetro**: 20 mAh su una cella economica (1500 mAh
reali) sono l'1,3 % della capacità, cioè ~15-18 mV nel tratto alto della curva —
esattamente i 20 mV misurati, ma anche esattamente la risoluzione dello
strumento. **L'evento si vede nei dati (il buco, il `seq` che riparte) e non
nella tensione**: è la stessa asimmetria dell'`gpio_hold_en()`, al contrario.
Chi guardasse solo il multimetro concluderebbe che il segmento 3 conferma il 2.

**Il nodo a muro è stato spostato** (2026-08-26, staccato dalla presa USB verso
le 10:51, ora in camera): il buco di 3,2 min in `/nodi/MeteoEsp32/2026-08-26.csv`
è quello, non un guasto. Da lì in poi le sue letture di temperatura e umidità
sono di **un'altra stanza** — il delta con il nodo a batteria passa da −0,00 a
−0,37 °C alle 11:00 in punto, ed è una differenza fra ambienti, non fra sensori.
Chi guarderà i grafici fra sei mesi vedrà uno scalino senza spiegazione: è qui.

**La co-locazione è finita, ma i dati per la calibrazione erano già stati
raccolti.** È il confronto che il backlog aspettava da giorni. 225 campioni
appaiati (25 agosto 06:00-20:00 e 26 agosto fino alle 10:51, abbinati per
timestamp entro 60 s):

| | media (muro − batteria) | sd | min .. max |
|---|---|---|---|
| temperatura | **+0,016 °C** | 0,031 | −0,10 .. +0,08 |
| umidità | **+0,11 %** | 0,15 | −0,3 .. +0,7 |
| pressione | **−1,265 hPa** | 0,034 | −1,43 .. −1,17 |

Cosa se ne ricava:

- **I due AHT20 non hanno bisogno di alcun offset**: concordano entro il rumore
  di quantizzazione, temperatura e umidità insieme. `offset_temp`/`offset_rh`
  per nodo in NVS, previsti come lavoro da fare, **non servono** — ed è una
  decisione da prendere adesso che il numero c'è, non un lavoro da fare per
  scrupolo. La soglia che ci si era dati era 0,2 °C: siamo a un decimo di
  quella.
- **Risponde anche alla domanda lasciata aperta il 23**: la co-locazione non era
  "pulita", perché `MeteoNode` dorme e `MeteoEsp32` sta sveglio col WiFi acceso
  su un DevKit v1 che ha regolatore e USB-seriale sempre alimentati. Ci si
  aspettava quindi di misurare soprattutto l'**autoriscaldamento**, che era
  stato stimato valere gradi interi. Su 225 campioni **non si vede**: +0,016 °C
  con sd 0,031, senza deriva nelle ore dopo l'addormentamento. Il montaggio del
  sensore su cavo, lontano dalla scheda, evidentemente basta.
- **La pressione resta non calibrata fra nodi**, come deciso il 2026-08-22: la
  previsione usa il **trend**, e un offset costante sparisce nella differenza.
  Il numero ora però è misurato bene — **1,265 hPa, sd 0,034**, stabile su due
  giorni e **invariato dopo lo spostamento** (−1,20 contro −1,25, dentro la
  dispersione), come dev'essere visto che la pressione è la stessa in tutta la
  casa. Se un giorno si vorranno confrontare i valori assoluti di due nodi —
  oggi la dashboard li mostra affiancati — il numero è questo, e non serve
  rifare la co-locazione. Quale dei due sia *giusto* non lo dice: per quello
  serve un riferimento esterno (il QNH di una stazione vicina riportato alla
  quota di casa).
- La differenza di temperatura annotata il 23 (27,7 contro 27,2 °C) era
  **transitoria** — equilibrio termico non ancora raggiunto, o una delle due
  schede appena accesa. Quella di pressione (~1,4 hPa) era reale ed è ancora lì,
  ora quantificata a 1,265.

Per un altro punto di calibrazione i due nodi vanno rimessi insieme: da oggi il
confronto misura le stanze, non i sensori.

**Il nodo a muro è passato da `v5` a `v11`** (OTA su 192.168.1.72, 1,16 MB in
10,8 s, `reset_reason=SW`): era rimasto indietro di sei versioni. Il firmware
compilato per la DOIT DevKit v1 occupa ora l'**88 %** della partizione
(1 158 460 su 1 310 720 byte), e su questa board la tabella è fissa — il
margine per crescere è quello, e va tenuto d'occhio ad ogni aggiunta.

**E qui è saltata fuori una trappola che non è di questo progetto**, quindi sta
in `CLAUDE.md` ("Aggiornare un nodo: un default nuovo vince su una chiave NVS
mai scritta") e non qui: dopo il riavvio l'intervallo di misura era **300 s
invece di 60**, senza che nessuno l'avesse cambiato — `settingsLoad()` legge da
NVS con i default del **firmware nuovo**, e fra `v5` e `v10`
`INTERVALLO_DEFAULT_S` è passato da 60 a 300 (la cadenza del nodo a batteria).
La chiave su quel nodo non era mai stata scritta; l'altitudine, che invece lo
era, è sopravvissuta. Rimesso a 60 s con `GET /api/config?intervallo=60`, che
ora lo **scrive**. Da ricordare qui: **dopo ogni OTA su un nodo della stazione,
ricontrollare intervallo, altitudine e `sleep`** — sono i tre parametri il cui
default può cambiare, e `sleep` è quello che farebbe il danno peggiore.

---

## Aggiornamento del 2026-08-25 — il secondo segmento di scarica, e un guasto dell'hub

**Segmento 2 della misura di scarica, chiuso.** Stessa cella, `v10`, cadenza
300 s, con l'`gpio_hold_en()` sul VCC del sensore:

| | segmento 1 (`v9`) | segmento 2 (`v10`) |
|---|---|---|
| cadenza | 60 s | 300 s |
| hold sul VCC del sensore | no | sì |
| finestra | 20,41 h | **24,93 h** |
| pacchetti attesi / scritti | 1212 / 1212 | 294 / 294 |
| batteria (multimetro, a riposo) | 4,18 → 4,12 V | **4,12 → 4,10 V** |
| caduta oraria | −2,94 mV/h | **−0,80 mV/h** |

È un **indizio concorde, non una misura**: 20 mV sono due cifre sul
multimetro; il segmento 2 sta più in basso sulla curva del litio, dove scende
più piano di suo; e fra i due segmenti sono cambiate **due** cose insieme
(60→300 s e l'hold), quindi i due contributi non sono separabili. Serve sempre
il partitore.

**Il nodo si è riavviato alle 00:38 del 25**, e per la prima volta è scattata
la rete di sicurezza: `seq` da 75 a 1 (RTC memory persa = `ESP.restart()`, non
un risveglio) preceduto da 1508 s di silenzio, cioè **cinque cicli da 300 s** —
la firma esatta di `RISVEGLI_MUTI_MAX`. Nella stessa finestra il gemello a muro
ha consegnato senza un buco, quindi l'hub riceveva: era il nodo a non essere
sentito. **Il canale ESP-NOW oggi è 1, non il 6 delle prime prove**: l'AP l'ha
cambiato per conto suo. Un nodo sveglio lo segue riassociandosi, uno che dorme
resta sul canale in RTC memory e diventa sordo finché non si riavvia. È
esattamente il caso per cui il piano dice **di fissare il canale 2,4 GHz nel
router** — e che finora non era mai stato osservato dal vivo. La rete di
sicurezza ha funzionato, ma costa 25 minuti di dati e un riavvio.

**L'hub è rimasto fermo 456 s il 24 alle 21:00**, senza riavviarsi (lo esclude
`uptime`). Causa trovata e corretta in `EnvNode_C3` `v12`: `streamFile()` del
core non controlla se il client accetta i dati e ogni write può attendere 10 s,
quindi un client andato via a metà scaricamento tiene `loop()` dentro
l'handler. **Il danno non resta sull'hub**: senza `remote_loop()` i DATA dei
nodi arrivavano alla radio e nessuno li prelevava dal driver, che tiene solo
l'ultimo — nei log sembravano perdite radio. Meccanismo completo, e le due
schede dove il difetto **resta**, in `CLAUDE.md`. Da `v12` `/api/stato` riporta
anche `loop_max_ms`/`loop_max_dove`/`loop_lenti`/`invii_interrotti`: la
prossima volta la risposta è lì invece che in un'indagine sui CSV.

Censimento su 23 giorni di log dell'hub (33 184 righe): sette buchi sopra i
150 s in tutto, di cui uno da 47,8 min il 2026-08-02 (sviluppo) e gli altri fra
2,5 e 7,6 min. È un evento raro, non un difetto continuo — ma ogni occorrenza
si porta via anche i dati dei nodi.

---

## Aggiornamento del 2026-08-23 — il nodo è morto di notte, e cosa ne è seguito

Il nodo lasciato a batteria la notte fra il 22 e il 23 è stato trovato spento:
cella a **3,04 V** al multimetro, tutto di nuovo operativo con una carica. Non
torna il conto — una 18650 anche solo da 1500 mAh reali, col C3 in WiFi,
dovrebbe stare in piedi 25-35 ore, non una notte. Le ipotesi, in ordine di
probabilità: **non è morta la cella ma la scheda** (3,04 V non è protezione
intervenuta, che stacca a ~2,5 V, ma è coerente con l'LDO della XIAO che va in
dropout e manda il C3 in brownout loop — se è così, la parte di 18650 sotto
~3,3-3,4 V non è utilizzabile e va tolta dal calcolo dell'autonomia); oppure la
cella non era carica davvero; oppure un consumo parassita (il LED del modulo
sensore).

**Il partitore della batteria non si può ancora cablare**: i componenti non ci
sono. Conseguenze accettate, da non dimenticare:

- il nodo **non torna a batteria** finché non c'è. Tutto lo sviluppo del deep
  sleep si fa su USB, dove per giunta la Serial resta viva;
- senza tensione di cella non c'è cutoff, quindi nemmeno protezione dalla
  scarica profonda: la cella va ricaricata subito e non lasciata a 3,04 V;
- **l'unica diagnostica che resta è "il nodo ha smesso di parlare"**, e per
  questo il rilevamento del nodo muto è stato scritto subito, insieme alla
  ricezione, invece che in un secondo giro;
- non serve esattamente 2×1 MΩ: va bene qualunque coppia di resistenze uguali
  fra ~100 kΩ e 1 MΩ (a 100 k sono ~16 µA, meno dei ~44 µA di deep sleep). Il
  100 nF aiuta l'assestamento dell'ADC ma non è indispensabile.

**Ordine di lavoro deciso** (conta, perché quando il nodo dorme si perde lo
strumento con cui lo si debuggherebbe): ricezione sull'hub → invio dal nodo
ancora sveglio e su USB → deep sleep per ultimo.

**Fatto il 2026-08-23, e provato su hardware vero**: `EnvNode_C3` `v4` (hub
ESP-NOW) e `MeteoNode_C3` `v3` (invio ESP-NOW). **La catena nodo → hub
funziona.** Entrambi caricati via `/update` senza toccare nessun cavo. Vedi
"EnvNode_C3 come hub provvisorio" e "Fase 1 bis" più sotto.

Misure della prima prova, dall'hub:

| | |
|---|---|
| associazione | **19 s** dal riavvio del nodo |
| canale | **6**, ricavato da solo dall'AP da entrambi i lati |
| primo DATA | 27,09 °C / 42,53 %RH / 1015,78 hPa |
| cadenza appresa | dal **secondo** DATA: 60 s, soglia del muto scesa da 900 a 180 s |
| pacchetti persi | **0 su 4** |

**Trappola trovata strada facendo, e vale per tutto il repo**: il gestore
dell'upload OTA non chiamava `Update.abort()` sul caso `UPLOAD_FILE_ABORTED`,
quindi **un solo upload caduto a metà rendeva la scheda non più aggiornabile
via rete, per sempre**. È la spiegazione retroattiva del "su `MeteoNode_C3`
l'OTA non è mai riuscito" annotato qui sopra il 2026-08-22. Dettagli e
correzione in `CLAUDE.md`.

---

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

### `EnvNode_C3` come hub provvisorio (fatto il 2026-08-23, `v4`)

`MeteoHub_S3` non esiste ancora come hub — è solo il bring-up del pannello — ma
il nodo ha bisogno di qualcuno che lo ascolti *prima* di poter dormire. Il
posto naturale è `projects/EnvNode_C3/`: è l'unica scheda di casa sempre accesa,
in funzione da oltre dieci giorni, con orologio NTP, microSD e web UI.

Non è solo comodità. **Il deep sleep uccide lo storico del nodo**: i 720 slot in
RAM e la previsione a 3 ore di `forecast.h` si azzererebbero ad ogni risveglio,
cioè ogni cinque minuti. Quella serie *deve* stare su una scheda sempre accesa
con un orologio vero. Ed è anche il banco di prova del lato hub di `EspNowLink`,
finora esercitato solo dalle demo: le modifiche che servono qui sono le stesse
che vorrà `MeteoHub_S3`.

**Cosa c'è** (`remote_nodes.h/.cpp`, ~230 righe, più tre rotte in `web_ui.cpp`):
ricezione dei DATA, tabella dei nodi in RAM con valori/cadenza/pacchetti persi/
riavvii, rilevamento del nodo muto, pagina `/nodi` con pairing e stato,
`/api/nodi` e `/api/pairing`. Costo: **+25 KB di flash** (63% → 64% della
partizione) e +1,1 KB di RAM globale.

**Scelte da conoscere**:

- **Canale `ESPNOW_LINK_CHANNEL_CURRENT` (0), non un numero esplicito.** È il
  motivo per cui l'ESP-NOW funziona senza toccare il router: questa scheda è su
  un AP, il canale lo detta lui, e `esp_wifi_set_channel()` su una STA connessa
  non serve o fa danni. Con lo 0 i peer sono registrati sul "canale corrente" e
  seguono l'AP da soli, anche se il WiFi si connette *dopo* `remote_begin()`.
  Resta vero che tutti i nodi devono stare sul canale dell'AP: `/api/nodi` lo
  riporta apposta, perché un nodo che dorme senza WiFi dovrà impostarlo a mano.
- **La soglia del "muto" è osservata, non configurata.** Il nodo decide la
  propria cadenza dalla sua pagina (2-3600 s): duplicare quel numero sull'hub
  sarebbe solo un modo per andare fuori sincrono. Si misura l'intervallo fra un
  DATA e il successivo e si dichiara muto dopo ~2,5 intervalli, con clamp a
  [90 s, 2 h]. I delta a cavallo di un riavvio del nodo, o più lunghi di 6 ore,
  non entrano nella media: senza quel filtro una notte di silenzio alzerebbe la
  soglia a giorni e il muto non scatterebbe mai più.
- **Il registro dei nodi è persistito in NVS** (fatto il 2026-08-23, `v7`), che
  era il prerequisito della Fase 4. Si salvano **solo MAC, tipo e nome**: il
  MAC è l'identità vera, bruciata nel chip e stabile ai riflash. Al boot i nodi
  tornano nel driver con `Link_Hub_AddPeer()`, quindi i loro DATA arrivano
  **anche a finestra di pairing chiusa** — che è ciò che serviva al deep sleep,
  dove un nodo può svegliarsi molto dopo i 5 minuti di finestra.
  **I valori non si salvano**, di proposito: dopo un riavvio l'hub mostrerebbe
  come "attuale" una lettura vecchia di giorni, cioè il guasto che il
  rilevamento del nodo muto serve a evitare. Verificato sull'hardware: al primo
  affaccio dopo il riavvio il nodo c'è con `pacchetti: 0`, `dati: false`,
  `valori: null`, e si riempie al primo pacchetto vero.
- **"Dimentica nodo"** (`POST /api/nodi/dimentica?mac=…`, pulsante su `/nodi` e
  in dashboard): toglie il nodo da libreria, RAM e NVS. Serve perché l'identità
  è il MAC — sostituendo una scheda, quella vecchia resterebbe in elenco per
  sempre come nodo muto, cioè un allarme falso permanente. Rovescio della
  medaglia: se il nodo dimenticato è ancora acceso e si crede associato non
  manda più HELLO, quindi per riprenderlo serve una finestra di pairing aperta
  o un suo riavvio.
- **La finestra di pairing si riapre da sola per 5 minuti ad ogni avvio**, ora
  solo per i nodi non ancora in elenco.
- **In pairing l'hub adotta anche un DATA unicast da un nodo sconosciuto**
  (correzione a `libraries/EspNowLink`, 2026-08-23). Senza, dopo un riavvio
  dell'hub il nodo — che si crede ancora associato e quindi **non manda più
  HELLO** — restava invisibile per sempre, e in modo **silenzioso da entrambe
  le parti**: l'ACK di ESP-NOW è di livello radio e arriva comunque, quindi il
  nodo contava i propri invii come riusciti (visti 15 su 15, zero falliti,
  pagina del nodo tutta verde) mentre l'hub non mostrava niente. Verificato dal
  vivo: caricato l'hub corretto **senza toccare il nodo**, che era proprio in
  quello stato, è stato riadottato alla trasmissione successiva (61 s) con
  dentro già i valori di quel pacchetto.
- **Due interfacce, di proposito.** `/nodi` è una pagina a sé **in PROGMEM**,
  quindi raggiungibile anche con una `dashboard.html` vecchia o rotta sulla SD:
  è la via di recupero, come `/dashboard-upload`. La dashboard personalizzata ha
  in più la card "Nodi remoti" (caricata sulla SD il 2026-08-23), che rilegge
  `/api/nodi` **un giro su tre** (15 s) invece che ad ogni tick: il WebServer è
  sincrono, e raddoppiare le richieste per una lista che cambia ogni minuto
  sarebbe tempo tolto a campionamento, SD e OTA. Quando serve reagire subito ci
  pensano i contatori `nodi`/`nodi_online`/`pairing` che `/api/stato` porta
  già, e che forzano un rilettura immediata quando cambiano.
- **Il nome del nodo arriva dalla radio**, quindi nella dashboard va scritto
  come testo e mai come markup: sedici caratteri scelti da chiunque sia a tiro
  d'antenna non sono un posto dove fidarsi.
- **Compilare ora vuole `--libraries libraries`**, che per `EnvNode_C3` prima
  non serviva.

**Fatto anche il log su SD dei valori remoti** (`v9`), in
`/nodi/<NOME>/AAAA-MM-GG.csv` — cartella separata da `/logs`, che ha una
scansione che si aspetta solo file, e che resta "quello che questa scheda
misura" contro "quello che le viene raccontato". Colonne
`ts_iso,ts_unix,fonte_ora,mac,seq,temp_c,hum_pct,press_hpa,batt_mv`: il MAC in
ogni riga perché il nome di un nodo può cambiare mentre il MAC no, `seq` perché
i pacchetti persi si vedano, e campo **vuoto** invece di zero per un valore che
il nodo non ha potuto misurare. Elenco e download da `/nodi`, caricati solo su
richiesta per non scansionare la card ad ogni polling.

**Da fare qui, quando si torna sopra**: spostamento di `forecast.h` dal nodo
all'hub, e i grafici dei nodi remoti nella dashboard (oggi c'è il download del
CSV, non ancora una curva).

### Fase 1 bis — invio ESP-NOW dal nodo (fatto il 2026-08-23, `v3`)

`projects/MeteoNode_C3/hub_link.h/.cpp`, gemello di quello del nodo camera ma
senza comandi. Il nodo manda un DATA ad ogni ciclo di misura (60 s di default,
è lo stesso intervallo già impostabile da pagina).

- **Canale**: `ESPNOW_LINK_CHANNEL_CURRENT` finché è connesso all'AP, canale
  fisso 6 come ripiego se l'AP non c'è. Quest'ultimo ramo oggi è solo una rete
  di sicurezza ma **diventerà la strada normale col deep sleep**, quando il
  nodo non si connetterà più al WiFi.
- **`value[0..2]` = °C, %RH, hPa**, con `LINK_NODE_SENSOR_TEMPERATURE` riusato
  invece di aggiungere un tipo nuovo in coda all'enum: finché i tipi di nodo
  sono due, cambiare la libreria condivisa non porta niente. Rimandabile.
- **La pressione si trasmette GREZZA**, non riportata al livello del mare: la
  correzione dipende dall'altitudine, che su questo nodo è ancora il default da
  40 m mai calibrato, e trasmettere il valore corretto scriverebbe un errore
  sistematico dentro lo storico dell'hub per sempre. Il trend — cioè la
  previsione — non cambia in nessuno dei due casi, è un offset costante.
- **Si trasmette anche una lettura fallita**, con NAN sui canali mancanti: "sono
  vivo ma il sensore non risponde" è una informazione, il silenzio no — da fuori
  sarebbe indistinguibile da un nodo morto, che è esattamente ciò che l'hub sta
  cercando di riconoscere. L'hub li serve come `null` (vedi la trappola del
  `String(NAN)` in `CLAUDE.md`).
- La pagina del nodo mostra stato di associazione, canale reale e contatori
  inviati/falliti: sono l'unico modo di accorgersi che la radio ha smesso di
  consegnare mentre il resto della pagina sembra a posto.
- **Compilare ora vuole `--libraries libraries`** anche per questo sketch:
  ```
  arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32C3:PartitionScheme=min_spiffs" --libraries libraries projects/MeteoNode_C3
  ```

**Da fare qui**: `battery_mv` resta 0 finché non c'è il partitore, e il
`forecast.h` andrà spostato sull'hub quando il nodo dormirà (la sua RAM si
azzera ad ogni risveglio).

### Secondo nodo su ESP32 "classico" (2026-08-23, `v4`)

Stesso sketch `projects/MeteoNode_C3/`, **una sola copia**, con pin, nome nodo,
hostname OTA e guardia della Serial scelti a compile-time da
`#if defined(CONFIG_IDF_TARGET_ESP32)`. Due cartelle gemelle sarebbero
divergute al primo bugfix; il prezzo è che il nome della cartella dice `_C3` ma
ci gira anche un ESP32 classico.

**Cablaggio sulla DOIT ESP32 DevKit v1** (provato e funzionante):

| Filo del modulo | Pin | GPIO |
|---|---|---|
| SDA | D21 | 21 |
| SCL | D22 | 22 |
| VCC (**commutato**) | D26 | 26 |
| GND | GND | — |

GPIO26 perché è RTC-capable (servirà a `gpio_hold_en()` col deep sleep), non è
di strapping ed è libero.

**Il motivo per cui i pin non potevano restare quelli della XIAO**: sull'ESP32
classico i **GPIO6-11 sono la flash SPI**, cioè proprio il GPIO6 che sulla XIAO
fa da SDA. Gli stessi numeri, sull'altro chip, non sono liberi ma fatali. Fuori
uso anche GPIO0/2/12/15 (strapping) e GPIO34-39 (solo ingresso). Il partitore
della batteria, quando ci sarà, va su GPIO35 (ADC1): **non** sul GPIO3, che lì
è la RX della UART0.

**`Serial.setTxTimeoutMs(0)` non compilava**: sull'ESP32 classico la `Serial` è
una UART vera, non la CDC USB, e quel metodo non esiste. Ora è dietro
`#if ARDUINO_USB_CDC_ON_BOOT`, che era già la regola scritta in `CLAUDE.md` ma
non applicata in questo sketch.

**Compilazione** (la board DOIT non espone l'opzione `PartitionScheme`: è fissa
a `default`, 1280 kB per app, con OTA):

```
arduino-cli compile --fqbn "esp32:esp32:esp32doit-devkit-v1" --libraries libraries projects/MeteoNode_C3
```

Occupazione: **87% della partizione app** (1.141.972 byte su 1.310.720). Ci sta,
ma è stretto: se questo sketch cresce ancora, su questa board servirà una
tabella delle partizioni su misura.

**Risultati**: sensore riconosciuto subito (AHT20 a 0x38, BMP280 a **0x77**,
chip ID 0x58), letture coerenti con l'altro nodo a pochi centimetri, ESP-NOW
associato all'hub **immediatamente**, zero pacchetti persi. Vedi anche
l'aggiornamento al "Limite noto" in `CLAUDE.md`: **il problema documentato
riguardava l'hub S3, non l'ESP32 classico come nodo.**

**Bug trovato grazie a questa prova** (`libraries/EspNowLink`): l'hub
**non aggiornava mai nome e tipo di un peer dopo il primo messaggio**. La
scheda aveva addosso `Link_Node_Demo` e si era associata come "TempTest"; dopo
averla riprogrammata come nodo meteo, l'hub ha continuato a chiamarla TempTest
**mostrando temperatura, umidità e pressione vere** — il modo peggiore di
sbagliare, perché tutto sembra funzionare. Il tipo conta anche più del nome:
la UI ci sceglie le unità di misura. Ora si aggiornano ad ogni messaggio, e
quando cambiano viene riscritta anche la NVS.

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

**Stato al 2026-08-23 sera — il ciclo di sonno FUNZIONA su hardware, a
batteria** (`MeteoNode_C3` `v9`). Fatto:

- ~~MAC dell'hub + canale nella RTC memory, e DATA diretto al risveglio~~ —
  fatto, ed è servito `Link_Node_ResumeWithHub()` nella libreria condivisa. Il
  fallback su HELLO resta, per la prima volta e per quando l'hub cambia.
- Finestra di veglia di 5 minuti ad ogni accensione vera (WiFi + OTA): senza,
  un nodo che dorme non si può più aggiornare né spegnere da remoto.
- Uscita di sicurezza dopo cinque risvegli senza consegna, che riaccende tutto.
- Contatori dei risvegli in **NVS** e non in RTC memory, che il power-cycle
  cancella — cioè proprio l'operazione con cui si recupera il nodo.

**La cosa che il piano non aveva previsto, ed è costata la serata**: il `seq` di
`EspNowLink` vive in RAM, quindi ogni risveglio ripartiva da zero e l'hub —
che scarta i DATA con `seq` uguale all'ultimo visto — ne accettava **uno** e
buttava tutti gli altri. Diciannove risvegli, diciannove invii confermati dal
nodo, uno solo arrivato. Risolto con `Link_Node_SetSeq()`/`GetSeq()` e il seq
conservato in RTC memory. Le trappole complete stanno in `CLAUDE.md`, sezione
"Deep sleep".

**La prima notte a batteria, misurata (2026-08-24)** — `v9`, cadenza 60 s,
senza `gpio_hold_en()`. Letta dai CSV dell'hub (`/nodi/MeteoNode/`):

| | |
|---|---|
| finestra continua | 2026-08-23 21:11:51 → 2026-08-24 17:36:34 |
| durata | **20,41 h** |
| pacchetti attesi / scritti | 1212 / **1212** — zero buchi, zero salti di `seq`, zero riavvii |
| ciclo reale | **60,68 s** su 60 s di sonno → veglia completa **~0,68 s**, duty cycle 1,1 % |
| batteria (multimetro, a riposo) | **4,18 V → 4,12 V**, cioè −60 mV in 20,41 h |

I 60 mV sono l'unico dato di consumo che esiste, ed è **un solo segmento della
curva**, per giunta nel tratto alto dove un litio scende in fretta: non basta a
stimare l'autonomia, e non va spacciato per una misura di capacità. Serve una
serie di punti — o il partitore, che è il modo giusto.

**Ancora da fare su questa fase**:

- ~~**`gpio_hold_en(D3/GPIO5)` + `gpio_deep_sleep_hold_en()` NON sono ancora nel
  codice**~~ — **fatto il 2026-08-24 in `MeteoNode_C3` `v10`**, insieme al
  rilascio dell'hold in `sensorPower(true)` (senza, al risveglio il pin resta
  inchiodato basso e il sensore non si riaccende più). La cadenza di default è
  tornata a 300 s nello stesso firmware. Quanto valga davvero in corrente resta
  **da misurare**: nei dati non si vede nulla, né prima né dopo.
- **Il partitore della batteria non è cablato** (`BATTERY_ADC_ENABLED 0`, D1 /
  GPIO3 libero): niente cutoff di sottotensione e `battery_mv` resta 0. Finché
  è così, un nodo a batteria va guardato a vista.
- **L'autonomia non è ancora misurata davvero.** La stima di 6-12 mesi qui
  sotto vale con il GPIO tenuto basso e a intervallo di 5 minuti — cioè la
  configurazione che esiste solo da `v10`. Quello che si sa è la tabella qui
  sopra: 60 mV in 20,4 h nella configurazione *sbagliata*. Il confronto utile
  è ripetere la stessa misura, stessa durata e stessa batteria, con `v10` a
  300 s: due segmenti confrontabili valgono più di una stima.
  - **Aggiornamento del 2026-08-26**: i segmenti a `v10` sono ora due
    (24,93 h e 23,43 h, −20 mV ciascuno), ma **nessuno dei due è pulito** —
    contengono uno e due interventi della rete di sicurezza, e una finestra di
    veglia da 5 minuti costa da sola più di un giorno di funzionamento
    regolare. Finché il canale non è risolto (Fase 9), una misura di scarica
    misura anche i riavvii. Numeri e conti nell'aggiornamento in testa al file.
  - **Aggiornamento del 2026-08-28**: la riserva qui sopra **cade**. Con la
    Fase 9 caricata, 23,7 ore di funzionamento non hanno avuto nemmeno un
    intervento della rete di sicurezza (285 campioni su 285, zero riavvii),
    e nel frattempo l’AP è passato dal canale 1 al 13 senza che il nodo
    perdesse un pacchetto. **Da qui in poi un segmento di scarica misura
    la scarica e basta**: il prossimo è finalmente confrontabile con i due
    precedenti, che erano sporchi.
- Spostare `forecast.h` sull'hub: la RAM del nodo si azzera ad ogni risveglio,
  quindi lo storico a 3 ore per il trend non può stare su di lui.

Sveglia ogni ~5 min, legge, manda, si riaddormenta. Con ~44 µA dormendo e ~0,5 s
di radio a risveglio l'ordine di grandezza è **6–12 mesi** su una 18650 reale.

**Modifiche necessarie a `libraries/EspNowLink`** (retrocompatibili: la libreria
è condivisa col nodo camera e con le demo):

- il **nodo** deve memorizzare MAC dell'hub + canale nella **RTC memory**, che
  sopravvive al deep sleep, e mandare DATA diretto al risveglio, ricadendo su
  HELLO solo dopo N fallimenti. Oggi ripartirebbe da HELLO in broadcast ad ogni
  risveglio, con l'hub costretto a stare sempre in pairing mode;
- ~~l'**hub** deve persistere il registro peer in **NVS**~~ — **fatto il
  2026-08-23** su `EnvNode_C3` `v7` (vedi la sezione dell'hub provvisorio).
  Restano da riportare su `MeteoHub_S3` quando esisterà: `remote_nodes.*` è già
  scritto per essere copiato, e le due funzioni che gli servono
  (`Link_Hub_AddPeer()` / `Link_Hub_ForgetPeer()`) stanno nella libreria
  condivisa, non nello sketch.

**Canale ESP-NOW**: l'hub sta sul WiFi del router, quindi il canale glielo impone
l'AP e tutti devono usare quello (`Link_InitEx` con il canale dell'AP, non
`Link_Init`). ~~**Fissare il canale 2,4 GHz nel router**~~ — **valutato e
scartato il 2026-08-25**, dopo che il caso si è presentato davvero: toglierebbe
all'AP la scelta automatica per risolvere il problema di un solo dispositivo.
La strada scelta è farlo cercare al nodo, vedi **Fase 9**. Resta vero il
perché: altrimenti un giorno
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

### Fase 7 — configurare i nodi dall'hub (idea del 2026-08-24, da fare dopo `MeteoHub_S3`)

**L'idea**: l'utente apre la UI dell'hub, imposta la cadenza di un nodo (o delle
fasce orarie con cadenze diverse), e il nodo la riceve **al suo prossimo
risveglio**, senza toccarlo. Oggi per cambiare quel numero bisogna staccare la
batteria, aspettare la finestra di veglia e usare la pagina del nodo — cioè
l'unica cosa che il deep sleep rende difficile è proprio configurarlo.

**Rimandata di proposito a dopo `MeteoHub_S3`**: la UI dove vivono queste
impostazioni è quella dell'hub vero, e costruirla ora su `EnvNode_C3` — che fa
da hub *provvisorio* — vorrebbe dire scriverla due volte.

**L'ACK di ESP-NOW NON può portare i settings.** È di livello MAC: lo genera il
driver, e ciò che arriva all'applicazione (`onSent()`) è solo consegnato/non
consegnato. Serve un COMMAND separato, con il nodo che resta in ascolto una
finestra dopo aver mandato il DATA. Per l'utente è identico; per il nodo no,
perché quella finestra è radio in RX.

**Il protocollo non va toccato**, ed è la ragione per cui la cosa è fattibile a
poco prezzo: in un COMMAND i tre `value[]` sono liberi (servono a
temperatura/umidità/pressione solo nei DATA), quindi `value[0]` = secondi di
sonno e ne restano due. Importante che basti: `link_peer.cpp:28` valida con
`len != sizeof(link_message_t)`, quindi allargare la struct farebbe scartare i
pacchetti da ogni firmware non aggiornato — il nodo camera compreso.

**Cosa manca davvero** (poco):

1. `hub_link.cpp:19` sul nodo scarta tutto ciò che non è WELCOME: va accettato
   anche COMMAND (copiare e basta — quel callback gira sul task del driver WiFi).
2. `cicloRisveglio()` va a `vaiADormire()` subito dopo l'invio
   (`MeteoNode_C3.ino:1264`): la finestra di ascolto va in mezzo.
3. Lato hub, una coda "comando pendente per questo MAC" svuotata da
   `remote_loop()` — **mai** dal callback di ricezione (stessa regola del
   WELCOME, vedi `CLAUDE.md`).

`Link_Hub_SendCommand()` (`link_hub.cpp:288`) esiste già e funziona.

**Il costo è energetico, e decide il disegno.** Ordini di grandezza per ciclo,
con la veglia **misurata** (0,68 s il 2026-08-24) e le correnti **stimate, mai
misurate** (~70 mA in veglia/RX, ~44 µA dormendo), cadenza 300 s:

| | carica per ciclo | sul totale |
|---|---|---|
| veglia attuale (0,68 s) | ~47,6 mAs | 78 % |
| sonno (300 s) | ~13,2 mAs | 22 % |
| **finestra RX di 300 ms, ogni ciclo** | **+21 mAs** | **+34 %** → autonomia −26 % |
| finestra RX 1 ciclo su 12 (~1 h) | +1,75 mAs | +2,9 % |
| finestra RX 1 ciclo su 288 (~1 giorno) | +0,07 mAs | +0,12 % |

Da leggere così: **ascoltare ad ogni ciclo è insostenibile**, ma già una volta
all'ora recupera il 97 % del risparmio possibile. Passare da un'ora a un giorno
recupera l'ultimo 3 % **pagandolo 24 volte in attesa**. Quindi la scelta
ragionevole è **raro e lungo** — una finestra generosa, presa di rado — con
default intorno all'ora, non al giorno. L'urgenza ha già la sua strada: il
power-cycle, che apre 5 minuti di veglia con WiFi e OTA.

**Nessuno dei due lati deve sincronizzarsi**, ed è la parte elegante: l'hub
tiene il comando in coda e lo rispedisce ad ogni DATA finché non risulta
applicato; quasi tutti quei tentativi cadono nel vuoto perché il nodo dorme già,
ma **il costo di provare sta tutto dalla parte alimentata a muro**. Attenzione a
usare l'invio semplice e non `sendReliable()` per questi tentativi: 3 ritentativi
x 300 ms sono fino a 900 ms bloccanti nel `loop()` di un hub che ha il
WebServer sincrono.

**Le fasce orarie stanno TUTTE sull'hub**, e il nodo non deve sapere che ora è:
l'hub, che ha NTP, calcola "quanto devi dormire adesso" e manda un numero.
Niente fuso, niente ora legale, nessuna tabella da portare attraverso il sonno
in RTC memory — tutte cose fragili su un nodo il cui orologio è una stima che il
power-cycle azzera. Conseguenza pratica: **la Fase 7 si può fare in due passi**,
e il secondo non tocca il firmware del nodo. Prima il comando one-shot "cambia
cadenza"; poi le fasce, che sono solo logica sull'hub.

**Le trappole da mettere in conto**:

- **Chi comanda?** Oggi la cadenza si imposta anche dalla pagina del nodo. Con
  due padroni si va fuori sincrono di sicuro: o l'hub è la fonte di verità e la
  pagina del nodo diventa di sola lettura, o il nodo ha un flag "segui l'hub"
  visibile da entrambe le parti.
- **NVS**: il valore ricevuto si scrive **solo se cambia**, o si scrive la flash
  ad ogni ciclo. Il valore attivo sta in RTC memory, che è dove il risveglio lo
  legge.
- **Una cadenza lunga è un semi-brick**: se l'hub dice 3600 s, per correggere si
  aspetta un'ora. Il clamp c'è già (`INTERVALLO_MIN_S`/`MAX_S`, 2..3600) e la
  via di rientro pure, ma la UI deve dire chiaramente che si sta scegliendo
  anche quanto ci vorrà a cambiare idea. Vale doppio per il "1 ciclo su N": se
  N arriva a sua volta per comando, sbagliarlo si paga N cicli.
- **Hub assente o sostituito**: il nodo conserva l'ultimo valore e continua. Non
  deve esistere uno stato "aspetto istruzioni".
- **Conferma**: l'hub misura già la cadenza reale dei nodi (media mobile,
  `remote_nodes.cpp:218`), quindi vede da sé quando un nodo cambia passo — ma ci
  mette ~4 cicli. Finché non c'è un ack esplicito, la UI deve mostrare "in
  attesa di conferma" e non fingere che il comando sia immediato.

**Nota sui termini**: sul nodo **non esiste un tempo di veglia da configurare**.
La veglia dura quanto serve (0,68 s misurati) e finisce da sola; l'unico
parametro è il periodo. L'unica veglia configurabile è quella dei 5 minuti con
WiFi/OTA dopo un'accensione vera (`VEGLIA_MS`), che è la via di recupero e
conviene lasciare com'è.

### Fase 8 — l'orologio senza internet (idea del 2026-08-25, nulla di fatto)

Nata da una domanda giusta: *internet serve solo a sincronizzare l'ora?* Sì.
Dashboard, OTA, ESP-NOW e log su SD vivono tutti sulla **rete locale**, e
funzionerebbero identici col router scollegato da internet. Le due dipendenze
vanno tenute separate perché si tolgono in modi diversi: **internet** lo si
toglie in tre modi, il **router** solo diventando SoftAP.

**Il punto che decide tutto**: l'ora impostata a mano **non sopravvive a un
distacco di corrente**, perché non c'è RTC tamponato (scelta confermata, niente
DS3231). Al riavvio si torna alla stima da build-time. Quindi l'orario manuale
non toglie una dipendenza: la sposta da internet **a una persona**, che deve
essere lì dopo ogni blackout e accorgersene. È il motivo per cui NTP c'è.

Le tre strade che tolgono internet *senza* metterci una persona:

1. **Il router come server NTP.** Molti router lo fanno. Una riga in
   `rtc_time.cpp`: il gateway come primo server in `configTzTime()`, i due
   pubblici come fallback. Automatico, e se il router non risponde il
   comportamento è quello di oggi. **Da provare per prima**: costa quasi nulla
   e potrebbe chiudere la questione da sola.
2. **L'ora la manda il browser**: la pagina, ad ogni caricamento, fa una POST
   con il proprio orologio — chi apre la dashboard semina l'hub senza saperlo.
   Copre anche il caso "router senza NTP". **Il JS va messo anche nelle pagine
   in PROGMEM** (`/nodi`, `/dashboard-upload`), non solo nella dashboard sulla
   SD, che è vecchia finché non la si ricarica a mano e quindi non si può
   assumere aggiornata.
3. **Impostazione manuale esplicita** nelle settings: utile come ultima risorsa
   e per correggere, ma è quella che invecchia peggio ed è l'unica che richiede
   che qualcuno se ne ricordi. La 2 costa quasi uguale ed è più furba.

**Cosa tocca, se si fa**: `settimeofday()`, un endpoint, e un **terzo valore per
`fonte_ora`** accanto a `NTP`/`STIMA`. Il progetto è già predisposto — quella
colonna esiste apposta per dire quanto fidarsi di un timestamp, e
`orario_registrabile()` è già il punto unico dove si decide se un dato è
databile. Vale per `rtc_time.*`, quindi si propaga per copia a `MeteoHub_S3` e
`Timelapse_XIAO`.

**Due numeri da non dare per buoni**:

- **la deriva senza sync** è quella del cristallo del C3, dell'ordine di
  qualche secondo al giorno. Per campioni al minuto è irrilevante per
  settimane, ma **va misurata**, non stimata;
- si può **salvare l'ultimo orario noto in NVS** ogni tanto: dopo un blackout
  la stima riparte dall'ultimo istante vissuto invece che dalla data di
  compilazione. Resta indietro della durata del guasto — quindi resta `STIMA` —
  ma toglie la patologia peggiore, cioè che la stima da build-time sia
  **identica ad ogni riavvio** (è ciò che il 2026-08-23 ha lasciato dieci righe
  con lo stesso timestamp `10:48:06`).

**Se invece l'obiettivo fosse l'indipendenza dal ROUTER**, l'architettura è
un'altra: hub in **SoftAP**, il telefono si collega direttamente a lui, ESP-NOW
su canale fisso con `Link_Init()` invece di seguire l'AP. Effetto collaterale
notevole: sparisce il problema del **canale che cambia da solo**, che il
2026-08-25 ha fatto riavviare il nodo a batteria. Prezzo: niente NTP (serve la
2) e il telefono esce dalla rete di casa per parlare con l'hub. Ha senso per
`MeteoHub_S3` se finirà dove non arriva il WiFi di casa; non per `EnvNode_C3`,
che sta in casa.

### Fase 9 — il nodo che cerca il canale da solo (FATTA il 2026-08-27, confermata sul campo il 2026-08-28)

> **Fatta.** Quello che segue è il ragionamento con cui è stata decisa, tenuto
> com’era per il perché delle scelte; **cosa è stato scritto davvero** sta
> nell’aggiornamento del 2026-08-27 (4) in testa al file, e **come si è
> comportata sul campo** in quello del 2026-08-28. La tabella dei costi qui
> sotto ha retto: il caso reale (AP passato da 1 a 13) è costato ~2,6 s di
> radio invece di 5 minuti di WiFi, e zero campioni.

Nasce dal guasto del 2026-08-25: l'AP ha cambiato canale da solo (da 6 a 1) e
il nodo a batteria, che tiene il canale in RTC memory, è diventato sordo finché
la rete di sicurezza non l'ha riavviato — 25 minuti di dati persi.

**Fissare il canale nel router è stato valutato e scartato** (2026-08-25):
toglie all'AP la scelta automatica, che serve a scansare le reti dei vicini, e
in un condominio denso peggiorerebbe il WiFi di casa per risolvere il problema
di **un solo** dispositivo. L'hub e il nodo a muro seguono l'AP da soli
riassociandosi; il sordo è solo chi dorme.

**L'idea**: il nodo sa già quando non è stato consegnato — è lo stesso segnale
che alimenta il contatore dei risvegli muti (`hub_sent_ok()`, cioè l'ACK di
livello radio, che è esattamente il segnale giusto per "sono sul canale
sbagliato"). Oggi quella conoscenza porta a un'unica reazione, la più cara:
dopo cinque risvegli muti si riavvia e resta sveglio 5 minuti con il WiFi.
Invece, al risveglio, se il DATA non viene consegnato: cambiare canale e
ritentare — prima 1, 6, 11 (i non sovrapposti, dove sta la quasi totalità dei
router domestici), poi eventualmente tutti. Al primo che risponde, salvarlo in
RTC memory **e in NVS** e tornare a dormire.

| | ripristino di oggi | scansione dei canali |
|---|---|---|
| radio accesa | 5 min di WiFi | ~1,5 s |
| carica spesa | ~7 mAh | ~0,03 mAh |
| in risvegli normali equivalenti | **~450** (≈1,6 giorni di budget) | **~2** |
| dati persi | **25 minuti** | nessuno |
| quando si paga | ad ogni cambio di canale | solo se il primo invio fallisce |

~200x più economico, e recupera **dentro lo stesso risveglio**. **Non**
sostituisce la rete di sicurezza: resta sotto, per quando l'hub è davvero giù e
nessun canale risponde.

**Cosa costa, in onestà**:

- serve una funzione in più in `libraries/EspNowLink` per cambiare canale a
  caldo (`esp_wifi_set_channel()` più l'aggiornamento del peer): oggi il canale
  si sceglie **solo** a `Link_InitEx()`. Una ventina di righe, ma è la libreria
  **condivisa** — la usano anche l'hub e il nodo camera, quindi va fatta
  retrocompatibile e va ricompilato tutto;
- risolve il canale che cambia, **non** l'hub che cambia MAC (scheda
  sostituita): per quello resta il fallback su HELLO, che c'è già;
- la scansione va **limitata**: un tentativo per canale con timeout corto, o il
  costo del caso peggiore mangia il vantaggio.

**Alternativa che chiude lo stesso problema da un'altra parte**: l'hub in
SoftAP con canale fisso (vedi Fase 8) — lì il canale non cambia mai, per
costruzione.

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
