# OBS News Ticker NEXUS â€” acuerdo de producto

Recuperado del texto facilitado por el usuario el 24 de octubre de 2026.

## Referencias

- `Diez diseÃ±os de rÃ³tulos broadcast para streaming.png`: diez estilos broadcast, referencia principal inicial.
- `CatÃ¡logo de Tickers para DJ en Directo.png`: diez estilos DJ, segundo grupo visual que se conserva para desarrollar.
- Los grÃ¡ficos son referencias, no fondos para recortar y poner en emisiÃ³n. Los presets compartirÃ¡n un motor de renderizado.
- Compacto, bandas finas, proporciones equilibradas; marca NEXUS Ãºnicamente en las propiedades, sin imponerla en emisiÃ³n.

## Contenido y comportamiento previstos

- Banda superior: Now Playing, reloj local, World Weather.
- Banda inferior: eventos Twitch y mensajes personalizados (hasta cinco mensajes segÃºn la conversaciÃ³n).
- Scroll continuo, bloques dinÃ¡micos y alternado.
- Anchura configurable, expansiÃ³n dinÃ¡mica de bloques.
- Colores, tipografÃ­a y elementos grÃ¡ficos personalizables.
- Cabecera de texto libre, color del texto, fondo y opacidad editables, anchura automÃ¡tica y vista previa.
- En bloques dinÃ¡micos, la cabecera queda fija y los mensajes salen de detrÃ¡s y regresan.

## Twitch Core Shared

El instalador final incluirÃ¡ el Core: instalar si falta, reutilizar una versiÃ³n compatible y comprobar la compatibilidad con otros plugins antes de actualizar. Una conexiÃ³n compartida para OAuth, EventSub, Helix, cachÃ©s y distribuciÃ³n de eventos. News Ticker tendrÃ¡ motor visual, animaciones y configuraciÃ³n propios y no requerirÃ¡ otros plugins NEXUS.

Contenido previsto: Last Follow, Last Sub, Daily Subs, Daily Cheers, Last Raid, Last Cheer y Last Gift/Gifted. La recuperaciÃ³n tras reinicio dependerÃ¡ de datos persistidos y de lo que Twitch permita consultar. No se presume que se puedan reconstruir todos los eventos histÃ³ricos desde la API.

## Fase 1 implementada

Estructura compilable de plugin nativo Windows x64, fuente de entrada OBS y propiedades estÃ¡ndar de OBS en espaÃ±ol e inglÃ©s. Renderizador inicial de dos bandas, texto manual, cabecera autoajustable, tipografÃ­a y colores con alfa. El texto se renderiza mediante fuentes privadas de Text (GDI+) de OBS. El texto largo se reduce para caber en su banda en esta fase.

Desde 0.1.1, por peticiÃ³n del usuario: altura de banda superior y altura de banda inferior independientes (24â€“400 px por banda, 50 px inicialmente). La altura total de la fuente es la suma. Las configuraciones antiguas conservan la divisiÃ³n de su altura total al migrar. La cabecera se ajusta a la banda inferior.

## Primer incremento visual â€” 0.2.0

Selector de diseÃ±o BÃ¡sico / News Channel (primera versiÃ³n). News Channel interpreta la referencia broadcast 1: cabecera izquierda en dos niveles con texto y subtÃ­tulo editables, etiquetas editables para ambas bandas, degradados y banda inferior clara. Un botÃ³n aplica la paleta explÃ­citamente; seleccionar un estilo conserva los colores existentes, los textos y las alturas. El color del texto inferior se configura por separado para mantener contraste sobre un fondo claro.

El primer estilo no es una reproducciÃ³n completa del grÃ¡fico: todavÃ­a faltan mapas y mÃ³dulos grÃ¡ficos de reloj/meteorologÃ­a. La selecciÃ³n inicial de dos bandas no reproduce la excepciÃ³n de una fila del preset broadcast Ultra Compact.

## Scroll continuo â€” 0.3.0

