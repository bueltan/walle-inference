#pragma once
#include <stdint.h>
#include "../porting/ei_classifier_porting.h"

// Instrumentación local de Wall-E. No modifica el audio ni los cálculos.
// Acumula microsegundos por etapa; se imprime DESPUÉS de la inferencia.
#ifndef WALLE_PROFILE_MFCC
#define WALLE_PROFILE_MFCC 1
#endif
namespace walle_profile {
enum Stage { TOTAL, PREP, MFE_PREP, READ, FFT, MEL, LOG, DCT, COPY, NORM, COUNT };
struct Stats { uint64_t us[COUNT] = {}; uint32_t frames = 0; };
inline Stats &stats() { static Stats value; return value; }
inline void reset() { stats() = Stats{}; }
// El reloj del port Arduino usa micros(). La resta uint32_t soporta su wrap.
inline uint32_t now() { return static_cast<uint32_t>(ei_read_timer_us()); }
class Timer {
public:
    explicit Timer(Stage stage) : stage_(stage) {
#if WALLE_PROFILE_MFCC
        start_ = now();
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
