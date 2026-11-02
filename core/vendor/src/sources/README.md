# Fuentes OBS — Fases 7 a 12

## Estado actual — 0.12.65

Fase 12 cerrada: Event List, Animated Alerts, Shoutout Card, Hype Train Meter
y Live Chat, diez presets por fuente, más Supporter Bar de fases anteriores.
Coordinación central, TEST privado, tipografía/herencia, recursos compartidos
y vídeo sin audio. Ver `docs/validation/phase-12-complete.md` para pruebas y
límites; Fase 13 no iniciada. Los apartados inferiores son historial y contienen
restricciones/pendientes que eran propios de sus versiones.

## Animated Alerts — Neon DJ, Cyberpunk, Retro CRT, Space, Horror, Love, Graffiti, Minimal y Urban City (0.12.25)

oto_animated_alerts es una fuente de vídeo sin audio, fija 1000×1000. Desde
0.12.32 consume un turno de PresentationQueue, etapa central de reproducción
compartida con Shoutout. El cursor del historial queda para TEST privado.
La etapa retiene los eventos normalizados fuera de los límites 64/16 del
historial visible. Un candidato puede esperar la foto del autor; solo hay un evento
activo preparado. TwitchService solicita Get Users/CDN por su transporte actual
y AssetManager mantiene la caché de imágenes compartida. No iniciar el timeline
mientras una foto esté pendiente. Reserva al fallar o para eventos anónimos.

AlertStyle contiene datos de atlas/títulos/colores; AlertFrame contiene capas
inmutables cacheadas, retrato circular (cuadrado en el tubo CRT) y nombre Unicode con ping-pong. El draw
usa el renderer compartido y coloca cada sprite completo según la envolvente
de su escala y giro, manteniendo dos píxeles de margen en el lienzo. No layout,
archivos, descargas o rasterizaciones por fotograma. video_tick solo publica una
petición GUI al terminar; ningún timer de animación y fuente inactiva sin reloj.

Plasma, aro, filamentos y partículas se generan con un shader compartido.
El perfil CRT añade carcasa mecanizada, mandos, barrido de fósforo y un pase
de vidrio sobre la foto real en el mismo programa; iconos pixelados articulados.
Salida: gestos de entrada invertidos con duración independiente. Reloj de
shader/iconos invertido desde la espera actual, sin reinicio de fase. Brillo
y escala enlazados durante el primer 12 %; conserva formas y centros.
Retrato fijo centrado, placa de ancho automático 192–480 px con padding 20 px.
Viewport y placa centrados en x=500; medir con la tipografía real una vez en GUI.
`iconAnimations` describe seis clips separados: hojas 1024×768 de 48 celdas
128²/24 fps, bucle 2 s. GUI carga/prepara solo el clip del evento activo;
video_render calcula su celda con el tiempo existente y cambia UV en la misma
textura, sin deformarla, decodificar ni subir frames nuevos. GIF de revisión
con damero; composición con RGBA completo. No reproductor/timer adicional.
TEST acepta login (`test_user`), resuelto por el transporte único existente;
login inválido se cancela, sin mostrar la foto de una cuenta distinta.

Tres tiempos entry_ms/hold_ms/exit_ms y dos FONT/herencias por instancia.
Seis TEST más LONG y Finalizar usan el canal Preview central privado de la
fuente; no alteran diarios. Ocultar/mostrar, cambiar preferencias o reiniciar
no reproducen el historial. GPU manual exporta 181 frames de cada variante con
61 comprobaciones de margen exterior y
100 px transparentes fuera del cuadrado y cero uploads/rasterizaciones extra.
Los demás estilos Animated Alerts siguen pendientes; no adelantar otras fuentes.

Cyberpunk selecciona energyPattern=1 del mismo programa GPU: polígonos negros,
paneles blancos diagonales, circuitos con pulsos, arcos segmentados y fragmentos.
Headline/caption oscuros como datos. Iconos articulados propios en hojas RGBA,
placa angular preparada una vez. Los perfiles clips-only omiten el atlas; si
hay un clip, no se carga un atlas de reserva. Neon DJ conserva patrón 0.

## Tipografía común — 0.12.3