Movimiento opcional de los textos superior e inferior hacia la izquierda, con velocidad de 10â€“400 px/s y separaciÃ³n de 16â€“600 px. Cabecera, subtÃ­tulo y etiquetas fijos. Recorte local por banda mediante dos superficies GPU reutilizables, tambiÃ©n al escalar/rotar la fuente. El texto largo conserva el ajuste vertical y recorre su contenido completo. Cada banda tiene su ciclo. Los mensajes cortos pueden mostrar dos copias durante la repeticiÃ³n.

El movimiento se pausa al ocultar la fuente; vuelve a continuar al mostrarla. Editar un texto reinicia su ciclo; cambiar velocidad conserva la posiciÃ³n. EstÃ¡tico sigue siendo el modo inicial. Las alturas y los colores independientes siguen disponibles en ambos diseÃ±os.

## Bloques dinÃ¡micos â€” 0.4.0

Tarjeta que sale desde el Ã¡rea fija izquierda, permanece visible, se retira hacia la izquierda y se oculta durante una pausa antes de repetir. Entrada y salida suavizadas. Tres tiempos configurables: entrada/salida (150â€“2000 ms), permanencia (500â€“20000 ms) y pausa (100â€“5000 ms). Valores iniciales: 500 / 3000 / 600 ms.

El ancho de cada tarjeta se adapta al texto, limitado por el espacio de su banda. El texto largo se reduce para caber en este modo. Se mantienen fijos cabecera, subtÃ­tulo y etiquetas. La animaciÃ³n se pausa al ocultar la fuente. Editar un mensaje reinicia Ãºnicamente su ciclo; cambiar modo o tiempos reinicia ambos.

En la versiÃ³n 0.4.0 se repetÃ­a un mensaje manual por banda; desde 0.5.0 la banda inferior rota los mensajes personalizados configurados durante la fase oculta.

## RotaciÃ³n y alternado â€” 0.5.0

Cinco mensajes personalizados en la banda inferior, reutilizando `lower_text` como mensaje 1. Se omiten entradas vacÃ­as o formadas Ãºnicamente por espacios ASCII. EstÃ¡tico y Scroll muestran el primero no vacÃ­o; Bloques cambia de mensaje durante su pausa oculta.

El modo Alternado con fundido mantiene estÃ¡tica la banda superior y rota los mensajes inferiores con entrada y salida por opacidad. Tiempo visible configurable (500â€“30000 ms, inicialmente 5000) y fundido (100â€“1000 ms, inicialmente 300), mÃ¡s 100 ms ocultos para preparar el nuevo texto nativo. RGB y alfa premultiplicados se atenÃºan conjuntamente.

Ocultar la fuente pausa el reloj. Editar la lista o cambiar modo/tiempos de alternado reinicia desde el primer mensaje no vacÃ­o. Guardar/reabrir conserva los cinco textos y ajustes; la posiciÃ³n de reproducciÃ³n empieza de nuevo por el primer mensaje. Cambiar colores conserva la posiciÃ³n activa.

## Reloj local â€” 0.6.0

Reloj fijo opcional a la derecha de la banda superior. Lee la hora y zona local de Windows, con formatos de 24 horas o 12 horas AM/PM y segundos opcionales. Comparte tipografÃ­a y color con el texto superior. Desactivado inicialmente para conservar los diseÃ±os existentes.

Se reserva un espacio independiente y se reduce el ancho disponible para el mensaje musical. En anchuras pequeÃ±as tambiÃ©n se ajusta el reloj para no invadir la otra banda ni el margen derecho. Es compatible con ambos diseÃ±os y todos los modos de movimiento. Desactivarlo devuelve el espacio al mensaje.

Se consulta la hora aproximadamente cuatro veces por segundo cuando estÃ¡ activo, pero el texto nativo solo cambia cuando cambia el valor mostrado; no reinicia las animaciones ni la lista de mensajes.

## Receptor Now Playing â€” 0.7.0

Entrada opcional mediante archivo JSON local con contrato versionado `nexus.now-playing.v1`: estado de reproducciÃ³n explÃ­cito, artista/tÃ­tulo y marca temporal UTC. Lector en segundo plano compartido por ruta normalizada; sin lectura ni anÃ¡lisis JSON en el renderizador. Caducidad configurable de 3â€“300 segundos, inicialmente 15.

