#!/usr/bin/env python3
"""Prepara dos pasadas sin cambiar muestras. Uso: --part 1 o --part 2.
Parte1: original [0,64). Parte2: calibración [0,5) + original [60,120).
Solapamiento [60,64) para revisar límites; no sumar detecciones duplicadas.
"""
from pathlib import Path
import argparse,wave,json,hashlib,csv
p=Path(__file__).resolve().parents[1]
a=argparse.ArgumentParser(description=__doc__);a.add_argument('--part',type=int,choices=[1,2],required=True);args=a.parse_args()
source=p.parent/'audio-dataset/oct4_test_continue.740o0f6n.wav'
with wave.open(str(source),'rb') as w:
 assert (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getcomptype(),w.getnframes())==(1,2,16000,'NONE',1920000)
 pcm=w.readframes(w.getnframes())
ranges=[(0,64)] if args.part==1 else [(0,5),(60,120)]
result=b''.join(pcm[start*32000:end*32000] for start,end in ranges)
(p/'data/real_audio.pcm').write_bytes(result)
(p/'include/real_audio_info.h').write_text(f'#pragma once\n#define WALLE_AUDIO_DESCRIPTION "OCT4 PARTE {args.part} | '+('original 0-64 s' if args.part==1 else 'calibracion 0-5 s; desde tiempo 5 s sumar 55 s para tiempo original')+'"\n')
report={'source':str(source),'part':args.part,'ranges_seconds':ranges,'duration':len(result)/32000,'pcm_sha256':hashlib.sha256(result).hexdigest(),'original_sha256':hashlib.sha256(pcm).hexdigest()}
(p/'reports/current-audio.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
