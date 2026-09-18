# UI Preview aislada

Esta carpeta contiene una reconstrucción **puramente gráfica** de la interfaz central. No ejecuta ni importa código Java del cliente y no tiene acceso a Minecraft, JNI, módulos, cuentas, perfiles reales o servicios online.

## Ejecutar en Windows

- Haz doble clic en `abrir-preview.bat`, o
- abre `index.html` directamente en Chrome, Edge o Firefox.

No hay que instalar Node, Java, Gradle ni ninguna dependencia.

## Qué se puede probar

- Navegación entre Modules, Friends y Profiles.
- Categorías, búsqueda y tarjetas de muestra.
- Sidecars, overlays, tema y animaciones.
- Switches y sliders visuales.
- Amigos, macros y perfiles ficticios guardados únicamente en memoria.

Al recargar la página se descartan todos los cambios. Los módulos visuales empiezan siempre en `OFF`.

## Aislamiento

- `app.js` no usa `fetch`, `XMLHttpRequest`, `WebSocket`, `localStorage` ni imports.
- `index.html` aplica una Content Security Policy con `connect-src 'none'`.
- No existe referencia a `src/main`, `gg.vape`, `NativeBridge` ni al classpath Gradle.
- Ningún control puede activar funciones del cliente; sólo cambia el DOM en memoria.

## Archivos

```text
ui-preview/
├── index.html
├── styles.css
├── app.js
├── abrir-preview.bat
└── README.md
```
