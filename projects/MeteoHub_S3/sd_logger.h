#pragma once

// COPIA da projects/EnvNode_C3/, adattata alla microSD della XIAO S3 Sense:
// pin diversi (CS 21) e nessuna SPI.begin() propria, perche' qui il bus e'
// condiviso con il pannello e-ink e lo apre il .ino.
//
// Di questo modulo l'hub usa la parte /nodi (sd_log_remote,
// sd_list_remote_days, sd_open_remote_day, sd_node_dir_name) piu' mount e
// spazio. Il log locale (/logs, sd_log_sample e i suoi accessori) e la
// dashboard su SD restano qui non usati: questa scheda non ha un sensore
// proprio e la sua pagina sta in PROGMEM. Sono rimasti dentro apposta, per
// poter riallineare il file con l'originale con un diff invece che a memoria.
#include <Arduino.h>
#include <time.h>
#include <FS.h>

// =====================================================================
//  sd_logger — microSD su modulo SPI HW-125 (NON quella SDMMC della board
//  AMOLED: qui serve un mount SPI esplicito, pin dedicati, vedi sd_logger.cpp)
//
//  Log CSV con rotazione giornaliera in /logs/YYYY-MM-DD.csv. Come in
//  storage.cpp (XIAO_S3_Camera): ogni scrittura apre/scrive/chiude, mai un
//  file tenuto aperto a lungo — un distacco di corrente in mezzo a una
//  scrittura perde al piu' quella riga, non l'intero file.
//
//  sd_record_count_total()/today() sono contatori in RAM aggiornati a ogni
//  scrittura riuscita: MAI una scansione della SD per rispondere a queste
//  due funzioni. Il totale e' salvato in NVS a intervalli (non a ogni
//  scrittura: risparmia cicli di erase della flash su un contatore che
//  altrimenti si riscrive ogni pochi secondi per anni — vedi il commento
//  su REC_TOTAL_FLUSH_EVERY in sd_logger.cpp) — un crash tra un flush e
//  l'altro puo' far perdere l'aggiornamento delle ultime unita' al solo
//  contatore mostrato in UI, i dati nel CSV non sono mai a rischio.
// =====================================================================

#define SD_LOG_DIR "/logs"

// Monta la card. false = card assente o non FAT32: il chiamante deve
// continuare a funzionare lo stesso (OLED/dashboard restano attivi, solo
// senza logging). Richiamabile a runtime per ritentare il mount.
bool sd_begin();
bool sd_mounted();
const char* sd_last_error();

uint64_t sd_total_mb();
uint64_t sd_free_mb();

// Contatore di avvii in NVS (namespace separato da settings.cpp): utile in
// diagnostica seriale, non finisce nel CSV (l'ordine cronologico e' gia'
// garantito dal timestamp reale, a differenza di DHT11_SD_Logger.ino che
// non aveva un RTC).
uint32_t sd_boot_id();

uint32_t sd_record_count_total();
uint32_t sd_record_count_today();

// Accoda una riga al CSV del giorno di `ts` (ruota automaticamente il file
// se e' cambiato il giorno rispetto all'ultima chiamata). timeSource deve
// essere "NTP" o "STIMA" (vedi rtc_time.h). false = SD non montata o
// scrittura fallita (vedi sd_last_error()).
bool sd_log_sample(time_t ts, const char* timeSource, float tempC, float humPct);

// Elenco delle date disponibili (nomi dei file /logs/*.csv, senza estensione),
// in ordine di directory (che per nomi "YYYY-MM-DD.csv" e' gia' ordine
// cronologico). Ritorna quante ne ha passate alla callback.
typedef void (*sd_date_cb_t)(const char* isoDate, size_t fileSizeBytes, void* arg);
int sd_list_days(sd_date_cb_t cb, void* arg, int maxItems);

// Legge il CSV di `isoDate` riga per riga, chiamando cb per ognuna (mai
// l'intero file in RAM: usata dall'API web per rispondere in streaming).
// Ritorna 0 se la data non esiste o non e' nel formato "YYYY-MM-DD".
typedef void (*sd_row_cb_t)(time_t ts, float tempC, float humPct, void* arg);
int sd_read_day(const char* isoDate, sd_row_cb_t cb, void* arg);

