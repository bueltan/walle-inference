#!/usr/bin/env python3
"""Incrusta los 13 WAV revisados sin recortar, rellenar ni normalizar.

La selección y las etiquetas proceden del CSV revisado por el usuario.
Comprueba formato, longitud y hash para evitar usar otro audio por accidente.
"""
import array
import csv
import hashlib
from pathlib import Path
import sys
import wave

project = Path(__file__).resolve().parents[1]
dataset = project.parent / 'audio-dataset/revision-audio-continuo'
rows = list(csv.DictReader((dataset / 'segmentos.csv').open()))
assert len(rows) == 13
assert sum(r['label_propuesto'] == 'walle' for r in rows) == 7
assert sum(r['label_propuesto'] == 'unknown' for r in rows) == 6
lines = ['// Generado por tools/preparar_suite_continuo.py; muestras originales.',
         '#pragma once', '#include <stdint.h>',
         'struct CasoAudio { const char *nombre; const char *esperado; const int16_t *datos; };']
entries = []
for i, row in enumerate(rows):
    assert row['estado_revision'] == 'validado_usuario'
    path = dataset / 'palabras' / row['archivo']
    with wave.open(str(path), 'rb') as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(),
                wav.getnframes(), wav.getcomptype()) == (1, 2, 16000, 16000, 'NONE')
        pcm = wav.readframes(16000)
    assert hashlib.sha256(pcm).hexdigest() == row['sha256_pcm']
    samples = array.array('h', pcm)
    if sys.byteorder != 'little': samples.byteswap()
    lines.append(f'static const int16_t muestra_{i}[16000] = {{')
    lines.extend(','.join(map(str, samples[j:j+24])) + ',' for j in range(0, 16000, 24))
    lines.append('};')
    entries.append(f'{{"{path.name}", "{row["label_propuesto"]}", muestra_{i}}}')
lines += ['static const CasoAudio casos[] = {', ',\n'.join(entries), '};']
(project / 'include/audio_suite_continuo.h').write_text('\n'.join(lines) + '\n')
print('Generados 13 casos: 7 walle, 6 unknown; PCM verificado con SHA-256.')
