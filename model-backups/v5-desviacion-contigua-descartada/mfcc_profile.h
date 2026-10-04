#pragma once
#include <stdint.h>
#include "../porting/ei_classifier_porting.h"

// Instrumentación local de Wall-E. No modifica el audio ni los cálculos.
// Acumula microsegundos por etapa; se imprime DESPUÉS de la inferencia.
#ifndef WALLE_PROFILE_MFCC
#define WALLE_PROFILE_MFCC 1
#endif
#ifndef WALLE_VERIFY_MEL
#define WALLE_VERIFY_MEL 1
#endif
#ifndef WALLE_VERIFY_STD
#define WALLE_VERIFY_STD 1
#endif
namespace walle_profile {
enum Stage { TOTAL, PREP, MFE_PREP, READ, FFT, MEL, LOG, DCT, COPY, NORM, MAIN_COUNT, FFT_BUFFER = MAIN_COUNT, FFT_PLAN, FFT_EXEC, FFT_FREE, FFT_MAG, FFT_POWER, FFT_END, CMVN_PREP = FFT_END, CMVN_PAD1, CMVN_MEAN, CMVN_SUB, CMVN_PAD2, CMVN_STD, CMVN_DIV, CMVN_SCALE, CMVN_END, STD_MEAN = CMVN_END, STD_SUMSQ, STD_SQRT, COUNT };
struct Stats { uint32_t std_checks = 0; uint32_t std_different = 0; uint32_t std_reference_us = 0; uint32_t mel_verified_frames = 0; uint32_t mel_different_frames = 0; uint32_t mel_verify_us = 0; uint64_t us[COUNT] = {}; uint32_t dct_verify_us = 0; bool dct_verified = false; bool dct_equal = false; uint32_t frames = 0; uint32_t fft_calls = 0; uint32_t plans_built = 0; bool in_power = false; bool in_cmvn = false; };
inline Stats &stats() { static Stats value; return value; }
inline void reset() { stats() = Stats{}; }
// El reloj del port Arduino usa micros(). La resta uint32_t soporta su wrap.
inline uint32_t now() { return static_cast<uint32_t>(ei_read_timer_us()); }
class Timer {
public:
    explicit Timer(Stage stage) : stage_(stage) {
        active_ = stage < MAIN_COUNT || (stage < FFT_END ? stats().in_power : stats().in_cmvn);
#if WALLE_PROFILE_MFCC
        if (active_) start_ = now();
#endif
    }
    void stop() {
#if WALLE_PROFILE_MFCC
        if (active_) { stats().us[stage_] += static_cast<uint32_t>(now()-start_); active_ = false; }
#endif
    }
    ~Timer() { stop(); } // También cierra la medición ante un retorno por error.
private:
    Stage stage_;
    uint32_t start_ = 0;
    bool active_ = true;
};
}
