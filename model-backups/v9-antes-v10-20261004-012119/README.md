# Wall-E — build v5, prueba sin micrófono

## Cargar

Abrir esta carpeta en VS Code. PlatformIO > Project Tasks > esp32-c3 > General > Upload, luego Monitor a 115200 baudios. Upload reemplaza el firmware anterior. Puerto configurado: /dev/ttyACM0. Si no conecta, mantener BOOT, pulsar y soltar RESET, soltar BOOT y reintentar.

Copiar una ronda completa desde WALL-E BUILD v5 hasta RESUMEN v5. Cada ronda informa predicciones, coincidencias sin umbral, tiempos y memoria; se repite tras 10 segundos.

## Modelo y datos

Export wall-e-cpp-mcu-v5-impulse-#1. Labels: noise, unknown, walle (sin guion). Entrada: 1 segundo mono a 16 kHz. MFCC: 13 coeficientes, frame 0.02 s, stride 0.03 s, 20 filtros, FFT 256, normalización 49. Se mantiene la configuración genérica de CPU para comparar tiempos.

Se usan 18 grabaciones del dataset nuevo del 2 de octubre: seis por categoría, distribuidas a lo largo de la lista ordenada de archivos, más un control de silencio digital. El manifiesto audio_suite.csv identifica exactamente los archivos usados. Todos tienen 16000 muestras y se conservan sus amplitudes. Los audios antiguos ya no se usan en main.cpp.

No conocemos qué archivos fueron reservados para TEST en Edge Impulse. Estas muestras pueden incluir datos de entrenamiento: el resumen mide funcionamiento, no precisión independiente. Para reproducir el test de Studio hace falta su selección exacta de archivos. El silencio digital es solo un control y no sustituye ruido real.

## Archivos

- src/main.cpp: prueba de 19 casos y resumen; comparaciones adaptadas a walle.
- include/audio_suite.h: grabaciones constantes en flash; se lee una por vez sin copiarlas completas a RAM.
- tools/preparar_suite.py: regenerador de audios y manifiesto, Python estándar. Ejecutar desde cualquier carpeta; usa ../audio-dataset/dataset-nuevo-2026-10-02/resultado/listos relativo al proyecto.
- lib/wall_e_model: export v5 y library.json de PlatformIO.
- model-backups: versiones anteriores fuera de la compilación. El respaldo v3 incluye el modelo, programa y suite anterior.
- include/audio_prueba.h y tools/preparar_audio.py: archivos históricos, no usados por el programa actual.

La RAM estática del compilador no incluye reservas dinámicas. Comprobar en placa el mínimo histórico y la estabilidad de memoria entre rondas. No se incluye captura de micrófono ni Wi-Fi. Las puntuaciones repetidas con el mismo audio son esperables.

## Prueba de stream simulado (sin micrófono)

Entorno predeterminado: `esp32-c3-stream`. En Project Tasks elegir este entorno, Upload y Monitor. La prueba anterior permanece disponible en `esp32-c3`. Puerto actual en platformio.ini; revisar si cambia al reconectar.

`src/stream_test.cpp` usa run_classifier_continuous: 4000 muestras por fragmento (250 ms), conservando estado dentro de cada escenario. Ejecuta los 19 audios a desplazamientos 0/80/160/240 ms respecto al inicio de fragmentos, con 1.5 s de silencio previo más desplazamiento y al menos 1.5 s posterior. El WAV completo conserva amplitud y duración. El inicio real de voz puede estar dentro del WAV. Se reinicia el estado únicamente al cambiar escenario. La prueba termina tras 76 escenarios; RESET la repite.

Activación exploratoria: walle ganadora y puntuación >=0.80. No es un umbral calibrado ni una probabilidad garantizada. Se registra si hay alguna activación en ventanas cuyo contexto temporal nominal se cruza con el WAV; se cuentan aparte activaciones fuera de ese contexto. Es una aproximación temporal, ya que el DSP gestiona bordes de frames. La primera detección se expresa como fin de ventana en el stream virtual, no como latencia desde el inicio real de voz. Una palabra puede generar varias activaciones; el resumen cuenta escenarios, no eventos individuales.

Los resultados cero iniciales se etiquetan llenando. Promedio y máximo incluyen llamadas de llenado; las líneas por fragmento permiten evaluar también las llamadas listas. Se informa cualquier llamada superior a 250 ms. El stream se ejecuta lo más rápido posible con cesiones de 1 ms, no captura en tiempo real; cumplir 250 ms aquí no garantiza hacerlo con micrófono, DMA y otras tareas. Impresiones excluidas del cronómetro. No se modifican matemáticas del SDK; los cronómetros detallados se desactivan en este entorno. Las optimizaciones del camino de ventanas completas no necesariamente cubren todos los pasos del camino continuo.

