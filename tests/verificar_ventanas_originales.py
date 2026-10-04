"""Simula selección causal de ventanas y compara con los 13 WAV revisados.
No ejecuta la red ni mide rendimiento de ESP32.
"""
import array
import csv
import math
from pathlib import Path
import sys
import wave

root = Path(__file__).resolve().parents[2]
dataset = root / 'audio-dataset/revision-audio-continuo'
with wave.open(str(dataset.parent / 'real_audio.73tte749.wav'), 'rb') as w:
    pcm = w.readframes(w.getnframes())
x = array.array('h', pcm)
if sys.byteorder != 'little': x.byteswap()
block, fs, window, capacity = 640, 16000, 16000, 32000
energies = [math.sqrt(sum(v*v for v in x[i:i+block])/block)
            for i in range(0, len(x), block)]
noise = sorted(energies[:125])[112]
high, low = max(100, noise*3), max(60, noise*1.8)
active = False
onset = quiet = count = 0
pending = None
windows = {}
omitted = []
ring = [0]*capacity

def complete(end):
    global pending
    if pending is None or end < pending[1]+window:
        return
    event, start = pending
    assert start >= max(0, end-capacity)
    values = [ring[(start+i)%capacity] for i in range(window)]
    assert values == list(x[start:start+window])
    windows[event] = (start, values)
    pending = None

for index, energy in enumerate(energies):
    end = (index+1)*block
    for j, value in enumerate(x[index*block:end]):
        ring[(index*block+j)%capacity] = value
    complete(end)
    if index < 125: continue
    if not active:
        onset = onset+1 if energy > high else 0
        if onset >= 2:
            active = True
            quiet = 0
            event_start = end-4480
            size = last = 4480
    else:
        size += block
        if energy > low:
            quiet = 0
            last = size
        else: quiet += 1
        if quiet >= 7:
            count += 1
            if last-3200 > fs*900//1000:
                omitted.append(count)
            else:
                assert pending is None
                start = (event_start+3200+event_start+last-window)//2
                pending = (count,start)
                complete(end)
            active = False
            onset = quiet = 0
assert count == 15 and omitted == [8] and pending is None
rows = list(csv.DictReader((dataset/'segmentos.csv').open()))
for row in rows:
    start, values = windows[int(row['evento_log'])]
    assert start == int(row['muestra_inicio']), row['archivo']
    with wave.open(str(dataset/'palabras'/row['archivo']), 'rb') as w:
        data = array.array('h',w.readframes(window))
    if sys.byteorder != 'little': data.byteswap()
    assert values == list(data), row['archivo']
print('OK: 15 eventos, 1 largo omitido, 14 ventanas causales; las 13 palabras coinciden muestra por muestra con los WAV revisados.')
