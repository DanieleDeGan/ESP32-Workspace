# AMOLED_1.91_LVGL — starter LVGL per la board AMOLED

Guida di progetto: si carica quando si lavora sui file di questa cartella. Per
l'architettura del workspace, i comandi di build e le regole valide su tutte le
schede vedi `CLAUDE.md` alla radice; per le trappole hardware gia' pagate (USB
CDC, deep sleep, OTA, scritture su SD, default NVS) `docs/Trappole-Hardware.md`.

## I file di questa cartella

| File | Ruolo |
|---|---|
| `AMOLED_1.91_LVGL.ino` | sketch principale — `setup()`/`loop()`, qui va SOLO la logica applicativa |
| `lv_conf.h` | configurazione LVGL a livello di progetto |
| `build_opt.h` | flag di compilazione globali (spiegati in `CLAUDE.md` alla radice) |
| `ui.h/.c` | stub segnaposto, sostituiti dall'export "UI Files" di SquareLine |

## Workflow SquareLine Studio

1. Nuovo progetto SquareLine: risoluzione **536×240**, colore **16 bit**, LVGL
   **8.3.x** (deve combaciare con `lv_conf.h` e la libreria installata).
2. **"Export UI Files"** con percorso di export = la cartella dello sketch. Questo
   sovrascrive `ui.h`/`ui.c` e porta anche `ui_helpers.*`, `ui_events.*`, screen e
   asset.
3. **Non** usare il `.ino` né il driver TFT_eSPI generati da SquareLine: il display
   è già gestito da `AMOLED191_Display`. Si tengono solo i file `ui_*`.
4. Il corpo degli eventi "Call function" definiti in SquareLine va in `ui_events.c`
   (non viene sovrascritto ai re-export).
5. Se l'IDE non vede i file appena aggiunti dopo un export, chiuderlo e riaprirlo (la
   build cache mantiene lo stato precedente).

