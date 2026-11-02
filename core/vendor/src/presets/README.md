# Sistema de presets — Fase 11

Estado actual 0.12.65: Fase 12 cerrada; diez definiciones incluyen diseños de
Supporter Bar, Event List, Animated Alerts, Shoutout Card, Hype Train y Chat.
Recursos bajo demanda, un único renderer y Registry inmutable. Las descripciones
de versiones siguientes conservan el historial anterior.

Animated Alerts (0.12.25): `alertStyle` opcional para Neon DJ/Cyberpunk/Retro CRT/Space/Horror/Love/Graffiti/Minimal/Urban City.
`energyPattern` 0–8 selecciona plasma, paneles/circuitos, TV/fósforo, maquinaria orbital, armadura fracturada, cristal Love pintura Graffiti líneas Minimal o maquinaria Urban City del programa compartido;
`headlineColor`/`captionColor` opcionales #AARRGGBB (sin ellos: acento/blanco).
Seis colores/títulos/captions, seis `iconAnimations` PNG de hojas 1024×768.
Perfil sin clips necesita `atlas` y seis `iconCrops`; clips-only puede omitirlos.
Cada variante debe tener recurso válido. Registro no carga imágenes. Fuente
carga solo clip activo, o atlas si no hay clip. No motor por preset.

Fase 12 añade `eventListStyle` opcional a cada Definition, parseado/validado con
las mismas reglas de Style. 0.12.2 lo incluye en Minimal y Neon DJ:
colores, familia/tamaños, tarjeta, glow y perfil de cambio como datos. La fuente
Event List consume esa definición del Registry existente, sin marco horizontal
de Supporter Bar. Neon DJ usa su propio atlas transparente de tarjetas. Los otros estilos de barra no se presentan
como diseños de lista hasta implementar sus referencias en nuevos checkpoints.

Un único Registry inmutable, cargado al iniciar el módulo, contiene metadatos y
parámetros. No crea renderers, conexiones, colas, fuentes de OBS ni timers.
El módulo lo inyecta junto al AssetManager compartido en SupporterHub.
Cada instancia guarda únicamente el ID elegido en sus ajustes OBS y una copia
de los parámetros resueltos. Instancias antiguas usan Minimal automáticamente.
Un ID ausente se conserva y muestra Minimal; sus propiedades indican el fallback.

`data/presets/registry.json` usa schemaVersion 1, hasta 64 definiciones y 64 KiB.
Minimal es obligatorio, los IDs son únicos y las entradas se validan antes de
publicarlas. Tras validar la infraestructura en 0.11.0, 0.11.1 incluye Minimal,
Neon DJ, Cyberpunk, Retro CRT, Space, Horror, Love, Graffiti, Urban City y
Futuristic Hologram. 0.11.2 sustituye los fondos genéricos rechazados por marcos
basados en referencia, tarjetas y dos atlas de iconos del mismo motor.

Cada definición contiene `id`, `name` y `style`. Parámetros disponibles:

- `family`: familia del sistema; Qt resuelve fallback al preparar texto activo.
- `background`, `title`, `value`: colores `#AARRGGBB`.
- `titleSize` (8–22 px), `valueSize` (12–40 px).
- `padding` (6–24 px), `gap` (0–12 px).
- `backgroundImage`: referencia opcional PNG relativa a `data/presets/assets`.
- `cards`: composición con tarjetas, iconos y texto en negrita; opcional.
- `iconImage`: atlas PNG con seis columnas, una por identidad de campo.
- `backgroundCrop`, `iconCrop`: `[x,y,w,h]` en milésimas, dentro de la imagen.
- `backgroundEdge` (0–400): anchura de extremo en milésimas del PNG; el nodo
  estira solo el centro y limita extremos al ancho disponible.
- `cardBackground`, `cardEdge`: colores; `cardEdges`/`titleColors`: seis colores
  opcionales en orden Follow/Sub/Cheer/Gift/Bits/Subs.
- `cardRadius` (0–24 px), `cardGlow` (0–8 px): radio y halo estático de la tarjeta,
  preparados una vez al construir el layout y incluidos en la clave de caché.
- `cardInset` (10–120): margen horizontal como porcentaje del alto; limitado
  al 20% del ancho. `cardTop` (5–40), `cardHeight` (30–80): porcentajes del alto;
  la suma no puede superar 95.