Datos pausados, detenidos, ausentes, invÃ¡lidos, futuros o caducados retiran el tema. El respaldo manual es opcional y comienza desactivado. Cambiar un tema externo no reinicia la lista inferior ni el reloj; volver a Manual recupera su texto guardado.

Es el receptor del contrato, no una conexiÃ³n directa terminada con OBS Playing Companion. La revisiÃ³n de su cÃ³digo no localizÃ³ una exportaciÃ³n pÃºblica compatible. No se han modificado sus archivos ni duplicado sus lectores DJ. Falta un publicador soportado o un adaptador del protocolo del Companion. Contrato y lÃ­mites en `now-playing-json-v1.md`.

Pendientes: conexiÃ³n directa de OBS Playing, demÃ¡s presets, cola de eventos en directo, meteorologÃ­a, eventos Twitch, contadores e instalador.

## Luz suave en NOW PLAYING â€” 0.7.1

La etiqueta superior de News Channel sustituye su rectÃ¡ngulo sÃ³lido por una luz ovalada tenue. El color y la opacidad parten del acento de cabecera; el brillo se desvanece suavemente hasta transparencia en todos sus bordes. El texto permanece nÃ­tido y conserva su posiciÃ³n. La cabecera principal y la etiqueta inferior mantienen su diseÃ±o.

## Decisiones pendientes de fases posteriores

Proveedor de meteorologÃ­a, publicador soportado de Now Playing, validaciÃ³n de Twitch con cuenta autorizada y eventos reales, catÃ¡logo definitivo de presets y medidas exactas por estilo. Resolver estos puntos con evidencia antes de integrar servicios.

## Receptor compartido Twitch â€” primer incremento

Observador opcional de la ABI 6 existente, sin OAuth adicional ni cambios en el propietario de la conexiÃ³n. Las propiedades muestran estado y Ãºltimo login de raid aceptado al abrirlas. Valida canal e instancia, descarta duplicados recientes y borra los datos al perder autorizaciÃ³n o cerrar Core.

Una opciÃ³n desactivada por defecto permite mostrar la Ãºltima raid en la banda inferior, con texto traducido y los colores y animaciÃ³n existentes. Sustituye los mensajes mientras haya una raid vÃ¡lida; al desactivar la opciÃ³n o perder el evento autorizado, vuelven los mensajes guardados. No cambia la etiqueta editable. Se mantiene la Ãºltima raid, sin caducidad ni cola de eventos. Las cuatro animaciones comparten la selecciÃ³n del contenido.

Las propiedades reciben la seÃ±al de actualizaciÃ³n de OBS cuando cambian la conexiÃ³n, el canal o la raid, incluso con la visualizaciÃ³n de raids desactivada. El sondeo de la copia local se realiza cada 250 ms, sin emitir seÃ±ales repetidas cuando nada cambia. Se identifica cada raid por su ID: otra raid con el mismo nombre tambiÃ©n reinicia la animaciÃ³n. La opciÃ³n de raids y los mensajes personalizados se conservan al guardar y recargar.

La prueba con OBS portÃ¡til y Twitch Core 1.1.0 real detectÃ³ una dependencia del orden de post-load. El receptor espera ahora al evento de OBS FINISHED_LOADING, cuando los propietarios ya terminaron de inicializarse. Se confirmÃ³ la suscripciÃ³n real, el estado Â«esperando canal autorizadoÂ» en propiedades y el cierre normal con cero fugas registradas. No se copiaron credenciales ni se autorizÃ³ una cuenta; sigue pendiente la recepciÃ³n real de eventos Twitch.

La opciÃ³n adicional Â«Intercalar la raid con los mensajesÂ» incorpora la Ãºltima raid como primer elemento de la secuencia en Bloques y Alternado, seguida de los mensajes personalizados no vacÃ­os. EstÃ¡ desactivada por defecto. EstÃ¡tico y Scroll mantienen la Ãºltima raid fija. Una nueva raid reinicia la secuencia; al perderla, vuelven los mensajes desde el primero. No constituye una cola histÃ³rica de eventos.

