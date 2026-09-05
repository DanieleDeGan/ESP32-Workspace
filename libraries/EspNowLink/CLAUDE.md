# `EspNowLink` — comunicazione ESP-NOW hub↔nodi

Guida della libreria: si carica quando si lavora sui suoi file. Il protocollo
deve restare identico su hub e nodi, quindi ogni modifica qui li tocca tutti.
Per i comandi di build e le regole valide su tutte le schede vedi `CLAUDE.md`
alla radice; per le trappole hardware gia' pagate `docs/Trappole-Hardware.md`.


## I file di questa cartella

| File | Ruolo |
|---|---|
| `src/EspNowLink.h` | l'**unica** intestazione pubblica: protocollo (`link_message_t`), tipi di nodo, e tutte le funzioni qui sotto |
| `src/link_peer.h/.cpp` | `LinkPeer`, il peer sopra `ESP_NOW_Peer` del core: invio affidabile con conferma e ritentativi, coda del WELCOME |
| `src/link_hub.cpp` | il lato hub: registro dei peer, adozione, WELCOME uno per giro, anello dei MAC sconosciuti |
| `src/link_node.cpp` | il lato nodo: HELLO, `seq`, ripresa dell'hub dopo il sonno, rinvio dell'ultimo DATA |
| `library.properties` | metadati Arduino, **senza** campo `depends`: con `--libraries` conta solo l'`#include` letterale |

## L'API pubblica

| Funzione | A cosa serve |
|---|---|
| `Link_Init(tipo, nome)` | avvio sul canale fisso `ESPNOW_LINK_CHANNEL` (6): per chi **non** sta su un AP |
| `Link_InitEx(tipo, nome, canale)` | avvio su un canale scelto; con `ESPNOW_LINK_CHANNEL_CURRENT` (0) non tocca la radio ed è l'unica forma lecita su una STA connessa |
| `Link_SetChannel(ch)` / `Link_GetChannel()` | sposta la radio **e riallinea i peer**; vietata a chi è connesso a un access point |
| `Link_SyncPeersToRadio()` | il rovescio della precedente: riallinea i peer al canale su cui la radio si trova **già**, senza toccarla |
| `Link_TestMisalignPeers(ch)` | fabbrica il guasto del canale disallineato, per provare la riparazione |
| `Link_OnMessage(cb)` | callback di ricezione: **accoda e basta**, il lavoro si fa dal `loop()` |
| `Link_Node_Poll()` / `Link_Hub_Poll()` | da chiamare ad ogni giro: HELLO periodici da un lato, WELCOME in coda dall'altro |
| `Link_Node_IsPaired()` / `Link_Node_ResumeWithHub(mac)` | stato dell'associazione, e la ripresa dopo un deep sleep senza rifare il pairing |
| `Link_Node_SetSeq()` / `Link_Node_GetSeq()` | il `seq` attraverso il sonno — senza, l'hub scarta tutto come doppioni |
| `Link_Node_SendData(msg)` | invio affidabile del DATA (incrementa il `seq`) |
| `Link_Node_ResendLast(tent, timeout)` | rimanda l'ultimo DATA **senza** incrementare il `seq`: è ciò che rende innocua la ricerca del canale |
| `Link_Hub_SetPairingMode(on)` | apre o chiude la finestra di associazione |
| `Link_Hub_AddPeer(mac, tipo, nome)` | rimette un peer conosciuto (il ripristino da NVS all'avvio dell'hub) |
| `Link_Hub_ForgetPeer(mac)` | lo toglie: serve quando si sostituisce una scheda, o resterebbe in elenco come nodo muto per sempre |
| `Link_Hub_GetPeerCount()` / `Link_Hub_GetPeerInfo(i, …)` | il registro, **pollato** dal `loop()` invece di una callback: così la concorrenza col task del driver WiFi resta tutta dentro la libreria |
| `Link_Hub_SendCommand(mac, msg)` | COMMAND unicast verso un nodo, con la stessa consegna affidabile |
| `Link_Hub_Unknown(i, …)` / `Link_Hub_UnknownClear()` / `Link_Hub_UnknownEsito(e)` | chi bussa e non entra: MAC sconosciuti con RSSI e **motivo** dello scarto, annotati anche a finestra chiusa |


