# Central Event Engine — Fase 3

Contrato interno C++ sin Qt/OBS. Una instancia en PluginCore, uso síncrono en el
hilo UI. EventDraft posee sus cadenas; process recibe el borrador por valor y lo
normaliza una vez. Un resultado válido contiene shared_ptr<const Event>, legible
por varios consumidores sin copiar el contenido ni retener memoria del productor.
Solo EventEngine puede construir el resultado validado.

Campos comunes: ID, ID del canal, fecha system_clock posterior al epoch, origen
Simulation, Preview o Twitch y payload tipado. Son un modelo interno, no un esquema EventSub.
Preview identifica muestras temporales por fuente, con canal efímero asignado por
Core. No hay validación alternativa: usa los mismos límites de campos y Unicode.

| Tipo | Contenido y validación |
| --- | --- |
| Follow | Usuario requerido |
| Sub | Usuario, nivel Tier1/2/3/Prime y meses positivos |
| Cheer | Bits positivos; usuario opcional para anonimato |
| Gift | Cantidad positiva, Tier1/2/3; donante opcional |
| Raid | Usuario y espectadores positivos |
| Chat | Autor y mensaje UTF-8 no vacío, hasta 4096 bytes |
| Hype Train | Etapa Begin/Progress/End, nivel positivo; objetivo positivo salvo End, total sin limitar al objetivo |

Los IDs admiten letras ASCII, dígitos y -_.:, hasta 128 bytes de entrada. Se recortan
espacios ASCII exteriores. El ID del mensaje Twitch es opaco: se conserva byte
por byte, admite ASCII imprimible sin espacios (incluidos +/=), y mantiene el
límite de 128 bytes. Los IDs de usuario/canal siguen usando la regla anterior.
Los nombres admiten hasta 256 bytes de entrada, se
recortan y usan el ID como alternativa cuando quedan vacíos. Se rechaza UTF-8
malformado, NUL y controles ASCII; Chat conserva el texto, permite LF y tabuladores.
Estos límites acotan los datos aceptados y no describen los límites de Twitch.

ProcessResult distingue motor detenido, ID, fecha, origen o payload inválido.
No se registra el payload recibido. No se persiste ni retiene historial.
Las excepciones de asignación pueden propagarse hasta el límite protegido de la UI.

makeSimulation genera datos ficticios. PluginCore aporta una secuencia creciente
por instancia y la hora; llamadas de una sesión reciben IDs distintos incluso con
la misma hora. Esto no promete unicidad global entre procesos o reinicios.
Las simulaciones atraviesan processEvent; no hay una segunda ruta de validación.

Inicio/parada idempotentes. Tras stop se rechazan entradas, pero un resultado ya
compartido sigue legible. Los handles deben liberarse antes de descargar la DLL.
Este módulo no contiene cola, deduplicación, prioridades ni coalescing: desde la
Fase 4, PluginCore conecta su resultado a la única ExecutionQueue de ../execution.
No hay callbacks de consumidores, timers, hilos, conexión Twitch, estado compartido
o renderer en este módulo.

Ver ../../docs/architecture.md, ../../tests/event-tests.cpp y ../../AGENTS.md.