// true se `isoDate` e' esattamente nel formato "YYYY-MM-DD" (10 char,
// trattini nelle posizioni giuste, resto cifre) — da usare SEMPRE su una
// data che arriva da una query string web, prima di comporci un path.
bool sd_name_is_safe(const char* isoDate);

// Elimina il file /logs/<isoDate>.csv. Se isoDate e' il giorno corrente
// (quello su cui sd_log_sample() sta scrivendo), azzera anche il contatore
// "record di oggi" in RAM: altrimenti resterebbe a un valore che non
// corrisponde piu' a nessuna riga sul file (che verra' ricreato da zero,
// con una nuova intestazione, alla prossima scrittura). NON tocca il
// contatore cumulativo sd_record_count_total(), che conta le scritture
// fatte nella vita del dispositivo, non i file presenti in questo momento.
// false = data non valida, file inesistente, o cancellazione fallita.
bool sd_delete_day(const char* isoDate);

// Apre il file CSV grezzo di `isoDate` per la lettura (es. per un download
// dalla dashboard, servito con WebServer::streamFile() invece che riga per
// riga come sd_read_day()). Il chiamante deve chiudere il File ricevuto.
// Ritorna un File "falsy" (operator bool() == false) se la data non e'
// valida, la SD non e' montata, o il file non esiste.
File sd_open_day(const char* isoDate);

// ---------------------------------------------------------------------
//  Log dei nodi remoti (ESP-NOW) — /nodi/<NOME>/YYYY-MM-DD.csv
// ---------------------------------------------------------------------
//  Cartella separata da /logs e NON una sottocartella di quella: dentro
//  /logs c'e' una scansione (sd_list_days) che si aspetta solo file, e
//  infilarci delle directory vorrebbe dire dover ricordare per sempre di
//  filtrarle. I due log restano anche concettualmente distinti: /logs e'
//  quello che questa scheda misura, /nodi e' quello che le viene raccontato.
//
//  Una cartella per nodo, con il NOME (sanificato) e non il MAC: sulla card
//  la si deve poter leggere. Il MAC sta comunque in una colonna di ogni
//  riga, cosi' se un nodo viene rinominato le righe vecchie restano
//  attribuibili — l'identita' vera e' il MAC, il nome puo' cambiare.
//
//  Colonne: ts_iso,ts_unix,fonte_ora,mac,seq,temp_c,hum_pct,press_hpa,batt_mv
//  Un valore non finito (sensore del nodo che non ha risposto) diventa un
//  campo VUOTO, non uno zero: nel grafico dev'essere un buco, non una
//  misura. `seq` c'e' apposta perche' i salti si vedano: su una tratta
//  radio il pacchetto perso e' un dato, non un incidente da nascondere.
// ---------------------------------------------------------------------

#define SD_NODI_DIR "/nodi"

// Sanifica un nome nodo per usarlo come cartella: tiene solo [A-Za-z0-9_-],
// scarta il resto, tronca a 16 caratteri. false se non ne resta niente di
// utilizzabile. Da usare SEMPRE su un nome che arriva dalla radio o da una
// query string, prima di comporci un path.
bool sd_node_dir_name(const char* nodeName, char* out, size_t outCap);

// Accoda una riga al CSV giornaliero del nodo (crea cartelle e intestazione
// al bisogno). `value` sono i tre float del protocollo, NAN dove manca.
// false = SD non montata, nome inutilizzabile o scrittura fallita.
bool sd_log_remote(const char* nodeName, const char* mac, time_t ts,
                   const char* timeSource, uint32_t seq,
                   const float value[3], uint16_t batteryMv);

// Come sd_list_days(), ma per un nodo remoto.
int sd_list_remote_days(const char* nodeName, sd_date_cb_t cb, void* arg, int maxItems);

// Come sd_open_day(), ma per un nodo remoto (download del CSV grezzo).
File sd_open_remote_day(const char* nodeName, const char* isoDate);

