# MorseKey_C3 — decoder Morse per tasto telegrafico

Guida di progetto: si carica quando si lavora sui file di questa cartella. Per
l'architettura del workspace, i comandi di build e le regole valide su tutte le
schede vedi `CLAUDE.md` alla radice; per le trappole hardware gia' pagate
`docs/Trappole-Hardware.md`.

## I file di questa cartella

| File | Ruolo |
|---|---|
| `MorseKey_C3.ino` | tutto: latch del tasto in interrupt, decoder adattivo, allenatore, OLED, tono di ascolto |

Un tasto telegrafico verticale **J. Junker Bad Honnef** su una **ESP32-C3
Supermini**: si batte, il testo decodificato scorre su un OLED SSD1306 128×64
con i WPM misurati, e un buzzer passivo fa il tono di ascolto. Seconda
modalita': un allenatore a lettere Koch che misura il rapporto linea/punto.
Self-contained, niente WiFi, niente `libraries/`.

```
arduino-cli compile --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc" projects/MorseKey_C3
```

## Cablaggio

| Da | A |
|---|---|
| tasto B, comune (leva) | GND |
| tasto A, lavoro (chiuso premendo) | GPIO3 |
| tasto C, riposo (chiuso a riposo) | GPIO4 |
| OLED SDA / SCL | GPIO5 / GPIO6 (come lo starter `C3_OLED_OTA`), VCC a 3V3 |
| buzzer **passivo** | + via 100 Ω a GPIO7, − a GND |
| LED onboard | GPIO8, attivo LOW: acceso a tasto premuto |
| tasto BOOT della scheda | GPIO9: decoder ↔ allenatore (dopo l'avvio e' un pulsante libero) |

L'OLED si collega **a scheda spenta**: la presenza si decide all'avvio, chiedendo
al bus se `0x3C` risponde. `oled.begin()` da solo non basta, perche' fallisce
solo se manca la RAM per il buffer e con il display staccato risponde «ok».

Il multimetro **va usato a scheda scollegata**: con i fili sui GPIO, i diodi di
protezione fanno suonare la continuita' fra A e B anche a tasto alzato.

## Perche' e' fatto cosi'

**Antirimbalzo a latch, non a tempo.** Il tasto ha un contatto in scambio: la
leva tocca o A o C, mai tutti e due. A chiuso = premuto, C chiuso = rilasciato,
entrambi aperti = leva in corsa o in rimbalzo, e lo stato non cambia. Nessun
ritardo, nessuna soglia da tarare. Verificato su 41 segni il 2026-10-13: segno
piu' corto 60 ms, nessun rimbalzo, mai A e C chiusi insieme.

**Il latch gira in interrupt** e mette in coda ogni cambio con il suo istante:
`oled.display()` tiene fermo `loop()` per ~25 ms, e un segno misurato da
`loop()` porterebbe quell'errore. Il tono invece parte da `loop()`, quindi puo'
tardare fino a ~25 ms se la pressione cade durante un ridisegno; le durate no.

**Le soglie delle pause derivano dalle medie di punto e linea**, non da medie
proprie. La v3 le aveva, e coi SOS veloci si e' vista la trappola: le pause fra
lettere (150–320 ms) cadevano sotto la soglia iniziale, venivano contate come
pause fra segni e alzavano la soglia invece di correggerla; S e O si fondevano
in `...---`. Le pause poi seguono il segno che precede (dopo un punto la pausa
fra lettere parte da ~150 ms, dopo una linea da ~235), quindi la fine lettera e'
`(punto+linea)/2` dopo un punto e `linea` dopo una linea. Da 3 SOS su 9 a 8 su 10.

**Quello che resta non si risolve con le soglie**: battendo veloce l'ultimo
punto di una lettera esce lungo (100–135 ms) e qualche linea corta (120–130),
e i due si sovrappongono. Lo spazio fra parole (3 linee medie, ~480 ms) e'
anch'esso un compromesso: pause fra lettere fino a 445 ms, fra parole da 400.

## Comandi dal tasto

- **8 o piu' punti** (il segnale di errore): nel decoder cancella l'ultimo
  carattere, nell'allenatore fa ritentare senza contare.
- **Tasto tenuto 1 s** (`AZZERA_MS`): nel decoder azzera lo schermo,
  nell'allenatore salta la lettera; quella pressione e la pausa che segue non
  entrano nelle medie.

## Allenatore (dalla v7)

Si entra col tasto BOOT; modo e livello restano in NVS (namespace `morsekey`,
chiavi `allena` e `livello`). Chiede una lettera alla volta fra le prime
`livello` dell'ordine Koch (`KMRSUAPTLOWI.NJEF0Y,VG5/Q9ZH38B?427C1D6X`, si
parte da 2); l'ultima entrata esce una volta su tre, mai la stessa due volte di
fila. Con **90 % di giuste sulle ultime 20** entra la successiva, annunciata a
schermo (`NUOVA`) e suonata due volte. Una lettera sbagliata conta una volta
sola: il buzzer fa sentire quella giusta col ritmo da manuale alla velocita'
misurata, sotto compare il codice, e la si ribatte.

**La misura che conta e' il rapporto linea/punto**, ed e' il motivo per cui
esiste l'allenatore: sapendo quale lettera ha chiesto, assegna ogni durata al
tipo giusto anche dove il decoder la classificherebbe male (solo se il numero
di segni torna). Ne escono il rapporto (obiettivo 3) e le **sovrapposizioni**,
cioe' le lettere in cui il punto piu' lungo dura quanto la linea piu' corta —
il difetto che al decoder costa di piu'. Le lettere senza punti o senza linee
(E, T, M, O, S...) non danno misura. Prima prova, 2026-10-13: 8 giuste su 9,
rapporto 2,7–3,2 battendo con calma, nessuna sovrapposizione (contro ~2 sui
SOS veloci).

`suona()` e' bloccante (al massimo ~2 s) e alla fine **butta la coda** degli
eventi: quello che si batte mentre suona non e' un tentativo. Per lo stesso
motivo, se la lettera la chiude una pressione, `tastoPremuto()` controlla
`eventiScartati` ed esce, e `loop()` comanda LED e tono da `premuto` e non
dall'evento.

## Diagnostica

`DIAGNOSTICA = true` stampa su Serial ogni segno con durata, pausa precedente,
medie e soglie. Si tara cosi': registrare 60–90 s di battuta nota (SOS, PARIS)
e ripassare i tempi fuori dalla scheda prima di toccare una soglia.
