# Activar Now Playing automático

1. Cierra OBS antes de actualizar sus plugins. Instala Ticker y la nueva compilación de Playing NEXUS que incluya el puente de Now Playing.
2. Abre OBS y añade o utiliza una fuente nativa de Playing NEXUS. Mantén una única fuente Playing configurada. Eliminar esa fuente interrumpe la recepción del ticker; esta dependencia es intencionada. Con la actualización de lectura independiente, Playing puede estar oculto: el ticker mantiene la recepción mientras lo consulta. Es necesario actualizar ambos plugins.
3. En las propiedades de News Ticker, selecciona **Contenido de la banda superior → OBS Playing — automático**. No necesitas elegir un archivo JSON.
4. Ajusta la anchura fija del texto Now Playing. La canción permanece fija y los cambios usan el fundido existente. Las noticias inferiores conservan su movimiento y contenido.
5. Reproduce una canción desde el proveedor configurado en Playing. Comprueba artista y título, cambio de canción y pausa. Si no hay reproducción válida, el tema desaparece; el respaldo manual solo se muestra si lo has activado.

## Si no llega la canción

- **Necesita la actualización del puente:** la DLL de Playing cargada es anterior. Instala el nuevo paquete de Playing y reinicia OBS.
- **Varias fuentes OBS Playing:** deja una sola fuente nativa para esta conexión automática. Esta fase no incluye selector de fuente.
- **Esperando datos:** comprueba que Playing esté activo y reciba una canción. El modo automático utiliza su publicación existente; no añade un detector DJ.
- **Datos caducados o no válidos:** revisa el estado de Playing. La caducidad es configurable; el respaldo manual comienza desactivado.

También puedes seguir usando Texto manual o el archivo JSON local. Las fuentes antiguas no cambian de modo automáticamente al instalar esta versión.

## Comprobado y pendiente

En OBS portable, las dos DLL reales superaron siete comprobaciones con canciones ficticias: creación, lectura inicial, cambio conservando noticias inferiores, desactivación, reactivación y retirada. Falta validar la cadena completa con el programa DJ real y una sesión de emisión prolongada. Twitch real y meteorología automática son tareas independientes.

## Movimiento ping-pong

En **Movimiento del texto Now Playing**, selecciona **Ping-pong (solo si el título no cabe)**. El texto se desplaza hasta mostrar el final y regresa al principio dentro de su anchura fija. Los títulos cortos permanecen quietos. Puedes ajustar velocidad de 10–200 px/s (40 inicialmente) y pausa de 0–5000 ms en cada extremo (1000 inicialmente).

Funciona con texto manual, archivo JSON y Playing automático, en estilos Normal y Compact. Es independiente del movimiento de noticias. Se pausa al ocultar la fuente. Al cambiar de canción, se conserva el fundido y el nuevo título comienza desde el principio. La opción Fijo sigue siendo el valor predeterminado.


## Lectura con Playing oculto — 2026-10-25

Probado con ambas DLL reales en OBS portable durante 90 segundos: 29 comprobaciones sin fallos, incluyendo canción conservada al desactivar Playing, cambio nuevo recibido con Playing inactivo y retirada sin título residual. Canciones de demostración; esta prueba no equivale a una nueva sesión DJ real ni a una emisión. El ticker solicita lectura mediante nexus_now_playing_demand_v1; la demanda caduca tres segundos después de la última consulta. No activa la fuente en OBS ni habilita sus anuncios de chat.
