# Shared Visual Engine / Text Layout Engine — Fases 6, 9 y 11

Fase 12 incorpora `PathNode`: QPainterPath genérico, stroke/fill y grosor en la
clave compartida. Máximo 256 elementos, coordenadas finitas ±4096, grosor .5–16.
Validación y rasterización solo durante prepare GUI; draw recibe la textura
habitual. Event List utiliza el mismo motor/cache/shaders y capas por fila.

0.12.1 incorpora recorte opcional genérico en `ObsVisualRenderer`: un QRectF del
canvas se convierte en límites UV del quad trasladado. Los shaders compartidos
de movimiento y cambio devuelven transparencia fuera de esos límites; también
funciona bajo la matriz de escena OBS. Sin recorte se mantiene el comportamiento
de Supporter Bar. No requiere otra textura ni readback/scissor global en producción.

`oto-visual` depende únicamente de Qt Gui y C++17. No incluye eventos, estado,
Twitch, OBS, widgets, fuentes OBS ni presets. Motion usa únicamente chrono y C++.
Está preparado para recibir
escenas genéricas de otros productos, sin extraer otro repositorio ahora.

## Flujo y propiedad

Scene → validación → clave completa → caché → TextLayoutEngine → imagen RGBA
premultiplicada inmutable → ObsVisualRenderer → textura compartida → dibujo.

El módulo posee un VisualEngine y un ObsVisualRenderer. Se inyectan mediante
referencias débiles en el panel. PluginCore e IPluginServices conservan su contrato
sin Qt: no necesitan conocer el renderer ni el contenido de las escenas.
SupporterHub recibe estos mismos servicios y entrega frames a las fuentes nativas;
ninguna instancia crea motores propios.

Una escena es una lista ordenada de rectángulos, textos e imágenes con caja de recorte.
El origen es la esquina superior izquierda; unidades y tamaños de fuente son
píxeles de salida, no puntos ni píxeles de escritorio. No hay contenedores anidados
ni soporte HDR en esta base. ImageNode admite imágenes inmutables del AssetManager
compartido de Fase 11. El backend añade opacidad/desplazamiento.

## Texto

QTextLayout se encarga del shaping, Unicode, bidi y fallback de fuentes del sistema.
El texto siempre es plano. Ellipsis usa QFontMetricsF; no se cortan bytes UTF-8
manualmente. Los glifos disponibles dependen de las fuentes instaladas.

- Clip: una línea a tamaño solicitado, recortada por su caja.
- Ellipsis: una línea con puntos suspensivos cuando no cabe horizontalmente.
- Wrap: ajuste por palabras o caracteres, respetando saltos explícitos.
- AutoFit: búsqueda acotada del tamaño entero entre mínimo y solicitado, sin
  ampliar; si ni el mínimo cabe, mantiene el mínimo y recorta.

Todos se recortan a su caja, incluida la altura. Hay alineación horizontal
izquierda/centro/derecha y vertical superior/centro/inferior. Métricas informan
tamaño, líneas, extensión, desbordamiento y elisión.

`prepareTextStrip` prepara una línea completa para un viewport horizontal.
Solo expande el ancho de la textura si el nombre no cabe, hasta 2048 px. Usa
QTextLayout una vez por preparación normal y comparte los strips en la misma
caché. Entradas patológicas que superan ese ancho conservan ellipsis como límite
seguro. `samplePingPong` calcula una ida/vuelta suave a 30 px/s de velocidad media,
con 1200 ms de pausa en cada extremo y recorrido mínimo de 800 ms. El backend
reutiliza el shader de opacidad/desplazamiento para muestrear el strip dentro
del ancho visible; ningún píxel invade iconos o tarjetas contiguas.

## Caché y límites

VisualEngine usa LRU de hasta 32 entradas/16 MiB de imágenes y claves serializadas.
La clave incluye orden, lienzo, texto, caja, familia, tamaños, color y alineaciones.
Se compara la clave completa, sin riesgo de colisión de hash. Una escena idéntica
devuelve el mismo FramePtr const y no vuelve a hacer layout/rasterización. Una
escena modificada se recalcula completa; Supporter Bar divide su contenido en escenas
de fondo y valores, y compara solo los campos visibles antes de solicitarlas.
Limpiar la caché también invalida la resolución de fuentes para nuevas
peticiones (necesario si cambia la base de fuentes en una integración futura).

Lienzo máximo 2048×2048, hasta 64 nodos, 4096 unidades UTF-16 por texto y 16384 por
escena; fuentes 8–128 px. Se rechazan dimensiones, enums y colores inválidos antes
de asignar la imagen. Un frame mayor que el presupuesto se devuelve sin guardarlo
y sin expulsar otros frames. Clear/expulsión no invalidan handles retenidos.

ObsVisualRenderer conserva hasta 128 texturas/16 MiB de píxeles, LRU por identidad
del FramePtr. Retiene el frame asociado para impedir que se reutilice su dirección
para otra imagen. Solo sube píxeles cuando no están en caché. El modo inactivo sale
sin crear recursos ni dibujar. La composición premultiplicada usa ONE/INVSRCALPHA.