Todas las fuentes con nombres/etiquetas deben reutilizar `font-properties.hpp`:
dos roles FONT nativos con casillas de herencia independientes, activadas por
defecto. `name_font`/`use_style_font` conserva las escenas anteriores y su migración
de `value_font`; `label_font`/`use_style_label_font` añade etiquetas heredadas a
escenas antiguas. Familia vacía/inválida vuelve al preset. No borrar objetos FONT
al marcar casillas; OBS guarda ambas elecciones completas por instancia. El
renderer consume solo la familia: tamaños, peso, color e iconos siguen el estilo.
Los títulos de Supporter Bar y badges de Event List usan el rol etiquetas;
nombres mantienen el rol previo y cantidades permanecen en el diseño.
Cambios de fuente asientan la animación sin replay. Supporter Bar solo reconstruye
base al cambiar etiquetas, reutilizando capas de nombres y contadores. Event List
reutiliza las capas cacheadas. Ningún cambio añade trabajo a video_render.
Esta regla se aplica también a las fuentes futuras sin adelantar su implementación.

## Event List — Fase 12 (0.12.28)

Desde 0.12.28 conserva 64 entradas por canal al reiniciar. SharedState restaura
la actividad con delivery=0: Event List prepara filas quietas, AlertCursor omite
todos esos registros aunque ya estuviera activo cuando llega el backfill.
TwitchService consulta Get Channel Followers una vez después de suscribir los
eventos; usa el mismo transporte/autorización y trae hasta 64 follows con sus
fechas reales. El Core normaliza/mezcla/guarda/publica sin pasar por el reductor
de contadores ni insertar alertas en la cola. Deduplica por ID y coincidencia
usuario/segundo de follow, preserva alertas vivas y deja el estado previo intacto
si falla el guardado. La lista respeta los filtros de cada instancia; vacía muestra
un aviso cacheado. TEST no se guarda ni sustituye el canal real tras reiniciar.
Twitch no reenvía eventos perdidos mientras está desconectado; solo follows tienen
backfill en este checkpoint. Subs/Bits/regalos/Raid se guardan desde su recepción.

0.12.14 completa los diez estilos Event List con Futuristic Hologram: atlas
de cristal azul/violeta, cinco iconos propios, eventListStyle como datos y
perfil compartido 5/forma 1/diez partículas. Opacidad del fondo, cantidades
derechas, cuatro entradas, duración y ambos FONT/herencias siguen compartidos.
Sin cambios de renderer, modelo, conexión, timer o audio.

0.12.13 añade Urban City como datos/atlas, perfil compartido 7/forma 1/diez
chispas. Etiquetas iluminadas por el acento del evento mediante badgeAccent.
Opacidad del fondo por instancia background_opacity, 0–100/defecto 100.
Minimal escala solo el alfa del relleno CardNode. Estilos con atlas usan
backgroundBox en milésimas como máscara interior de ImageNode: tonos oscuros
se atenúan, luces y perímetro quedan intactos. Textos se componen después.
El VisualEngine prepara una fila temporal acotada y cachea el resultado en GUI,
sin modificar el atlas ni añadir trabajo en video_render. Cambio asienta
eventos sin replay y conserva nombres, fuentes y ajustes. Todos los diez estilos.

0.12.12 incorpora Graffiti solo como atlas y eventListStyle; perfil compartido
changePattern=6/changeParticleShape=3, doce gotas como máximo. Reproduce tiras
negras y etiquetas angulares de la referencia sin fondo. Cantidades sobre
interior oscuro, con margen derecho 28 px. Sin renderer, modelo ni timers nuevos.

0.12.11 incorpora Love exclusivamente como atlas y eventListStyle; perfil
compartido changePattern=0/changeParticleShape=2, diez corazones como máximo.
No modifica renderer, modelo ni timers. Carga visible/no vacía, caché compartida.

0.12.10 añade duración total por instancia: motion_ms/1000–12000 ms, defecto
6000 ms para fuentes antiguas. ListSettings::durationMs participa en igualdad y
validación. El sampler normaliza al timeline original de 6000 ms; movimiento y
salida duran durationMs/12, y el inicio del ping-pong nuevo espera durationMs/6.
Draw usa la misma duración para desplazamiento, salida, progreso y estado settled.
Cambiar duración prepara en reposo; no reproduce eventos ni añade timers.
La propiedad se deshabilita junto con entry_side cuando animate está apagado.

0.12.9 corrige TEST y muestras: lista previa llena, inserción de un nuevo evento,
bajada de todas las anteriores y salida/desvanecimiento inferior de 500 ms por defecto.
`ListFrame::exiting` retiene como máximo rows tarjetas (1–10) con imágenes
compartidas; no genera texturas por fotograma. Ráfagas retoman posiciones y
mantienen inicio del desvanecimiento. TEST prepara once eventos efímeros para
cubrir diez filas previas más una nueva; no escribe contadores. Las galerías
GPU muestran ocho filas completas a 600×600. Sin nuevos estilos/conexiones/timers.

