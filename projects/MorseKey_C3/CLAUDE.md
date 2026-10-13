# MorseKey_C3 — decoder Morse per tasto telegrafico

Guida di progetto: si carica quando si lavora sui file di questa cartella. Per
l'architettura del workspace, i comandi di build e le regole valide su tutte le
schede vedi `CLAUDE.md` alla radice; per le trappole hardware gia' pagate
`docs/Trappole-Hardware.md`.

## I file di questa cartella

| File | Ruolo |
|---|---|
| `MorseKey_C3.ino` | tutto: latch del tasto in interrupt, decoder adattivo, OLED, tono di ascolto |

Un tasto telegrafico verticale **J. Junker Bad Honnef** su una **ESP32-C3
Supermini**: si batte, il testo decodificato scorre su un OLED SSD1306 128×64
con i WPM misurati, e un buzzer passivo fa il tono di ascolto. Self-contained,
niente WiFi, niente `libraries/`.

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

- **8 o piu' punti** (il segnale di errore): cancella l'ultimo carattere.
- **Tasto tenuto 1 s** (`AZZERA_MS`): azzera lo schermo; quella pressione e la
  pausa che segue non entrano nelle medie.

## Diagnostica

`DIAGNOSTICA = true` stampa su Serial ogni segno con durata, pausa precedente,
medie e soglie. Si tara cosi': registrare 60–90 s di battuta nota (SOS, PARIS)
e ripassare i tempi fuori dalla scheda prima di toccare una soglia.
