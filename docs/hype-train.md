# Breaking News — Hype Train

Activa **Breaking News — Hype Train → Mostrar Breaking News de Hype Train**.
La opción está desactivada inicialmente para conservar las dimensiones de las fuentes existentes.

La banda aparece encima del ticker al comenzar un Hype Train válido y desaparece mediante fundido al finalizar o caducar. Muestra exclusivamente:

`BREAKING NEWS | WE HAVE A HYPE TRAIN | LEVEL n | porcentaje | mm:ss`

Solo WE HAVE A HYPE TRAIN parpadea suavemente. El parpadeo puede desactivarse o ajustarse mediante Duración del ciclo de parpadeo: 200–4000 ms, inicialmente 700 ms. Un valor menor es más rápido. El nivel, el porcentaje y el tiempo proceden de Twitch Core; el tiempo se calcula con la hora de expiración, y se actualiza cuando Twitch extiende el tren. El porcentaje es el progreso del nivel dividido por su objetivo, limitado al 100 %.

## Tamaño y aspecto

- Ancho independiente en píxeles. Cero usa automáticamente el 65 % del ancho del ticker. Nunca supera el ancho total de la fuente.
- Altura independiente entre 16 y 200 px; 28 px inicialmente.
- Estilo, materiales y colores adaptados a los 10 estilos normales y 7 compactos mediante un único renderizador. No se utilizan las láminas de propuestas como fondos con texto incrustado.
- Colores personalizados opcionales para texto, fondo y acento, incluyendo opacidad.
- El ancho de las bandas originales se conserva. La banda adicional queda pegada al borde superior del ticker. Al habilitarla o cambiar su altura, el lienzo se amplía hacia arriba conservando la posición de las bandas originales en cada instancia de escena; se respetan escala, giro y límites. El espacio queda reservado y transparente durante los periodos sin un tren activo. La entrada sube desde detrás del ticker, sin separación adicional.

## Vista de prueba

**Vista de prueba (datos ficticios; visible en la emisión)** muestra un nivel 3 al 67 % con cuenta atrás. Permite ajustar el aspecto sin un Hype Train real. Desactívala antes de usar datos reales. No conecta Twitch ni genera eventos en el canal.

## Twitch Core

El instalador incluye Twitch Core 1.3.1, ABI 6. La extensión opcional nexus_twitch_core_get_hype_api comparte la misma conexión y autorización del resto de plugins. Mantiene intactas las interfaces ABI 6 y la extensión de noticias existente. No añade una segunda conexión, OAuth o EventSub propios del ticker.

Se necesita el permiso de lectura channel:read:hype_train. Si la conexión no está autorizada, cambia de canal, termina el tren o caducan los datos, la banda se oculta. El motor utiliza solo eventos de origen Twitch para el modo real; los eventos de simulación y los de otras cuentas no se presentan como reales.

Referencia oficial: https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/#channelhype_trainprogress

## Verificación

- Core: pruebas de datos de comienzo, progreso, finalización, caducidad, aislamiento de cuenta y pérdida de autorización. Pruebas ABI de conexión compartida, liberación y cierre.
- Ticker: 35 comprobaciones nativas con libobs y D3D11, incluyendo los 17 estilos, ancho/altura independientes, composición premultiplicada, parpadeo del mensaje y flujo controlado hasta nivel, porcentaje, cuenta atrás y retirada.
- Regresión general: 216 comprobaciones nativas sin fallos, además de los verificadores de meteorología, Playing y observador Twitch.
- La comprobación de eventos utiliza datos controlados. No certifica un Hype Train real de Twitch; no se ha iniciado autorización ni una emisión como parte de estas pruebas.

- Prueba final en OBS portable: 90 segundos, Playing oculto y cambios de canción, meteorología real, demo Hype activa y los 16 estilos broadcast recorridos (Basic probado en el renderizador nativo). 30 comprobaciones sin fallos, cierre normal y cero fugas notificadas por OBS.
- Instalador: 23 comprobaciones de actualización desde Core 1.1.0 y 24 desde Core 1.2.0, incluyendo el caso reconocido sin registro; configuraciones y archivos ajenos conservados en las carpetas aisladas.


## Simulación desde Twitch Overlays

El TEST del panel de Twitch Overlays se recibe mediante una extensión de vista previa separada, sin autorización Twitch. Se lee exclusivamente el canal de prueba seleccionado por ese panel. El mensaje muestra TEST · WE HAVE A HYPE TRAIN y el nivel, porcentaje y tiempo de esa simulación. Al pulsar Finalizar prueba, desaparece la simulación y vuelve el estado real; si la vista de prueba local sigue activada, vuelve a ella. El TEST del panel tiene prioridad sobre la vista de prueba local mientras está activo.

Los eventos de prueba no entran en los contadores ni en la conexión de datos reales. No hay un segundo OAuth, una segunda conexión Twitch ni escritura en el canal.

## Corrección de colocación y pruebas — 2026-10-25

41 comprobaciones nativas incluyen anclaje al activar, redimensionar y desactivar, fuentes giradas/escaladas/con límites, banda sin separación, parpadeo y transmisión de TEST hasta el texto visible. Los redondeos de coordenadas de OBS se comprueban con tolerancia de medio píxel. Se utiliza el mismo simulatePanelEvent del panel de Overlays en las pruebas de Core, incluyendo notificación temporizada y Finalizar TEST. La validación no equivale a un Hype Train real.
