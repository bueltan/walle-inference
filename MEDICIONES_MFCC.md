# Medición interna de MFCC

## Recorrido de ejecución

1. `src/main.cpp` reinicia los contadores antes de `run_classifier`, mide la inferencia completa e imprime el perfil después. No hay impresiones dentro de los bucles DSP.
2. `lib/wall_e_model/edge-impulse-sdk/classifier/ei_run_dsp.h`: `extract_mfcc_features` prepara el preénfasis y las dimensiones, llama a `mfcc` y aplica normalización CMVN.
3. `lib/wall_e_model/edge-impulse-sdk/dsp/speechpy/feature.hpp`: `mfcc` llama a `mfe` (preparación, lectura por frames, FFT/espectro y filtros Mel), luego aplica logaritmos, DCT y selección de coeficientes.
4. `lib/wall_e_model/edge-impulse-sdk/dsp/speechpy/processing.hpp`: `cmvnw` implementa normalización. Se mide la llamada completa desde ei_run_dsp.h.

## Cómo funciona el cronómetro

`edge-impulse-sdk/dsp/mfcc_profile.h` define contadores de microsegundos y la clase Timer. Al construirla guarda el reloj. `stop()` suma el tiempo transcurrido una sola vez. Si no se llama explícitamente, su destructor lo hace al salir del bloque, incluso ante un retorno por error. En etapas por frame los tiempos se suman a lo largo de toda la ventana.

Ejemplo:

```cpp
walle_profile::Timer fft_timer(walle_profile::FFT);
// llamada a power_spectrum(...)
fft_timer.stop();
```

`Lectura + preenfasis` incluye el callback que lee las muestras y aplica preénfasis. `FFT + potencia` incluye toda la llamada power_spectrum, no solo FFT. `Energia + filtros Mel` incluye suma de energía y aplicación de filtros. Preparar frames/filtros incluye construir tablas y reservar buffers. Normalización incluye media y varianza locales.

Total interno envuelve el extractor completo: NO se suma a las otras etapas. Residual = total interno menos etapas registradas; contiene preparación restante de matrices, liberaciones, bucles y costo de instrumentación no atribuido. El total del SDK puede diferir por sus límites de medición y redondeo. Ante errores, los contadores muestran un perfil parcial.

## Validación en placa

Cargar con Upload y abrir Monitor. Comparar las puntuaciones de los mismos 19 casos con v5 sin instrumentación. Copiar una ronda completa. Primero comprobar que no cambian las predicciones; después identificar la etapa dominante. Estos cronómetros añaden algo de tiempo, por lo que para verificar una optimización final se debe repetir sin instrumentación.

Para desactivar timers e impresión del perfil, agregar a build_flags en platformio.ini:

```
-D WALLE_PROFILE_MFCC=0
```

No se alteraron modelo, datos de entrada ni operaciones matemáticas. Compilar no garantiza igualdad en ejecución: falta comprobarla en placa. La instrumentación es global y está pensada para una inferencia a la vez; no es segura para múltiples inferencias concurrentes. El reloj usa microsegundos y resta sin signo de 32 bits, válida para intervalos inferiores a unos 71 minutos.

## Respaldo y futuras exportaciones

Los dos headers originales están en model-backups/v5-antes-perfil. tools/mfcc_instrumentacion.patch documenta sus cambios. El header nuevo mfcc_profile.h debe conservarse también. Reemplazar el SDK con otro build elimina esta instrumentación: reaplicarla revisando el código nuevo antes de compilar. No se ha aplicado ninguna optimización todavía.

## Segunda medición: detalle de FFT y potencia

Se instrumentó `edge-impulse-sdk/dsp/numpy.hpp`:

- `rfft` con salida float: reserva de salida compleja y cálculo de magnitud mediante raíz cuadrada.
- `rfft` con salida compleja: reserva, copia y relleno de entrada.
- `software_rfft`: crear plan/tablas con kiss_fftr_alloc, ejecutar kiss_fftr y liberar el plan.
- `power_spectrum`: cuadrado y escala de la magnitud para obtener potencia.

Los subcontadores solo se activan dentro de power_spectrum para no mezclar FFT usadas por otras etapas como DCT. El detalle está contenido en FFT + potencia; NO se suma al total de las etapas principales. Residual FFT incluye selección del backend, liberación de buffers, verificaciones y costo de instrumentación. Las liberaciones automáticas de los buffers pueden quedar parcialmente dentro del cronómetro de la etapa que los precede; el cronómetro Liberar plan mide específicamente la liberación del plan KISS.

