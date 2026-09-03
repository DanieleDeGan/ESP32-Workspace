# Feature — il taccuino delle cose da fare

Il posto unico da cui attingere quando c'è voglia di aggiungere qualcosa. Prima
di questo documento le idee stavano sparse fra `docs/Stazione-Meteo.md` (le
"Fasi"), i commenti nel codice ("se un giorno la si volesse…") e le note di
lavoro: trovarle richiedeva ricordarsele, che è il modo migliore per riscoprire
tre volte la stessa cosa e per rifare un lavoro già scartato con buone ragioni.

**Come si usa**

- Si pesca da **Da fare**, si fa, si sposta la voce in **Fatte** con la data e
  il commit. Il racconto lungo di *come* è andata resta in
  `docs/Stazione-Meteo.md`: qui basta una riga.
- Un'idea nuova si scrive qui **anche se non si farà mai**. Costa tre righe e
  vale come promemoria del ragionamento.
- Quando una cosa si scarta, **non si cancella**: si sposta in **Valutate e
  scartate** con il perché. È la sezione più utile del documento — impedisce di
  riproporre a settembre quello che si è escluso ad agosto, e se le premesse
  cambiano si vede subito quale premessa era.
- Le stime di costo sono grossolane e servono solo a ordinare: **basso** = una
  sera, **medio** = un fine settimana, **alto** = più di così o tocca l'hardware.

**Stato dell'hardware al 2026-08-30** (per sapere quanto spazio c'è):
hub `MeteoHub_S3` a `v14`, flash app **41 %** usata (~1,9 MB liberi), heap
libero **220 kB**, microSD 14,9 GB liberi su 14,9.

---

## Riferimenti tecnici verificati

Numeri misurati davvero il **2026-08-30**, non stimati. Da rifare se passa
molto tempo: i servizi cambiano.

| servizio | protocollo | chiave | risposta | latenza |
|---|---|---|---|---|
| Open-Meteo previsioni (`api.open-meteo.com/v1/forecast`) | **HTTP**, niente TLS | **nessuna** | 450 B | 172 ms |
| Open-Meteo alba/tramonto (stesso endpoint, `daily=sunrise,sunset`) | HTTP | nessuna | 346 B | 172 ms |
| Open-Meteo qualità aria (`air-quality-api.open-meteo.com`) | HTTP | nessuna | 349 B | — |
| ntfy.sh (notifiche push) | risponde in HTTP (302 sulla root) | nessuna | — | **invio non ancora verificato** |

**Perché contano questi numeri**: niente TLS significa nessun handshake da
~40 kB di RAM, nessun certificato che scade e nessuna libreria in più; niente
chiave significa nessun segreto da tenere fuori dal repo pubblico; 450 byte si
parsano a mano cercando le sottostringhe, senza tirarsi dentro ArduinoJson.

Una previsione a 3 giorni costa quindi, su questo hardware, quanto una
richiesta HTTP qualsiasi — cioè quasi niente. È il motivo per cui le voci 7-9
sono classificate "costo medio" e non "alto".

---

## Da fare

### 1. Min/max del giorno per nodo — FATTA IN PARTE il 2026-08-31 (`v23`-`v25`)
**Fatto**: minimo e massimo di temperatura **delle ultime 24 ore** (non dalla
mezzanotte) sul pannello, nella pagina dettaglio, letti dall'anello di 48
mezz'ore che già serviva al grafico — quindi a costo zero di memoria.
**Resta da fare**: la finestra *dalla mezzanotte locale*, che è un'altra cosa e
ha l'avvertenza qui sotto; l'umidità; e gli stessi valori in dashboard.

**Cosa** (testo originale): minimo e massimo di temperatura (e umidità) dalla
mezzanotte, per ogni nodo, sul pannello e in dashboard.
**Perché qui**: l'hub riceve ogni lettura e la scrive, ma *"quanto ha fatto
oggi"* non esiste da nessuna parte — per saperlo bisogna scaricare un CSV. È il
dato che si guarda più spesso e l'unico che non c'è.
**Costo**: basso. Due campi in `RemoteNode`, azzerati al cambio di giorno.
**Dipendenze**: nessuna.
**Attenzione**: azzerare a mezzanotte **locale**, non UTC, e solo quando
l'orario è sincronizzato — altrimenti al primo boot senza NTP si registra un
minimo dell'anno 1970.

### 2. Riconoscere un sensore bloccato
**Cosa**: se un nodo manda lo stesso valore identico per N letture consecutive,
segnalarlo come **fermo** — stato diverso da *muto*.
**Perché qui**: oggi un nodo è considerato vivo se trasmette. Ma un AHT20 che
si inchioda continua a trasmettere quel valore per sempre, e da fuori sembra
perfettamente sano: è un guasto reale che **nessuna diagnostica attuale vede**.
La rete sa già dire "non parla"; non sa dire "parla ma non misura".
**Costo**: basso. Il confronto sta dove i valori arrivano già (`remote_nodes`).
**Dipendenze**: nessuna.
**Attenzione**: la soglia va scelta sul rumore vero del sensore. Un AHT20 in
una stanza chiusa di notte può davvero dare lo stesso decimo per parecchie
letture: guardare i CSV prima di fissare N, o si crea un allarme falso
ricorrente — che è peggio di nessun allarme.