0.12.8 incorpora Horror mediante atlas y `eventListStyle`, iconos de porcelana/
hueso/cristal y marco oscuro rojo, sin fondo. Perfil Horror existente 4, diez
partículas de niebla forma 4; ningún renderer/shader/timer/modelo nuevo.
Seis diseños disponibles, cuatro pendientes. Mismas cuatro entradas,
cantidades derechas, sin edades, ping-pong y dos roles FONT/herencias.

0.12.7 incorpora Space solo como datos `eventListStyle` y atlas transparente
de cinco tarjetas con iconos cósmicos, recortes, badges vacíos y texto dinámico.
Perfil orbital existente (`changePattern=3`), doce destellos como
máximo; sin nuevos shader/modelo/renderer/timers. Selector de cinco estilos,
cinco diseños pendientes. Conserva cantidades derechas, sin edades, ping-pong,
dos roles FONT/herencias y cuatro entradas por instancia.

0.12.6 incorpora Retro CRT como datos `eventListStyle`: atlas transparente de
cinco tarjetas, recortes explícitos y perfil CRT existente (`changePattern=2`,
ocho partículas cuadradas). Selector Minimal/Neon DJ/Cyberpunk/Retro CRT; seis
estilos de lista pendientes. Conserva layout sin edades, cantidades derechas,
cuatro entradas y dos roles FONT. No cambia el modelo ni el renderer.

0.12.5 elimina edades, su propiedad `show_age` (escenas antiguas la ignoran),
capas y QTimer compartido. Cantidades justificadas a la derecha con margen
interior de 28 px y separación de 12 px del nombre. Las filas sin cantidad
aprovechan toda la zona libre. Se aplica a todos los estilos del layout compartido.

0.12.4 incorpora Cyberpunk como `eventListStyle` de datos: atlas transparente
de cinco tarjetas recortadas y perfil de escaneo digital/partículas cuadradas
del mismo programa GPU. Selector Minimal/Neon DJ/Cyberpunk; mantiene los dos
roles tipográficos y cuatro entradas independientes por instancia. Sin cambios
al modelo/renderer para este nuevo diseño. Siete estilos de lista pendientes.

0.12.2 añade `preset` por instancia, selector Minimal/Neon DJ y el AssetManager
compartido del módulo en EventListHub. Resuelve solo definiciones que tengan
`eventListStyle`; otras o desconocidas usan Minimal. Se prepara el atlas en GUI
únicamente para vistas visibles no vacías y se retiene/reutiliza en ListFrame.
La fila conserva el acento de su tipo; el efecto compartido y el detalle usan
ese color. Neon DJ compone ImageNode recortado, etiqueta y texto dinámico; Minimal
conserva CardNode/PathNode. Tipografía heredada y elección personalizada permanecen
independientes del preset. Cambiar estilo asienta filas sin replay. Sin nuevos
timers, renderers, conexiones ni lectura/rasterización por fotograma.

0.12.1 añade `ListEntrance` y propiedad entera `entry_side` por instancia:
Appearance/Left/Right/Top. La ausencia del ajuste conserva Appearance. Desactivar
`animate` deshabilita el selector; su valor se conserva. Preferencias nuevas
asientan las filas existentes, sin replay. Laterales parten a ancho+4 px y Top
a rowHeight+12 px; todas llegan a offset cero a los 500 ms. Efectos, texto y
desplazamiento retenido mantienen sus tiempos. Modos direccionales dibujan filas
en orden inverso para que la nueva quede delante; Appearance conserva el orden
original. Todas las capas y efectos reciben recorte al canvas del mismo renderer.

`oto_event_list` es una segunda fuente INPUT/CUSTOM_DRAW, vídeo únicamente.
Cada ListInstance conserva ajustes/visibilidad/frame inmutable y canal TEST propio;
un EventListHub recibe los mismos Core/VisualEngine/ObsVisualRenderer/Registry del
módulo. Una notificación central refresca ambas familias de fuentes tras cada lote.
El snapshot central aporta hasta 64 actividades por canal; filtros Follow/Sub/Bits/
Gift/Raid y 1–10 filas solo afectan la vista de cada instancia. Canvas 420–1200 px,
tarjetas 48–96 px, 8 px de separación. Ningún fondo de las referencias entra en la
fuente. Primer diseño Minimal desde `eventListStyle` del preset compartido; los
otros cinco diseños de lista aún no se anuncian como implementados.

