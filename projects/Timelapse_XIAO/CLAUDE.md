# Timelapse_XIAO — camera timelapse con galleria web

Guida di progetto: si carica quando si lavora sui file di questa cartella. Per
l'architettura del workspace, i comandi di build e le regole valide su tutte le
schede vedi `CLAUDE.md` alla radice; per le trappole hardware gia' pagate (USB
CDC, deep sleep, OTA, scritture su SD, default NVS) `docs/Trappole-Hardware.md`.

## I file di questa cartella

| File | Ruolo |
|---|---|
| `Timelapse_XIAO.ino` | timer degli scatti, gestione dello spazio, impostazioni — qui va la logica applicativa |
| `storage.h/.cpp` | microSD SPI organizzata per giorno: `/timelapse/<giorno>/<ora>.JPG` + CSV giornaliero |
| `camera.h/.cpp` | copia di quello del nodo camera (stesso hardware, stessi pin) |
| `rtc_time.h/.cpp` | copia di quello di `EnvNode_C3`: stima da build-time, poi NTP |
| `net_ota.h/.cpp` | WiFi + ArduinoOTA + `/update`, con watchdog di riconnessione — di norma non si tocca |
| `web_ui.h/.cpp` | pagina di controllo, galleria/riproduzione e API HTTP |


Cresciuto da `starters/XIAO_S3_Camera/`, stessa scheda, scatto comandato da un
**timer** invece che dal PIR:

```
timer -> scatto -> /timelapse/<AAAA-MM-GG>/<HHMMSS>.JPG  ->  galleria web
```

**Cosa cambia rispetto allo starter da cui nasce**:
- **niente PIR e niente ESP-NOW**, quindi niente `hub_link.*` e **nessuna
  dipendenza da `libraries/`**: la cartella si sposta fuori dal repo così com'è
  (a differenza del nodo camera, che si porta dietro `EspNowLink`);
- `storage.*` è riscritto: non un progressivo `IMG_%05lu` in un'unica cartella,
  ma **una cartella per giorno** e il nome = ora locale dello scatto. I nomi
  sono a lunghezza fissa con zeri iniziali, quindi ordinarli alfabeticamente
  equivale a ordinarli nel tempo — la web UI non deve leggere le date dal
  filesystem, che `SD.h` non espone in modo affidabile su tutte le versioni del
  core;
- c'è `rtc_time.*` (copia da `EnvNode_C3`, modulo puro): senza orario vero i
  nomi non significano niente. Vale lo stesso ordine obbligato in `setup()`:
  `rtctime_begin(TZ_POSIX)` → `rtctime_seedFromBuild()` → (WiFi) →
  `rtctime_onWifiConnected()`, **a ogni riconnessione** — qui la riconnessione
  la segnala `net_takeReconnectedFlag()`;
- `net_ota.*` ha in più il **watchdog di riconnessione WiFi** (ritento a 30 s,
  re-init dello stack a 5 min, riavvio a 30 min), come su `EnvNode_C3`: sta
  acceso per settimane.

**Scelte da conoscere prima di metterci le mani**:
- **Gli slot sono allineati all'orologio**, non "ultimo scatto + intervallo":
  il prossimo istante è un multiplo dell'intervallo contato sull'epoch. Il conto
  si rifà ad ogni giro, quindi il primo sync NTP (che sposta l'orologio anche di
  ore) o uno stream MJPEG lungo **non** lasciano una coda di scatti arretrati da
  recuperare tutti insieme: gli slot persi si saltano e basta (contati in
  `s_skipped`).
- **Durante lo stream non si scatta**: `app_pump()` — chiamata ad ogni frame dal
  ciclo dello stream, dove il web server è fermo — riallinea soltanto il timer.
  La camera sta già servendo frame allo stream e una scrittura su SD lo
  bloccherebbe. Come nello starter, dentro `app_pump()` **non** si chiama
  `net_loop()`.
- **Spazio esaurito**: due politiche, in NVS. `APP_FULL_STOP` (default) smette
  di scattare; `APP_FULL_RING` elimina il **giorno più vecchio** — mai quello in
  corso, o con una card piccola si finirebbe a cancellare le foto di un'ora fa.
  La soglia è in MB liberi (`min_liberi`, default 200).
- **Non si scatta finché l'orario non è sincronizzato** (`orario_registrabile()`,
  da `v3`), con la stessa finestra di grazia di 5 minuti di `EnvNode_C3`. Qui
  pesa più che altrove perché l'orario **è** il nome del file e la cartella che
  lo contiene: prima del primo sync NTP le foto finirebbero in
  `/timelapse/<giorno-di-build>/`, un giorno diverso da quello vero e già
  passato — e con `APP_FULL_RING`, che elimina il giorno **più vecchio**, quella
  cartella sarebbe la prima candidata alla cancellazione. Si perderebbero cioè
  proprio le foto appena scattate, e lo si scoprirebbe solo a card piena. Non
  c'è invece rischio di sovrascrittura: `sd_save_photo()` aggiunge già un
  suffisso `_N` se il nome esiste. Gli slot saltati per questo motivo finiscono
  in `s_skipped`, quindi la web UI li mostra.
- **Il CSV giornaliero registra anche gli scatti falliti** (`esito` diverso da
  `ok`): in un timelapse il buco nella sequenza è proprio ciò che si vuole
  spiegare. Colonne: `ts_iso,boot_id,fonte_ora,sorgente,file,byte,esito`.
  `sd_delete_day()` cancella le foto ma **lascia** il CSV.
- **Niente miniature sulla card**: la galleria mostra i JPEG a piena
  risoluzione rimpiccioliti dal browser (`loading=lazy`). Generarle costerebbe
  una seconda codifica per scatto e il doppio dello spazio; il prezzo è che una
  giornata da migliaia di scatti fa una pagina pesante.
- **Risoluzione e qualità non sono persistite** (a differenza di intervallo,
  finestra oraria e politica dello spazio): dopo un riavvio la camera riparte
  dai default di `camera.cpp`. Se servisse, il posto giusto è `settings_save()`
  nel `.ino`, non `camera.cpp`.
- Il **fuso orario** è una costante di compilazione (`TZ_POSIX` nel `.ino`), non
  un'impostazione da web come in `EnvNode_C3`: questa scheda non viaggia.

**Dove scrivere la logica**: nel `.ino` (timer, scatto, spazio, nuove
periferiche in `loop()` senza bloccare a lungo). `camera.*`, `storage.*`,
`rtc_time.*`, `net_ota.*`, `web_ui.*` sono boilerplate per compito. GPIO liberi
per aggiunte: D1–D5 (GPIO 2, 3, 4, 5, 6) — D8/D9/D10 e GPIO21 sono la microSD.

