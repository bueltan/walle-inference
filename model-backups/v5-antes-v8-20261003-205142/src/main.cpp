#ifndef WALLE_STREAM_TEST
#define WALLE_STREAM_TEST 0
#endif
#if !WALLE_STREAM_TEST && !defined(WALLE_REAL_AUDIO)
#include <Arduino.h>
#include <cstring>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#if defined(WALLE_REVIEWED_CLIPS) && WALLE_REVIEWED_CLIPS
#include "audio_suite_continuo.h"
#else
#include "audio_suite.h"
#endif

static_assert(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE == 16000, "Se requiere ventana de 1 s");
static_assert(EI_CLASSIFIER_FREQUENCY == 16000, "Se requiere audio de 16 kHz");
static const CasoAudio *actual = nullptr;
static constexpr size_t cantidad = sizeof(casos) / sizeof(casos[0]);

// El SDK recibe PCM convertido a float, sin cambiar la escala de amplitud.
// nullptr representa silencio digital; no se reserva otro buffer en RAM.
static int leer_audio(size_t offset, size_t length, float *out) {
    if (!actual || offset > 16000 || length > 16000 - offset) return -1;
    for (size_t i = 0; i < length; ++i)
        out[i] = actual->datos ? actual->datos[offset + i] : 0.0f;
    return 0;
}
static void memoria(const char *momento) {
    Serial.printf("%s: libre=%u, mayor bloque=%u, minimo desde arranque=%u bytes\n",
                  momento, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
}
// TOTAL incluye las etapas y el trabajo residual (allocaciones, bucles y relojes).
// No sumar TOTAL a las etapas: es una medición envolvente.
static void imprimir_perfil() {
#if WALLE_PROFILE_MFCC
    const auto &p = walle_profile::stats();
    if (p.mel_verified_frames) {
        Serial.printf("Mel comparacion bit a bit: %s | frames: %u | diferentes: %u | referencia: %.3f ms\n",
            p.mel_different_frames ? "DIFERENTE: se usa la original" : "IDENTICA",
            p.mel_verified_frames, p.mel_different_frames, p.mel_verify_us / 1000.0);
        Serial.printf("Mel sin comprobacion de referencia (aprox): %.3f ms\n",
            (static_cast<int64_t>(p.us[walle_profile::MEL])-p.mel_verify_us)/1000.0);
    }
    if (p.dct_verified) {
        Serial.printf("DCT comparacion bit a bit: %s | costo de referencia: %.3f ms\n",
                      p.dct_equal ? "IDENTICA" : "DIFERENTE: se usa la original", p.dct_verify_us / 1000.0);
        Serial.printf("DCT sin calculo de referencia: %.3f ms\n",
                      (static_cast<int64_t>(p.us[walle_profile::DCT])-p.dct_verify_us)/1000.0);
    }

    const char *names[] = {"Total interno", "Preparacion", "Preparar frames/filtros",
        "Lectura + preenfasis", "FFT + potencia", "Energia + filtros Mel",
        "Logaritmos", "DCT", "Energia/copia coeficientes", "Normalizacion CMVN"};
    uint64_t suma = 0;
    Serial.printf("--- Perfil MFCC: %u frames ---\n", p.frames);
    for (int i = 0; i < walle_profile::MAIN_COUNT; ++i) {
        Serial.printf("  %s: %.3f ms\n", names[i], p.us[i] / 1000.0);
        if (i) suma += p.us[i];
    }
    Serial.printf("  Residual: %.3f ms\n", (static_cast<int64_t>(p.us[0])-static_cast<int64_t>(suma))/1000.0);
    const char *fft_names[] = {"Buffers/copia", "Crear plan/tablas KISS", "Ejecutar KISS FFT",
                              "Liberar plan", "Magnitud (sqrt)", "Cuadrado/escala potencia"};
    uint64_t fft_sum = 0;
    Serial.printf("  Detalle FFT + potencia (%u llamadas KISS, %u planes creados):\n", p.fft_calls, p.plans_built);
    for (int i = walle_profile::MAIN_COUNT; i < walle_profile::FFT_END; ++i) {
        Serial.printf("    %s: %.3f ms\n", fft_names[i-walle_profile::MAIN_COUNT], p.us[i]/1000.0);
        fft_sum += p.us[i];
    }
    Serial.printf("    Residual FFT: %.3f ms\n", (static_cast<int64_t>(p.us[walle_profile::FFT])-static_cast<int64_t>(fft_sum))/1000.0);


    const char *cmvn_names[] = {"Reservas/preparacion", "Relleno simetrico inicial", "Medias de ventanas",
        "Restar medias", "Relleno tras centrar", "Desviaciones (total)", "Division final", "Escalado opcional"};
    uint64_t cmvn_sum = 0;
    Serial.println("  Detalle CMVN (incluido en Normalizacion CMVN):");
    for (int i = walle_profile::CMVN_PREP; i < walle_profile::CMVN_END; ++i) {
        Serial.printf("    %s: %.3f ms\n", cmvn_names[i-walle_profile::CMVN_PREP], p.us[i]/1000.0);
        cmvn_sum += p.us[i];
    }
    Serial.printf("    Residual CMVN: %.3f ms\n", (static_cast<int64_t>(p.us[walle_profile::NORM])-static_cast<int64_t>(cmvn_sum))/1000.0);
    const char *std_names[] = {"Media para desviacion", "Suma de diferencias al cuadrado", "Division y raiz"};
    uint64_t std_sum = 0;
    Serial.println("    Dentro de Desviaciones (no sumar otra vez):");
    for (int i = walle_profile::STD_MEAN; i < walle_profile::COUNT; ++i) {
        Serial.printf("      %s: %.3f ms\n", std_names[i-walle_profile::STD_MEAN], p.us[i]/1000.0);
        std_sum += p.us[i];
    }
    Serial.printf("      Residual desviaciones: %.3f ms\n", (static_cast<int64_t>(p.us[walle_profile::CMVN_STD])-static_cast<int64_t>(std_sum))/1000.0);

#endif
}
void setup() {
    Serial.begin(115200);
    delay(3000);
}
void loop() {
    unsigned correctas = 0, completadas = 0, errores = 0;
    unsigned positivas = 0, positivasCorrectas = 0, negativas = 0, falsasActivaciones = 0;
    unsigned activacionesPositivas = 0, activacionesNegativas = 0;
    double sumaMs = 0;
    float minimoMs = 1e9f, maximoMs = 0;
    Serial.printf("\n=== WALL-E v5 | AUDIOS COMPLETOS | %s | %u MHz | %u casos ===\n",
                  ESP.getChipModel(), ESP.getCpuFreqMHz(), static_cast<unsigned>(cantidad));
    Serial.println("run_classifier | WAV original 1 s | umbral exploratorio 0.80 + walle ganadora");
    Serial.printf("MFCC: %u filtros, normalizacion %u, stride %.3f s.\n",
                  unsigned(ei_dsp_config_1124860_4.num_filters),
                  unsigned(ei_dsp_config_1124860_4.win_size),
                  double(ei_dsp_config_1124860_4.frame_stride));
#if defined(WALLE_REVIEWED_CLIPS) && WALLE_REVIEWED_CLIPS
    Serial.println("DATASET: 13 palabras revisadas del audio continuo | 7 walle + 6 unknown");
    Serial.println("Ventanas de 1 s con ambiente original; sin detector ni ceros agregados.");
#endif
    Serial.println("Prueba funcional con audios del proyecto; NO mide precision independiente.");
    for (size_t n = 0; n < cantidad; ++n) {
        actual = &casos[n];
        Serial.printf("\n[%u/%u] %s | esperado=%s\n", static_cast<unsigned>(n+1),
                      static_cast<unsigned>(cantidad), actual->nombre, actual->esperado);
        memoria("Antes");
        signal_t signal;
        signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
        signal.get_data = leer_audio;
        ei_impulse_result_t result = {};
        walle_profile::reset();
        uint32_t inicio = micros();
        EI_IMPULSE_ERROR error = run_classifier(&signal, &result, false);
        float ms = (micros() - inicio) / 1000.0f;
        memoria("Despues");
        imprimir_perfil();
        if (error != EI_IMPULSE_OK) {
            ++errores;
            Serial.printf("ERROR: %d\n", static_cast<int>(error));
        } else {
            ++completadas;
            sumaMs += ms;
            if (ms < minimoMs) minimoMs = ms;
            if (ms > maximoMs) maximoMs = ms;
            Serial.printf("MFCC: %d ms | Red: %d ms | Total medido: %.2f ms\n",
                          result.timing.dsp, result.timing.classification, ms);
            size_t best = 0;
            for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; ++i) {
                Serial.printf("%s: %.5f\n", result.classification[i].label, result.classification[i].value);
                if (result.classification[i].value > result.classification[best].value) best = i;
            }
            const char *predicho = result.classification[best].label;
            const bool activado = strcmp(predicho, "walle") == 0 && result.classification[best].value >= 0.80f;
            if (activado) {
                if (strcmp(actual->esperado, "walle") == 0) ++activacionesPositivas;
                else ++activacionesNegativas;
            }
            Serial.printf("Activacion con umbral 0.80: %s\n", activado ? "SI" : "NO");
            bool coincide = strcmp(predicho, actual->esperado) == 0;
            if (coincide) ++correctas;
            // Se informa la etiqueta ganadora, sin aplicar umbral de activacion.
            if (strcmp(actual->esperado, "walle") == 0) {
                ++positivas;
                if (coincide) ++positivasCorrectas;
            } else {
                ++negativas;
                if (strcmp(predicho, "walle") == 0) ++falsasActivaciones;
            }
            Serial.printf("Ganadora: %s | %s\n", predicho, coincide ? "COINCIDE" : "NO COINCIDE");
        }
        delay(300);
    }
    Serial.printf("\n=== RESUMEN v5 ===\nCompletadas: %u/%u | Errores: %u\n",
                  completadas, static_cast<unsigned>(cantidad), errores);
    Serial.printf("Coincidencias: %u/%u | Wall-E reconocido: %u/%u\n",
                  correctas, completadas, positivasCorrectas, positivas);
    Serial.printf("Wall-E ganador en negativos: %u/%u\n", falsasActivaciones, negativas);
    Serial.printf("Con umbral 0.80: Wall-E=%u/%u | negativos activados=%u/%u\n",
                  activacionesPositivas, positivas, activacionesNegativas, negativas);
    if (completadas) Serial.printf("Tiempo total: promedio %.2f ms | minimo %.2f | maximo %.2f\n",
                                   sumaMs / completadas, minimoMs, maximoMs);
    memoria("Fin de ronda");
    Serial.println("Nueva ronda en 10 s. Copiar una ronda completa para comparar.");
    delay(10000);
}

#endif