// Legge il CSV di un giorno di un nodo riga per riga, gia' parsato. E' il
// posto UNICO dove si interpreta quel formato: prima lo stesso parser stava
// copiato in due punti del .ino, e una colonna aggiunta un domani avrebbe
// dovuto essere ricordata in entrambi.
//
// La lettura e' BUFFERIZZATA (512 byte per volta). Non e' un dettaglio: la
// versione precedente chiamava File::read() un byte alla volta, e su un bus
// SPI ogni chiamata attraversa il driver della card. Con un giorno a 60 s
// (1440 righe, ~115 kB) il riepilogo costava 2125 ms di loop() bloccato.
//
//   codaMaxBytes  0 = tutto il file. Altrimenti si parte da quel tanto di
//                 coda (la prima riga, tagliata a meta', viene scartata):
//                 serve a chi ricostruisce una finestra recente e non vuole
//                 pagare la lettura di giorni interi.
//
// Torna quante righe sono state passate alla callback.
typedef void (*sd_remote_row_cb_t)(time_t ts, uint32_t seq, const float v[3], void* arg);
int sd_read_remote_day(const char* nodeName, const char* isoDate,
                       sd_remote_row_cb_t cb, void* arg, size_t codaMaxBytes);

// ---------------------------------------------------------------------
//  Riepilogo giornaliero — /nodi/<NOME>/riepilogo.csv
// ---------------------------------------------------------------------
// Una riga per giorno CHIUSO, scritta una volta sola. Il calcolo sta in
// daily.h (puro) e la colla nel .ino: questo modulo sa solo dove va il file
// e come non scriverlo due volte.
//
// PERCHE' NON E' UN TIMER A MEZZANOTTE. Una riga prodotta da un timer
// sparisce per sempre se in quel minuto la scheda e' spenta, sta facendo un
// OTA o e' appena ripartita — e nessuno se ne accorge, perche' l'assenza di
// una riga non somiglia a un guasto. Il giorno si chiude invece quando si
// scopre che ne e' cominciato uno nuovo, il che rende il lavoro idempotente
// e recuperabile: dopo qualunque assenza l'hub ritrova i giorni rimasti
// indietro e li fa, in ordine, uno per giro.
//
// Colonne (un valore non finito e' un campo VUOTO, mai uno zero — nello
// storico dev'essere un buco, non una misura che nessuno ha fatto):
//   giorno,campioni,attesi,completezza_pct,buchi,
//   t_min,t_min_ora,t_max,t_max_ora,t_med,
//   h_min,h_max,h_med, p_min,p_max,p_med,p_var24, td_min,td_max,td_med

#define SD_RIEP_FILE "riepilogo.csv"

// L'ultimo giorno gia' presente nel riepilogo del nodo ("YYYY-MM-DD").
// false se il file non c'e' o e' vuoto — cioe' "non ne ho ancora fatto
// nessuno", che e' l'inizio del backfill e non un errore.
bool sd_riep_ultimo_giorno(const char* nodeName, char* out, size_t outCap);

// Accoda una riga gia' formattata (senza a capo). Scrive l'intestazione se
// il file e' nuovo. false = card assente o scrittura non arrivata sulla
// card: il chiamante NON deve considerare fatto quel giorno, o resterebbe
// un buco permanente nello storico.
bool sd_riep_append(const char* nodeName, const char* riga);

// Il file per il download. File falsy se assente.
File sd_open_riep(const char* nodeName);

// Elimina il riepilogo di un nodo, cosi' che il loop() lo rifaccia da zero.
// E' LA VIA DI RIENTRO, e serve piu' di quanto sembri: una riga di riepilogo
// non viene mai riscritta, quindi un giorno calcolato male (formato cambiato,
// difetto corretto dopo) resterebbe sbagliato per sempre e l'unico rimedio
// sarebbe smontare la card. true anche se il file non c'era.
bool sd_riep_azzera(const char* nodeName);

// ---------------------------------------------------------------------
//  Dashboard personalizzata su SD (/www/dashboard.html): se presente,
//  web_ui.cpp la serve al posto di quella incorporata nel firmware. La
//  pagina di upload/ripristino (web_ui.cpp) resta pero' SEMPRE quella in
//  PROGMEM, indipendente da questi file: anche una dashboard.html rotta
//  non puo' mai bloccare fuori chi deve poterla sostituire.
// ---------------------------------------------------------------------
#define WWW_DIR            "/www"
#define WWW_DASHBOARD_PATH "/www/dashboard.html"

