#!/usr/bin/env python3
"""Genera pruebas de 1 s desde los recortes existentes, sin cortar palabras.
Solo usa Python estándar. Los labels proceden de los archivos del usuario;
no es un conjunto independiente de evaluación ni transcripción verificada.
"""
import array
import csv
from pathlib import Path
import sys
import wave

project = Path(__file__).resolve().parents[1]
dataset = project.parent / 'audio-dataset'
cases = []
# Primero la referencia exacta usada en v1/v2; después otras pronunciaciones.
for number in [6, 1, 3, 9, 15, 20]:
    cases.append(('wall-e', dataset / 'wall-e-separado' / f'wall-e_{number:03d}.wav'))
for number in [4, 6, 7, 11, 12, 17]:
    cases.append(('unknown', dataset / 'unknown-preparado/resultado/clips' / f'unknown_{number:03d}.wav'))
lines = ['// Generado por tools/preparar_suite.py. Audio PCM16 en flash.',
         '#pragma once', '#include <stdint.h>',
         'struct CasoAudio { const char *nombre; const char *esperado; const int16_t *datos; };']
entries, manifest = [], []
for index, (label, path) in enumerate(cases):
    with wave.open(str(path)) as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 16000)
        samples = array.array('h', wav.readframes(wav.getnframes()))
    if sys.byteorder != 'little': samples.byteswap()
    assert 0 < len(samples) <= 16000, f'No se recortan clips largos: {path}'
    original_size = len(samples)
    left = (16000 - original_size) // 2
    values = [0] * left + list(samples) + [0] * (16000 - original_size - left)
    lines.append(f'static const int16_t muestra_{index}[16000] = {{')
    lines.extend(','.join(map(str, values[i:i+24])) + ',' for i in range(0, 16000, 24))
    lines.append('};')
    entries.append(f'{{"{path.name}", "{label}", muestra_{index}}}')
    manifest.append([path.name, label, str(path), original_size, left, 16000-original_size-left])
# Silencio digital como control técnico; no representa ruido ambiental real.
entries.append('{"silencio_digital", "noise", nullptr}')
lines += ['static const CasoAudio casos[] = {', ',\n'.join(entries), '};']
(project / 'include/audio_suite.h').write_text('\n'.join(lines) + '\n')
with (project / 'audio_suite.csv').open('w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['archivo', 'label_esperado', 'origen', 'muestras_originales', 'ceros_antes', 'ceros_despues'])
    writer.writerows(manifest)
print('Generados 12 audios y un control de silencio digital.')
