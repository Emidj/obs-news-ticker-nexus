# Execution Queue — Fases 4, 5 y 8

Fase 8 añade prepareBatchFor(origin, channel, limit): selección de un ámbito en la
misma cola/scheduler, con commit por revisión. TEST/PREVIEW entrega su lote inmediato
sin consumir trabajos manuales pendientes. Conserva los límites, prioridades e
identidad por origen/canal/ID; no crea una cola por fuente. Core usa el mismo
consumidor/reductor para lotes generales y filtrados.

Una instancia propiedad de PluginCore. Recibe EventPtr ya validado por EventEngine,
sin normalizarlo de nuevo. Contrato C++ interno, sin Qt/OBS. Todas sus operaciones
se realizan en el hilo UI. Sin worker, timers, polling, callbacks ni escritura a disco.

## Admisión y límites

enqueue devuelve Enqueued, Coalesced, Duplicate, Full, Stopped o InvalidEvent.
Enqueued/Coalesced incluyen un JobId cancelable; el resto no constituye aceptación.
Límites por defecto: 256 trabajos pendientes, 2048 IDs recientes, ventana de 10
minutos y hasta 32 entregas por llamada. QueueLimits permite límites menores en
pruebas; se rechazan valores cero o duraciones no positivas.

La cola llena rechaza la nueva entrada con Full sin retirar otras ni marcarla
como vista: el productor puede reintentar. Un progreso combinable sí se acepta
con la cola llena porque no añade trabajos. No hay crecimiento ilimitado.

## Prioridad, orden y lotes

| Prioridad | Eventos |
| --- | --- |
| Alta | Raid, Sub, Gift |
| Normal | Follow, Cheer |
| Baja | Chat y todas las etapas de Hype Train |

Planificador por turnos ponderados: Alta ×4 → Normal ×2 → Baja ×1. Salta clases
vacías y mantiene el turno entre llamadas, incluso con lotes de un evento. Cada
clase conserva FIFO; las clases bajas reciben servicio aunque siempre haya altas.
No se promete orden cronológico global entre prioridades ni ejecución en un plazo
de tiempo sin un consumidor que solicite lotes.

takeBatch(n) retira hasta min(n, límite configurado, pendientes). Cero no cambia
la cola. Cada Delivery conserva JobId, prioridad y el mismo EventPtr compartido.
La asignación del vector se realiza antes de retirar trabajo. Una llamada acota
su trabajo por tamaño; no es todavía un presupuesto temporal de renderizado.

## Deduplicación

Clave: origen + canal + ID normalizados, con separación no ambigua. La ventana usa
steady_clock, con reloj inyectable para pruebas; valores que retroceden se ajustan
al último observado. El vencimiento se limpia al recibir entradas, sin timers.

Los IDs de los trabajos que siguen pendientes se protegen aunque su registro haya
caducado o salido del historial. Los IDs entregados, cancelados y sustituidos por
coalescing se recuerdan hasta 10 minutos desde admisión, o hasta ser expulsados por
el límite de 2048 (lo que ocurra antes). Repetir un ID no prolonga su ventana.
No se promete deduplicación permanente o entre reinicios. No es Shared State/Cache:
solo es el registro privado acotado necesario para la cola.

## Coalescing

Únicamente combina snapshots de Progress de Hype Train pendientes del mismo canal
y origen. Busca el último Hype pendiente de ese canal y no atraviesa Begin/End.
Se conserva el contenido con occurredAt más reciente; con fecha igual gana la nueva
entrada. Un progreso atrasado se consume sin sustituir el más nuevo. Mantiene el
JobId y la posición del trabajo, de modo que el token inicial sigue cancelándolo.
Todas las etapas Hype comparten prioridad para conservar su orden FIFO.

No se suman ni eliminan Follow, Sub, Cheer, Gift, Raid o Chat: cada uno conserva
su identidad y aportación. La política actual usa los límites Begin/End del modelo
simulado; la integración Twitch futura deberá incluir la identidad del Hype Train
real antes de aceptar snapshots de sesiones distintas o eventos fuera de orden.

## Cancelación y ciclo de vida

cancel(JobId) retira solo el pendiente indicado; repetirlo o usar un ID desconocido
devuelve false. cancelAll vacía pendientes conservando la deduplicación reciente.
Una entrega ya retirada pertenece al consumidor y no puede deshacerse con cancel.
No hay callbacks ni código de consumidor ejecutándose dentro de la cola.

start es idempotente. stop vacía pendientes e IDs, reinicia turnos y desactiva la
admisión. Los JobId no se reutilizan en posteriores start del mismo objeto, por
lo que un token antiguo no cancela trabajo nuevo. Agotar IDs falla explícitamente.
Los handles entregados deben liberarse antes de descargar la DLL.

## Banco de pruebas actual

El panel encola simulaciones, procesa lotes y cancela pendientes de forma manual.
Desde Fase 5, PluginCore es el único consumidor y actualiza SharedState antes de
devolver el resultado al panel. Se registran cantidades/revisiones, sin payload.
No hay entrega automática ni overlays; las fuentes no vaciarán colas propias.

prepareBatch es una lectura: conserva pendientes y turno de prioridades. Devuelve
un PreparedBatch con datos const y token de propietario/revisión. commitBatch
confirma sin asignaciones y rechaza tokens de otra cola, repetidos o invalidados
por admisiones/cancelación/reinicio. Los tokens no deben sobrevivir a su cola.
Core prepara el estado antes de confirmar: las excepciones no pierden el lote.
takeBatch sigue como operación interna de conveniencia y pruebas; ya no se expone
desde PluginCore. El escritor único no admite reentrada entre confirmar y publicar.

Pruebas deterministas en ../../tests/queue-tests.cpp y de integración con el Core
en ../../tests/core-tests.cpp. Sin esperas reales para validar vencimientos.