### 3. Aggregati giornalieri su card
**Cosa**: `/nodi/<NOME>/riepilogo.csv`, una riga per giorno: min, max, media,
numero di campioni, buchi.
**Perché qui**: oggi ogni vista storica deve leggere i CSV interi. È il motivo
per cui il seeding legge solo la coda e il grafico si ferma a 24 h. Con gli
aggregati diventano possibili "l'ultimo mese" e "l'anno" a costo quasi zero.
**Costo**: medio. Sblocca le feature storiche future ed è il prerequisito
naturale della 8.
**Dipendenze**: nessuna.
**Attenzione**: la riga del giorno si chiude **alla rotazione del CSV**, non a
mezzanotte in punto — un riavvio a cavallo della mezzanotte non deve lasciare
un giorno senza riga né una riga scritta due volte.
**Nota**: è la stessa idea del "rollup giornaliero" già concordato per
`EnvNode_C3` il 2026-08-09 e mai fatto (vedi voce 12): se si fa qui, conviene
farla lì con lo stesso formato.

### 4. Backup e ripristino della configurazione
**Cosa**: un JSON scaricabile con pagine, registro nodi, messaggio attivo e
altitudine; e la strada inversa per ricaricarlo.
**Perché qui**: tutta la configurazione vive in NVS. Se si azzera — o la scheda
si guasta e va sostituita — quel lavoro è perso e va rifatto a mano voce per
voce. Oggi non esiste **nessun** modo di portarlo via.
**Costo**: basso.
**Dipendenze**: nessuna.
**Attenzione**: il ripristino deve rifiutare un file di una versione di blob
diversa invece di scriverlo a caso; e i MAC dei nodi vanno rimessi anche nel
driver (`Link_Hub_AddPeer`), non solo nel registro.

### 5. Alba e tramonto calcolati a bordo
**Cosa**: l'ora di alba e tramonto sul pannello, calcolata con la formula NOAA.
**Perché qui**: su un display che si guarda passando, "quanta luce resta" è
l'informazione giusta. E **non serve rete**: bastano latitudine, longitudine e
la data che l'hub ha già.
**Costo**: basso. ~50 righe, header-only e puro come `forecast.h`.
**Dipendenze**: nessuna. Una latitudine/longitudine da mettere in NVS accanto
all'altitudine, che è già lì.
**In più**: la stessa formula permetterebbe di legare le "ore di silenzio" del
pannello al buio vero invece che a un orario fisso.

### 6. QR code sul pannello per la dashboard
**Cosa**: un QR nell'angolo di una pagina, che apre la dashboard dell'hub.
**Perché qui**: gli IP sono DHCP e si spostano, `.local` **non risolve** dal PC
Windows di casa (annotato da tempo), e ogni volta si finisce a cercare
l'indirizzo. Il pannello è già in bianco e nero: un QR è esattamente ciò che sa
disegnare meglio.
**Costo**: medio-basso. Serve la libreria `QRCode` di ricmoo (~10 kB di flash,
è nel Library Manager e non dipende da nessun display: produce la matrice, il
disegno lo fa il nostro codice come per le icone).
**Dipendenze**: nessuna.
**Attenzione**: va rigenerato quando l'IP cambia, non solo al boot.

**Misure fatte il 2026-08-31** — servono a decidere *dove* può stare, ed è la
parte che rende la voce pronta da pescare:

- `http://192.168.1.73/` sono 20 byte → QR **versione 2, 25×25 moduli**. Sotto
  non si scende: togliendo `http://` molti telefoni non aprono più il browser.
- il pannello è 400 px su ~85 mm, cioè **4,7 px/mm**. Quindi:

  | px per modulo | lato del QR | modulo fisico | si legge |
  |---|---|---|---|
  | 2 | 58 px | 0,42 mm | al limite, da ~10 cm |
  | 3 | 87 px | 0,64 mm | comodo, da 20-30 cm |
  | 5 | 137 px | 1,06 mm | da mezzo metro |

- **nella pagina nodi non ci sta comodo**: con due nodi ogni blocco è alto 114
  px e ne usa 90, quindi restano 24 px sotto il trend e una fascia libera alta
  44 nella metà destra. Ci entra solo la versione da 58 px, che è quella al
  limite; per una da 87 bisogna togliere spazio ai dati.

**Le tre strade, valutate il 2026-08-31 e non scelte** (l'utente ha preferito
rimandare):

1. **pagina INFO dedicata** — QR da 137 px più IP, nome e versione. Non toglie
   un pixel ai dati, si aggiunge e ruota come le altre pagine, e il QR si legge
   da mezzo metro. È la strada coerente con la regola già scritta altrove: su
   e-ink il tempo è la dimensione in più.
2. **piccolo nel piede della pagina nodi** — il piede cresce da 30 a ~64 px e i
   nodi perdono 34 px di altezza. Sempre presente, ma da inquadrare da vicino.
3. **in fondo alla pagina dettaglio** — lì lo spazio per un QR da 87 px c'è già,
   ma si vede solo quando quella pagina è in mostra.

