#!/usr/bin/env python3
"""Convierte un WAV mono PCM16 de 16 kHz (hasta 1 s) en un arreglo para flash."""
import array
from pathlib import Path
import sys
import wave

if len(sys.argv) != 2:
    raise SystemExit('Uso: python3 tools/preparar_audio.py /ruta/audio.wav')
with wave.open(sys.argv[1]) as wav:
    if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, 16000):
        raise SystemExit('Se requiere WAV mono PCM de 16 bits a 16000 Hz.')
    samples = array.array('h', wav.readframes(wav.getnframes()))
if sys.byteorder != 'little':
    samples.byteswap()
if not 0 < len(samples) <= 16000:
    raise SystemExit('El audio debe durar como máximo 1 segundo y no estar vacío.')
# Centrar sin cortar voz; completar la ventana con silencio digital.
left = (16000 - len(samples)) // 2
samples = [0] * left + list(samples) + [0] * (16000 - len(samples) - left)
out = Path(__file__).resolve().parents[1] / 'include/audio_prueba.h'
out.write_text('// PCM16 mono 16 kHz, centrado a 1 s. const conserva el audio en flash.\n'
               '#pragma once\n#include <stdint.h>\nstatic const int16_t audio_prueba[16000] = {\n' +
               ''.join(','.join(map(str, samples[i:i+24])) + ',\n' for i in range(0, 16000, 24)) + '};\n')
print(out)
