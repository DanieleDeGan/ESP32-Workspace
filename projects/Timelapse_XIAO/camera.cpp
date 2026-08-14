#include "camera.h"

// ---------------------------------------------------------------------
//  Pinout camera della XIAO ESP32-S3 Sense (cablato sulla scheda).
//  Copia fedele di CAMERA_MODEL_XIAO_ESP32S3 in camera_pins.h del core.
// ---------------------------------------------------------------------
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

// Frame da scartare prima di uno scatto "fresco": con fb_count=2 e
// CAMERA_GRAB_LATEST bastano due giri per avere un'immagine attuale e con
// l'esposizione automatica assestata.
#define CAM_STALE_FRAMES 2

// Tabella delle risoluzioni offerte dalla web UI.
static const struct {
  const char* name;
  framesize_t fs;
} SIZES[] = {
  { "QVGA 320x240",   FRAMESIZE_QVGA },
  { "VGA 640x480",    FRAMESIZE_VGA  },
  { "SVGA 800x600",   FRAMESIZE_SVGA },
  { "XGA 1024x768",   FRAMESIZE_XGA  },
  { "HD 1280x720",    FRAMESIZE_HD   },
  { "UXGA 1600x1200", FRAMESIZE_UXGA },
};
static const int SIZES_N = sizeof(SIZES) / sizeof(SIZES[0]);

#define SIZE_INDEX_DEFAULT 2   // SVGA: buon compromesso stream/foto
#define SIZE_INDEX_NOPSRAM 0   // QVGA: senza PSRAM non si va oltre

static bool s_ready      = false;
static int  s_sizeIndex  = SIZE_INDEX_DEFAULT;
static int  s_quality    = 12;
static bool s_vflip      = false;
static bool s_hmirror    = false;
static char s_sensorName[16] = "n/d";

bool camera_ready() { return s_ready; }
const char* camera_sensor_name() { return s_sensorName; }

bool camera_begin() {
  const bool hasPsram = psramFound();
  if (!hasPsram) {
    Serial.println("[CAM] PSRAM non trovata: compila con PSRAM \"OPI PSRAM\" "
                   "(FQBN ...:PSRAM=opi). Ripiego su QVGA, buffer singolo.");
    s_sizeIndex = SIZE_INDEX_NOPSRAM;
  }

  camera_config_t cfg = {};
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_sccb_sda = SIOD_GPIO_NUM;
  cfg.pin_sccb_scl = SIOC_GPIO_NUM;
  cfg.pin_d7       = Y9_GPIO_NUM;
  cfg.pin_d6       = Y8_GPIO_NUM;
  cfg.pin_d5       = Y7_GPIO_NUM;
  cfg.pin_d4       = Y6_GPIO_NUM;
  cfg.pin_d3       = Y5_GPIO_NUM;
  cfg.pin_d2       = Y4_GPIO_NUM;
  cfg.pin_d1       = Y3_GPIO_NUM;
  cfg.pin_d0       = Y2_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.pixel_format = PIXFORMAT_JPEG;      // JPEG: e' cio' che serve sia al web sia alla SD
  cfg.frame_size   = SIZES[s_sizeIndex].fs;
  cfg.jpeg_quality = s_quality;

  if (hasPsram) {
    cfg.fb_count    = 2;                  // doppio buffer: stream piu' fluido
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode   = CAMERA_GRAB_LATEST; // scarta i frame vecchi invece di accodarli
  } else {
    cfg.fb_count    = 1;
    cfg.fb_location = CAMERA_FB_IN_DRAM;
    cfg.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[CAM] init fallita: 0x%x. Controlla che il cavo flat della "
                  "scheda Sense sia inserito a fondo e nel verso giusto.\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    camera_sensor_info_t* info = esp_camera_sensor_get_info(&s->id);
    if (info && info->name) {
      strncpy(s_sensorName, info->name, sizeof(s_sensorName) - 1);
      s_sensorName[sizeof(s_sensorName) - 1] = '\0';
    }
    // L'OV3660 (montato su una parte delle Sense) esce di fabbrica
    // capovolto e slavato: e' la stessa correzione dell'esempio ufficiale.
    if (s->id.PID == OV3660_PID) {
      s->set_vflip(s, 1);
      s->set_brightness(s, 1);
      s->set_saturation(s, -2);
      s_vflip = true;
    }
  }

  s_ready = true;
  Serial.printf("[CAM] pronta: sensore %s, %s, qualita' %d\n",
                s_sensorName, SIZES[s_sizeIndex].name, s_quality);
  return true;
}

camera_fb_t* camera_grab() {
  if (!s_ready) return nullptr;
  return esp_camera_fb_get();
}

camera_fb_t* camera_grab_fresh() {
  if (!s_ready) return nullptr;
  for (int i = 0; i < CAM_STALE_FRAMES; i++) {
    camera_fb_t* stale = esp_camera_fb_get();
    if (!stale) return nullptr;
    esp_camera_fb_return(stale);
  }
  return esp_camera_fb_get();
}

void camera_release(camera_fb_t* fb) {
  if (fb) esp_camera_fb_return(fb);
}

int         camera_size_count()          { return SIZES_N; }
const char* camera_size_name(int index)  { return (index >= 0 && index < SIZES_N) ? SIZES[index].name : "?"; }
int         camera_size_index()          { return s_sizeIndex; }

bool camera_set_size_index(int index) {
  if (!s_ready || index < 0 || index >= SIZES_N) return false;
  sensor_t* s = esp_camera_sensor_get();
  if (!s || s->set_framesize(s, SIZES[index].fs) != 0) return false;
  s_sizeIndex = index;
  return true;
}

int camera_quality() { return s_quality; }

bool camera_set_quality(int q) {
  if (!s_ready || q < 10 || q > 40) return false;   // <10 rischia frame troncati
  sensor_t* s = esp_camera_sensor_get();
  if (!s || s->set_quality(s, q) != 0) return false;
  s_quality = q;
  return true;
}

void camera_set_flip(bool vflip, bool hmirror) {
  if (!s_ready) return;
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  s->set_vflip(s, vflip ? 1 : 0);
  s->set_hmirror(s, hmirror ? 1 : 0);
  s_vflip   = vflip;
  s_hmirror = hmirror;
}

bool camera_vflip()   { return s_vflip; }
bool camera_hmirror() { return s_hmirror; }
