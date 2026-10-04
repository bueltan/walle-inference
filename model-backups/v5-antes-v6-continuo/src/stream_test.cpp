#if defined(WALLE_STREAM_TEST) && WALLE_STREAM_TEST
#include <Arduino.h>
#include <cstring>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "audio_suite.h"

static constexpr size_t rate = EI_CLASSIFIER_FREQUENCY;
static constexpr size_t window = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
static constexpr size_t slice = window;
static constexpr size_t hop = rate * 400 / 1000;
static_assert(rate == 16000 && window == 16000,
              "Esta prueba requiere ventana 1 s a 16 kHz");
static constexpr float threshold = 0.80f; // Exploratorio: no calibrado.
static const CasoAudio *clip;
static size_t clip_start, slice_start;

// Stream virtual: silencio inicial + WAV completo sin modificar + silencio final.
// No reserva el stream completo en RAM ni accede fuera del fragmento solicitado.
static int read_stream(size_t offset, size_t length, float *out) {
    if (offset > slice || length > slice-offset) return -1;
    for (size_t i=0; i<length; ++i) {
        size_t t = slice_start + offset + i;
        out[i] = clip->datos && t >= clip_start && t-clip_start < window
            ? clip->datos[t-clip_start] : 0.0f;
    }
    return 0;
}
void setup() {
    Serial.begin(115200);
    delay(3000);
}
void loop() {
    const unsigned shifts_ms[] = {0,100,200,300};
    unsigned positive=0, detected=0, negative=0, false_positive=0;
    unsigned failures=0, overruns=0, calls=0, silence_hits=0;
    double total_ms=0; float max_ms=0;
    Serial.println("\n=== STREAM SIMULADO | 16 kHz | ventanas 1 s | avance 400 ms ===");
    Serial.println("Umbral exploratorio 0.80 + walle ganadora; sin microfono, sin ritmo real.");
    Serial.println("No mide precision independiente. Desplazamiento del WAV, no inicio exacto de voz.");
    for (const auto &audio : casos) for (unsigned shift : shifts_ms) {
        clip=&audio;
        clip_start = rate*3/2 + shift*rate/1000;
        const size_t end = clip_start + window + rate*3/2;
        const size_t slices = (end-window+hop-1)/hop+1;
        // Cada ventana se evalua completa con el mismo preprocesamiento del entrenamiento.
        bool hit=false, failed=false;
        float peak=0;
        int first_end_ms=-1;
        unsigned scenario_calls=0;
        double scenario_ms=0; float scenario_max=0;
        Serial.printf("\nCASO %s | esperado=%s | desplazamiento=%u ms | WAV inicia=%u ms\n",
            audio.nombre,audio.esperado,shift,unsigned(clip_start*1000/rate));
        for (size_t n=0;n<slices;++n) {
            slice_start=n*hop;
            signal_t signal; signal.total_length=slice; signal.get_data=read_stream;
            ei_impulse_result_t result={};
            uint32_t begin=micros();
            auto error=run_classifier(&signal,&result,false);
            float ms=(micros()-begin)/1000.0f;
            ++calls; ++scenario_calls; total_ms+=ms; scenario_ms+=ms;
            if(ms>max_ms) max_ms=ms;
            if(ms>scenario_max) scenario_max=ms;
            if(ms>400) ++overruns;
            if(error!=EI_IMPULSE_OK) {
                Serial.printf("ERROR slice=%u codigo=%d\n",unsigned(n),int(error));
                ++failures; failed=true;
                Serial.println("Prueba detenida por error; no se continuan otros escenarios.");
                while(true) delay(1000);
            }
            size_t best=0; float score=0, sum=0;
            for(size_t j=0;j<EI_CLASSIFIER_LABEL_COUNT;++j) {
                sum+=result.classification[j].value;
                if(result.classification[j].value>result.classification[best].value) best=j;
                if(result.classification[j].label && strcmp(result.classification[j].label,"walle")==0) score=result.classification[j].value;
            }
            // Una ventana completa debe producir puntuaciones al terminar.
            bool ready=sum>0;
            size_t ending=slice_start+slice;
            bool overlap=ending>=window && ending>clip_start && ending-window<clip_start+window;
            bool activation=ready && score>=threshold && result.classification[best].label && strcmp(result.classification[best].label,"walle")==0;
            if(ready && overlap) {
                if(score>peak) peak=score;
                if(activation && !hit) { hit=true; first_end_ms=int(ending*1000/rate); }
            }
            if(activation && !overlap) ++silence_hits;
            Serial.printf("slice=%u fin=%u ms | %.2f ms | walle=%.5f | %s%s\n",
                unsigned(n),unsigned(ending*1000/rate),ms,score,ready?"lista":"sin puntuaciones",
                activation?" | ACTIVACION":"");
            delay(1); // Ceder CPU; no simula el tiempo de captura.
        }
        if(!failed) {
            if(strcmp(audio.esperado,"walle")==0) {++positive; if(hit) ++detected;}
            else {++negative; if(hit) ++false_positive;}
        }
        Serial.printf("RESULTADO %s | pico=%.5f | primera ventana detectada termina=%d ms | media=%.2f max=%.2f ms | heap=%u minimo=%u\n",
            failed?"ERROR":hit?"ACTIVADO":"SIN ACTIVACION",peak,first_end_ms,
            scenario_ms/scenario_calls,scenario_max,ESP.getFreeHeap(),ESP.getMinFreeHeap());
    }
    Serial.printf("\nRESUMEN STREAM: walle=%u/%u | negativos activados=%u/%u | activaciones fuera del WAV/contexto=%u | errores=%u\n",
        detected,positive,false_positive,negative,silence_hits,failures);
    Serial.printf("Llamadas=%u | promedio=%.2f ms | maximo=%.2f ms | llamadas >400 ms=%u\n",
        calls,total_ms/calls,max_ms,overruns);
    Serial.println("Fin. Repetir con RESET. Los tiempos excluyen impresiones y captura real.");
    while(true) delay(1000);
}
#endif
