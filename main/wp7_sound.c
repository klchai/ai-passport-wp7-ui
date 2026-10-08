#include "wp7_sound.h"

#include <math.h>
#include <stdint.h>
#include "bsp_audio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* bsp_audio wakes the codec at 16 kHz, 16-bit mono unless told otherwise. */
#define SOUND_RATE_HZ      16000
#define SOUND_VOLUME       80
#define SOUND_CHUNK        256
#define SOUND_PEAK         10000
/* One chime is a rising C6-E6-G6 arpeggio of decaying bell tones, repeated
   after a pause until silenced or for at most a minute. */
#define CHIME_NOTE_MS      150
#define CHIME_GAP_MS       30
#define CHIME_PAUSE_MS     900
#define CHIME_ATTACK_MS    3
#define CHIME_DECAY_MS     70
#define RING_MAX_US        (60 * 1000000LL)
/* The I2S DMA holds about 90 ms; let it play out before the codec sleeps. */
#define TAIL_MS            120

static const char *TAG = "wp7_sound";
static const uint16_t s_chime_hz[] = { 1047, 1319, 1568 };

static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_ready;
static volatile bool s_ringing;
static int64_t s_ring_start_us;
static int16_t s_sine[256];
static uint32_t s_decay_q16;

/* Ringing stops when silenced or when its minute is up. */
static bool ring_active(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_ringing && esp_timer_get_time() - s_ring_start_us >= RING_MAX_US) {
        s_ringing = false;
    }
    const bool active = s_ringing;
    taskEXIT_CRITICAL(&s_lock);
    return active;
}

/* Writes `ms` of one bell tone (0 Hz for silence). Returns false when
   silenced or when the codec rejects the data. */
static bool play_tone(uint32_t hz, uint32_t ms, bool stop_when_silenced)
{
    int16_t pcm[SOUND_CHUNK];
    const uint32_t step = (uint32_t)(((uint64_t)hz << 32) / SOUND_RATE_HZ);
    const uint32_t attack = SOUND_RATE_HZ * CHIME_ATTACK_MS / 1000;
    uint32_t left = SOUND_RATE_HZ * ms / 1000;
    uint32_t phase = 0;
    uint32_t n = 0;
    uint32_t level = (uint32_t)SOUND_PEAK << 16;

    while (left > 0) {
        if (stop_when_silenced && !ring_active()) return false;
        const uint32_t count = left < SOUND_CHUNK ? left : SOUND_CHUNK;
        for (uint32_t i = 0; i < count; i++, n++) {
            int32_t amp = 0;
            if (hz != 0) {
                if (n < attack) {
                    amp = (int32_t)((uint64_t)SOUND_PEAK * n / attack);
                } else {
                    level = (uint32_t)(((uint64_t)level * s_decay_q16) >> 16);
                    amp = (int32_t)(level >> 16);
                }
            }
            pcm[i] = (int16_t)(s_sine[phase >> 24] * amp / 32767);
            phase += step;
        }
        if (bsp_audio_write(pcm, count * sizeof(pcm[0])) != ESP_OK) return false;
        left -= count;
    }
    return true;
}

static bool play_chime(void)
{
    for (size_t i = 0; i < sizeof(s_chime_hz) / sizeof(s_chime_hz[0]); i++) {
        if (!play_tone(s_chime_hz[i], CHIME_NOTE_MS, true) ||
                !play_tone(0, CHIME_GAP_MS, true)) {
            return false;
        }
    }
    return true;
}

static void sound_task(void *argument)
{
    (void)argument;
    /* The codec sleeps between alarms; it wakes at 16 kHz mono on demand. */
    esp_err_t err = bsp_audio_init();
    if (err == ESP_OK) {
        bsp_audio_set_volume(SOUND_VOLUME);
        err = bsp_audio_sleep();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker unavailable, timers stay silent: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    s_ready = true;

    for (;;) {
        if (!ring_active()) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        err = bsp_audio_wake();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "codec wake failed: %s", esp_err_to_name(err));
            wp7_sound_silence();
            continue;
        }
        while (ring_active() && play_chime()) {
            /* A silence() notifies, which ends the pause early. */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CHIME_PAUSE_MS));
        }
        play_tone(0, TAIL_MS, false);
        err = bsp_audio_sleep();
        if (err != ESP_OK) ESP_LOGW(TAG, "codec sleep failed: %s", esp_err_to_name(err));
    }
}

void wp7_sound_init(void)
{
    if (s_task) return;
    for (size_t i = 0; i < sizeof(s_sine) / sizeof(s_sine[0]); i++) {
        s_sine[i] = (int16_t)lrintf(32767.0f * sinf(2.0f * (float)M_PI * (float)i / 256.0f));
    }
    s_decay_q16 = (uint32_t)lrintf(65536.0f *
                                   expf(-1000.0f / (CHIME_DECAY_MS * (float)SOUND_RATE_HZ)));
    if (xTaskCreate(sound_task, "wp7_sound", 4096, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGW(TAG, "sound task unavailable, timers stay silent");
        s_task = NULL;
    }
}

void wp7_sound_ring(void)
{
    if (!s_ready) return;
    taskENTER_CRITICAL(&s_lock);
    s_ring_start_us = esp_timer_get_time();
    s_ringing = true;
    taskEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(s_task);
}

bool wp7_sound_silence(void)
{
    taskENTER_CRITICAL(&s_lock);
    const bool was_ringing = s_ringing;
    s_ringing = false;
    taskEXIT_CRITICAL(&s_lock);
    if (was_ringing && s_task) xTaskNotifyGive(s_task);
    return was_ringing;
}

bool wp7_sound_ringing(void)
{
    return ring_active();
}