Se cuenta cuántas llamadas a KISS corresponden al espectro. Este build usa operaciones genéricas, por lo que se esperan 33 llamadas para los 33 frames. Un backend distinto podría cambiar ese número y dejar parte del tiempo en residual.

No se sustituyeron operaciones matemáticas ni se reutilizó memoria todavía. Verificar las puntuaciones con los mismos 19 casos antes de optimizar. Respaldo de numpy.hpp, mfcc_profile.h y main.cpp anterior a esta ampliación: model-backups/v5-antes-perfil-fft.

## Optimización: reutilizar el plan de FFT dentro de MFE

Se agregó `numpy::ReusableFFTPlan` en numpy.hpp. MFE crea un objeto local antes del bucle de frames y lo pasa a power_spectrum -> rfft -> software_rfft. La primera llamada construye las tablas; las siguientes reutilizan el mismo plan. El destructor libera el plan al terminar MFE, incluso ante un retorno por error. Si se pidiera otro tamaño, el plan se reconstruye. No se comparte entre inferencias ni cambia la ruta de llamadas que no recibe este objeto (por ejemplo otras transformaciones).

Se siguen ejecutando las 33 FFT y todas las operaciones matemáticas originales; se evita reconstruir las mismas tablas 33 veces. El registro debe indicar 33 llamadas KISS y 1 plan creado. Crear plan/tablas ahora incluye las consultas del plan ya existente. La liberación final del plan ocurre fuera del tramo FFT + potencia y queda en el residual principal; Liberar plan ya no representa esa liberación final.

Mantener el plan durante la ventana prolonga su vida en RAM. Se libera antes de normalización. La memoria pico se debe volver a medir en placa, aunque no se esperan asignaciones persistentes tras la inferencia.

Compilación completada correctamente. Pendiente verificación en ESP32: comparar las puntuaciones de los 19 casos con el registro anterior, tiempos de primera y siguientes ventanas y memoria al final de varias rondas. No se afirma todavía equivalencia numérica ni ahorro medido. Respaldo de los headers y main.cpp anteriores: model-backups/v5-antes-cache-fft.

## DCT: reutilización con comparación exacta

La DCT matricial ahora crea un ReusableFFTPlan local y lo pasa a dct2 por fila, dct_transform y rfft. No cambian las expresiones matemáticas ni el orden por fila. Este plan es independiente del usado para el espectro y se libera al terminar la DCT.

WALLE_VERIFY_DCT está activado por defecto: antes de procesar se copia la entrada, se calcula la DCT optimizada y después la original sobre esa copia. memcmp compara todos los coeficientes bit por bit. Si hay alguna diferencia, se copian los coeficientes originales a la salida para continuar la inferencia con ellos. El monitor imprime IDENTICA o DIFERENTE. Si falla la reserva o cálculo se devuelve error.

La verificación cuesta RAM temporal (en este modelo, 33 x 20 x 4 = 2640 bytes) y tiempo adicional. Durante esta prueba los tiempos totales y DCT incluyen ambas rutas: NO compararlos directamente con la versión anterior para juzgar velocidad. La línea DCT sin calculo de referencia resta solo el cálculo de referencia cronometrado; sigue incluyendo copia, comparación, asignaciones e instrumentación, así que es aproximada.

Después de comprobar IDENTICA en todos los casos, agregar -D WALLE_VERIFY_DCT=0 a build_flags para medir el ahorro real sin doble cálculo. No se desactiva automáticamente. Comparar también puntuaciones y memoria en placa. La comparación prueba igualdad de los coeficientes en estas entradas, no una garantía universal de precisión en cualquier audio. Respaldo de numpy.hpp y main.cpp anterior: model-backups/v5-antes-cache-dct.

## Verificación en placa y medición sin duplicación

El registro del usuario mostró 32 comparaciones DCT IDENTICA y 19/19 coincidencias por ronda. Las últimas 19 puntuaciones impresas coinciden con el registro anterior. La DCT optimizada, descontando cálculo de referencia, tardó aproximadamente 35.7 ms en audio real frente a 64.3 ms de la original. Estas cifras aún incluyen parte de la instrumentación y la copia de verificación.

Se configuró WALLE_VERIFY_DCT=0 en platformio.ini para medir la versión sin cálculo duplicado. Se conservan los cronómetros MFCC. Para reactivar comparación, cambiarlo a 1. Igualdad comprobada en las entradas ensayadas, no prueba exhaustiva de todos los audios posibles.

## Pesos Mel reutilizados por ventana