La interfaz revisada publica raids y Hype Train; no publica espectadores de raid, follows, subs, cheers ni regalos. No se simulan estos datos como integraciÃ³n terminada. LÃ­mites y pruebas en `twitch-observer.md`. Se conserva la versiÃ³n 0.7.1 durante este desarrollo.

## Twitch Core 1.2.0 â€” integraciÃ³n ampliada

La limitaciÃ³n anterior corresponde a la ABI 6 original. Core 1.2.0 conserva esa interfaz y aÃ±ade un contrato opcional para los Ãºltimos follows, subs, cheers, regalos, raids con espectadores y contadores diarios registrados. News Ticker inicia el mismo runtime compartido sin depender de otro plugin, conserva una referencia de biblioteca y libera solamente su propietario al salir. La autorizaciÃ³n y desconexiÃ³n son explÃ­citas desde propiedades; no se solicita escritura de chat ni ediciÃ³n del canal para el ticker.

Las siete categorÃ­as son seleccionables, desactivadas por defecto, y pueden intercalarse con los mensajes en Bloques y Alternado. Anonimato, deduplicaciÃ³n, contabilidad y persistencia permanecen en el motor central. Los contadores usan el dÃ­a local de Windows y reflejan eventos registrados, con regalos contados una sola vez y mensajes de resuscripciÃ³n excluidos. El historial local restaura los Ãºltimos eventos sin volver a entregarlos como alertas.

El instalador Windows incluye Core, inspecciona contratos y consumidores antes de actualizar, reutiliza versiones compatibles y conserva configuraciones, credenciales, archivos ajenos y backups. La desinstalaciÃ³n conserva el Core compartido. La versiÃ³n del ticker se mantiene en 0.7.1; la biblioteca compartida pasa a 1.2.0 con ABI 6. Detalles y lÃ­mites actuales en `twitch-core.md`.

## Referencia visual y separaciÃ³n de bandas â€” revisiÃ³n actual

La referencia completa de 1370Ã—73 sustituye la primera interpretaciÃ³n de News Channel: cabecera roja con relieve y sombras, subtÃ­tulo claro centrado, sÃ­mbolos musicales, Now Playing fijo, reloj, zona meteorolÃ³gica manual y mapa azul con pulso y barrido de luz suaves. El mapa usa solamente esa regiÃ³n del original suministrado; el resto se dibuja nativamente. La meteorologÃ­a automÃ¡tica sigue pendiente; no se inventan temperaturas.

Now Playing tiene ancho configurable, con letra de tamaÃ±o constante y recorte de tÃ­tulos largos. Al cambiar una canciÃ³n se funde la anterior, se prepara el nuevo texto oculto y entra con fundido. El movimiento seleccionado afecta exclusivamente a las noticias inferiores.

Todos los eventos activados y mensajes inferiores se unen con un separador â€¢, salvo en Bloques dinÃ¡micos, que los muestra uno a uno. EstÃ¡tico, Scroll y Fundido de banda completa utilizan la lÃ­nea completa. La antigua opciÃ³n de intercalar raids ya no es necesaria. Â«Aplicar disposiciÃ³n de referenciaÂ» ajusta presentaciÃ³n y medidas sin reemplazar textos ni conexiones. Esta revisiÃ³n sustituye las reglas anteriores de movimiento compartido y rotaciÃ³n en Alternado.

## Estado actual — 2026-10-25

Esta revisión sustituye los pendientes históricos de Now Playing, presets e instalador descritos arriba. Hay dos grupos de estilos, Normal y Compact; fuente nativa, noticias y mensajes unidos salvo Bloques dinámicos, alturas independientes y cabecera editable. El paquete incluye Twitch Core compartido 1.2.0.

Now Playing automático consulta el procedimiento público de la fuente nativa de desarrollo de OBS Playing. Implementado y comprobado con ambos módulos reales en OBS portable usando canciones demo. Requiere Playing actualizado y una única fuente activa. No certifica todavía una sesión con programa DJ real. No modifica el instalador de Playing ni el OBS habitual.

