# Prueba real VirtualDJ → Playing → News Ticker

Fecha: 2026-10-25. VirtualDJ 8.5.9295.0, Playing nativo de desarrollo y Ticker 0.7.1 en OBS portable 32.2.2. Modo VirtualDJ Automatic, datos demo desactivados, sin archivo JSON intermedio ni historial como entrada.

Se abrió VirtualDJ y se cargaron dos canciones reales de la biblioteca local: Counting Crows — Mr. Jones [Intro] - Clean y Supertramp — Breakfast In America [Intro] - Clean. La salida maestra de VirtualDJ se bajó a cero antes de reproducir; no se inició ninguna emisión.

Comprobado: llegada de artista y título de ambos temas al texto nativo del ticker; retirada al pausar; recuperación al reanudar y al quedar reproduciendo el otro deck; noticias inferiores conservadas. La pausa sin otro deck reproduciendo se publica como waiting y el ticker se vacía. No se afirma que el proveedor distinga semánticamente pausa y parada.

El observador del OBS portable registró 7 comprobaciones, 0 fallos, 183.328 segundos y 5.501 fotogramas. Hubo tres publicaciones de reproducción: dos temas distintos y una recuperación del primero. OBS cerró y declaró cero fugas en su contador interno. Esto no certifica toda la memoria externa, una emisión prolongada ni que el algoritmo seleccione siempre la mezcla audible durante crossfader/preescucha. El master a cero permitió probar detección y estados, no una salida acústica.

Al terminar se descargaron ambos temas de los decks, con el pitch de nuevo a cero, se restauró el volumen maestro y se cerró VirtualDJ. El módulo de prueba se retiró del portable. No se cambió el OBS habitual ni el instalador/proyecto de Playing.

Evidencia local: validation/virtualdj-real-session.log, virtualdj-real-obs.log y virtualdj-real-report.json. El JSON conserva las huellas de ambas DLL probadas. El observador usa el procedimiento público de Playing y el texto GDI+ del ticker, sin modificar el estado del lector DJ. La interacción con VirtualDJ se hizo por sus controles visibles.

Quedan Twitch con eventos reales, validación detallada de mezclas/crossfader/preescucha, otros proveedores y sesiones prolongadas. Esta prueba sustituye el pendiente general de una primera conexión real con un programa DJ, pero no esos escenarios adicionales.