Libreria per reti di sensori/attuatori: una board fa da **hub** e riceve dati da
moduli indipendenti ("nodi") via **ESP-NOW** (scelto invece di MQTT/WiFi perché
alcuni nodi sono a batteria e non serve infrastruttura broker/AP). Costruita
sopra la libreria ufficiale `ESP_NOW`/`ESP_NOW_Peer` del core Arduino ESP32
(bundled in `.../packages/esp32/hardware/esp32/<versione>/libraries/ESP_NOW/`),
non su `esp_now.h` grezzo. **Indipendente da LVGL/`AMOLED191_Display`**: gira
anche su schede senza schermo (è il caso tipico di un nodo sensore reale).

**Protocollo** (`link_message_t`, 37 byte, ben sotto i 250 byte limite
ESP-NOW v1.0): `protocol_version`/`msg_type` (HELLO/WELCOME/DATA/COMMAND)/
`node_type` (temp/livello_acqua/batteria/attuatore/camera/hub, estensibile)/
`name`/`seq`/`battery_mv`/`value[3]`. Pairing dinamico: un nodo manda HELLO in
broadcast finché non associato; l'hub, solo mentre `Link_Hub_SetPairingMode(true)`,
accetta il primo HELLO sconosciuto e risponde con WELCOME (mai da dentro il
callback di ricezione — troppo lento, va accodato e inviato da
`Link_Hub_Poll()`, stessa regola dei callback LVGL). Il nodo poi manda DATA
in unicast; l'hub può mandare COMMAND allo stesso modo. Registro peer
**solo in RAM** (nessuna persistenza SD/NVS in questo giro — scelta
deliberata, non ancora implementata).

**In pairing l'hub adotta anche un DATA unicast da un MAC sconosciuto**, non
solo un HELLO in broadcast. Serve perché il registro peer vive in RAM: dopo un
riavvio dell'hub il nodo **si crede ancora associato**, quindi non manda più
HELLO, e i suoi DATA verrebbero scartati — resterebbe invisibile per sempre.
Il guasto è **silenzioso da entrambe le parti**, ed è ciò che lo rende
cattivo: l'ACK di ESP-NOW è di livello radio e arriva comunque, quindi il nodo
continua a contare i propri invii come riusciti e la sua pagina dice che va
tutto bene, mentre l'hub non mostra niente. Osservato su hardware il
2026-08-23 fra `EnvNode_C3` e `MeteoNode_C3`: quindici DATA "consegnati", zero
nodi visti. Il DATA che fa scoprire il nodo viene anche conservato come prima
lettura, o andrebbe perso (`onReceive()` non è stato chiamato: il peer non
esisteva ancora). Finché il registro non è persistito, **tenere una finestra
di pairing aperta per qualche minuto ad ogni avvio dell'hub** è ciò che fa
rientrare i nodi già noti senza intervento.

**Scoperta bidirezionale**: `ESP_NOW.onNewPeer()` scatta per MAC *sorgente*
sconosciuto, quindi non solo l'hub deve gestirlo per gli HELLO — anche il
nodo deve gestirlo per il WELCOME dell'hub (sconosciuto finché non arriva).
`Link_Init()` registra il gestore giusto in base al ruolo internamente.

**Consegna affidabile**: `Link_Node_SendData()`/`Link_Hub_SendCommand()`
usano `LinkPeer::sendReliable()` — attendono la conferma di consegna
(`onSent`) e ritentano fino a 3 volte se non arriva entro il timeout, invece di
un fire-and-forget silenzioso. La sincronizzazione tra `onSent()` (gira sul
task del driver WiFi, tipicamente Core 0) e chi attende la conferma (chiamante
su `loop()`, tipicamente Core 1) usa un **semaforo FreeRTOS**
(`SemaphoreHandle_t`), non un `volatile bool`: un bool nudo non garantisce
visibilità tra core su un chip dual-core e faceva sì che il ritentativo non
vedesse mai la conferma in tempo, esaurendo sempre tutti i tentativi anche a
invio riuscito (bug reale trovato e corretto durante il test su hardware).

