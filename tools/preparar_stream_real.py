#!/usr/bin/env python3
"""Extrae PCM sin modificar amplitud ni tiempos. Requiere WAV mono PCM16/16 kHz."""
import argparse, wave
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('wav',type=Path);a=p.parse_args()
out=Path(__file__).resolve().parents[1]/'data/real_audio.pcm'
with wave.open(str(a.wav)) as w:
    if (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getcomptype()) != (1,2,16000,'NONE'):
        raise SystemExit('Se requiere WAV PCM16 mono a 16000 Hz; no se convierte automaticamente.')
    pcm=w.readframes(w.getnframes())
    if not any(pcm):raise SystemExit('Audio completamente silencioso: no se reemplaza la prueba.')
    out.parent.mkdir(exist_ok=True);out.write_bytes(pcm)
    print(f'{out}: {w.getnframes()/16000:.2f} s, {len(pcm)} bytes')