Estos presupuestos no representan memoria total: excluyen metadatos, cachés internas
de Qt/driver y frames retenidos fuera de la caché CPU, incluidos los de la GPU.
También excluyen los recursos de imagen retenidos por esos frames.
Durante una inserción puede coexistir temporalmente el nuevo recurso con el viejo.
El límite de entradas GPU aumenta para alojar las pequeñas capas independientes;
el presupuesto de píxeles sigue siendo 16 MiB. Superarlo puede provocar expulsiones
y nuevas subidas: no se promete capacidad ilimitada de fuentes residentes.

## Animación básica

`motion.hpp` calcula una entrada smoothstep con reloj monotónico: opacidad 0→1
y, opcionalmente, desplazamiento vertical 8→0 px. No posee timers ni hilos.
DrawParams transmite posición, opacidad y desplazamiento al backend genérico.
Un único shader compartido, compilado bajo demanda, desplaza la muestra UV,
recorta al rectángulo de cada textura y multiplica RGBA premultiplicado por opacidad.
Opacidad cero no dibuja/sube; finalizada la entrada se utiliza el efecto base de OBS.
El estado de matriz, blending y framebuffer sRGB se restaura tras cada dibujo.

## Animación de cambio (0.11.3)

Motion::StyleChange usa sampleChange, una secuencia finita normalizada: destello
de entrada, salida del nombre anterior, entrada del nuevo con barrido y chispas,
glow residual y fade a reposo exacto. Reloj monotónico, sin timers/hilos propios.
Solo cambios de valores visibles y geometría compatible la disparan; carga de
escena, mostrar fuente, cambio de preset o de preferencias no reproducen eventos.
Cada capa retiene como máximo una textura saliente; un cambio rápido la sustituye
y publica el último valor, sin cola gráfica creciente. Se deja de dibujar la
textura saliente al terminar; su handle queda acotado hasta el siguiente cambio.

Un único programa GPU procedural combina ocho barridos (arco, digital, CRT,
órbita, niebla, holograma, reflejo diagonal y skyline) y cinco partículas
(chispa, píxel, corazón, pintura y niebla), con hasta doce partículas deterministas.
No usa texturas de partículas ni rasteriza por
frame. Su quad queda limitado al campo (margen horizontal <= mitad del gap) y
al alto de la fuente (margen vertical <=24 px). El valor nuevo requiere solo su preparación habitual.
En reposo sin nombres desbordados no se muestrea el reloj ni se dibuja el efecto.
Solo el ping-pong de un nombre visible que no cabe requiere muestrear el reloj
después de terminar la alerta; no vuelve a ejecutar layout/rasterización/subida.
DrawSupporterFrame es
compartido por la fuente y la comprobación GPU manual. Esta última exporta
fixtures de cinco estados/41 frames por cada uno de los diez presets (410 frames);
comprueba que las otras tarjetas son idénticas, el final vuelve al estado exacto
y no hay rasterizaciones/subidas adicionales con las texturas ya calentadas.
El readback y los PNG son exclusivos de
esa comprobación, nunca de video_render. No constituyen una prueba de directo.

## Hilos y cierre

Prepare, caché CPU y TextLayoutEngine: hilo GUI de QGuiApplication. Se rechaza
prepare desde otros hilos. Los frames publicados solo se leen. El renderer no
ejecuta layout ni usa QPainter dentro de draw.

Draw requiere el contexto gráfico de OBS adquirido y un destino/proyección SDR
configurados por el llamador. Todas las operaciones de la caché GPU se serializan
mediante ese contexto; stats/clear lo adquieren internamente. El backend restaura
blending y framebuffer sRGB. No se debe invocar dentro de otro effect_loop activo.
Supporter Bar publica handles atómicamente y llama draw desde video_render de OBS.

Durante FRONTEND_EVENT_EXIT se retira el panel y sus diálogos, se liberan las
texturas dentro del contexto gráfico y se vacía la caché CPU antes de detener Core.
SupporterHub detiene su trabajo y suelta frames bajo contexto gráfico antes de
limpiar las cachés. Los callbacks tardíos salen sin dibujar. No retener
consumidores/handles después de descargar la DLL. El módulo revierte inicios fallidos.

## Comprobación de desarrollo

El botón del panel crea una escena genérica fija con texto, rectángulos y una
imagen inmutable de 2×2 con píxeles transparentes/semitransparentes. La dibuja dos veces en un destino
privado mediante el backend compartido, lee los píxeles de GPU y los compara con
la imagen CPU (tolerancia máxima 1/255). Comprueba identidad CPU, reutilización GPU
y el camino inactivo. También comprueba opacidades 0/0.5/1, desplazamiento 4 px
y cero subidas adicionales. El contexto gráfico serializa todo el muestreo para
que otras fuentes no alteren sus contadores durante la prueba.
Al cerrar el diálogo permanecen las cachés para reutilización;
al cerrar OBS se liberan. La comprobación manual no crea timers ni hilos propios.

La lectura GPU→CPU es exclusiva de esta comprobación manual y no debe usarse en
video_render de las fuentes. OBS debe componer una fuente visible en cada
frame: «renderizar solo cuando cambia» significa evitar repetir layout, rasterización
y subidas; no omitir la composición exigida por OBS. Elementos inactivos no preparan
ni dibujan. Este checkpoint no mide rendimiento de varias fuentes ni hace profiling.

API consultada: [QTextLayout](https://doc.qt.io/qt-6/qtextlayout.html),
[OBS Graphics](https://docs.obsproject.com/reference-libobs-graphics-graphics).
Implementación contrastada con las cabeceras y fuentes del SDK fijado OBS 31.1.1.
