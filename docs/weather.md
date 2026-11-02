# Meteorología automática — Open-Meteo gratuito

En las propiedades del ticker, elige **Meteorología → Open-Meteo gratuito (uso no comercial)**. Las fuentes nuevas incluyen las 24 capitales seleccionadas por el usuario, en este orden:

Madrid, Londres, París, Roma, Tokio, Nueva Delhi, Washington D. C., Abu Dabi, Seúl, Pekín, Buenos Aires, Bogotá, Santiago de Chile, Brasilia, Ciudad de México, Ottawa, Berlín, Ámsterdam, Bruselas, Varsovia, Lisboa, Viena, Atenas, Estocolmo.

Las entradas muestran el nombre y el código de país. Para estas capitales se utilizan coordenadas verificadas por país con Open-Meteo/GeoNames; no se selecciona una ciudad homónima en cada actualización. El catálogo y sus identificadores están en data/weather-capitals.json. Los nombres mostrados conservan la selección en español.

La **lista ordenable de ciudades** permite añadir, editar, quitar y reordenar entradas. Para otras ciudades, usa nombre y país completos, por ejemplo `London, United Kingdom`; se utiliza el primer resultado del buscador. Comprueba el país mostrado. El motor admite hasta 64 ciudades distintas compartidas entre las fuentes; las 24 predeterminadas caben completas.

Las listas personalizadas anteriores se conservan al actualizar. Para sustituirlas por esta selección, pulsa **Cargar las 24 capitales predeterminadas**. Una lista vaciada explícitamente no se rellena de nuevo por sí sola. La migración conserva el orden y el modo fijo/rotativo de los cinco campos anteriores.

Se muestran condición, temperatura redondeada, ciudad y país. Los datos actuales proceden del modelo meteorológico del proveedor, no de un sensor propio. El icono responde al código meteorológico y al día/noche. La representación adapta algunos códigos a símbolos generales.

Las consultas HTTPS y el análisis JSON se ejecutan en un trabajador compartido. Las fuentes de la misma ciudad reutilizan la consulta; hasta 64 ciudades simultáneas por módulo. Se espera brevemente al editar y se actualiza cada diez minutos, con reintento cada dos minutos tras error. No hay consultas sin seleccionar el modo automático y una ciudad válida. El estilo Basic no muestra ni solicita meteorología. La ciudad y la IP de conexión son visibles para el proveedor; no se solicita geolocalización automática.

La temperatura se oculta al fallar la consulta, recibir datos inválidos o superar treinta minutos desde la fecha del proveedor. También se rechazan fechas adelantadas más de cinco minutos. No se sustituye un fallo por una temperatura de ejemplo. El modo Manual conserva el texto guardado y sigue siendo el predeterminado de las fuentes existentes.

## Rotación World Weather

La rotación está preseleccionada en fuentes nuevas y utiliza las 24 capitales al activar el modo meteorológico automático. El modo Manual sigue siendo el predeterminado: crear una fuente no inicia consultas. Ajusta **Tiempo por ciudad**, entre 5 y 300 segundos (15 por defecto). Desactivar la rotación devuelve inmediatamente la primera ciudad si está disponible, sin perder el resto de la lista. Las fuentes anteriores conservan su elección de rotación.

La secuencia usa solo ciudades con datos válidos y recientes. Omite campos vacíos, consultas idénticas duplicadas y ciudades con errores o datos caducados. Con una sola ciudad válida permanece en ella; sin ninguna se oculta la temperatura. Las demás ciudades se incorporan cuando llegan sus datos. Se conserva el orden de la lista para el ciclo normal.

El avance por tiempo se pausa al ocultar la fuente. Cambiar de ciudad no reinicia Now Playing, su fundido ni las noticias inferiores. Editar la lista reinicia la selección meteorológica conservando las consultas que puedan reutilizarse. Cambiar únicamente el intervalo conserva la posición actual.

