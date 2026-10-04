#include <Arduino.h>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "audio_prueba.h"

static_assert(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE == 16000, "La muestra debe coincidir con la ventana del modelo");
static_assert(EI_CLASSIFIER_FREQUENCY == 16000, "El audio requiere 16 kHz");

// El SDK pide bloques de audio. Convertimos PCM a float sin normalizar a [-1,1]:
// el MFCC exportado espera la escala original de las muestras de 16 bits.
static int leer_audio(size_t offset, size_t length, float *out) {
    if (offset > 16000 || length > 16000 - offset) return -1;
    for (size_t i = 0; i < length; ++i) out[i] = audio_prueba[offset + i];
    return 0;
}

static void memoria(const char *momento) {
    Serial.printf("%s: libre=%u, mayor bloque=%u, minimo desde arranque=%u bytes\n",
                  momento, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
}

void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.printf("\nPrueba de inferencia: %s, CPU %u MHz\n", ESP.getChipModel(), ESP.getCpuFreqMHz());
    Serial.println("Audio fijo wall-e_006.wav, 1 s a 16 kHz. Sin microfono.");
}

void loop() {
    Serial.println("\n--- Nueva inferencia ---");
    memoria("Antes");
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = leer_audio;
    ei_impulse_result_t result = {};
    const uint32_t inicio = micros();
    const EI_IMPULSE_ERROR error = run_classifier(&signal, &result, false);
    const uint32_t elapsed = micros() - inicio;
    memoria("Despues");
    if (error != EI_IMPULSE_OK) {
        Serial.printf("ERROR de inferencia: %d\n", static_cast<int>(error));
    } else {
        Serial.printf("MFCC: %d ms | Red: %d ms | Total medido: %.2f ms\n",
                      result.timing.dsp, result.timing.classification, elapsed / 1000.0f);
        for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; ++i) {
            Serial.printf("%s: %.5f\n", result.classification[i].label, result.classification[i].value);
        }
        Serial.println("Inferencia completada correctamente.");
    }
    // Repetir permite comprobar estabilidad de memoria y abrir el monitor tarde.
    delay(5000);
}