La función MFE en `dsp/speechpy/feature.hpp` calcula una tabla compacta de pesos antes de recorrer los frames. Conserva las expresiones originales de división, el tratamiento del bin central y el orden de suma. Solo almacena los pesos que se utilizan; la tabla se libera al salir de MFE, incluso ante errores. La memoria adicional es cuatro bytes por peso almacenado, más 80 bytes de referencia para los 20 filtros cuando está activa la comprobación.

`WALLE_VERIFY_MEL=1` en platformio.ini activa la comparación bit a bit de los 20 resultados Mel de cada frame, antes de zero_handling y logaritmos. Si un frame difiere, se reemplaza su salida por la original. El monitor muestra IDENTICA/DIFERENTE, cantidad de frames comparados, diferencias y tiempo de referencia. Se esperan 33 frames y cero diferencias por audio, en los 19 casos. Esto verifica estos casos, no todos los audios posibles.

La preparación de pesos está incluida en Preparar frames/filtros; el cálculo duplicado y la comparación están incluidos en Energia + filtros Mel. La cifra que resta la referencia es aproximada. Después de validar todos los casos, cambiar WALLE_VERIFY_MEL a 0, recompilar y medir una ronda completa para conocer el ahorro real y el mínimo de heap. No se cambia esta opción automáticamente.

Respaldo anterior: `model-backups/v5-antes-cache-mel/` (feature.hpp, mfcc_profile.h, main.cpp y platformio.ini). El PR de upstream no incluye este experimento.

## Validación Mel en placa y siguiente medición

El registro 50aee5e4 contiene una ronda completa de 19 casos con 33 frames cada uno: 627 frames idénticos bit a bit y cero diferencias. Clasificación: 19/19 coincidencias, 6/6 walle y cero activaciones en los 13 negativos. Heap libre estable en 307540 bytes y mínimo histórico 294920 bytes. Tiempo medio con verificación: 377.23 ms. El tramo Mel sin referencia se estima en unos 13.07 ms para audio real, frente a unos 33.16 ms antes; preparar filtros pasó de unos 0.76 a 1.45 ms. Estas estimaciones no sustituyen una medición sin doble cálculo.

Se desactiva WALLE_VERIFY_MEL en platformio.ini para la siguiente prueba. Se conserva el código de comparación para reactivarlo con valor 1. Falta cargar y medir la versión sin verificación en el ESP32.

## Medición Mel sin verificación en ESP32

Registro f599fd4f: último resumen, media 346.55 ms, mínimo 283.34 ms (silencio), máximo 350.59 ms. Frente a 365.22 ms antes de reutilizar pesos, ahorro medio 18.67 ms (5.11%). Energia + filtros Mel ronda 12.84 ms en audio real, frente a 33.16 ms anteriores; preparación de frames/filtros ronda 1.45 ms. Los resúmenes indican 19/19 coincidencias, 6/6 walle, cero activaciones en 13 negativos y cero errores. Las puntuaciones impresas de los casos visibles coinciden con el registro de verificación. Heap libre estable 307540 bytes, bloque mayor 278516, mínimo histórico 295016. Se mantiene instrumentación de tiempos; no es una evaluación independiente de precisión.

## Tablas de senos y cosenos para DCT

`numpy.hpp` calcula una tabla DCTRotation local por llamada a DCT matricial. Se conserva el ángulo float original y el tipo de retorno de cos/sin mediante decltype; no se fuerzan los resultados a float. Se conserva el orden de multiplicaciones, suma/resta y normalización. La tabla se libera automáticamente al retornar. Con 20 columnas ocupa 20 * sizeof(DCTRotation) bytes (320 si ambos resultados son double, 160 si son float).

WALLE_VERIFY_DCT vuelve a 1. La referencia usa el plan FFT reutilizado ya validado, pero recalcula los senos/cosenos por fila como antes. Compara los 33 x 20 coeficientes bit a bit y conserva la referencia si difieren. El tiempo de verificación está incluido en DCT; no comparar el tiempo total con 346.55 ms para evaluar ahorro hasta desactivar nuevamente la comprobación. WALLE_VERIFY_MEL permanece en 0.

Prueba host: `test/dct_rotations_host.cpp`, 48 matrices de 33 filas, tamaños 20/32/64/256 y silencio/señales generadas, igualdad bit a bit con la ruta sin tablas. Compilada con GCC C++11 -O2, WALLE_PROFILE_MFCC=0 y WALLE_VERIFY_DCT=0, incluyendo kiss_fft.cpp y kiss_fftr.cpp y un directorio de includes cuyo enlace edge-impulse-sdk apunta al SDK local. Esta prueba no sustituye la validación en placa.