**Canale e convivenza col WiFi**: `Link_Init()` forza il canale fisso
`ESPNOW_LINK_CHANNEL` (6) e presuppone che nessuno sia connesso a un access
point — il caso normale di una rete di nodi a batteria. Un nodo che sta
**anche** su una rete WiFi (il nodo camera `starters/XIAO_S3_Camera/`, che ha
web UI e OTA) non può scegliere il canale: glielo impone il router. Per quel
caso c'è `Link_InitEx(tipo, nome, canale)` con `ESPNOW_LINK_CHANNEL_CURRENT`
(0), che non tocca il canale e registra i peer con channel 0 ("quello
corrente"). Conseguenza da non dimenticare: la radio è una sola, quindi **anche
l'hub va inizializzato sul canale di quell'AP** (`Link_InitEx(LINK_NODE_HUB,
"Hub", canale_AP)`), altrimenti i due non si sentono. Se il router cambia canale
da solo, il nodo lo segue al riavvio e l'hub no.

**Il `seq` è anche un filtro anti-doppioni, e con un nodo che dorme diventa una
trappola.** L'hub scarta un DATA il cui `seq` è **uguale** all'ultimo visto
(`remote_nodes.cpp`: `if (r->hasData && dato.seq == r->seq) continue;`). Il
contatore però vive in RAM dentro `link_node.cpp`, quindi un nodo che si
risveglia da deep sleep riparte da zero ad ogni ciclo: il primo DATA passa
(zero è diverso dall'ultimo seq della veglia) e **tutti i successivi vengono
buttati come doppioni**. Per questo esistono `Link_Node_SetSeq()` /
`Link_Node_GetSeq()`: il nodo conserva il seq in RTC memory e lo rimette dopo
ogni risveglio.

Il guasto è **silenzioso da entrambe le parti**, come quello del registro peer
qui sopra e per lo stesso motivo: l'ACK di ESP-NOW è di livello radio e arriva
comunque, quindi il nodo conta i propri invii come riusciti mentre l'hub non
mostra niente. Misurato il 2026-08-23: **19 risvegli, 19 invii confermati dal
nodo, uno solo visto dall'hub**. Si è cercato il guasto nel timer, nella radio,
nell'alimentazione e nell'USB; era una riga di deduplica.

**`Link_Node_ResumeWithHub(mac)`** serve allo stesso scenario: un nodo che si
sveglia non sa più di essere associato e rifarebbe il pairing (un HELLO ogni
2 s, più l'attesa che l'hub accodi il WELCOME dal suo `loop()`), che su un nodo
sveglio pochi secondi per volta è la parte più lunga e più incerta del ciclo,
tutta a radio accesa. Conservando il MAC dell'hub si registra il peer e si passa
dritti al DATA.

**Dopo un blackout un nodo alimentato dalla rete resta muto, e non si vede.**
Il piu' cattivo dei guasti di canale trovati finora, perche' non somiglia a un
guasto: la scheda risponde in rete, i sensori leggono, la sua pagina mostra il
canale **giusto** e i contatori degli invii falliti restano a zero.

La catena: la scheda si riaccende **insieme al router**, `net_begin()` rinuncia
dopo 15 s, e `hub_begin_ex()` — vedendo il WiFi assente — ripiega sul canale
fisso `ESPNOW_LINK_CHANNEL` (6). Quando poi il WiFi arriva, la radio va sul
canale dell'AP (13) ma **i peer restano registrati sul 6**, e nessuno rifa'
l'init: `hub_begin_ex()` si chiama solo in `setup()` e al risveglio dal sonno.
Da li' in poi i pacchetti escono verso un canale su cui non c'e' nessuno.

**Nemmeno la finestra di associazione lo recupera**, ed e' il dettaglio che
smaschera la diagnosi sbagliata: se non rientra *nemmeno in pairing*, il
problema non e' l'adozione — e' che non si sentono proprio. Osservato il
2026-09-01 su `MeteoEsp32` dopo un'interruzione di corrente vera: dodici minuti
muto, risolto solo staccando la corrente al nodo.

**Rimedio, da `v14` del nodo**: `Link_SyncPeersToRadio()` riallinea i peer al
canale su cui la radio si trova **gia'**, senza toccarla — ed e' il rovescio di
`Link_SetChannel()`, che invece la sposta e per questo e' vietata su una STA
connessa. `hub_loop()` la chiama alla prima connessione WiFi se ESP-NOW era
partito senza AP. I peer finiscono su `ESPNOW_LINK_CHANNEL_CURRENT`, quindi da
li' in avanti seguono la radio da soli.

**Il canale dei peer va ESPOSTO**, o il guasto resta invisibile: il nodo mostra
`espnow_peer_canale` accanto a `espnow_canale` (radio), e la pagina scrive
`PEER SUL CANALE x, RADIO SUL y` quando divergono. Il campo del canale radio,
da solo, mente per omissione — dice la verita' su una cosa che non e' quella
rotta.

**E si prova senza aspettare il prossimo blackout**:
`GET /api/comando?c=prova-riallineo` fabbrica il guasto
(`Link_TestMisalignPeers()`, sposta i peer e basta) e il giro dopo la
riparazione deve rimetterli a posto. Stessa disciplina di `prova-canale`: una
funzione che si attiva una volta all'anno, e mai sotto osservazione, e' una
funzione che non si sa se esiste. Verificata cosi' sull'hardware il 2026-09-01.

**Il canale si puo' cambiare a caldo** (da `v12` del nodo meteo, 2026-08-27):
`Link_SetChannel(ch)` sposta la radio **e riallinea i peer gia' registrati**
(itera il registro del driver con `esp_now_fetch_peer`/`esp_now_mod_peer`, cosi'
non deve sapere se gira su un hub o su un nodo). Prima il canale si sceglieva
solo a `Link_InitEx()`.

**Vietata a chi e' connesso a un access point**: la radio e' una sola e il
canale lo detta l'AP, quindi cambiarlo fa cadere la connessione. Serve a un
nodo che sta su ESP-NOW e basta — tipicamente uno a batteria, con il WiFi
spento, che si e' portato il canale in RTC memory.

Insieme c'e' `Link_Node_ResendLast(tentativi, timeout)`, che rimanda l'ultimo
DATA **senza incrementare il `seq`**. Non e' un dettaglio: `Link_Node_SendData()`
il seq lo incrementa ad ogni chiamata, quindi riprovare lo stesso campione su
piu' canali produrrebbe salti di numerazione, e l'hub li conta come pacchetti
persi sulla tratta radio — **buchi inventati dentro il registro che serve
proprio a contare i buchi veri**.

**Spostare un nodo da un hub a un altro: prima lo si DIMENTICA sul vecchio.**
Un hub che ha gia' il nodo nel registro gli rimanda il WELCOME anche a finestra
di pairing **chiusa** (`link_peer.cpp`: un HELLO da un peer noto mette
`welcomePending = true`, senza guardare il pairing). E' deliberato — serve a
non lasciare bloccato un nodo che si e' riavviato — ma vuol dire che a un HELLO
in broadcast rispondono **tutti** gli hub che quel nodo lo conoscono, e se lo
prende il primo che risponde.

Ordine giusto: **1)** `POST /api/nodi/dimentica?mac=…` sul vecchio hub,
**2)** finestra di pairing aperta sul nuovo, **3)** power-cycle del nodo. Il
power-cycle non e' evitabile: il MAC dell'hub sta in RTC memory e finche' e' li'
il nodo riprende con quello senza mandare HELLO. Un riavvio software non basta,
perche' la RTC memory sopravvive.