### 7. La previsione vera accanto alla tua
**Cosa**: previsione a 3 giorni da Open-Meteo su una pagina del pannello.
**Perché qui**: la parte interessante **non** è mostrarla. È che questo progetto
ha già una previsione — quella barometrica di `forecast.h` — e nessuno sa
quanto ci prenda. Registrando le due e confrontandole con quello che è poi
successo, *"la mia regola empirica vale qualcosa?"* diventa una domanda con una
risposta numerica. È la feature più interessante dell'elenco, ed è l'unica che
produce un dato che oggi non esiste.
**Costo**: medio.
**Dipendenze**: Open-Meteo (HTTP, nessuna chiave, 450 B).
**Attenzione**: leggi la nota sulle dipendenze esterne in fondo. La pagina deve
dire "non raggiungibile" invece di mostrare dati vecchi come se fossero freschi.

### 8. Notifica push quando qualcosa non va
**Cosa**: l'hub manda una riga al telefono (ntfy.sh) quando un nodo tace, la
card rifiuta righe, o `/api/salute` passa a *guasto*.
**Perché qui**: `/api/salute` sa già dire tutto — ma **solo a chi va a
guardare**. Il buco di dati del 24/08 è stato trovato giorni dopo, a mano. Una
diagnostica che richiede una persona non scatta quasi mai.
**Costo**: basso.
**Dipendenze**: ntfy.sh (nessun account). **Da verificare che l'invio funzioni
in HTTP semplice**: sulla root risponde 302, il che potrebbe voler dire
redirect a HTTPS.
**Attenzione**: mettere un tetto agli invii (uno per evento, non uno per giro
di `loop()`), o un nodo muto genera una notifica ogni cinque secondi.

### 9. Qualità dell'aria della zona
**Cosa**: PM2.5 e PM10 come pagina del pannello.
**Perché qui**: d'inverno in pianura è un dato che si guarda davvero.
**Costo**: basso **una volta fatta la 7** — stesso codice, altro URL.
**Dipendenze**: Open-Meteo air quality (HTTP, nessuna chiave, 349 B).

### 10. Pubblicare i dati su MQTT / Home Assistant
**Cosa**: l'hub pubblica le letture dei nodi su un broker MQTT, con discovery
per Home Assistant.
**Perché qui**: è la voce che **cambia di più cosa può fare la stazione**. I
nodi diventano entità della casa: storico lungo, automazioni, allarmi, grafici,
senza scrivere altro firmware. L'hub resta la fonte, HA diventa il posto dove i
dati vivono a lungo.
**Costo**: medio.
**Dipendenze**: un broker in casa (Mosquitto o quello di HA) e una libreria
MQTT — sarebbe la **prima dipendenza esterna del repo** oltre a quelle dei
sensori, contro la convenzione "tutto core o bundled".
**Attenzione**: MQTT non deve poter bloccare `loop()`. E resta vero che senza
broker la stazione deve funzionare identica: è un'uscita in più, non un pezzo
del percorso principale.

---

## Da fare — dall'analisi del codice del 2026-09-02

Una lettura sistematica di `MeteoHub_S3` `v43`, `MeteoNode_C3` `v15` e
`EspNowLink`, partendo dal codice invece che dalle idee. Il ragionamento esteso,
con i conti, le prove e le avvertenze, sta in **`docs/Proposte-2026-09-02.md`**:
qui, come da regola di questo taccuino, resta una riga per voce.

**Sei difetti trovati nel codice** (Parte 1 del documento). Non sono feature,
e tre di loro falsano numeri su cui poggerebbero le voci statistiche:

### 17. La cadenza appresa si inquina con i pacchetti persi — FATTA (`v44`)
`aggiornaDaLibreria()` impara l'intervallo da qualunque DATA con `seq`
crescente, buchi compresi: un pacchetto perso porta la cadenza da 300 a 375 s e
ci vogliono ~8 pacchetti per rientrare. Trascina con sé `sogliaMuto` (+3 min sul
rilevamento di un nodo morto), `nodoInRitardo()` e la cadenza dei refresh.
**Costo**: bassissimo. **Da fare prima di qualunque statistica.**

### 18. L'hub non misura il proprio tempo di giro
`loop_max_ms`/`loop_max_dove`/`loop_lenti` esistono su `EnvNode_C3` (spenta) e
non sull'hub, che è la scheda che blocca davvero (2630 ms per refresh, 20 s di
budget d'invio, decine di secondi di OTA). Oggi sa dire *che è ripartito*, non
*che è rimasto fermo*. **Costo**: basso, il codice si copia.

### 19. Il watchdog non è armato: `WDT_TASK` non può comparire
`app_reset_reason()` traduce tre cause di watchdog, ma né hub né nodo chiamano
`esp_task_wdt_add()`. Un `loop()` piantato lascia un pannello e-ink perfetto e
bistabile con dei numeri non più veri, e toglie insieme a sé tutta la
diagnostica. **Costo**: bassissimo. Timeout proposto 60 s (sopra i 2,6 s del
refresh e i ~30 s del caso peggiore di invio), watchdog sospeso durante l'OTA.

### 20. Il driver tiene UN dato per nodo: un `loop()` fermo perde letture
E le **addebita alla radio**, perché il buco nel `seq` finisce in `persi`. È lo
stesso guasto del 2026-08-24 su `EnvNode_C3` (456 s fermo, sette pacchetti
persi). Serve una coda FIFO in `EspNowLink` (12 slot = 564 byte) più un
contatore di traboccamento — *una coda che trabocca in silenzio è peggio del
difetto che risolve*. **Costo**: medio, tocca la libreria condivisa.

