#!/usr/bin/env python3
"""Flujo local reproducible. Python estándar; nunca carga firmware ni entrena.
Usar --help y la ayuda de cada subcomando. Rutas con espacios: entre comillas.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[1]
LIVE = PROJECT / 'lib/wall_e_model'
STATE = PROJECT / 'tools/workflow/sdk-baseline.json'
PATCH = PROJECT / 'tools/patches/sdk-local-v5-v6.patch'


def hashes(path):
    if not path.is_dir():
        raise ValueError(f'Falta carpeta: {path}')
    return {str(f.relative_to(path)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in sorted(path.rglob('*')) if f.is_file()}


def read_json(path):
    return json.loads(Path(path).read_text())


def emit(data, output=None):
    text = json.dumps(data, ensure_ascii=False, indent=2)
    if output:
        Path(output).write_text(text+'\n')
    print(text)


def version(root):
    text = (root/'model-parameters/model_metadata.h').read_text()
    match = re.search(r'#define\s+EI_CLASSIFIER_PROJECT_DEPLOY_VERSION\s+(\d+)', text)
    if not match:
        raise ValueError('No se reconoce la versión exportada')
    return int(match[1])


def build(env):
    bundled = Path.home()/'.platformio/penv/bin/platformio'
    pio = str(bundled) if bundled.is_file() else shutil.which('platformio')
    if not pio:
        raise ValueError('No se encontró PlatformIO')
    log = PROJECT/'reports'/f'build-{env}.log'
    with log.open('w') as stream:
        result = subprocess.run([pio, 'run', '-e', env], cwd=PROJECT,
                                stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print('\n'.join(log.read_text().splitlines()[-30:]))
        raise RuntimeError(f'Falló compilación; registro: {log}')
    return {'environment': env, 'status': 'SUCCESS', 'log': str(log)}


def update(args):
    """Preparar y validar antes de reemplazar. SDK desconocido: detenerse."""
    source = Path(args.export).resolve()
    baseline = read_json(STATE)
    if hashes(source/'edge-impulse-sdk') != baseline['pristine']:
        raise ValueError('SDK nuevo diferente de la base aprobada. Requiere revisión; no se modificó el proyecto.')
    if hashes(LIVE/'edge-impulse-sdk') != baseline['optimized']:
        raise ValueError('El SDK local cambió. Actualizar/revisar el parche y la base antes de continuar.')
    # Comprobar también la entrada que admite el firmware actual.
    metadata = (source/'model-parameters/model_metadata.h').read_text()
    for key, expected in [('EI_CLASSIFIER_FREQUENCY',16000),('EI_CLASSIFIER_RAW_SAMPLE_COUNT',16000),('EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME',1)]:
        m = re.search(r'#define\s+'+key+r'\s+(\d+)', metadata)
        if not m or int(m[1]) != expected:
            raise ValueError(f'{key} no compatible con esta prueba: se requiere {expected}')
    for folder in ['model-parameters','tflite-model']:
        if not hashes(source/folder):
            raise ValueError(f'Export vacío: {folder}')
    variables = (source/'model-parameters/model_variables.h').read_text()
    labels = re.search(r'ei_classifier_inferencing_categories(?:_\d+_\d+)?\[.*?\]\s*=\s*\{(.*?)\}', variables, re.S)
    if not labels or re.findall(r'"([^"]+)"',labels[1]) != ['noise','unknown','walle']:
        raise ValueError('Etiquetas distintas de noise/unknown/walle; revisar firmware')
    metrics = read_json(args.metrics) if args.metrics else None
    old, new = version(LIVE), version(source)
    with tempfile.TemporaryDirectory(prefix='walle-stage-',dir=PROJECT/'model-backups') as temp:
        staged = Path(temp)/'wall_e_model'
        shutil.copytree(source,staged)
        shutil.copy2(LIVE/'library.json',staged/'library.json')
        for folder in ['model-parameters','tflite-model']:
            if hashes(staged/folder) != hashes(source/folder):
                raise ValueError('Copia del modelo inconsistente')
        result = subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(PATCH)],cwd=staged,capture_output=True,text=True)
        if result.returncode:
            raise ValueError('Parche no aplicable: '+result.stdout+result.stderr)
        if hashes(staged/'edge-impulse-sdk') != baseline['optimized']:
            raise ValueError('El parche no reproduce el SDK aprobado')
        if args.check:
            emit({'checked_version':new,'sdk':'compatible; parche exacto','installed':False})
            return
        stamp = dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        backup = PROJECT/'model-backups'/f'v{old}-antes-v{new}-{stamp}'
        backup.mkdir()
        # Fuentes/configuración y audios pequeños permiten reconstruir la prueba.
        for name in ['src','include']:
            shutil.copytree(PROJECT/name,backup/name)
        for name in ['platformio.ini','README.md']:
            shutil.copy2(PROJECT/name,backup/name)
        (backup/'data-hashes.json').write_text(json.dumps(hashes(PROJECT/'data'),indent=2)+'\n')
        os.rename(LIVE,backup/'wall_e_model')
        try:
            os.rename(staged,LIVE)
        except BaseException:
            os.rename(backup/'wall_e_model',LIVE)
            raise
    report = {'version':new,'previous_version':old,'backup':str(backup),
              'sdk':'parche exacto verificado','installed':True,
              'export':str(source),'model_hashes':hashes(LIVE/'tflite-model')}
    if metrics is not None:
        target=PROJECT/'reports'/f'v{new}-metrics.json'
        target.write_text(json.dumps(metrics,indent=2)+'\n')
        report['metrics']=str(target)
    if args.build:
        try:
            report['build']=build(args.build)
        except Exception:
            report['build']={'status':'FAILED','note':'Modelo instalado; respaldo conservado. No subir.'}
            (PROJECT/'reports/latest-update.json').write_text(json.dumps(report,indent=2)+'\n')
            raise
    (PROJECT/'reports/latest-update.json').write_text(json.dumps(report,indent=2)+'\n')
    emit(report)


def metrics_summary(path):
    result={}
    for split,variants in read_json(path).items():
        if not isinstance(variants,dict): continue
        for kind,data in variants.items():
            if not isinstance(data,dict) or 'confusion_matrix' not in data:continue
            labels=data['class_names']; matrix=data['confusion_matrix']; w=labels.index('walle')
            total=sum(map(sum,matrix)); correct=sum(matrix[i][i] for i in range(len(matrix)))
            result[f'{split}/{kind}']={'correct':correct,'total':total,
                'accuracy_pct':round(100*correct/total,2) if total else None,
                'walle_correct':matrix[w][w],'walle_total':sum(matrix[w]),
                'negatives_as_walle':sum(row[w] for i,row in enumerate(matrix) if i!=w),
                'confusion_matrix':matrix,'labels':labels}
    return result


def log_summary(path):
    text=Path(path).read_text()
    # Los monitores repiten rondas. Mostrar la última completa, nunca mezclarlas.
    starts=list(re.finditer(r'^=== (?:WALL-E v\d+|V\d+ \| AUDIO REAL)',text,re.M))
    rounds=[]
    for i,start in enumerate(starts):
        part=text[start.start():starts[i+1].start() if i+1<len(starts) else len(text)]
        if 'Fin de ronda:' in part or 'Fin. No se asignan' in part: rounds.append(part)
    if not rounds:raise ValueError('No hay una ronda completa en el registro')
    part=rounds[-1]
    real='RESUMEN REAL:' in part
    summary=part[part.index('RESUMEN REAL:' if real else '=== RESUMEN'):].splitlines()
    scores=[]
    pattern=r'EVENTO (\d+) \|' if real else r'\[(\d+)/\d+\] ([^\n]+)'
    sections=list(re.finditer(pattern,part))
    for i,item in enumerate(sections):
        segment=part[item.end():sections[i+1].start() if i+1<len(sections) else len(part)]
        match=re.search(r'walle[:=]\s*([0-9.]+)',segment)
        scores.append({'case':int(item[1]),'name':None if real else item[2],
                       'walle':float(match[1]) if match else None,
                       'activated':bool(re.search(r'\| ACTIVACION \||Activacion con umbral [\d.]+: SI',segment)),
                       'omitted':'OMITIDO:' in segment})
    # No inferir etiquetas del stream: una grabación futura puede tener otro orden.
    return {'header':part.splitlines()[0],'complete_rounds':len(rounds),
            'summary':summary[:8],'scores':scores,
            'note':'En stream comparar eventos con el orden conocido; omitido no equivale a acierto.'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    u=commands.add_parser('update',help='Instalar export conservando optimizaciones, con respaldo')
    u.add_argument('export');u.add_argument('--metrics');u.add_argument('--check',action='store_true')
    u.add_argument('--build',choices=['esp32-c3-real','esp32-c3-clips','esp32-c3-full'])
    m=commands.add_parser('metrics',help='Resumir métricas sin imprimir el JSON entero')
    m.add_argument('file');m.add_argument('--compare');m.add_argument('--output')
    l=commands.add_parser('log',help='Resumir última ronda completa del monitor')
    l.add_argument('file');l.add_argument('--output')
    b=commands.add_parser('build',help='Compilar sin subir; registro completo en reports')
    b.add_argument('environment',choices=['esp32-c3-real','esp32-c3-clips','esp32-c3-full'])
    args=parser.parse_args()
    if args.command=='update':update(args)
    elif args.command=='metrics':
        result={'current':metrics_summary(args.file)}
        if args.compare:result['previous']=metrics_summary(args.compare)
        emit(result,args.output)
    elif args.command=='log':emit(log_summary(args.file),args.output)
    else:emit(build(args.environment))


if __name__=='__main__':
    try:main()
    except (ValueError,RuntimeError,OSError,KeyError) as error:
        raise SystemExit(f'ERROR: {error}')