**Sbagliare l'ordine da' un guasto silenzioso e simmetrico**, osservato il
2026-08-27 spostando `MeteoNode` da `EnvNode_C3` a `MeteoHub_S3`: il nodo torna
sul vecchio hub, mentre il nuovo — che era in pairing — se lo mette in elenco e
resta a `pacchetti: 0`. Da un lato una lista con un nodo che non parla,
dall'altro un nodo convinto di essere associato: **nessuno dei due dice che
sta parlando con qualcun altro**, e la lettura giusta viene solo dal campo
`espnow_hub` del nodo confrontato col MAC dell'hub che ci si aspetta.

**L'RSSI c'e', in `hub_on_new_peer()`, e per anni nessuno l'ha letto.** Il
commento in `EspNowLink.h` diceva che l'RSSI non e' disponibile: e' vero del
**wrapper Arduino** (`ESP32_NOW.cpp` butta `info` prima di chiamare
`onReceive()`), non di quel callback, che riceve `esp_now_recv_info_t` e quindi
`rx_ctrl->rssi`. Non e' l'RSSI di ogni pacchetto — per quello servirebbe
sostituire il dispatch — ma e' esattamente quello che serve **mentre si
associa**: dice se il nodo e' dove si pensa che sia, prima di adottarlo.

**L'ascolto durante l'associazione** (`Link_Hub_Unknown()`, hub da `v45`, esposto
su `/api/pairing/ascolto`). Quando un nodo non si associava, l'hub non mostrava
**niente**: nessun tentativo, nessun contatore. Le cause sono almeno cinque
(canale sbagliato, nodo legato a un altro hub, registro pieno, versione diversa,
nodo che non trasmette) e da fuori erano tutte una lista che non cresce.
L'informazione veniva buttata **due volte** nello stesso callback: un `return`
fuori dalla finestra di pairing e un altro sul payload non parsabile.