Pendientes funcionales y de validación: meteorología automática, Twitch con cuenta y eventos reales, sesión DJ real y estabilidad prolongada en emisión. El historial de desarrollo anterior se conserva como registro, no como lista actual de pendientes. Guía: now-playing-setup.md.

## Meteorología automática — primer incremento

Open-Meteo gratuito, seleccionado explícitamente para uso no comercial. Una ciudad por fuente, búsqueda por ciudad y país con primer resultado identificado en propiedades, Celsius, estado e icono por código del proveedor. Consultas compartidas en segundo plano cada diez minutos, reintentos y retirada de datos inválidos o caducados. Manual conservado por defecto. Rotación mundial, selector de ciudades y modalidad comercial quedan fuera de este incremento. Validada consulta HTTPS real y recepción en la fuente nativa; detalles en weather.md.

## World Weather con rotación

Hasta cinco ciudades configurables, rotación opcional y tiempo por ciudad de 5–300 segundos, 15 inicialmente. Se omiten ciudades no disponibles y duplicados de la misma consulta, con reloj de rotación pausado al ocultar la fuente. Mantiene música, noticias y consultas compartidas independientes. Desactivar vuelve a la primera ciudad sin perder las adicionales. Esta revisión sustituye el pendiente de rotación mundial del primer incremento meteorológico.

## Selección definitiva de capitales

Sustituye el límite anterior de cinco ciudades. Las 24 capitales indicadas por el usuario se incluyen, en su orden, en una lista editable: Madrid, Londres, París, Roma, Tokio, Nueva Delhi, Washington D. C., Abu Dabi, Seúl, Pekín, Buenos Aires, Bogotá, Santiago de Chile, Brasilia, Ciudad de México, Ottawa, Berlín, Ámsterdam, Bruselas, Varsovia, Lisboa, Viena, Atenas, Estocolmo. Todas seleccionadas inicialmente para la rotación en fuentes nuevas, manteniendo la meteorología Manual por defecto hasta activar el servicio. Catálogo con país, coordenadas e identificador verificados; las listas personalizadas anteriores se migran y existe un botón para restaurar la selección completa.

## Ping-pong opcional de Now Playing

El texto musical admite Fijo o Ping-pong para títulos que exceden la anchura disponible. Mantiene tamaño y recorte, velocidad independiente y pausa en los extremos. El temporizador se pausa al ocultar y se reinicia al cambiar el tema, respetando el fundido entre canciones. Las noticias inferiores conservan sus controles independientes. Fijo permanece como valor inicial para preservar configuraciones anteriores.

## Idioma de emisión

Los textos automáticos de eventos y contadores se muestran en inglés, independientemente del idioma de OBS: LAST FOLLOW, LAST SUB, LAST CHEER, LAST GIFTED, LAST RAID, DAILY SUBS y DAILY BITS. Se mantiene la indicación (recorded) en los contadores. Los mensajes y cabeceras personalizados no se traducen automáticamente. Las capitales meteorológicas se muestran en inglés sin cambiar las ubicaciones ni el orden guardados. Los controles de propiedades mantienen la localización de OBS.

La franja de Hype Train está implementada con LEVEL, BREAKING NEWS y WE HAVE A HYPE TRAIN. Ancho y altura independientes, parpadeo opcional, vista de prueba explícita y datos de nivel, progreso y finalización desde Core 1.3.1. Detalles: hype-train.md.


## Breaking News — implementación 2026-10-25

Banda opcional sobre los 17 estilos. Se conserva la altura de las bandas existentes; al habilitarla se reserva su altura hacia arriba, sin separación adicional. Ancho automático 65 % o ancho fijo en píxeles, altura 16–200 px y colores opcionales. Twitch Core 1.3.0 conserva ABI 6 y expone una extensión de lectura compartida para Hype Train. La prueba de eventos reales en una emisión sigue pendiente.

Corrección: la banda crece hacia arriba sin mover las bandas existentes. Parpadeo configurable en ms y TEST de Twitch Overlays conectado por una vía independiente, con marca TEST y sin modificar los datos reales.