Cada fila separa tarjeta/icono/tipo/detalle y nombre en capas cacheadas.
Los cinco iconos son paths genéricos acotados, preparados por VisualEngine GUI;
renderer no conoce Twitch ni dibuja paths/QPainter por frame. Nueva entrada con duración predeterminada:
0–200 ms aparición/brillo, hasta 500 ms entrada, 500–1000 ms texto, hasta 4 s
destacado suave y hasta 6 s fade al reposo exacto. Filas retenidas se desplazan
500 ms reutilizando sus texturas; una actualización rápida captura su posición
actual, sin cola de animaciones creciente. Ocultar, cargar y cambiar ajustes no
reproducen eventos históricos. Nombres desbordados reutilizan el ping-pong.

La lista se prepara al cambiar datos, preferencias o visibilidad. El transcurso
del tiempo no reconstruye frames ni produce actualizaciones de GUI.
No hay timers de edad, timers por fuente, sondeo de Twitch,
video_tick ni trabajo de texto/disco/readback en video_render.

TEST y LONG TEXT solicitan once eventos al Core por la misma cola/reductor y un
canal Preview efímero. Una lista previa se prepara en reposo y la nueva entrada
desplaza las anteriores, expulsando la última. Nombres marcados TEST. No consume pendientes ajenos ni
escribe persistencia. Finalizar/destroy liberan solo ese canal. Los callbacks GUI
usan referencias débiles/coalescing; stop libera frames bajo contexto gráfico
antes de las cachés compartidas. La prueba manual GPU está fuera de video_render.

Los siguientes apartados conservan la evolución de Supporter Bar.

Estado actual 0.12.29: la barra consume el estado central real/simulado, presets y
efectos compartidos. Los apartados de fases anteriores documentan su evolución.
BarContext guarda generación, canal, revisiones de últimos campos y entregas
por tipo junto al frame preparado. Las diferencias de texto no bastan para
iniciar efectos: solo una nueva revisión del campo, o nueva entrega Cheer/Sub/
Gift para sus diarios, permite animar valores que cambien. Carga, backfill,
cambio de canal/generación y retorno de TEST quedan quietos en los cuatro modos.
TEST explícito conserva los efectos; contextos sin cambios visuales actualizan
la referencia sin rasterizar ni reiniciar tiempos. SharedState recupera últimos
campos vacíos/obsoletos desde actividad guardada real con revisión/delivery cero,
sin alterar contadores, descargar fotos, encolar o reproducir alertas.
Los cuatro campos de nombres usan tiras de texto cacheadas y recorte GPU:
si desbordan, ping-pong con pausa de 1200 ms en cada extremo; si caben, quietos.
Los contadores y títulos mantienen Ellipsis. No hay layout ni rasterización por
fotograma. Solo una fuente visible con transición o nombre desbordado consulta
el reloj. El movimiento empieza después de la transición de cambio de valor.

`name_font` es la propiedad FONT nativa de OBS: su botón abre el diálogo clásico.
Solo se aplica su familia; tamaño, negrita y colores siguen el preset.
`use_style_font`, casilla debajo del botón, hereda el preset sin borrar la selección
personalizada. Mientras está marcada, el botón queda deshabilitado. OBS gestiona
el diálogo; el Hub ya no enumera familias al arrancar.
Se migra la cadena antigua `value_font` sin perder una familia seleccionada.
Cambiar familia rehace solo los cuatro nombres, sin
alerta ni cambios de títulos, iconos o contadores. OBS persiste el ajuste en su
escena. Una familia ausente conserva la selección y utiliza el fallback de Qt.
Ver [checkpoint 0.11.4](../../docs/validation/phase-11-ping-pong.md).