### 21. Due indicizzazioni fragili — FATTA (`v44`)
`s_ritardoDa[]` nello sketch è indicizzato per posizione e non viene compattato
da `remote_forget()` (il modulo compatta i suoi array paralleli, questo no); e
`r->persi += (seq - prec - 1)` non ha tetto, quindi un `seq` sporco dalla RTC
memory può inventare milioni di pacchetti persi in modo permanente.
**Costo**: bassissimo per entrambi.

### 40. Dopo un blackout l'hub puo' sparire per secondi mandando i WELCOME — FATTA (`v44`)
*(e' il sesto difetto del gruppo 17-21: il numero e' in coda perche' e' stato
trovato dopo, rileggendo `link_hub.cpp` per intero.)*
`Link_Hub_Poll()` manda un WELCOME **per ogni** peer in attesa nello stesso giro,
e `sendReliable()` blocca fino a ~1 s l'uno: con otto nodi che si riavviano
insieme — cioe' esattamente cosa succede a un blackout — il `loop()` puo'
restare fermo **otto secondi**, proprio mentre tutti i nodi trasmettono. E un
nodo irraggiungibile fa pagare quel secondo **ad ogni giro**, finche' non torna.
**Correzione**: un WELCOME per giro **e a turno**. Il solo `break` non basta e
peggiora le cose — il flag si pulisce solo se l'invio riesce, quindi ripartendo
sempre da zero un nodo spento a meta' associazione si prenderebbe l'unico invio
di ogni giro e **affamerebbe tutti gli altri**. Con il turno non ritarda niente:
i nodi ripetono l'HELLO ogni 2 s e il loop gira migliaia di volte in quel tempo.
**Costo**: una riga. Oggi il blocco **non e' osservabile**: non lascia traccia
in nessun contatore (serve la voce 18).

---

**Le proposte vere**, in ordine di valore (22-36 riguardano l'hub, **37-39 il
nodo**). Dettaglio, conti e prove nel documento; qui il titolo e il perché in
una riga.

### 22. La striscia del verdetto — progettata il 2026-08-21 e mai fatta
«Conviene aprire le finestre?» sul pannello, dal confronto fra **umidità
assoluta** dentro e fuori — la funzione è già in `meteo_calc.h`, scritta per
questo. Manca solo un attributo per nodo (dentro/fuori) da mettere in **due
chiavi NVS separate**, mai allargando il blob del registro (trappola di
`PAGES_MAX`). **Costo**: basso. È la funzione che il piano chiamava *«l'unica
informazione per cui il progetto esiste»* e che non è mai stata costruita.
**La soglia va applicata con ISTERESI, non come banda secca**: misurato con
`tools/analisi.py`, con la banda allargarla da 0 a 0,3 g/m³ porta i cambi da
10,9 a **26,4 al giorno** (attraversare la zona neutra ne costa due), con
l'isteresi a **2,9**. Stessa scelta già fatta in `forecast.h`.

### 23. Il diario degli eventi su card
`/eventi/AAAA-MM.csv`: boot, sync NTP, nodo muto, nodo che torna, errori card,
OTA, pairing. Una riga **per transizione**, mai per campione. Tre indagini
raccontate in `docs/Stazione-Meteo.md` sono state, in sostanza, la ricostruzione
a mano di questo diario. **Costo**: basso, `sd_log_refresh()` si copia.

### 24. `LINK_MSG_STATUS`: il nodo si racconta senza essere toccato
Oggi un nodo che dorme è ispezionabile solo con un power-cycle, che **azzera in
RTC memory proprio lo stato che si voleva guardare**. Un messaggio di stato ogni
12 risvegli (fw, `boot_count`, risvegli, `scan_ok`, canale, intervallo, errori)
costa **+0,5 %** di carica. Compatibile per costruzione: `link_parse_message()`
rifiuta le lunghezze diverse, quindi i firmware vecchi lo ignorano.

### 25. L'ascolto durante l'associazione
Oggi, se un nodo non si associa, l'hub non mostra **nulla**. `hub_on_new_peer()`
riceve già `esp_now_recv_info_t` — **con l'RSSI** — e scarta in silenzio due
volte. Un anello di sei voci (MAC, RSSI, motivo dello scarto) permette di dire
*«un nodo sconosciuto sta cercando un hub, RSSI −62, 8 s fa: apri
l'associazione»*. **Costo**: basso.

### 26. `/api/rev`: il polling smette di rifare lavoro che non è cambiato
La dashboard chiede ~1,6 kB ogni 5 s (~1,15 MB/h) mentre il contenuto vero
cambia 24 volte l'ora. Un endpoint da ~90 byte con dei contatori di revisione
taglia circa un decimo dei byte e della CPU. È `firmaValori()` applicata al lato
web. **Costo**: basso. Con fallback per la dashboard vecchia sulla card.