Respaldo anterior: model-backups/v5-antes-tablas-dct. Próximo paso: cargar firmware, comprobar DCT IDENTICA en los 19 casos, puntuaciones y heap. Después desactivar verificación y medir el ahorro real. No se modificó el PR upstream.

## Validación de tablas DCT en placa

Los registros bc63cde3 y 98cf1ff8 cubren los 19 casos: todas las comparaciones DCT visibles son IDENTICA, incluidos los cuatro primeros casos que faltaban en el primer registro. Resumen 19/19 coincidencias, 6/6 walle, 0/13 activaciones en negativos. Heap libre estable 307540 bytes, mínimo histórico 295016. DCT descontando referencia aproximadamente 17.52 ms en audio real frente a referencia de 35.39 ms. Media total con doble cálculo 363.00 ms; no representa el rendimiento final. Se configura WALLE_VERIFY_DCT=0 para medir la versión sin duplicación; igualdad validada solo en las entradas probadas.

## Resultado final de tablas DCT sin comparación

Registro 58bd0b24: dos resúmenes con promedio 327.57 ms, mínimo 265.04 ms (silencio) y máximos 331.61/331.58 ms. Frente a 346.55 ms tras optimizar Mel, ahorro 18.98 ms (5.48%). DCT entre 16.249 y 17.436 ms en los casos registrados. Las puntuaciones impresas de los 19 casos coinciden con la versión de verificación. Ambos resúmenes indican 19/19 coincidencias, 6/6 Wall-E, 0/13 activaciones en negativos y cero errores. Heap libre estable 307540 bytes, bloque mayor 278516 y mínimo histórico 295016. No se observa pérdida progresiva de heap. Se mantiene perfilado; las pruebas no son una evaluación independiente de precisión.

Evolución de medias: original 781.86 ms; reutilización FFT y DCT 365.22 ms; pesos Mel 346.55 ms; tablas trigonométricas DCT 327.57 ms. Reducción acumulada frente al original: aproximadamente 58.1%.

## Perfil interno de CMVN

Se agregan cronómetros en speechpy/processing.hpp, función cmvnw: reservas/preparación, primer relleno simétrico, medias de ventanas, resta de medias, segundo relleno simétrico, desviaciones completas, división final y escalado opcional. En numpy.hpp, std_axis0 separa media, suma de diferencias al cuadrado y división/raíz por columna. Sus contadores solo se activan dentro de CMVN y la ruta genérica; el backend hardware no se descompone internamente.

El detalle CMVN está incluido en Normalizacion CMVN. El detalle de desviaciones está incluido en Desviaciones (total): no sumar ambos niveles. Los residuos incluyen comprobaciones, creación de vistas, liberaciones y costo de instrumentación no atribuido. Los relojes por columna añaden tiempo; este build sirve para localizar costos, no para medir una nueva optimización. El escalado opcional es cero si no se ejecuta.

No se cambiaron expresiones matemáticas, datos ni orden de sumas. No hay impresiones dentro del cálculo: se acumulan tiempos y se imprimen después de la inferencia. WALLE_PROFILE_MFCC=0 desactiva cronómetros e impresión como antes. La instrumentación es para una inferencia a la vez. Backup: model-backups/v5-antes-perfil-cmvn (processing.hpp, numpy.hpp, mfcc_profile.h y main.cpp).

Próxima comprobación en placa: una ronda de 19 casos; comparar puntuaciones con la versión de 327.57 ms, revisar memoria y compartir el bloque Detalle CMVN. No se ha aplicado una optimización de CMVN.

## Resultados de perfil CMVN en placa

Registro 0aa6f7d6: 19/19 coincidencias, puntuaciones impresas iguales a las anteriores para los 19 casos. Promedio total 331.12 ms frente a 327.57 ms sin detalle CMVN (3.55 ms adicionales; incluye efecto de instrumentación/build). En audio real CMVN ronda 80.5 ms: medias iniciales ~14.5 ms; desviaciones ~63.5 ms, de las cuales media ~15.5 ms, suma de diferencias al cuadrado ~44.5 ms y división/raíz ~2 ms; división final ~1.9 ms. Reservas y rellenos consumen menos de 0.2 ms juntos. Las subetapas de desviación están contenidas en sus 63.5 ms.

Heap 307812 bytes tras primer caso, 307460 desde el segundo y estable después; mínimo final 294936. No se atribuye la caída inicial a una causa específica sin evidencia; no se observa pérdida progresiva en esta ronda. Próxima investigación: recorrido de columnas en std_axis0, preservando orden de sumas y expresiones. No reemplazar varianza por E[x²]-E[x]² ni medias por sumas deslizantes sin evaluar cambios de redondeo. La media de desviación se calcula sobre datos ya centrados; no equivale a la primera media de CMVN.