Compartir el registro completo, especialmente RESULTADO y RESUMEN STREAM. Comprobar errores, memoria, detecciones para cada desplazamiento y falsos positivos. Son audios del proyecto, no evaluación independiente ni garantía de comportamiento con ruido real o voz continua. La ruta continua debe validarse: no se presupone que conserve las puntuaciones del clasificador por ventanas aisladas.

## Corrección: el modelo v5 requiere ventanas completas

La primera prueba continua falló en los 76 escenarios con frame_length 0.02 < frame_stride 0.03. Esa configuración no está admitida por extract_mfcc_per_slice. No hubo inferencias válidas y los tiempos de ~0.61 ms no miden el modelo. El registro mostró también descenso de heap durante errores repetidos, cuya causa no se atribuye sin investigación. Se archiva ese programa en model-backups/stream-continuo-incompatible.

El entorno esp32-c3-stream ahora utiliza run_classifier sobre ventanas completas de 1 segundo, con avance 400 ms y superposición 600 ms. Los cuatro desplazamientos del WAV son 0/100/200/300 ms; mantiene silencios y 76 escenarios. Recalcula MFCC por ventana: no es la ruta incremental run_classifier_continuous. Esta sección sustituye los parámetros de 250 ms y la descripción incremental anteriores. Se informan llamadas >400 ms. Ante cualquier error se detiene la prueba para no repetirlo. El firmware no cambia el modelo ni el SDK. El análisis temporal usa la ventana exacta entregada al clasificador, aunque inicio del WAV no equivale a inicio de voz. Umbral exploratorio 0.80, sin prueba de captura real.

## Modelo v6: ruta continua habilitada

El build activo es wall-e-cpp-mcu-v6-impulse-#1: frames MFCC 20 ms, paso 20 ms, 20 filtros, FFT 256 y CMVN 49. El entorno esp32-c3-stream vuelve a run_classifier_continuous con 4000 muestras (250 ms), 4 slices por contexto, offsets 0/80/160/240 ms, 76 escenarios y umbral provisional 0.80. Esta sección reemplaza la configuración v5 de ventanas completas. No reinicia estado entre slices de un escenario. Reporta además tiempo medio/máximo de llamadas con puntuaciones disponibles, excluyendo llenado inicial. Error de SDK detiene la prueba.

El SDK original v6 coincide con v5 archivo por archivo (comprobación binaria). Se preservaron las cinco modificaciones locales sobre esa misma base. tools/patches/sdk-local-v5-v6.patch reproduce exactamente esas modificaciones, incluyendo instrumentación y comprobaciones; se probó su aplicación contra v6 limpio. No aplicar sin comprobar compatibilidad a futuros SDK. El build/modelo anterior y firmware se respaldaron en model-backups/v5-antes-v6-continuo. Las pruebas en dispositivo de equivalencia/rendimiento del v6 quedan pendientes; los resultados de v5 no garantizan los de v6.

Métricas recibidas: test int8 45/49 (91.84%), walle 18/20, un noise clasificado walle; validación int8 32/39 (82.05%). Esto no es evaluación de streaming. Subir esp32-c3-stream, capturar el registro completo y revisar activaciones por desplazamiento, negativos, errores, heap y llamadas >250 ms. No modificar umbral antes de evaluar.

## Comparación v6 sobre WAV originales completos

Entorno predeterminado actual: esp32-c3-full. Elegir Upload y Monitor de ese entorno. Ejecuta run_classifier sobre los mismos 19 audios de 16000 muestras, sin desplazamientos, sin recorte, sin cambio de amplitud y sin silencios añadidos. Se conserva el control de silencio digital. Los cronómetros internos están desactivados; se mide la llamada completa. No cambia el modelo ni el SDK.

Cada caso informa puntuaciones, etiqueta ganadora y activación con la misma regla exploratoria del stream: walle ganadora y score >=0.80. El resumen distingue coincidencia de etiqueta de activación por umbral. Comparar 6 audios positivos y 13 negativos con los correspondientes escenarios desplazados del stream, no tratar las cuatro variantes como ejemplos independientes. El programa repite la ronda tras 10 s; compartir al menos una ronda completa. El entorno esp32-c3-stream permanece disponible.

## Audio real con v5 y detector de energía (entorno actual)

Entorno predeterminado: esp32-c3-real. Se restauró el v5 optimizado (frame 20 ms, stride 30 ms). El v6, fuentes y configuración previos están respaldados en model-backups/v6-antes-audio-real. Los entornos usan la librería activa v5; el viejo entorno incremental esp32-c3-stream no es compatible con ella y no debe usarse mientras v5 esté activo.