- `changeGlow` (0–100), `changeParticles` (0–12), `changeSweep` (booleano),
  `changeColors` (seis colores #AARRGGBB): perfil de animación compartido.
  Por defecto 70/8/true y colores de títulos/borde. Minimal usa 85/4/true;
  Neon DJ usa 100/12/true. No hay un renderer distinto por estilo.
- `changePattern` (0–7): arco, escaneo digital, CRT, órbita, niebla, holograma,
  reflejo diagonal o skyline. `changeParticleShape` (0–5): chispa, píxel,
  corazón, gota de pintura, niebla o cristal facetado. Las combinaciones son datos.

Para Event List, `backgroundBands` (1–5) selecciona las bandas Follow/Sub/Bits/
Gift/Raid del atlas; `rowCrops` permite cinco recortes explícitos `[x,y,w,h]` en
milésimas, validados dentro de la imagen. `nameStart` (0–400) reserva espacio para
icono y etiqueta. `badgeBox` usa milésimas de la tarjeta para el texto dinámico de
la etiqueta. Todos entran en la igualdad del estilo. Neon DJ reserva 330/1000
del ancho; reutiliza las cinco regiones del mismo handle de imagen retenido.
El AssetManager se inyecta también en EventListHub; solo la lista visible con
filas carga el atlas. El frame conserva su handle incluso tras expulsión LRU.
Ver [recurso y prompt](../../data/presets/EVENT-LIST-ARTWORK.md).

Solo se lee el PNG al preparar una instancia visible con campos activos. Registrar
un preset no comprueba ni decodifica sus imágenes. No se admiten URLs, rutas
absolutas, navegación fuera del paquete, vídeos ni formatos animados.
AssetManager verifica el destino canónico, limita el archivo a 4 MiB, la entrada
a 4096×1024/4 megapíxeles y la residencia a 1024×1024 conservando proporción.
La caché LRU tiene hasta 8 imágenes/8 MiB de píxeles decodificados. Estas cifras
no incluyen imágenes retenidas por frames, metadatos ni buffers temporales.
Expulsar/clear no invalida handles const aún retenidos. Cierre de OBS libera la
caché después de soltar frames y texturas. No hay sondeo ni recarga automática.

El renderer recibe un ImageNode genérico con un handle inmutable; no conoce
presets ni Twitch. Su clave incluye identidad del recurso, caja y cacheKey de
QImage, recorte y extremo; el frame retiene el recurso para impedir reutilización
de esa identidad. CardNode dibuja tarjetas con gradiente/borde/radio genéricos.
La clave de texto también incluye la negrita. Una fuente activa reutiliza sus
handles retenidos antes de consultar el LRU, evitando recargas tras expulsión.
La composición CPU solo sucede al cambiar. `video_render` compone texturas
preparadas mediante el mismo backend OBS, sin disco ni layout por frame.

Los diez estilos, incluido Minimal, usan los marcos e iconos descritos en
[autoría y prompts](../../data/presets/ARTWORK.md). Son trece PNG de 2172×724,
reducidos a 1024×341 al cargar; 12.111.497 bytes en disco. Neon DJ tiene un atlas
propio afinado con la lámina de animación; ocho estilos comparten el multicolor
y Hologram usa el cian. No se promete residencia
ilimitada ni se carga el catálogo completo al iniciar. Minimal conserva los
ajustes de escena y datos, pero cambia de apariencia por la corrección pedida.
Los callers sin estilo conservan el layout antiguo para compatibilidad.

Los fondos no contienen texto. Las familias son del sistema con fallback Qt. TEST/PREVIEW,
LONG TEXT y las animaciones comunes recorren la infraestructura existente.
Los fixtures PNG artificiales de límites/corrupción permanecen temporales.
Ver [galería](../../docs/validation/phase-11/factory-presets-0.11.2.png) y
[validación](../../docs/validation/phase-11-fidelity.md).
La [validación de 0.11.3](../../docs/validation/phase-11-animation.md) incluye
41 frames por preset con las otras tarjetas idénticas, reposo final exacto y
cero rasterizaciones/subidas adicionales durante cada secuencia calentada.

Minimal: contornos finos blanco/plata y azul hielo; arcos abiertos, reflejos
viajeros y seis iconos articulados. Sin gran panel de fondo. Retrato centrado,
placa automática, FONT/herencias, tres tiempos y salida inversa continua.

Urban City: marco industrial compacto de bronce con esquinas desplegables,
faroles ámbar, rejillas/reflejos móviles y seis iconos mecánicos articulados.
1000² transparente, foto centrada/esperada, tres tiempos y salida inversa.

0.12.27 añade Futuristic Hologram Animated Alerts: patrón 9 del mismo renderer,
aros/escaneo/fragmentos abiertos GPU y seis clips de facetas translúcidas.
Diez estilos de alertas; foto esperada/centrada y opciones comunes conservadas.
