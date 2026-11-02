# Shared State/Cache — Fases 5, 7 y 8

Estado actual 0.12.65: actividad persistente de hasta 64 eventos por canal,
restaurada con delivery=0, sin alertas ni sumas al arrancar. Chat y Hype Train
activo son temporales. Fuente independiente de datos de prueba; ninguna imagen
o catálogo de emotes se guarda como evento real. Los apartados antiguos describen
sus checkpoints, antes de la persistencia del historial introducida en 0.12.28.

## Ampliación de Fase 12 (0.12.0)

Cada ChannelState conserva `activity`, hasta 64 Follow/Sub/Cheer/Gift/Raid de la
sesión. Se insertan por occurredAt descendente, con orden de entrega inverso para
empates; la prioridad de la cola no altera la cronología. Cada evento aparece una
vez tras el commit central y comparte EventPtr/TextPtr con los últimos valores.
Un lote con varios Follow conserva todas sus entradas. Chat/Hype Train no se
insertan en activity ni desplazan esos eventos. Las fuentes solo filtran/limitan
la vista. El historial no se guarda ni se reconstruye desde últimos valores:
reiniciar comienza vacío, sin replay ni nuevas sumas. Snapshot antiguo intacto.
El historial se prepara dentro de la transacción existente; fallo de persistencia
retiene lote y snapshot completo. No añade cola, conexión, caché o hilo.
Los apartados inferiores describen checkpoints anteriores; desde 0.10.4 también
se restauran los cuatro últimos datos de Supporter Bar junto a DailyBook.

PluginCore posee un SharedState y un TextCache. No dependen de Qt/OBS, disco ni red.
El Core es el único consumidor de la cola; los lectores reciben snapshots const.
Los canales de origen Preview contienen muestras temporales por fuente mediante
el mismo reductor y caché. dailyBook los excluye de la persistencia. Core elimina
su canal al finalizar; reiniciar restaura únicamente los contadores normales.

## Estado publicado

Snapshot contiene generación, revisión y hasta 16 pares origen/canal. Cada canal
conserva siete últimos eventos, sus nombres compartidos y revisiones, cantidades
de eventos entregados por tipo y totales de sesión. Nunca guarda un historial de
chat ni una lista creciente de eventos. Conserva los EventPtr normalizados.

- Bits: suma de Cheer.bits entregados.
- Subs: un incremento por Sub, más Gift.count por regalos.
- Gifts: suma de Gift.count.
- Hype Train: último snapshot de etapa/progreso recibido; no suma snapshots.

Son semánticas del modelo simulado, no cifras verificadas de Twitch. El adaptador
real deberá evitar doble cómputo entre notificaciones correlacionadas de regalos
y suscripciones. Daily Bits/Subs usan el día local del ordenador (YYYYMMDD).
Solo suman eventos cuya fecha local coincide con el día de procesamiento. Los
eventos atrasados conservan la semántica de sesión, sin contaminar el total de hoy.
Al procesar en otro día se reemplaza el bucket; la presentación muestra cero para
un bucket de otra fecha incluso antes del siguiente lote. No hay archivo histórico.
Core restaura únicamente DailyBook; últimos eventos y sesión empiezan vacíos.
Los contadores saturan en UINT64_MAX y marcan saturated en lugar de desbordar.

Un evento atrasado incrementa contadores pero no sustituye un último evento con
fecha posterior. Empates de fecha usan el orden de entrega. Los eventos anónimos
tienen displayName null; la UI futura elegirá la traducción adecuada.

## Publicación y lectores

prepare construye un nuevo estado sin modificar el publicado. Copia una vez cada
canal cambiado del lote y conserva los punteros de canales sin cambios. Publicar
una revisión por lote evita actualizaciones parciales entre sus eventos. Una
entrada inválida o superar la capacidad de canales falla antes de publicar;
no se expulsa un canal silenciosamente perdiendo sus contadores.

En Core: preparar lote → preparar estado → guardar DailyBook si cambia → confirmar
lote → publicar snapshot → notificar al Hub. Si falla preparar o guardar, la cola
y la instantánea previa permanecen intactas.
La caché puede conservar entradas útiles preparadas antes del fallo; no se altera
el estado lógico. El usuario puede cancelar los pendientes y volver a procesar.
Duplicados o cancelados no llegan al reductor; la garantía de deduplicación conserva
los límites temporales/de capacidad de la cola, no es persistente ni permanente.

sharedSnapshot usa publicación atómica de shared_ptr de C++17. Las mutaciones y
el caché son del hilo UI; varios lectores pueden leer los mismos objetos const
desde otros hilos. No se promete lock-free ni se crean hilos de producción.
Un lote vacío devuelve el mismo puntero y no aumenta la revisión. Cada lote no
vacío publicado aumenta la revisión una vez. Los campos latest solo cambian su
revisión cuando sustituyen su evento. La revisión del canal cubre sus contadores.

Los consumidores compararán (generation, revision), luego los campos relevantes.
start idempotente; stop publica null. Un start posterior del mismo SharedState
crea una generación nueva con revisión cero. Las generaciones son locales al
objeto, no identificadores globales entre procesos. Instantáneas retenidas siguen
siendo legibles, pero deben liberarse antes de descargar el plugin.

## Caché CPU de textos

TextCache.intern reutiliza shared_ptr<const string> por contenido idéntico. Se usa
para nombres ya normalizados, sin reprocesarlos por consumidor. Es independiente
de Twitch y reutilizable. No sustituye al futuro Asset Manager de texturas, fuentes
tipográficas u otros recursos GPU, que necesita su propio ciclo de vida gráfico.

Límites por defecto: 256 entradas y 64 KiB de bytes de contenido residente. LRU
expulsa primero lo menos usado. Las claves y metadatos tienen sobrecoste adicional
acotado por el número y tamaño de entradas; el límite no representa toda la RAM.
Un texto que excede el presupuesto se devuelve sin almacenar ni expulsar entradas.
Las asignaciones ocurren antes de expulsar entradas. Los aciertos no crean textos.
invalidate elimina una entrada; clear libera referencias propias y reinicia métricas.
Expulsión/invalidación no rompe handles previamente entregados.

Los lectores deben liberar versiones que ya no usan: los límites de caché/estado
controlan referencias del servicio, no copias de snapshots retenidas externamente.
No hay TTL, polling, IO ni trabajo en reposo. Hit/miss/eviction se consultan en el
hilo UI mediante cacheStats; no son una medida de rendimiento CPU/GPU.

Pruebas: ../../tests/state-tests.cpp y ../../tests/core-tests.cpp.
Ver ../../docs/validation/phase-5.md para validación real en OBS.
