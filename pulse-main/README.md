# Pulse Network Optimizer

**Pulse** es una potente aplicación de optimización de red nativa para Windows desarrollada en C++ que ayuda a mejorar la latencia y el rendimiento en juegos reduciendo la pérdida de paquetes y mejorando la respuesta de red.

Esta versión cuenta con una interfaz gráfica (GUI) ultra-premium diseñada en **ImGui** con renderizado en **DirectX 11** y transparencias nativas por composición (**DirectComposition**).

---

## Características Principales

* **Optimización de Red Activa**:
  * **Smart Packets**: Ajustes automáticos MTU y optimización de cola de paquetes.
  * **Hit Registration**: Tweaks de latencia de red TCP/IP para registrar disparos de forma inmediata.
  * **Knockback Reduction**: Ajustes finos de velocidad y latencia para reducir el retardo en juegos de combate.
  * **Selector de Conexión**: Permite elegir de forma dinámica entre conexión **Ethernet** y **Wi-Fi**.
* **Interfaz de Usuario Premium (DirectX 11)**:
  * **Ventana flotante translúcida**: El fondo fuera de la tarjeta de la aplicación es 100% transparente y cuenta con **clics de paso directo (click-through)** para interactuar con lo que hay detrás en tu escritorio.
  * **Tema Oscuro Neutro**: Diseño en negro puro y grises oscuros ("oscuro negro black") con acentos en rojo neón.
  * **Controles Personalizados**: Sliders modernos con barra de llenado gruesa y tiradores con anillo de brillo concéntrico, además de interruptores deslizantes estilo iOS/pill.
  * **Contraste Dinámico Inteligente**: Si configuras un color de acento muy claro (como blanco) en los ajustes, el texto del botón se adaptará automáticamente a negro para garantizar su lectura.
  * **Arrastre Manual**: Puedes mover el recuadro haciendo clic y arrastrando desde cualquier parte no interactiva del panel.

---

## Estructura del Proyecto

El repositorio está unificado y ordenado de la siguiente manera:
```
PulseCpp/
├── Pulse.sln             # Solución de Visual Studio 2022
├── Pulse.vcxproj         # Archivo del proyecto de la GUI
├── framework/            # Código fuente de la Interfaz Gráfica (ImGui)
│   ├── app/              # Inicialización de la GUI y renderizado
│   ├── core/             # Lógica de interfaz con el optimizador y ventana principal
│   ├── pages/            # Páginas de la interfaz (Main page, settings)
│   └── theme/            # Paleta de colores, diseño y escala de la UI
├── backend/              # Motor del optimizador de red en C++ puro
│   ├── include/          # Cabeceras (.h) de red, registro y configuración de juegos
│   └── src/              # Código fuente (.cpp) de optimización de red
├── thirdparty/           # Dependencias de terceros (ImGui, FreeType, stb, shaders)
└── bin/                  # Salidas compiladas (Pulse.exe se genera aquí)
```

---

## Compilación y Ejecución

### Requisitos
* **Windows 10 u 11** de 64 bits.
* **Visual Studio 2022** con el compilador de C++ instalado.
* **Privilegios de Administrador** (requeridos para modificar la configuración de red TCP/IP y claves de registro).

### Compilar desde la terminal (Developer Command Prompt / PowerShell)
Para realizar una compilación limpia de la solución a través de MSBuild:

```powershell
# Compilación en modo Release para x64
msbuild Pulse.vcxproj -p:Configuration=Release -p:Platform=x64
```

El ejecutable compilado estará listo para usar en la ruta:
`bin/Pulse.exe`

### Ejecución
El programa tiene incrustado un manifiesto UAC que solicita automáticamente elevación de administrador. Solo debes hacer doble clic en `bin/Pulse.exe` y aceptar el aviso de control de cuentas de usuario (UAC) de Windows.