`oto_supporter_bar` es una fuente INPUT de vídeo con CUSTOM_DRAW, sin audio.
Muestra Last Follow/Sub/Cheer/Gifted y Daily Bits/Subs del canal simulado central.
Los callbacks siguen el contrato de [OBS sources](https://docs.obsproject.com/reference-sources).

Cada SourceHandle posee BarInstance (ajustes, dimensiones, visibilidad, frame const)
y una referencia débil al único SupporterHub. Los ajustes se guardan en la escena
de OBS: ancho 600–1920, alto 80–240 y seis campos activables. Todo desactivado produce
una fuente transparente. El estilo se selecciona por ID de preset compartido;
Minimal es el valor predeterminado compatible. Ver [presets](../presets/README.md).
El motor compartido prepara los textos sin cortar bytes UTF-8.

El Hub recibe Core, VisualEngine y ObsVisualRenderer del módulo; no crea otros.
Una notificación tras publicar el lote obtiene una instantánea y fecha local para
preparar las instancias visibles. Dos escenas idénticas comparten FramePtr y textura.
Una configuración distinta genera otra escena sin modificar las demás fuentes.
El plan BarFrame conserva una capa de fondo/títulos y hasta seis capas de valores.
Solo campos visibles modificados solicitan imágenes al motor. Las fuentes nunca
consumen la Execution Queue.

## Hilos y visibilidad

Create/update/show/hide solicitan trabajo GUI mediante invokeMethod encolado,
coalescido por instancia. Ajustes usan mutex; dimensiones/flags y publicación del
frame son atómicos. Prepare/layout/rasterización solo se ejecutan en el hilo GUI.
video_render únicamente compone capas ya preparadas y calcula opacidad/posición
mientras haya una entrada activa, dentro del contexto gráfico
de OBS. No hay video_tick, readback, acceso a disco ni QTextLayout por frame.

Se usa show/hide (visibilidad, incluida la previsualización de OBS), no activate:
la barra debe verse también sin iniciar una emisión. Ocultar suelta el frame de la
instancia y suspende preparación/dibujo; la caché compartida puede conservarlo bajo
sus límites LRU. Volver a mostrar prepara el estado actual. Destroy libera el handle;
las peticiones encoladas usan referencias débiles.

Un solo QTimer single-shot hasta la próxima medianoche local refresca las barras
visibles y vuelve a programarse. Sin fuentes visibles se detiene. No hay polling.
Los cambios manuales de reloj/zona horaria se reflejan al mostrar/actualizar o al
vencer el aviso existente; no se monitorizan continuamente.

Durante EXIT, stop se serializa con video_render mediante obs_enter_graphics,
desactiva el Hub y suelta frames. Después se limpian las cachés y se detiene Core.
El Hub vive hasta unload para descartar sus peticiones Qt en el hilo GUI. No se
retienen consumidores después de descargar la DLL.

## Contadores diarios

Cheer suma bits; Sub suma uno y Gift suma su cantidad. Eventos de otros días no
incrementan el total de hoy. El estado conserva un bucket por canal, sin histórico;
la vista muestra cero cuando su fecha no coincide con hoy. El archivo puede seguir
conteniendo el bucket anterior hasta que un nuevo lote lo sustituya.

Core restaura diarios, últimos cuatro campos y actividad acotada desde
JsonDailyStore al iniciar; los totales de sesión empiezan vacíos. El archivo
`daily-totals.json` usa schemaVersion 3, calendar system-local, hasta 16 canales,
64 entradas de actividad por canal y 2 MiB. Lee v1/v2 y recupera los cuatro últimos
eventos disponibles de v2 como actividad quieta. No inventa fechas ni eventos.
Los enteros de 64 bits son cadenas decimales para evitar pérdida de precisión.
QSaveFile reemplaza atómicamente; archivos inválidos/incompatibles se conservan y
fallan la carga del módulo, y cambios externos impiden guardarlos.

Preparar estado → guardar si cambia diario/últimos/actividad → confirmar lote → publicar →
notificar fuentes. Un fallo de guardado conserva lote y estado anterior. No es
un journal de todos los eventos: Chat/Hype Train no forman parte del historial.
La actividad guardada evita duplicados restaurados; la cola central mantiene su
deduplicación en memoria para todos los eventos nuevos.

## Verificación y límites

`tests/supporter-tests.cpp` prueba campos, medianoche con reloj inyectado, eventos
atrasados, snapshots inmutables, frames compartidos, opciones independientes,
persistencia/reinicio, precisión UINT64_MAX y errores de guardado.
Ver [validación real](../../docs/validation/phase-7.md).
Sin Twitch ni presets. Animaciones básicas validadas en [Fase 9](../../docs/validation/phase-9.md).

## TEST/PREVIEW — Fase 8

Tres botones de propiedades: TEST / PREVIEW, LONG TEXT y Finalizar prueba. El
callback GUI llama al Hub, que solicita cuatro eventos al Core. Core asigna un
canal efímero de origen Preview, valida por EventEngine y encola en la misma
ExecutionQueue. Prepara un lote filtrado por ese origen/canal y lo entrega mediante
el mismo applyBatch/reductor/publicación/notificación. No drena pendientes del panel.

Las muestras viven en canales del único SharedState; no hay otro motor, cola,
reductor ni caché por fuente. dailyBook excluye Preview, por lo que la muestra no
provoca escrituras de totales. La fuente conserva solo el ID del canal, en GUI;
sus etiquetas muestran TEST. El renderer sigue sin conocer eventos ni rutas.

Un nuevo botón sustituye la muestra anterior. Finalizar borra el canal efímero y
prepara el estado actual del canal normal; otras muestras permanecen intactas.
Destroy encola la limpieza al hilo GUI con un handle retenido hasta completarla.
Stop vacía todo el estado. IDs/muestras no forman parte de obs_data ni settings.
Ocultar o cerrar propiedades conserva la prueba hasta Finalizar, destroy o reinicio.
Si la fuente es visible, la muestra aparece también en cualquier salida OBS que la use.

Máximo 14 previews y límite global de 16 canales (se reserva espacio antes de crear
una muestra). Fallos de admisión parcial cancelan únicamente los jobs de la muestra.
El historial acotado de deduplicación puede conservar sus IDs; nunca se reutilizan.
Los valores de la muestra se mantienen 100/6 aunque cambie el día; los datos normales
siguen usando la fecha local. No se añaden timers para finalizar la prueba.

## Actualización selectiva y movimiento — Fase 9

BarFrame es un plan inmutable por instancia, publicado atómicamente. Comparar
valores visibles idénticos devuelve el mismo plan sin tocar la caché; un cambio
reutiliza fondo, otros valores y sus tiempos de inicio. Cambios de dimensiones,
campos o títulos rehacen la distribución; la caché puede reutilizar sus imágenes.
El texto de valores se rasteriza sobre transparencia para poder componerlo y moverlo
independientemente. Mantiene geometría/recorte; el suavizado puede diferir de dibujar
glifos directamente encima del fondo, como se hacía en Fase 8.

Modo y duración pertenecen a cada fuente (None/Fade/SlideFade, 100–1000 ms).
Cambiar preferencias asienta la entrada actual sin rasterizar. Un valor que vuelve
a cambiar durante la entrada inicia la nueva muestra; no hay cola de animaciones ni
retención de imágenes salientes. El reloj deja de consultarse al finalizar todas
las entradas; OBS sigue componiendo capas estáticas cacheadas.

El seguimiento de plan/fin en el hilo gráfico es débil para no retener imágenes
ocultas. Hide y publicación comparten el mutex de ajustes; stop se serializa con
dibujo. TEST/PREVIEW suprime únicamente la notificación intermedia de su transacción
síncrona GUI y prepara una vez su fuente con el snapshot final.

Pruebas: `tests/preview-tests.cpp` y [validación OBS](../../docs/validation/phase-8.md).

Space: energyPattern=3 del mismo shader, carcasa orbital, anillos mecánicos,
rails iónicos y estrellas. Foto circular centrada, seis clips propios preparados
bajo demanda, títulos horizontales, placa automática; mismo timeline/cola/cache.

Horror: energyPattern=4 compartido, placas fracturadas, humo, grietas y
brasas GPU; seis iconos articulados preparados bajo demanda. Foto centrada,
placa automática, dos FONT, tres tiempos y salida inversa sin nuevo renderer.

Love: energyPattern=5 compartido, cristal violeta/rosa, cintas, corazones y
destellos GPU; seis clips propios bajo demanda. Foto centrada/esperada,
placa automática, FONT/herencias y tres tiempos; salida inversa continua.

Graffiti: energyPattern=6 compartido, arcos de pincel, ráfagas diagonales,
gotas con trayectorias curvas y bandas de tinta estrechas; seis clips de marcador articulados.
Foto centrada/esperada, placa automática, FONT/herencias y tres tiempos.

Minimal: contornos finos blanco/plata y azul hielo; arcos abiertos, reflejos
viajeros y seis iconos articulados. Sin gran panel de fondo. Retrato centrado,
placa automática, FONT/herencias, tres tiempos y salida inversa continua.

Urban City: marco industrial compacto de bronce con esquinas desplegables,
faroles ámbar, rejillas/reflejos móviles y seis iconos mecánicos articulados.
1000² transparente, foto centrada/esperada, tres tiempos y salida inversa.

0.12.27 añade Futuristic Hologram Animated Alerts: patrón 9 del mismo renderer,
aros/escaneo/fragmentos abiertos GPU y seis clips de facetas translúcidas.
Diez estilos de alertas; foto esperada/centrada y opciones comunes conservadas.