Archivo: audio-dataset/real_audio.73tte749.wav (fuera del proyecto, en la carpeta padre). Tiene 60 s PCM16 mono/16 kHz, con muestras no nulas. tools/preparar_stream_real.py extrae PCM a data/real_audio.pcm sin alterar amplitud ni tiempos. Se incrusta en flash, usando partición huge_app de 3 MB sin OTA en este entorno; no se escribe el audio en NVS ni se carga a un servicio externo.

src/real_audio_test.cpp reproduce 320 muestras cada 20 ms mediante una tarea FreeRTOS de prioridad 2. La tarea copia PCM a una cola de 50 bloques, capacidad 1 s; el consumidor procesa actividad y clasifica. Durante run_classifier la reproducción sigue entregando audio, permitiendo medir atrasos y pérdidas reales de la simulación. Las impresiones también pueden contribuir a llenar la cola. No incluye I2S/DMA ni garantiza rendimiento futuro con micrófono.

Calibra ruido con percentil 90 RMS de los primeros 5 s. Inicio: RMS >max(100,3*p90) durante 60 ms; fin: 260 ms por debajo de max(60,1.8*p90). Umbrales provisionales PCM16. Guarda 200 ms previos a los 60 ms que disparan. Conserva hasta 80 ms posteriores a la última actividad y centra ese segmento en una entrada de 1 s con ceros. No cambia velocidad ni volumen. Eventos mayores de 1 s se omiten y se informan, no se cuenta automáticamente que sean negativos. Puede unir palabras cercanas, cortar voz suave o disparar ante ruido: se trata de un detector de energía, no de voz.

Inferencia v5 por evento, activación walle ganadora y score>=0.80. Reporta inicio de buffer, fin estimado de actividad, cierre, puntuaciones, tiempo de inferencia y memoria. Los timestamps corresponden al audio de entrada, no a anotaciones manuales. La captura termina en 60 s; luego se vacía la cola. El resumen muestra eventos, inferencias, activaciones, omitidos, errores, bloques perdidos y cola observada. No se declara precisión automáticamente. Comparar la secuencia con las siete pronunciaciones esperadas; los ruidos también generan eventos. RESET repite la grabación.

Inspección previa en PC con la misma regla de energía: p90 inicial 113.75, 15 eventos, 14 <=1 s y uno de ~1.86 s. Tiempos de inicio aproximados: 6.40, 8.98, 11.56, 13.94, 16.90, 19.32, 21.22, 22.14 (largo), 25.66, 29.26, 32.50, 35.14, 37.86, 40.16, 43.22 segundos. No se usaron estos tiempos para segmentar en firmware. No detecta actividad en la pausa final con estos parámetros. Falta clasificación y validación en placa.


### Ensayo actual: bloques de 40 ms

esp32-c3-real define WALLE_AUDIO_BLOCK_MS=40 en platformio.ini. Admite 20,
30 o 40 y requiere recompilar. Cambia los bloques del detector, no el modelo,
los frames MFCC, la ventana de 1 s ni el umbral 0.80.
Con 40 ms: 640 muestras por bloque, cola de 25 bloques (1 s), inicio de 80 ms,
cierre tras 280 ms de silencio y calibración de 5 s. Conserva 200 ms previos
al inicio y hasta 80 ms posteriores a la actividad. Las esperas se redondean
hacia arriba. Con 30 ms: inicio 60 ms, cierre 270 ms, calibración 5010 ms y
cola 1020 ms. Con 20 ms se recuperan los parámetros anteriores.
La cabecera muestra tiempos efectivos. El RMS y su percentil de calibración
pueden cambiar al agrupar las muestras de otra forma.

Cargar con `platformio run -e esp32-c3-real -t upload -t monitor`.
Referencia de 20 ms: 15 eventos, 14 inferencias, 6 activaciones, 1 omitido,
0 bloques perdidos; según el orden hablado, 5/7 Wall-E y un falso positivo
con Walter. Comparar eventos y puntuaciones, no solo el total de activaciones.
Un bloque mayor reduce la resolución temporal y no garantiza mejor detección.


## Modelo activo v7

Se instaló wall-e-cpp-mcu-v7-impulse-#1. Se reemplazaron model-parameters y
 tflite-model completos, conservando library.json de PlatformIO y el SDK local.
El SDK original v7 coincide por SHA-256 en sus 1376 archivos con el original v6.
Se conservaron sin cambios las cinco modificaciones: classifier/ei_run_dsp.h,
dsp/mfcc_profile.h, dsp/numpy.hpp, dsp/speechpy/feature.hpp y
 dsp/speechpy/processing.hpp (cachés FFT/DCT/Mel, tablas DCT e instrumentación).
El parche tools/patches/sdk-local-v5-v6.patch sigue correspondiendo a esta base.
El manifiesto tools/patches/v7-preservation-manifest.json registra los hashes.
Respaldo anterior: model-backups/v5-antes-v7-20261003-200135.

