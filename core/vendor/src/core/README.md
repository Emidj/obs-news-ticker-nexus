# Plugin Core — Fase 5

Implementado: ciclo de vida, Settings, logging y contratos de servicios/persistencia.
Posee EventEngine, ExecutionQueue, SharedState y TextCache únicos. processEvent valida/normaliza
entradas y las encola; simulateEvent crea un borrador y lo entrega al mismo método.
SubmissionResult distingue validación y admisión en cola. processPendingEvents
prepara un lote y su snapshot, confirma la retirada y publica la actualización.
Sustituye a takeEventBatch: los consumidores no pueden saltarse el estado compartido.
cancelEvent/cancelPendingEvents cancelan pendientes. Estos métodos, pendingEventCount
y cacheStats se invocan solo en el hilo UI. sharedSnapshot es de solo lectura,
seguro entre hilos y forma parte de IPluginServices.
Sin dependencias de OBS o Qt. Las implementaciones concretas están en ../adapters.
Ver ../../docs/architecture.md y ../../AGENTS.md.