- **Si annota SEMPRE, anche fuori dalla finestra**: contare non e' adottare.
  L'hub resta sordo — quella scelta non cambia — ma smette di essere ignaro, e
  la UI puo' dire «un nodo sconosciuto sta cercando un hub, RSSI -62, 8 s fa».
- **Lunghezza e versione si guardano SEPARATAMENTE**: `link_parse_message()`
  torna `false` per entrambe, ma «non e' il nostro protocollo» e «e' il nostro,
  di un'altra versione» mandano a cercare in due posti diversi.
- **E' un anello di sei voci, non un log**: senza tetto, un vicino con un
  dispositivo rumoroso lo riempirebbe. Si scrive dal callback della radio,
  quindi dentro la sezione critica va solo una copia di pochi byte.
- **Il limite sta nella risposta**, non lasciato dedurre: un elenco vuoto
  significa che nessun nodo sta **cercando** un hub, non che non ce ne siano di
  accesi — uno gia' associato altrove non manda HELLO, e i suoi DATA vanno in
  unicast a un altro MAC, quindi questa scheda non li riceve nemmeno.

**Un WELCOME per giro, e a turno** (da `v44` dell'hub, 2026-09-03).
`Link_Hub_Poll()` mandava un WELCOME a **ogni** peer in attesa nello stesso
giro, e `sendReliable()` blocca fino a ~1 s l'uno: con otto nodi che si
riavviano insieme — cioè quello che succede a un blackout — il `loop()` restava
fermo **fino a otto secondi**, proprio mentre tutti i nodi ritrasmettono. Otto
secondi in cui il WebServer non risponde, un OTA si pianta e **i DATA non
vengono prelevati dal driver**, che ne tiene uno solo per nodo.

**Il turno non è un dettaglio, è la metà che rende la correzione corretta.** Il
flag `welcomePending` si pulisce solo se l'invio riesce (di proposito: un
fallimento transitorio si ritenta invece di perdere la finestra di pairing di
quel nodo). Fermarsi al primo pendente ripartendo sempre dall'indice 0
significherebbe che **un nodo spento a metà associazione si prende l'unico
invio di ogni giro e affama tutti gli altri** — da un blocco di otto secondi si
passerebbe a un'associazione che non funziona più, in silenzio. Si riparte
quindi da dove ci si era fermati. Spalmarli non ritarda niente: i nodi ripetono
l'HELLO ogni 2 s e il `loop()` gira migliaia di volte in quel tempo.