## Experimento: columnas contiguas para desviaciones

numpy::std_axis0_contiguous copia una columna a un buffer temporal y ejecuta las mismas dos pasadas (media y suma de diferencias al cuadrado), en el mismo orden. Solo CMVN usa esta variante. La función original std_axis0 se conserva para referencia. La ruta hardware sigue delegando al backend existente.

Para ventana de 49 filas, buffer adicional de 196 bytes; comparación de 13 salidas requiere otros 52 bytes, más overhead del asignador. La copia y reserva se incluyen en Desviaciones (total) y aparecen en su residual. No se presupone ahorro: las operaciones float siguen siendo las mismas y la copia puede anular la ventaja del acceso consecutivo.

WALLE_VERIFY_STD=1 ejecuta ambas versiones por ventana, compara los 13 valores bit a bit y restaura la referencia si difieren. Se esperan 33 ventanas verificadas y cero diferentes por audio. El tiempo de referencia se imprime aparte y no se incluye en los subcontadores optimizados; sí está dentro del total CMVN/inferencia. El residual CMVN descuenta esa referencia. Los tiempos de las dos rutas tienen distinta instrumentación, por lo que no sirven como comparación definitiva de rendimiento. Tras igualdad en placa, medir sin duplicación y comparar con la línea base de 331.12 ms con perfil CMVN (327.57 ms sin detalle CMVN).

Test host std_contiguous_host.cpp: 120 matrices, filas 1/2/33/49/101 y columnas 1/13/20, silencio y señales generadas, igualdad bit a bit con GCC -O2. Falta validación en ESP32. Respaldo en model-backups/v5-antes-desviacion-contigua. El PR no se modifica.

## Validación en placa de columnas contiguas

Registro 659dbd22: los 19 casos tienen 33 ventanas IDENTICA, cero diferencias; puntuaciones impresas iguales a la línea base y 19/19 coincidencias. Heap estable 307444 bytes, mínimo 294920. Media con referencia duplicada 392.28 ms. Desviaciones optimizadas rondan 64 ms, frente a ~63.5 ms de la línea base instrumentada; no hay ahorro demostrado. La copia/reservas aumentan el residual de desviaciones de ~1.47 a ~3.42 ms. La referencia impresa (~61 ms) tiene los subcronómetros desactivados, por lo que no es una comparación directa. Se desactiva WALLE_VERIFY_STD para comparar la media final con 331.12 ms de la línea base con perfil CMVN. Pendiente medición en placa; si no mejora se recomienda retirar el experimento.

## Descartada la variante de columnas contiguas

Registro c28aa96d: sin comparación duplicada, media 333.05 ms frente a 331.12 ms de la línea base con perfil CMVN (+1.93 ms, ~0.58%). Los 19 casos mantienen puntuaciones impresas y clasificaciones, sin errores. No se demuestra beneficio de rendimiento. Se restaura el cálculo de desviaciones original y se conserva la instrumentación CMVN y las optimizaciones FFT/Mel/DCT validadas. La variante descartada se archiva en model-backups/v5-desviacion-contigua-descartada. El test host std_contiguous_host.cpp pertenece al experimento y se archiva junto a él.

## Experimento en PC: CMVN con sumas deslizantes

Test reproducible en test/cmvn_rolling_host.cpp. Dos pasadas, relleno simétrico del SDK y acumuladores double de suma y cuadrados para actualizar ventanas de 49 filas. Se compara contra processing::cmvnw del SDK con 120 matrices de 33 x 13, incluyendo silencio, constante, casi constante con offset grande y señales sinusoidales. Compilación GCC C++11 -O2, WALLE_PROFILE_MFCC=0; incluir directorio con enlace edge-impulse-sdk al SDK local.

Resultado: 118/120 matrices y 24305/51480 coeficientes difieren bit a bit. Diferencia absoluta máxima en señales sinusoidales 3.57627869e-06; máxima global 0.278020799 en la prueba casi constante. Sin salidas no finitas. Son discrepancias respecto al SDK, no errores de reconocimiento medidos. La fórmula de momentos y el distinto redondeo de medias no conservan la referencia, incluso con acumuladores double. No se midieron predicciones ni tiempo ESP32: no pasó el requisito numérico previo. No se incorpora al firmware ni al PR. Se conserva la implementación previamente validada.
