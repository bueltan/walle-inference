"""Regresiones del resumen: matrices, rondas incompletas y omisiones."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('workflow',Path(__file__).resolve().parents[1]/'tools/walle.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)

class WorkflowTests(unittest.TestCase):
    def test_matrix(self):
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/'m.json'
            f.write_text('{"test":{"int8":{"class_names":["noise","unknown","walle"],"confusion_matrix":[[2,0,1],[0,3,0],[0,1,4]]}}}')
            r=w.metrics_summary(f)['test/int8']
            self.assertEqual((r['correct'],r['total'],r['walle_correct'],r['negatives_as_walle']),(9,11,4,1))
    def test_incomplete_round_ignored(self):
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/'log.txt'
            f.write_text('=== WALL-E v11 | AUDIOS COMPLETOS ===\n[1/1] a.wav | esperado=walle\nwalle: 0.9\nActivacion con umbral 0.80: SI\n=== RESUMEN v11 ===\nFin de ronda: libre=1\n=== WALL-E v11 | AUDIOS COMPLETOS ===\n[1/1] a.wav\nwalle: 0.2\n')
            r=w.log_summary(f)
            self.assertEqual(r['complete_rounds'],1)
            self.assertTrue(r['scores'][0]['activated'])
            self.assertEqual(r['scores'][0]['walle'],0.9)
    def test_omitted_not_negative(self):
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/'log.txt'
            f.write_text('=== V11 | AUDIO REAL ===\nEVENTO 1 | largo\nOMITIDO: largo\nRESUMEN REAL: omitidos=1\nFin. No se asignan aciertos\n')
            r=w.log_summary(f)['scores'][0]
            self.assertIsNone(r['walle']);self.assertTrue(r['omitted'])

if __name__=='__main__':unittest.main()