**Due punti di robustezza sistemati il 2026-08-30**, entrambi invisibili
nell'uso normale:

- **il registro dei peer aveva una finestra di race**. `hub_insert_peer()`
  controlla "pieno o gia' presente" e poi inserisce, ma fra le due cose il lock
  si rilascia per forza: `new` e `addPeer()` allocano memoria e chiamano il
  driver, e dentro una `portENTER_CRITICAL` (spinlock, interrupt disabilitati)
  non si possono fare. Le due strade che arrivano li' girano su **task
  diversi** — la scoperta radio sul task del driver WiFi, il ripristino da NVS
  in `loop()` — e si sovrappongono davvero all'avvio dell'hub, quando i peer
  salvati si rimettono mentre i nodi stanno gia' trasmettendo. Con il registro
  quasi pieno due inserimenti simultanei scrivevano **oltre la fine
  dell'array**. Ora il controllo si rifa' dentro la sezione critica finale e
  chi perde il ballottaggio viene disfatto.
- **il nome che arriva dalla radio non era garantito terminato**: sedici byte
  pieni sono un payload legittimo. `link_parse_message()` ora chiude sempre
  `name` con un NUL — nell'unico punto da cui ogni messaggio passa, cosi' vale
  anche per il codice applicativo che verra' scritto dopo.

**Limite noto**: l'unicast ESP-NOW tra un hub ESP32-S3 e un nodo ESP32
"classico" (Xtensa D0WD) è risultato inaffidabile/lento ad associarsi su
hardware reale (broadcast sempre ok, WELCOME/unicast spesso perso), coerente
con un'issue nota e irrisolta nell'ecosistema arduino-esp32
([espressif/arduino-esp32#10895](https://github.com/espressif/arduino-esp32/issues/10895)).
Con nodi ESP32-C3 il pairing è immediato e affidabile.

**Aggiornamento del 2026-08-23 — il limite è dell'hub S3, non del chip
classico.** Provato un nodo ESP32 "classico" (DOIT DevKit v1, Xtensa D0WD)
contro un hub **ESP32-C3** (`projects/EnvNode_C3/`): pairing **immediato**,
DATA unicast consegnati, zero pacchetti persi. Quindi la raccomandazione va
letta al contrario di come era scritta: il problema sta nella combinazione con
un **hub S3**, e un ESP32 classico è un nodo perfettamente valido se l'hub è un
C3. Resta aperto se lo stesso valga per un hub S3 con le versioni attuali del
core — quella prova non è stata rifatta.

**Aggiornamento del 2026-08-27 — il limite non si è ripresentato, e la prova
rimasta aperta è chiusa.** Rifatta esattamente la combinazione sospetta: hub
**ESP32-S3** (`projects/MeteoHub_S3/`, XIAO S3 Sense) e nodo ESP32 "classico"
(lo stesso DOIT DevKit v1, MAC `70:4B:CA:82:9E:70`), core 3.3.10, canale 1.
Power-cycle del nodo con la finestra di associazione aperta sull'hub:
HELLO → WELCOME → primo DATA unicast **consegnato al primo tentativo**
(`espnow_inviati: 1, espnow_falliti: 0`), nodo adottato in pochi secondi.

Quindi **la raccomandazione qui sopra non vale più con il core attuale**: la
combinazione hub S3 ↔ nodo classico funziona. Resta scritta perché il guasto
era reale quando è stato osservato, e sapere che *poteva* presentarsi aiuta a
riconoscerlo se tornasse: il sintomo era broadcast che passa e unicast no, cioè
un nodo che si vede annunciare e non si riesce ad associare.

