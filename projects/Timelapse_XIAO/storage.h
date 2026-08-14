#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <time.h>

// =====================================================================
//  storage — microSD della scheda di espansione Sense, organizzata per
//  giorno (e' un timelapse: le foto si guardano a giornate, non tutte
//  insieme in un unico elenco lungo migliaia di righe).
//
//      /timelapse/2026-08-14/143000.JPG      foto, nome = ora locale
//      /timelapse/log/2026-08-14.csv         una riga per scatto
//
//  I nomi sono a lunghezza fissa e con gli zeri iniziali, quindi
//  ordinarli alfabeticamente equivale a ordinarli nel tempo: la web UI
//  non deve leggere le date dal filesystem (SD.h non le espone in modo
//  affidabile su tutte le versioni del core).
//
//  La microSD della XIAO Sense sta su SPI, non su SDMMC come quella della
//  board AMOLED (percio' questo file NON usa la libreria AMOLED191_SD, che
//  e' scritta per l'altra scheda):
//      CS = GPIO21, SCK = GPIO7 (D8), MISO = GPIO8 (D9), MOSI = GPIO9 (D10)
//
//  ATTENZIONE: GPIO21 e' anche il LED utente della XIAO. Con la Sense
//  montata quel pin e' il chip select della SD, quindi il LED lampeggia da
//  solo ad ogni accesso alla card e non e' utilizzabile come spia di stato.
//  E lo slot occupa l'intero bus SPI: finche' usi la microSD, D8/D9/D10 non
//  sono disponibili per altre periferiche SPI (sulla scheda Sense c'e' il
//  ponticello J3 per scollegare la card e riprendersi il bus).
//
//  Ogni scrittura apre / scrive / chiude: piu' lento, ma un'interruzione di
//  corrente (o l'estrazione della card) perde al massimo l'ultimo scatto.
//  Card <= 64 GB formattate FAT32; le exFAT non montano.
// =====================================================================

#define SD_ROOT_DIR "/timelapse"
#define SD_LOG_DIR  "/timelapse/log"

// Namespace NVS condiviso con lo sketch (contatori qui, impostazioni li').
#define NVS_NAMESPACE "xiaotl"

// Lunghezza di un nome di giorno "YYYY-MM-DD" (senza terminatore).
#define SD_DAY_LEN 10

// Monta la card. false = card assente o non FAT32: lo sketch deve
// continuare a funzionare lo stesso (web UI e OTA restano attivi, solo
// senza salvataggio). Richiamabile a runtime dopo aver inserito una card.
bool sd_begin();
bool sd_mounted();

// Ultimo errore in chiaro, per la web UI ("card assente", "FAT32?"...).
const char* sd_last_error();

// Contatore di accensioni tenuto in NVS: distingue due run diverse dentro
// lo stesso CSV giornaliero.
uint32_t sd_boot_id();

// Scatti salvati da sempre (in NVS, sopravvive ai riavvii).
uint32_t sd_shot_total();

uint64_t sd_total_mb();
uint64_t sd_used_mb();
uint64_t sd_free_mb();

// Salva un JPEG nella cartella del giorno di `when` (creata se manca),
// col nome HHMMSS.JPG dell'ora locale. Se quel nome e' gia' preso (scatto
// manuale nello stesso secondo di quello automatico) aggiunge un suffisso.
// day_out/name_out ricevono cartella e nome del file. false = card non
// montata o scrittura fallita (sd_last_error() dice perche').
bool sd_save_photo(const uint8_t* data, size_t len, const struct tm* when,
                   char* day_out, size_t day_cap, char* name_out, size_t name_cap);

// Aggiunge una riga al CSV del giorno. `sorgente` = "AUTO" | "WEB",
// `esito` = "ok" oppure il motivo del fallimento. Va chiamata anche
// quando lo scatto NON e' riuscito: il buco nella sequenza e' proprio
// l'informazione interessante quando si rivede un timelapse.
void sd_log_shot(const struct tm* when, const char* fonte_ora, const char* sorgente,
                 const char* file, size_t bytes, const char* esito);

// Quante foto ci sono nella cartella del giorno corrente (contatore in
// RAM: la directory si scandisce una volta sola, al primo scatto del
// giorno, non ad ogni richiesta della web UI).
uint32_t sd_photos_today();
const char* sd_today_dir();   // "" finche' non e' stato salvato niente oggi

// Elenco dei giorni presenti (cartelle YYYY-MM-DD sotto SD_ROOT_DIR), in
// ordine di directory. Ritorna quanti ne ha passati alla callback.
typedef void (*sd_day_cb_t)(const char* day, void* arg);
int sd_list_days(sd_day_cb_t cb, void* arg, int max_items);

// Elenco delle foto di un giorno.
typedef void (*sd_photo_cb_t)(const char* name, size_t size, void* arg);
int sd_list_photos(const char* day, sd_photo_cb_t cb, void* arg, int max_items);

// Giorno piu' vecchio presente sulla card (il primo in ordine
// alfabetico). false se non ce n'e' nessuno. Usata dalla politica
// "buffer circolare" quando lo spazio finisce.
bool sd_oldest_day(char* out, size_t cap);

// Validazione dei parametri che arrivano dal web: SEMPRE, o si apre la
// card intera a chi conosce l'URL.
bool sd_day_is_valid(const char* day);    // esattamente "YYYY-MM-DD"
bool sd_name_is_safe(const char* name);   // niente "/" ne' ".."

// Apre una foto in lettura (web UI). Il chiamante deve chiuderla.
File sd_open_photo(const char* day, const char* name);
bool sd_delete_photo(const char* day, const char* name);

// Cancella tutte le foto di un giorno e la sua cartella. Il CSV del
// giorno resta: e' il registro di cosa e' successo, occupa pochi KB e
// senza foto non serve piu' a niente lo spazio, ma la storia si'.
bool sd_delete_day(const char* day);

// Apre il CSV di un giorno in lettura (download dalla web UI).
File sd_open_log(const char* day);
