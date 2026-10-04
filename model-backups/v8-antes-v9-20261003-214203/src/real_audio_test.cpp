#if defined(WALLE_REAL_AUDIO) && WALLE_REAL_AUDIO
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

extern const uint8_t pcm_begin[] asm("_binary_data_real_audio_pcm_start");
extern const uint8_t pcm_end[] asm("_binary_data_real_audio_pcm_end");
// Bloques del detector; no cambia los frames MFCC del modelo.
#ifndef WALLE_AUDIO_BLOCK_MS
#define WALLE_AUDIO_BLOCK_MS 40
#endif
static constexpr size_t BLOCK_MS=WALLE_AUDIO_BLOCK_MS;
static_assert(BLOCK_MS==20 || BLOCK_MS==30 || BLOCK_MS==40,"Usar 20, 30 o 40 ms");
static constexpr size_t FS=16000, BLOCK=FS*BLOCK_MS/1000, CAPACITY=32000, WINDOW=16000;
// Redondear hacia arriba para no acortar las esperas.
static constexpr size_t ONSET_BLOCKS=(60+BLOCK_MS-1)/BLOCK_MS;
static constexpr size_t QUIET_BLOCKS=(260+BLOCK_MS-1)/BLOCK_MS;
static constexpr size_t CAL_BLOCKS=(5000+BLOCK_MS-1)/BLOCK_MS;
static constexpr size_t QUEUE_BLOCKS=(1000+BLOCK_MS-1)/BLOCK_MS;
static constexpr size_t PRE=FS*200/1000+ONSET_BLOCKS*BLOCK;
static_assert(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE==WINDOW && EI_CLASSIFIER_FREQUENCY==FS,"Modelo 1 s/16 kHz requerido");
struct Packet { uint32_t index; int16_t samples[BLOCK]; };
static QueueHandle_t queue;
static volatile bool finished=false;
static volatile uint32_t lost=0;
static int16_t history[PRE], event_audio[CAPACITY], input_audio[WINDOW];
static float calibration[CAL_BLOCKS];
static size_t hist_pos=0, event_size=0, last_active=0;
static unsigned onset=0, quiet=0, events=0, activations=0, skipped=0, errors=0, backlog=0;
static bool active=false, overflow=false;
static float high_threshold, low_threshold;
static uint32_t event_start=0;
static double inference_total=0;
static float inference_max=0;
static unsigned inferences=0;
static int read_window(size_t offset,size_t length,float *out) {
    if(offset>WINDOW || length>WINDOW-offset)return -1;
    for(size_t i=0;i<length;++i)out[i]=input_audio[offset+i];
    return 0;
}
static float rms(const int16_t *p) {
    double sum=0; for(size_t i=0;i<BLOCK;++i) {double x=p[i];sum+=x*x;}
    return sqrt(sum/BLOCK);
}
// Productor independiente: entrega PCM real cada BLOCK_MS ms aun durante inferencia.
static void capture(void*) {
    TickType_t next=xTaskGetTickCount();
    const size_t count=(pcm_end-pcm_begin)/(2*BLOCK);
    Packet packet;
    for(size_t n=0;n<count;++n) {
        vTaskDelayUntil(&next,pdMS_TO_TICKS(BLOCK_MS));
        packet.index=n;
        memcpy(packet.samples,pcm_begin+n*BLOCK*2,BLOCK*2);
        if(xQueueSend(queue,&packet,0)!=pdTRUE)++lost;
    }
    finished=true;
    vTaskDelete(nullptr);
}
static void classify_event(uint32_t end_sample) {
    ++events;
    // Mantener 80 ms posteriores a la ultima actividad, retirar solo el resto de la espera.
    const size_t used=std::min(event_size,last_active+size_t(1280));
    Serial.printf("\nEVENTO %u | inicio buffer=%.3f s | fin actividad=%.3f s | cierre=%.3f s | muestras=%u\n",
        events,event_start/double(FS),(event_start+last_active)/double(FS),end_sample/double(FS),unsigned(used));
    if(overflow || used>WINDOW) {
        ++skipped; Serial.println("OMITIDO: evento demasiado largo para 1 s; no se recorta voz ni se estira.");return;
    }
    memset(input_audio,0,sizeof(input_audio));
    const size_t padding=(WINDOW-used)/2;
    memcpy(input_audio+padding,event_audio,used*sizeof(int16_t));
    signal_t signal;signal.total_length=WINDOW;signal.get_data=read_window;
    ei_impulse_result_t result={};
    uint32_t begin=micros();auto err=run_classifier(&signal,&result,false);
    float ms=(micros()-begin)/1000.0f;
    ++inferences;inference_total+=ms;inference_max=std::max(inference_max,ms);
    if(err!=EI_IMPULSE_OK){++errors;Serial.printf("ERROR inferencia=%d\n",int(err));return;}
    size_t best=0;
    for(size_t i=0;i<EI_CLASSIFIER_LABEL_COUNT;++i) {
        Serial.printf("%s=%.5f ",result.classification[i].label,result.classification[i].value);
        if(result.classification[i].value>result.classification[best].value)best=i;
    }
    bool detected=strcmp(result.classification[best].label,"walle")==0 && result.classification[best].value>=0.80f;
    if(detected)++activations;
    Serial.printf("| %s | %.2f ms | cola=%u | heap=%u\n",detected?"ACTIVACION":"SIN ACTIVACION",ms,unsigned(uxQueueMessagesWaiting(queue)),ESP.getFreeHeap());
}
void setup() {
    Serial.begin(115200);delay(3000);
    Serial.println("\n=== V8 | AUDIO REAL | DETECTOR ENERGIA + run_classifier ===");
    Serial.printf("Duracion=%.2f s | bloques=%u ms | umbral clasificador=0.80\n",(pcm_end-pcm_begin)/32000.0,unsigned(BLOCK_MS));
    Serial.printf("Inicio=%u ms | silencio de cierre=%u ms | calibracion=%u ms | cola=%u ms\n",
        unsigned(ONSET_BLOCKS*BLOCK_MS),unsigned(QUIET_BLOCKS*BLOCK_MS),unsigned(CAL_BLOCKS*BLOCK_MS),unsigned(QUEUE_BLOCKS*BLOCK_MS));
    queue=xQueueCreate(QUEUE_BLOCKS,sizeof(Packet)); // 1 segundo de captura en espera.
    if(!queue){Serial.println("ERROR creando cola");return;}
    if(xTaskCreate(capture,"audio_replay",3072,nullptr,2,nullptr)!=pdPASS) {
        Serial.println("ERROR creando productor");vQueueDelete(queue);queue=nullptr;
    }
}
void loop() {
    if(!queue){delay(1000);return;}
    static uint32_t expected=0;
    Packet p;
    if(xQueueReceive(queue,&p,pdMS_TO_TICKS(100))==pdTRUE) {
        backlog=std::max(backlog,unsigned(uxQueueMessagesWaiting(queue)));
        if(p.index!=expected) {
            Serial.printf("HUECO: esperado bloque %u recibido %u; se cancela evento pendiente\n",expected,p.index);
            if(active)++skipped;
            active=false;onset=quiet=0;memset(history,0,sizeof(history));hist_pos=0;
        }
        expected=p.index+1;
        float energy=rms(p.samples);
        for(size_t i=0;i<BLOCK;++i){history[hist_pos]=p.samples[i];hist_pos=(hist_pos+1)%PRE;}
        if(p.index<CAL_BLOCKS) {
            calibration[p.index]=energy;
            if(p.index==CAL_BLOCKS-1) {
                std::sort(calibration,calibration+CAL_BLOCKS);
                float noise=calibration[CAL_BLOCKS*90/100]; // Percentil 90 del ruido inicial.
                high_threshold=std::max(100.0f,noise*3.0f);
                low_threshold=std::max(60.0f,noise*1.8f);
                Serial.printf("CALIBRADO RMS p90=%.2f | inicio=%.2f | fin=%.2f\n",noise,high_threshold,low_threshold);
            }
            return;
        }
        if(!active) {
            onset=energy>high_threshold?onset+1:0;
            if(onset>=ONSET_BLOCKS) { // Historial incluye 200 ms anteriores a la secuencia de inicio.
                active=true;overflow=false;quiet=0;event_size=PRE;last_active=PRE;
                event_start=(p.index+1)*BLOCK-PRE;
                for(size_t i=0;i<PRE;++i)event_audio[i]=history[(hist_pos+i)%PRE];
            }
        } else {
            if(event_size+BLOCK<=CAPACITY) {memcpy(event_audio+event_size,p.samples,sizeof(p.samples));event_size+=BLOCK;}
            else overflow=true;
            if(energy>low_threshold){quiet=0;last_active=event_size;}else ++quiet;
            if(quiet>=QUIET_BLOCKS){classify_event((p.index+1)*BLOCK);active=false;onset=quiet=0;}
        }
    } else if(finished) {
        if(active){classify_event(expected*BLOCK);active=false;}
        Serial.printf("\nRESUMEN REAL: eventos=%u | inferencias=%u | activaciones=%u | omitidos=%u | errores=%u\n",events,inferences,activations,skipped,errors);
        Serial.printf("Audio perdido=%u bloques | cola maxima observada=%u/%u | media=%.2f ms | max=%.2f ms | heap=%u minimo=%u\n",lost,backlog,unsigned(QUEUE_BLOCKS),inferences?inference_total/inferences:0,inference_max,ESP.getFreeHeap(),ESP.getMinFreeHeap());
        Serial.println("Fin. No se asignan aciertos automaticamente: comparar tiempos con la grabacion. RESET repite.");
        vQueueDelete(queue);queue=nullptr;
    }
}
#endif
