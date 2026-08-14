#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// =====================================================================
//  camera — OV2640/OV3660 della XIAO ESP32-S3 Sense
//
//  Incapsula esp_camera: pinout della scheda, configurazione JPEG e
//  cattura. I pin NON sono negoziabili (sono cablati sulla scheda di
//  espansione Sense) e sono gli stessi che il core ESP32 usa in
//  libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h alla
//  voce CAMERA_MODEL_XIAO_ESP32S3.
//
//  SERVE LA PSRAM ABILITATA (FQBN ...:PSRAM=opi / Tools > PSRAM: "OPI
//  PSRAM"): senza, l'init riesce solo a risoluzioni minime. camera_begin()
//  in quel caso ripiega su QVGA e lo dice su Serial invece di fallire.
// =====================================================================

// Inizializza la camera. false = sensore non rilevato (cavo flat non
// inserito bene? e' la causa piu' comune) o memoria insufficiente.
bool camera_begin();
bool camera_ready();

// Nome del sensore rilevato ("OV2640", "OV3660", ...), "n/d" se non pronto.
const char* camera_sensor_name();

// Frame corrente, per lo streaming: veloce, ma puo' essere un frame gia'
// in coda (fino a ~1 frame di ritardo).
camera_fb_t* camera_grab();

// Frame "fresco" per la foto: scarta i frame gia' bufferizzati, cosi' lo
// scatto non e' l'immagine di un attimo prima e l'esposizione automatica
// ha avuto il tempo di assestarsi. Piu' lento di camera_grab().
camera_fb_t* camera_grab_fresh();

// Ogni frame ottenuto DEVE essere restituito, altrimenti dopo pochi
// scatti non ci sono piu' buffer liberi e la camera si pianta.
void camera_release(camera_fb_t* fb);

// --- Risoluzione: indice nella tabella interna, non il valore grezzo di
// framesize_t (cosi' la web UI non dipende dalla numerazione dell'enum). ---
int         camera_size_count();
const char* camera_size_name(int index);
int         camera_size_index();
bool        camera_set_size_index(int index);

// Qualita' JPEG: 10..40, valori PIU' BASSI = immagine migliore e file piu'
// grande (e' la convenzione di esp32-camera, non un refuso).
int  camera_quality();
bool camera_set_quality(int q);

// Orientamento: utile quando la scheda e' montata capovolta nel gavone.
void camera_set_flip(bool vflip, bool hmirror);
bool camera_vflip();
bool camera_hmirror();
