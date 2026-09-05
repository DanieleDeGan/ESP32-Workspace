# Trappole hardware — le lezioni gia' pagate

Sei difetti trovati su hardware vero fra il 2026-08-22 e il 2026-08-31, ognuno
costato da mezza giornata a una serata. Valgono per **ogni sketch nuovo** di
questo repo, non solo per il progetto su cui sono stati scoperti: sono la
ragione per cui certe righe apparentemente inutili non si tolgono.

Il riassunto in tre righe l'una sta in `CLAUDE.md` alla radice; qui c'e' il
perche' lungo, il sintomo con cui si presentano e — la parte che serve davvero
— **perche' il sintomo non somiglia alla causa**.

| trappola | dove morde |
|---|---|
| [deep sleep](#deep-sleep--quattro-trappole-trovate-su-hardware) | ogni nodo a batteria |
| [`Update.abort()`](#updateabort-nel-caso-upload_file_aborted--obbligatorio) | ogni sketch con OTA |
| [`streamFile()`](#streamfile-e-le-risposte-grosse--un-client-morto-ferma-la-scheda) | ogni handler che manda un file |
| [scritture su microSD](#una-scrittura-su-microsd-che-nessuno-controlla-e-un-logger-che-mente) | ogni logger |
| [default NVS](#aggiornare-un-nodo-un-default-nuovo-vince-su-una-chiave-nvs-mai-scritta) | ogni OTA su una scheda in funzione |
| [`Serial.setTxTimeoutMs(0)`](#serialsettxtimeoutms0--obbligatorio-su-c3-e-s3) | ogni sketch su C3/S3 |

## Deep sleep — quattro trappole trovate su hardware

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

## `Update.abort()` nel caso `UPLOAD_FILE_ABORTED` — obbligatorio

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

## `streamFile()` e le risposte grosse — un client morto ferma la scheda

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

## Una scrittura su microSD che nessuno controlla e' un logger che mente

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

## Aggiornare un nodo: un default nuovo vince su una chiave NVS mai scritta

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

## `Serial.setTxTimeoutMs(0)` — obbligatorio su C3 e S3

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
