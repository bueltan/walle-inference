# Flujo de trabajo Wall-E

Para actualizar modelos o interpretar ensayos, leer primero ../ESTADO-WALLE.md y,
si existe, reports/latest-update.json. Ver ../FLUJO-RAPIDO-WALLE.md para comandos.
Usar tools/walle.py para resumir métricas/logs e instalar modelos con respaldo,
verificación del SDK/parche y compilación. No volcar JSON, exports o monitores
completos salvo que el resumen no baste. No regenerar audios al cambiar modelo.
Mantener dataset, umbral y detector salvo solicitud del usuario. No modificar
parámetros MFCC exportados ni eliminar optimizaciones. Si cambia SDK, revisar
compatibilidad antes de renovar hashes. Usuario carga placa mediante PlatformIO;
compilar no implica subir. Documentar resultados importantes en el estado breve.