bool sd_dashboard_exists();

// File "falsy" se la SD non e' montata o il file non esiste.
File sd_open_dashboard();

// ---------------------------------------------------------------------
//  Pagine sostituibili in /www
// ---------------------------------------------------------------------
// La dashboard era la prima; da v18 lo sono anche le altre pagine del
// firmware. Il nome NON e' libero: e' una whitelist decisa da chi chiama
// (web_ui), non un pezzo di path che arriva dalla rete. Cosi' non esiste
// path traversal da controllare, e sulla card non si accumulano file che
// nessuno serve.
//
// Il file e' sempre /www/<nome>.html.
bool sd_www_exists(const char* nome);
File sd_open_www(const char* nome);
File sd_open_www_for_write(const char* nome);
bool sd_delete_www(const char* nome);

// Registro di quando e con quale firmware una pagina e' stata caricata, in
// /www/caricate.csv. Serve a vedere una pagina rimasta indietro rispetto al
// firmware che la usa: e' il rischio nuovo che si prende spostando le pagine
// sulla card, e l'unico modo di accorgersene e' scriverselo.
void sd_www_registra(const char* nome, const char* fw, time_t quando);
bool sd_www_info(const char* nome, char* fwOut, size_t fwCap, time_t* quandoOut);

// Crea /www se serve e apre il file in scrittura (tronca un eventuale file
// preesistente). File "falsy" se la SD non e' montata o l'apertura fallisce.
File sd_open_dashboard_for_write();

// Ripristina la dashboard di default eliminando il file su SD. true anche
// se il file non esisteva gia' (nessun errore in quel caso).
bool sd_delete_dashboard();

// ---------------------------------------------------------------------
//  Pagine immagine su microSD — /images/<nome>.bin
// ---------------------------------------------------------------------
//  Il formato e' quello prodotto da www/dither.html e gia' provato dal
//  bring-up del pannello: 400x300 a 1 bit, 50 byte per riga, **15.000 byte
//  esatti**, bit a 1 = bianco. Nessuna conversione a bordo: i byte si
//  leggono dalla card e si spingono nel controller cosi' come sono — e'
//  tutto il motivo per cui il dithering si fa nel browser.
//
//  La dimensione non e' un dettaglio ma il contratto: un file di lunghezza
//  diversa NON e' un'immagine per questo pannello, e va rifiutato quando
//  si carica, non scoperto quando si disegna (li' si vedrebbe come una
//  pagina storta, che somiglia a un guasto del display).
// ---------------------------------------------------------------------

#define IMG_DIR        "/images"
#define IMG_W_PX       400
#define IMG_H_PX       300
#define IMG_BYTES_ESATTI 15000u
#define IMG_NOME_MAX   20

// Sanifica un nome immagine (lista bianca [A-Za-z0-9_-], max IMG_NOME_MAX):
// stessa regola di sd_node_dir_name(), e per lo stesso motivo — il nome
// arriva da una query string e ".." o "/" non devono nemmeno poter esistere
// in un path composto qui. false se non resta niente di utilizzabile.
bool sd_img_name_safe(const char* nome, char* out, size_t outCap);

// Elenco delle immagini presenti (nome senza estensione, byte del file).
typedef void (*sd_img_cb_t)(const char* nome, size_t bytes, void* arg);
int sd_img_list(sd_img_cb_t cb, void* arg, int maxItems);

// Elenco A PAGINE, con il filtro sul nome. Serve perche' la card ne tiene
// quante ne vuole (15.000 byte l'una su 14,9 GB) mentre la pagina web ne puo'
// mostrare una dozzina: senza paginazione l'unico modo di limitare il costo
// era il tetto di maxItems, che oltre a essere arbitrario era MUTO -- la
// trentatreesima immagine non compariva e nessuno lo diceva.
//
//   da      quante saltarne (0 = dall'inizio)
//   quante  quante consegnarne al massimo
//   cerca   se non vuoto, tiene solo i nomi che lo contengono (senza
//           distinguere maiuscole e minuscole)
//   totale  se non nullptr, riceve quante ne esistono DOPO il filtro: e' il
//           numero che permette alla pagina di dire "12 di 87" invece di
//           lasciare l'utente a indovinare se ce ne sono altre
//
// Torna quante ne ha consegnate.
int sd_img_page(sd_img_cb_t cb, void* arg, int da, int quante,
                const char* cerca, int* totale);