Parámetros v7: entrada 1 s a 16 kHz, frame MFCC 20 ms, paso 30 ms, 20 filtros,
FFT 256, normalización 29. Continúa usando run_classifier; el entorno incremental
esp32-c3-stream no es compatible con este paso mayor que la longitud de frame.
La prueba predeterminada esp32-c3-real usa bloques del detector de 40 ms y
umbral 0.80. Cambiar WALLE_AUDIO_BLOCK_MS a 20 permite comparar v7 con los
mismos tiempos del detector usados en el registro anterior de v5. Modelo y
tamaño de bloque cambian en esta prueba; sus efectos no se pueden atribuir
por separado sin hacer esa comparación.

Métricas exportadas int8: validación 45/48 (93.75 %), test 58/61 (95.08 %).
Test: noise 19/19, unknown 18/18, walle 21/24 (1 noise, 2 unknown).
Son clasificaciones de Studio, no activaciones con nuestro umbral 0.80 ni
resultados de la grabación continua. La preservación del SDK se comprobó por
hashes; rendimiento y reconocimiento del v7 en placa requieren el nuevo ensayo.


## Prueba actual: las 13 palabras revisadas del audio continuo

Entorno predeterminado `esp32-c3-clips`: modelo v7 optimizado, run_classifier,
umbral 0.80. Usa las 7 palabras walle y 6 unknown revisadas por el usuario en
`../audio-dataset/revision-audio-continuo/palabras`. El CSV registra esa validación.
`tools/preparar_suite_continuo.py` verifica formato y hashes y genera
`include/audio_suite_continuo.h`, sin ceros añadidos ni cambios de amplitud.
La prueba clasifica cada segundo completo con su ambiente original; no ejecuta
el detector de energía ni simula captura. Mantiene el número de evento original
en cada nombre. Reporta etiquetas, activaciones, tiempos, memoria y errores.

Cargar `esp32-c3-clips` y compartir una ronda de 13 casos hasta RESUMEN v7.
La suite anterior de 19 casos sigue en `esp32-c3-full` y la simulación de la
grabación de 60 s sigue en `esp32-c3-real`. El SDK y la red no se modificaron.


## Comparación actual: v5 con las 13 palabras revisadas

Modelo activo restaurado: v5, desde model-backups/v5-antes-v7-20261003-200135.
Librería completa verificada por hashes contra ese respaldo; el SDK optimizado
coincide con el usado en v7. El v7 quedó guardado en v7-antes-comparacion-v5-20261003-203041 dentro de model-backups.
Se mantienen exactamente include/audio_suite_continuo.h (7 walle, 6 unknown),
run_classifier y el umbral 0.80. Entorno predeterminado: esp32-c3-clips.
La cabecera debe mostrar v5, 13 casos y normalización 49.
Referencia v7 sobre estos archivos: 3/7 walle ganadores, 0/7 activaciones,
1/6 negativos activados (hola), 303.06 ms de media, 0 errores.
Cargar esp32-c3-clips y compartir una ronda hasta RESUMEN v5.


## Modelo activo v8: misma suite de 13 palabras

Export v8 instalado conservando el SDK optimizado. Su SDK original coincide
con v7 y el parche aplicado sin fuzz produce exactamente nuestro SDK local.
Modelo y parámetros verificados contra el export; audios incrustados intactos.
Respaldo anterior: model-backups/v5-antes-v8-20261003-205142.
Normalización 49, frame 20 ms, stride 30 ms; entrada 16000 muestras a 16 kHz.
Cargar esp32-c3-clips: 13 casos, umbral 0.80, cabecera v8. No se cambia el detector
ni los archivos de audio para esta comparación.
Métricas Studio int8: test 60/61 (98.36 %), walle 23/24; validation 44/48
(91.67 %), walle 9/11. No equivalen a activaciones sobre nuestra grabación.


## Modelo activo v9: misma suite de 13 palabras

Export v9 instalado; SDK original idéntico a v8. El parche aplicado sin fuzz
a v9 limpio reproduce exactamente el SDK optimizado local conservado.
Modelo y parámetros verificados contra el export; audios incrustados intactos.
Respaldo anterior: model-backups/v8-antes-v9-20261003-214203.
MFCC: 13 coeficientes, frame 20 ms, stride 30 ms, 20 filtros, FFT 256,
normalización 49, frecuencias 0/0, preénfasis 0.98. Entrada 1 s a 16 kHz.
Entorno esp32-c3-clips, mismos 13 casos, umbral 0.80, cabecera v9.
Métricas Studio int8: test 53/55 (96.36 %), walle 19/21, sin negativos
clasificados walle; validación 43/47 (91.49 %), walle 9/10, tres negativos
clasificados walle. El tamaño del test cambió respecto de v8 (61 a 55), así
que los porcentajes no representan una comparación sobre los mismos ejemplos.
Resultados en placa pendientes; no se modificó la suite para esta comparación.
