# Validación real pendiente

Las comprobaciones automatizadas no sustituyen una sesión prolongada con el programa DJ y una cuenta Twitch autorizada. Una primera prueba real con VirtualDJ ya está completada; ver virtualdj-real-validation.md. No se inicia una emisión ni se autoriza una cuenta durante las pruebas aisladas.

## Now Playing con el DJ

Con OBS cerrado, instala las compilaciones compatibles de Playing y Ticker. Usa una sola fuente nativa Playing activa y selecciona OBS Playing automático en la banda superior del ticker. Elige en Playing el proveedor DJ que utilices y desactiva sus datos demo.

Reproduce dos canciones distintas. Comprueba artista y título, ancho fijo y fundido al cambiar, sin reiniciar las noticias inferiores. Comprueba pausa, parada y recuperación según las capacidades del proveedor DJ. Anota aplicación y versión: no todos los lectores proporcionan los mismos estados de pausa. Comprueba también una escena sin Playing activo y su recuperación al volver.

## Twitch

Conecta tu cuenta desde las propiedades, utilizando el Twitch Core compartido. Revisa las categorías que quieras mostrar y su intercalado con los mensajes personalizados. Los contadores diarios representan eventos registrados por Core; no prometen reconstruir eventos que el servicio no haya recibido.

Verifica con eventos reales que ocurran en tu canal: follow, sub, cheer, regalo y raid con espectadores. No confundas datos de ejemplo o históricos restaurados con eventos nuevos. Revisa que regalos y resuscripciones no dupliquen los totales, y que el anonimato se conserve. No hace falta publicar mensajes de chat para esta comprobación.

Comprueba desconexión y reconexión; la música y las noticias manuales deben seguir funcionando. Guarda y reinicia OBS para comprobar que tus opciones se conservan.

## Sesión prolongada

Usa tu resolución, frecuencia y escenas habituales. Observa cambios de canción, noticias, reloj y meteorología durante una sesión representativa. La meteorología gratuita utiliza Open-Meteo para uso no comercial, con crédito público según weather.md.

Registra duración, versiones, comportamiento observado y estadísticas de OBS. Una variación de memoria o GPU no identifica por sí sola al plugin responsable. Conserva los registros localmente; no incluyas claves, tokens ni credenciales en informes compartidos.

## Evidencia automatizada disponible — 2026-10-25

El módulo actual del ticker pasó 180 comprobaciones funcionales, 106 gráficas y 17 del instalador. Meteorología: 25 comprobaciones incluyendo consulta HTTPS real y 7 de integración nativa.

La prueba aislada de tres minutos con OBS portable y ambas DLL reales pasó 45 comprobaciones: 34 cambios de canción demo recorriendo los 16 estilos, cuatro movimientos inferiores, meteorología real, desactivación, reactivación y retirada. Se renderizaron más de 5.300 fotogramas. OBS terminó con código 0 y declaró cero fugas en su contador interno; esto no certifica toda la memoria de bibliotecas externas ni una emisión prolongada.

Evidencia: validation/portable-soak.log, portable-soak-obs.log y portable-soak.json, que registra las huellas de las DLL. El módulo de prueba no forma parte del instalador y se retira del portable al cerrar. El OBS habitual y el proyecto de Playing no se modifican.

Para repetir como desarrollador: compila con scripts/build.py y ejecuta scripts/test-portable-soak.ps1. Requiere el entorno OBS portable ya preparado en build/obs-qa y la DLL de desarrollo de Playing; admite -PlayingDll y -Seconds (90–1800). Esta prueba usa datos demo de música y realiza una consulta meteorológica de Madrid.

## Actualización: primera sesión DJ real

Completada la conexión real con VirtualDJ 8.5.9295.0, dos canciones locales, pausa y recuperación: 7 comprobaciones y 0 fallos. Sigue pendiente la emisión prolongada y los escenarios de mezcla descritos arriba. Evidencia y límites en virtualdj-real-validation.md.