// La miniatura di un'immagine: 80x60 a 1 bit, 10 byte per riga, 600 byte
// esatti. E' l'immagine da 400x300 sottocampionata a blocchi di 5x5, con il
// blocco che diventa nero se lo sono almeno 13 dei suoi 25 pixel.
//
// Esiste per la banda, non per la grafica: un'anteprima piena sono 15.000
// byte, e una pagina con dodici anteprime ne sarebbe 180.000 su un web server
// SINCRONO -- cioe' altrettanti byte durante i quali l'hub non preleva i DATA
// dei nodi. Cosi' sono 7.200.
//
// out dev'essere grande almeno MINI_BYTES. Torna false se l'immagine non c'e'
// o non e' lunga 15.000 byte.
#define MINI_W      80
#define MINI_H      60
#define MINI_STRIDE (MINI_W / 8)
#define MINI_BYTES  (MINI_STRIDE * MINI_H)
bool sd_img_mini(const char* nome, uint8_t* out);

// ---------------------------------------------------------------------
//  Registro dei refresh del pannello
// ---------------------------------------------------------------------
// Una riga per refresh in /epd/AAAA-MM.csv, con il motivo e quanto e'
// costato. Serve a rispondere a domande che il contatore in RAM non puo':
// quanti ne ho fatti questo mese, quanti erano completi, se la cadenza sta
// peggiorando.
//
// PERCHE' SULLA CARD E NON IN NVS: la flash interna ha cicli di erase finiti
// e la NVS e' anche il posto dove vivono le pagine e il registro dei nodi --
// roba che non si puo' permettere di consumare per un contatore. La microSD
// ha spazio (157 refresh al giorno sono ~10 kB al mese su 14,9 GB) e cicli
// che qui non si esauriranno mai.
//
// Colonne: ts_iso,motivo,tipo,ms
//   motivo  stato | valori | ghosting | pagina | silenzio
//   tipo    completo | parziale
bool sd_log_refresh(const char* motivo, bool completo, uint32_t ms);

// ---------------------------------------------------------------------
//  Diario degli eventi
// ---------------------------------------------------------------------
// Una riga per TRANSIZIONE in /eventi/AAAA-MM.csv: boot, sync NTP, un nodo
// che tace, un nodo che torna, la card che rifiuta righe, un OTA, una
// finestra di associazione.
//
// PERCHE' ESISTE. `docs/Stazione-Meteo.md` racconta almeno tre indagini che
// sono state, in sostanza, la ricostruzione a mano di questo diario: il buco
// di 456 s del 24/08, l'uptime di 0,7 h del 30/08, i dodici minuti muti dopo
// il blackout dell'01/09. Tutte e tre hanno la stessa forma -- l'evento e'
// passato e l'unica traccia che ha lasciato e' indiretta. I contatori in RAM
// dicono QUANTO (boot_count 49), mai QUANDO; i CSV dei nodi dicono che c'e'
// un buco, mai perche'.
//
// E' UNA DIAGNOSI, NON UN LOG. Se ci finisse dentro ogni pacchetto diventerebbe
// illeggibile e non lo guarderebbe piu' nessuno: una riga per CAMBIO DI STATO,
// mai una riga per campione. `nodo_muto` si scrive quando il nodo DIVENTA
// muto, non finche' lo e'.
//
// Sulla card e non in NVS per la stessa ragione del registro dei refresh: gli
// eventi sono pochi, testuali e storici, e la flash interna ha cicli di erase
// finiti mentre la card no.
//
// Colonne: ts_iso,tipo,dettaglio
bool sd_log_evento(const char* tipo, const char* dettaglio);

// Il file di un mese ("AAAA-MM"), per il download. File falsy se assente o se
// il nome non e' valido.
File sd_open_eventi(const char* mese);

bool sd_img_exists(const char* nome);
File sd_img_open(const char* nome);              // lettura, File falsy se assente
File sd_img_open_for_write(const char* nome);    // crea /images, tronca
bool sd_img_delete(const char* nome);