### 27. Il pannello sa più cose della dashboard
Min/max 24 h, rugiada, umidità assoluta e humidex sono disegnati sul vetro e non
escono da `/api/nodi`; `remote_temp_history()` (48 mezz'ore, 96 byte in RAM) non
ha nessun endpoint, e per lo stesso grafico la dashboard scarica il CSV intero.
**Costo**: basso. Chiude la parte «resta da fare» della voce 1.

### 28. La completezza del dato, accanto a ogni aggregato
`campioni / attesi`. Un minimo calcolato sul 40 % dei campioni ha lo stesso
aspetto di un minimo vero. Dipende dalla voce 17, o la completezza esce sopra il
100 %. **Costo**: basso. **Prerequisito di tutte le statistiche.**

### 29. Gradi giorno (HDD/CDD)
Due colonne nel riepilogo giornaliero (voce 3) e una card in dashboard.
Rispondono all'unica domanda energetica che ci si pone in casa e che oggi non ha
risposta. Base 18 °C (internazionale) o 20 °C (DPR 412/93): **una sola, e
dichiarata**. **Costo**: bassissimo dopo la voce 3.

### 30. Dare un voto alla propria previsione, senza internet
Confrontare la tendenza a 3 h con la **persistenza** («fra tre ore sarà come
adesso»), che è il riferimento con cui si giudica ogni previsione. Risponde alla
stessa domanda della voce 7 — *«la mia regola empirica vale qualcosa?»* — senza
la dipendenza esterna, e può dare la risposta scomoda, che sarebbe un ottimo
risultato. **Si può provare sui CSV già raccolti prima di scrivere una riga.**

### 31. «Il giorno prima, in una pagina» + i record della stazione
Una pagina del pannello che si disegna **una volta al giorno** all'uscita dalle
ore di silenzio (un refresh su 287, cioè lo 0,3 %) — l'uso più adatto che
esista per un display bistabile — e il confronto con lo storico: *«il 2
settembre più caldo da quando misuro»*. Entrambe gratis dopo la voce 3. I record
si **ricalcolano** dal riepilogo, mai accumulati in NVS.

### 32. Il sensore fermo si riconosce meglio guardando le tre grandezze insieme
Variante della voce 2: che T, RH **e** P restino identiche contemporaneamente
non è meteorologia — sono due sensori diversi sullo stesso bus I2C. Quasi nessun
falso positivo, e il caso «lettura fallita» è già distinto perché il nodo
trasmette NAN.

### 33. `tools/analisi.py` — FATTA il 2026-09-02
Zero rischio, zero firmware, e **risponde alle tarature** delle voci 22, 30 e
32 prima di costruirle (è ciò che ha già fatto `refresh_simula.py`). Con i dati
già sulla card si ricavano: la **costante di tempo termica della casa**, se il
**sole batte sul sensore esterno** (tutte le massime estive sarebbero sbagliate,
e nessuna diagnostica attuale lo vedrebbe), il **conteggio degli arieggiamenti**
(che è anche la controprova della voce 22) e **gli orari del riscaldamento**.
**Resta da fare**: girarlo sui CSV veri dell'hub — finora ha girato solo su dati
sintetici. Ha già prodotto due risultati: l'isteresi della voce 22, e la scoperta
che il metodo ovvio per la costante di tempo sbaglia dell'**87 %** in modo
silenzioso (37 h dove il vero era 20). Per quello ha un'**autoprova**
(`--autoprova`) che genera dati con verità note e verifica che gli stimatori le
ritrovino: *una verifica che non gira mai non si sa se funziona.*

### 34. Il piede di navigazione va generato dalla tabella delle rotte
Sette copie da tenere allineate a mano, dichiarate come debito in `CLAUDE.md`.
Un `/nav.js` generato da `ROTTE[]` fa comparire una pagina nel menu **perché è
stata registrata** — la stessa disciplina di `/api/elenco`. Il piede statico
resta come rete, o si sposterebbe il punto di rottura invece di toglierlo.

### 35. Le pagine dettaglio si legano al nome del nodo, che può cambiare
Rinominare un nodo fa scrivere al pannello «NODO NON IN ELENCO» con una
spiegazione falsa. `param` è `char[24]`: un MAC testuale (17+1) **ci sta**, il
blob NVS non va toccato. In alternativa una `remote_on_rename()`, che serve
comunque alla voce 13.

### 36. Prima dei COMMAND, la numerazione anti-doppione
Nota da fissare *prima* di scrivere la voce 11: un comando accettato senza
autenticazione può mettere a dormire un nodo per un'ora, e la via di recupero è
andarci di persona. Un contatore monotono in NVS sul nodo copre il caso facile
(rigiocare un comando) a un centesimo del costo della cifratura — che comunque
non protegge l'HELLO, perché il broadcast ESP-NOW non si può cifrare. E
«dimentica nodo» dovrebbe accodare una **dissociazione**, o il nodo resta
convinto di essere associato (oggi serve un power-cycle).

### 37. La pagina del nodo promette uno storico che a sonno acceso non arriva
Con il deep sleep la RAM si azzera 288 volte al giorno, quindi i tre grafici a
24 h del nodo restano vuoti per costruzione e il trend resta `TREND_IGNOTO` per
sempre — ma la pagina scrive *«raccolgo dati: servono tre ore di storico»*, che
è una promessa falsa, e la nota sotto i grafici elenca «riavvio o stacco della
batteria» dimenticando il risveglio, che è il caso **normale**. Stessa cosa per
i min/max, che a sonno acceso valgono quanto la lettura corrente e hanno
l'aspetto di una giornata senza escursione. La previsione vera **c'è già e sta
sull'hub**: la pagina deve dirlo. **Costo**: bassissimo. Il ragionamento era già
scritto per intero in `remote_nodes.h` — è stato applicato all'hub e non alla
pagina del nodo.

### 38. Il watchdog sul nodo vale più che sull'hub: un blocco costa la batteria
Un hub bloccato perde dati; un nodo bloccato **non si riaddormenta** e svuota
una cella da 1500 mAh in **~21 ore** (WiFi acceso, ~70 mA) contro i mesi che
dovrebbe durare. E la rete di sicurezza non aiuta, perché sta *dentro* il
percorso del sonno: si esegue solo se il codice sta girando, che è proprio ciò
che non succede. Due timeout diversi: ~60 s nella finestra di veglia (sospeso
durante l'OTA), ~10 s nel ciclo di risveglio, che dura 0,68 s misurati. **Da
disarmare prima di `esp_deep_sleep_start()`.** È la voce col miglior rapporto
valore/costo dell'analisi.

### 39. Il verdetto delle finestre non si fa sul nodo — FATTA (solo commento)
`MeteoNode_C3.ino` prevede un `ventilation.h` **sul nodo** che ragioni di
umidità assoluta. La grandezza è quella giusta, il posto no: il verdetto è un
confronto **fra due nodi**, e un nodo vede solo sé stesso. Stessa ragione per
cui il trend barometrico è finito sull'hub. Correggere il commento, o qualcuno
ripartirà dalla parte sbagliata. **Costo**: zero.

---

## Da fare — idee più vecchie, raccolte da altri documenti

### 11. Configurare i nodi dall'hub (era "Fase 7" del piano, 2026-08-24)
**Cosa**: si imposta la cadenza di un nodo dalla UI dell'hub, e il nodo la
riceve **al suo prossimo risveglio**.
**Perché qui**: oggi per cambiare quel numero bisogna staccare la batteria,
aspettare la finestra di veglia e usare la pagina del nodo — cioè l'unica cosa
che il deep sleep rende difficile è proprio configurarlo.
**Stato**: era rimandata *"a dopo `MeteoHub_S3`"* perché la UI doveva vivere
sull'hub vero. **Quell'hub adesso esiste**: la voce è sbloccata.
**Attenzione**: l'ACK di ESP-NOW **non** può portare la configurazione — è di
livello MAC e dice solo consegnato/non consegnato. Serve un COMMAND separato,
con il nodo che resta in ascolto una finestra dopo il proprio DATA.

### 12. Rollup e decimazione per la web UI di `EnvNode_C3` (2026-08-09)
**Cosa**: (a) una riga per giorno con min/media/max, così lo storico
multi-giorno non fa una lettura di CSV per giorno; (b) `/api/giorno?max=N` che
salta righe lato server, perché su un canvas da 500 px i 1440 punti di una
giornata non si distinguono.
**Stato**: concordate e mai fatte. Oggi quella scheda è **spenta**, quindi la
voce vale solo se torna in servizio — o come formato da riusare per la voce 3.
**Attenzione**: se si fanno, la dashboard va adeguata **con fallback**, perché
deve continuare a funzionare su un nodo che non espone ancora le rotte nuove.

### 13. Spostare la cartella su SD quando un nodo cambia nome
**Cosa**: oggi rinominare un nodo crea una cartella nuova e lo storico vecchio
resta in quella vecchia — visibile solo digitando l'URL a mano.
**Stato**: scelta consapevole, non svista (le cartelle vecchie restano come
archivio). Il codice per farlo sarebbe una callback `remote_on_rename()`
agganciata a `SD.rename()` nel `.ino`.
**Attenzione**: due casi da gestire, ed è il motivo per cui non è stata fatta —
la cartella di destinazione che **esiste già** (nome riciclato: i due storici
andrebbero fusi, non sovrascritti) e la **SD assente** proprio in quel momento,
che lascerebbe il rinominare a metà.

### 14. Il partitore della batteria (hardware)
**Cosa**: due resistenze da 1 MΩ su D1/GPIO3 del nodo a batteria, e
`battery_mv` smette di essere 0.
**Perché qui**: è l'unico modo di misurare l'autonomia davvero. Oggi la curva
di scarica si costruisce a mano col multimetro, e sul plateau della LiPo la
tensione non dice quasi niente — da 4,06 V a 3,9 V mancano fra 32 e 39 giorni a
seconda di quale pendenza si usa (la forbice era 8-39 fino al 2026-09-01, quando
48,3 h di lettura ferma a 4,06 V hanno escluso la pendenza del segmento 3: un
limite superiore vale più di un punto in più, perché non dipende dalla
differenza fra due letture da 10 mV di risoluzione).
**Stato**: fermo perché **i componenti non ci sono**. Costo ~1,7 µA, quindi
conviene lasciarlo fisso.

### 15. Anteprima 1:1 del pannello nel browser
**Cosa**: vedere sul telefono esattamente ciò che il pannello sta mostrando.
**Perché non è banale**: in GxEPD2 1.6.9 `_buffer` è `private` e non esiste
`getBuffer()`. Servirebbe disegnare su un `GFXcanvas1` nostro (15 kB di RAM, ci
sono) e spingerlo con `drawImage()` — cioè far passare **tutte** le funzioni di
disegno per un `Adafruit_GFX&` invece che per il `display` globale.
**Stato**: **FATTA il 2026-08-31** (`v22`, commit `b3db893`). Il refactor è
stato quello previsto — ogni disegno passa da una tela `GFXcanvas1` — più due
cose che si sono scoperte solo facendolo: la pagina immagine scriveva **dritta
al controller** con `writeImage()`, saltando il framebuffer di GxEPD2 (quindi
leggerlo non sarebbe bastato comunque), e `drawImage()` dentro il paging
avrebbe dato **due refresh**, il secondo col buffer vuoto: pannello bianco.
Costo misurato: refresh completo 2200 → 2630 ms, orologio invariato.
**Nota**: è tornato utile saperlo il 2026-08-30, quando il grafico è stato
caricato e non c'era modo di verificarne l'aspetto da remoto — il motivo per cui
esiste `temp_campioni`, che dice se i dati ci sono ma non se la curva è bella.

### 16. Grafico anche della pressione (o dell'umidità)
**Cosa**: la pagina grafico oggi mostra solo la temperatura. Il tipo di pagina
ha già un campo `param` inutilizzato: basterebbe usarlo per scegliere la
grandezza.
**Costo**: basso — l'anello della pressione a 3 h esiste già, ma per 24 h ne
servirebbe uno come quello della temperatura.
**Attenzione**: ogni anello in più sono ~100 byte per nodo. Con 8 nodi e tre
grandezze si arriva a 2,4 kB: ancora poco, ma non più trascurabile.

---

## Valutate e scartate

Non si cancellano: se un giorno le premesse cambiano, si vede subito **quale**
premessa era.

| idea | quando | perché no |
|---|---|---|
| **Server web asincrono** (ESPAsyncWebServer) su `EnvNode_C3` | 2026-08-09 | Non risolve il collo di bottiglia: la lentezza è lettura SPI dalla SD più serializzazione, che l'async non accorcia. In più gli handler con I/O andrebbero riscritti, `/update` usa l'API del server sincrono, e sarebbe una dipendenza esterna su una scheda in produzione. |
| **Task FreeRTOS propri** sui C3 | 2026-08-26 | Sui C3 il loop cooperativo resta la scelta giusta. Misurare il tempo di giro **prima** di toccare qualcosa. |
| **Fissare il canale 2,4 GHz nel router** | 2026-08-25 | Toglierebbe all'AP la scelta automatica per risolvere il problema di un solo dispositivo. La strada scelta è stata farlo cercare al nodo (Fase 9), e ha funzionato. |
| **Orologio manuale al posto di NTP** (era "Fase 8") | 2026-08-25 | Senza RTC tamponato l'ora impostata a mano non sopravvive a un distacco di corrente: non toglie una dipendenza, la sposta da internet **a una persona**, che deve essere lì dopo ogni blackout e accorgersene. |
| **Immagine + testo composti a bordo** | 2026-08-28 | Il `.bin` è già retinato: rimpicciolirlo a bordo ricampiona un pattern e produce moiré. La strada giusta è comporre nel browser alla dimensione finale — **fatto** poi in `v12`. |
| **OTA automatico da GitHub** | 2026-08-30 | Una scheda che si aggiorna da sola, senza che tu possa raggiungerla fisicamente se va storta, è un rischio che non vale la comodità. |
| **Ritenzione, decimazione o compressione dei CSV su card** | 2026-09-02 | Conto fatto sul formato vero: 82 byte a riga, 23 kB al giorno per nodo, **66 MB all'anno con otto nodi** su 14,9 GB liberi, cioè **232 anni**. Non c'è nessun problema da risolvere, e una politica di cancellazione aggiungerebbe solo un modo nuovo di perdere dati. Se un giorno servisse decimare sarebbe per **velocità di lettura**, che è un problema diverso con una soluzione diversa (il riepilogo giornaliero). |
| **Server-Sent Events / long polling / WebSocket** per la dashboard | 2026-09-02 | Il `WebServer` del core è **sincrono**: una connessione tenuta aperta dentro un handler blocca `handleClient()`, e con lui OTA, prelievo dei DATA dalla radio e disegno del pannello. Sarebbe il difetto di `streamFile()` reso permanente e per progetto. `/api/rev` (voce 26) ottiene quasi lo stesso risparmio restando su richieste corte. |
| **Un task FreeRTOS per il disegno del pannello**, per non fermare il `loop()` 2,6 s | 2026-09-02 | Pannello e microSD **condividono il bus SPI**, e `CLAUDE.md` avverte già: *«regge perché tutto gira in `loop()`»*. Si comprerebbe un blocco più corto al prezzo di una classe di guasti (corruzione sul bus condiviso) oggi impossibile per costruzione. Il watchdog (voce 19) e la coda dei DATA (voce 20) tolgono le conseguenze **senza** creare il rischio. |
| **Calcolare le grandezze derivate sul nodo** | 2026-09-02 | Contraddice la scelta già motivata in `meteo_calc.h`: il nodo trasmette **misure**, e un errore di formula finirebbe nello storico su card per sempre. Sull'hub si ricalcola ad ogni disegno, e i CSV di ieri bastano a rifare i conti. |
| **Alzare `NODI_VISIBILI` oltre 4** sul pannello | 2026-09-02 | Il limite non è il numero ma il corpo del carattere: da tre nodi in su si passa già al blocco compatto, e a cinque il pannello smetterebbe di leggersi da tre metri — che è la distanza a cui la pagina funziona. Per più nodi ci sono già la pagina dettaglio e la rotazione. |
| **Cifrare ESP-NOW adesso** (PMK + LMK) | 2026-09-02 | Fattibile — una LMK condivisa sta dentro il limite di sei del chip — ma il **broadcast non si può cifrare**, quindi l'HELLO resta in chiaro e la fase scoperta resta scoperta. Ha senso solo insieme ai COMMAND, e anche allora **dopo** la numerazione anti-doppione (voce 36), che copre il caso facile a un centesimo del costo. In più una chiave finita per sbaglio in un commit va cambiata su tutti i nodi: un OTA ciascuno più un power-cycle per quelli a batteria. |

---

## Fatte

Solo la riga essenziale: il racconto sta in `docs/Stazione-Meteo.md`.

| feature | quando | dove |
|---|---|---|
| Ricerca automatica del canale ESP-NOW (Fase 9) | 2026-08-27, confermata sul campo il 28 | `MeteoNode_C3` `v12` |
| Pagine del pannello configurabili, messaggi, immagini | 2026-08-28 | `MeteoHub_S3` `v4`-`v11` |
| Testo sopra le foto composto nel browser | 2026-08-30 | `v12`, commit `89698c6` |
| `/pannello` con elenco unico, 16 slot, errori visibili | 2026-08-30 | `v12` |
| `/api/salute` + `reset_reason`/`boot_count` | 2026-08-30 | `v13`, commit `4fb85a1` |
| Pagina grafico 24 h + salute in dashboard | 2026-08-30 | `v14`, commit `2030697` |
| Tre correzioni al grafico viste solo sul pannello | 2026-08-30 | `v15`-`v17` |
| **Pagine dell'interfaccia sostituibili dalla card + `/api` autogenerata** | 2026-08-30 | `v18` |
| Compositore immagini: più font, corpo regolabile, emoji retinate | 2026-08-31 | `v21`, commit `3d82072` |
| **Anteprima 1:1 del pannello** (era la voce 15) + `tools/pannello_png.py` | 2026-08-31 | `v22`, commit `b3db893` |
| Rugiada, humidex, acqua nell'aria, min/max 24 h, Δ3h | 2026-08-31 | `v23`-`v24` |
| Icone sul pannello (con due scartate perché ambigue) | 2026-08-31 | `v24`, commit `07480ee` |
| Pagina **dettaglio** per nodo, e pagina nodi alleggerita | 2026-08-31 | `v25`, commit `c2ba72d` |
| Galleria immagini: pagine, ricerca, miniature, tabella | 2026-08-31 | `v26`-`v27` |
| **Pagina nodi ridisegnata**: barra min/max 24 h, testata leggera, delta al posto della parola | 2026-09-01 | `v38` |
| Piede che si misura invece di stimare, e il falso «muto» dopo ogni riavvio | 2026-09-01 | `v39`-`v40` |
| `tools/larghezza_testo.py`: quanto e' largo un testo prima di disegnarlo | 2026-09-01 | — |
| **Nodo muto dopo un blackout**: peer riallineati alla radio + `espnow_peer_canale` + `prova-riallineo` | 2026-09-01 | `MeteoNode_C3` `v15`, `EspNowLink` |
| `tools/analisi.py` + le sue tarature (isteresi del verdetto, bias del tau) | 2026-09-02 | — |
| **Blocco A**: cadenza dai soli DATA consecutivi, tetto al salto di `seq`, timer per MAC, un WELCOME per giro a turno, stato dell'antighosting in `/api/stato`, testo dell'errore in dashboard | 2026-09-03 | `MeteoHub_S3` `v44`, `EspNowLink` |

---

## La tensione da decidere una volta sola

Le voci **7, 8, 9, 10** rompono una scelta esplicita del progetto: ESP-NOW è
stato scelto **perché non serve infrastruttura**, e finora la stazione funziona
identica con internet giù. Aggiungere servizi esterni significa che qualche
pagina resterà vuota quando il collegamento manca, e che si dipende da qualcosa
che può cambiare o sparire.

Non è un motivo per non farle. È un motivo per farle in modo che **il guasto
resti visibile e circoscritto**:

- ogni dato che viene da fuori sta su una **pagina sua**, mai dentro la pagina
  dei nodi, che deve continuare a funzionare da sola;
- quando il servizio non risponde, la pagina lo **dice** — non mostra l'ultimo
  dato ricevuto facendolo passare per fresco. È la stessa regola per cui il
  pannello scrive l'**ora** dell'ultimo pacchetto invece di "38 s fa": un dato
  vecchio spacciato per nuovo è peggio di un dato mancante;
- nessuna chiamata di rete dentro un handler HTTP o in un punto che blocca
  `loop()`: si accoda e la fa il ciclo, come già si fa per i refresh del
  pannello.
