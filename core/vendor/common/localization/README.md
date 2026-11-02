# Idiomas compartidos de NEXUS

Componente reutilizable para todos los plugins NEXUS, independiente de OBS,
Twitch y sus fuentes. Cinco idiomas: en-US, es-ES, it-IT, fr-FR y de-DE.
Las variantes regionales comparten el idioma: es-MX → español, en-GB → inglés.
Todo idioma de sistema no admitido usa en-US. Un ajuste manual inválido vuelve
a Automático. Las traducciones que falten usan inglés.

Qt/C++: `Nexus::Localization`, `languageFor`, `selection`, `systemLanguage` y
`Catalog`. Cada plugin proporciona su directorio de catálogos; OTO los incrusta
como recursos mediante su pequeño adaptador `src/localization.cpp`.
El catálogo es inmutable, cargado una vez y seguro para consultas concurrentes.
No requiere lectura de archivos, trabajo de red ni temporizadores por fotograma.

.NET: `NexusLocalization.cs`, la misma política y los mismos INI UTF-8
incrustados en instalador/desinstalador. `CurrentUICulture` hereda el idioma de
interfaz de Windows. Los mensajes usan placeholders `%1`, `%2`, etc., iguales
en ambas plataformas. Los INI contienen valores de cadena entre comillas con
escapes JSON; se verifican ambos lectores.

Para integrar otro plugin: incluir este componente común, aportar sus propios
catálogos con las cinco variantes y usar claves para los textos visibles.
No se comparten credenciales ni configuraciones entre productos. Un selector
por fuente es una preferencia de esa fuente; no cambia el idioma de otras.
Los nombres de producto, estilos, mensajes de usuarios, categorías de Twitch
y el texto incorporado a las imágenes originales no se traducen automáticamente.