La rotación cambia la presentación, no la frecuencia de las consultas: los datos de cada ciudad siguen actualizándose en segundo plano cada diez minutos. Se mantienen el trabajador y el límite global de 64 consultas de ciudades distintas compartidas por módulo. No crea una conexión nueva en cada cambio de ciudad.

## Condiciones y atribución

La modalidad gratuita de la API es para uso no comercial. Una emisión monetizada requiere comprobar y cumplir las condiciones comerciales del proveedor; este incremento no incluye claves comerciales. Condiciones oficiales: https://open-meteo.com/en/pricing

Datos por Open-Meteo, licencia CC BY 4.0: https://open-meteo.com/ y https://creativecommons.org/licenses/by/4.0/ . El ticker redondea la temperatura y adapta los códigos a símbolos. Incluye el nombre y enlace de Open-Meteo en la descripción pública de tu emisión.

Referencias técnicas: https://open-meteo.com/en/docs y https://open-meteo.com/en/docs/geocoding-api . No se incorpora código del servidor Open-Meteo, solo se utiliza su API.

## Evidencia — 2026-10-25

- 25 comprobaciones de parser, validación y trabajador incluyendo HTTPS real para Madrid: weather-live-test.log.
- 7 comprobaciones con libobs, D3D11 y la DLL real del ticker: weather-native-live-test.log. Recepción nativa, noticias y música conservadas, guardado, vuelta a Manual y retirada con ciudad vacía.
- Captura GPU: weather-live-preview-20261025.png. La temperatura procede de la consulta de esa prueba y no representa una temperatura permanente.

Falta una sesión prolongada en emisión y validar visualmente el modo automático en todos los anchos elegidos por el usuario. Las referencias manuales de estilos se conservan para las comparaciones gráficas.

## Validación de rotación — 2026-10-25

Pruebas deterministas del temporizador, salto de ciudades no disponibles, vuelta al inicio, fuente oculta y una única ciudad saludable. La prueba nativa con HTTP real alterna Madrid (ES) y London (GB), confirma el guardado de lista e intervalo, restaura la ciudad principal al desactivar y verifica que música y noticias no cambian. Registros: world-weather-unit-test.log y world-weather-native-test.log.

Para repetir la prueba externa, usa NEXUS_TICKER_WEATHER_QA=1 con integration-test.exe; scripts/test.ps1 ejecuta las pruebas locales sin depender de Internet.

## Selección de 24 capitales — 2026-10-25

El catálogo se verificó mediante país y tipo de capital en el servicio de geocodificación. La prueba externa obtiene datos actuales de las 24 capitales y comprueba que otra fuente reutiliza las mismas sesiones, sin duplicar sus consultas. Las pruebas locales cubren el orden completo y la vuelta al inicio, persistencia, migración, restauración de capitales y lista explícitamente vacía.

Para repetir las consultas reales: weather-test.exe --capitals-live. Registros: capitals-live-test.log, capitals-native-test.log y capitals-full-regression.log.

## Distribución y transiciones

La opción de alineación al borde derecho coloca la meteorología a la izquierda y el reloj en el extremo derecho. Cada campo conserva su espacio; el contenido queda centrado. El ancho reservado de meteorología es configurable. Los cambios de ciudad usan un fundido de salida y entrada de 250 ms por tramo.

Open-Meteo deja de aparecer dentro del ticker. La información de licencia permanece en las propiedades; para uso público debe proporcionarse la atribución y el enlace correspondientes en un lugar público, por ejemplo la descripción de la emisión: https://open-meteo.com/en/licence.

Los estados de conexión de las propiedades se actualizan al abrirlas o pulsar Actualizar estado. No se reconstruye automáticamente la ventana mientras se edita la lista de ciudades, evitando invalidar sus controles durante un diálogo modal.


Los nombres de capitales en la emisión se muestran ahora en inglés (London, Rome, Tokyo...), conservando las mismas coordenadas, selección y orden guardados. La lista de configuración conserva los nombres originales para no alterar las preferencias existentes.
