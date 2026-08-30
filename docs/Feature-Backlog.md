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

### 1. Min/max del giorno per nodo
**Cosa**: minimo e massimo di temperatura (e umidità) dalla mezzanotte, per
ogni nodo, sul pannello e in dashboard.
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
**Costo**: medio-basso. Un generatore QR minimale, nessuna libreria grafica.
**Dipendenze**: nessuna.
**Attenzione**: va rigenerato quando l'IP cambia, non solo al boot.

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
tensione non dice quasi niente — da 4,06 V a 3,9 V mancano fra 8 e 39 giorni a
seconda di quale pendenza si usa.
**Stato**: fermo perché **i componenti non ci sono**. Costo ~1,7 µA, quindi
conviene lasciarlo fisso.

### 15. Anteprima 1:1 del pannello nel browser
**Cosa**: vedere sul telefono esattamente ciò che il pannello sta mostrando.
**Perché non è banale**: in GxEPD2 1.6.9 `_buffer` è `private` e non esiste
`getBuffer()`. Servirebbe disegnare su un `GFXcanvas1` nostro (15 kB di RAM, ci
sono) e spingerlo con `drawImage()` — cioè far passare **tutte** le funzioni di
disegno per un `Adafruit_GFX&` invece che per il `display` globale.
**Stato**: valutato il 2026-08-28 e non fatto. È un refactor meccanico ma vero.
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
