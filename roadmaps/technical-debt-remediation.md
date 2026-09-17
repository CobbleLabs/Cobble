# Roadmap de remediación de deuda técnica

Última actualización: 2026-09-07. Este roadmap prioriza reducción de riesgo antes de refactors grandes. Los IDs enlazan con [`../audit/findings.md`](../audit/findings.md).

## Principios

1. **Primero observabilidad y tests.** No extraer monolitos críticos a ciegas.
2. **Cambios pequeños y reversibles.** Mantener comportamiento antes de rediseñar.
3. **Una causa por iniciativa.** Evitar PRs que mezclen parser, JNI y UI.
4. **No cerrar por intención.** Solo cerrar con criterio y evidencia.
5. **No optimizar seguridad a costa de lifecycle.** Una DLL descargable debe controlar todas sus tareas y hooks.
6. **No crear abstracciones genéricas prematuras.** Extraer dominios reales: tiempo, ángulos, inventario, rotación, packet policy.

## Fase 0 — Baseline automatizado

**Objetivo:** poder cambiar sin depender exclusivamente de prueba manual.  
**Hallazgos:** AUD-001, AUD-014.  
**Bloquea:** fases 2–4.

### Entregables

- Workflow Windows que compile `Cobble.vcxproj` y `loader/test37.vcxproj` en modo Dev sin deploy.
- Separación clara de `build`, `pack` y `deploy`; CI solo necesita build Dev.
- Check de que los `.cpp` de `src/` coinciden con el proyecto.
- Check de formato sin mutar archivos.
- clang-tidy/Code Analysis con flags reales, inicialmente informativo.
- Framework de tests C++ y primer ejecutable de tests puros.
- Baseline de warnings; nuevas warnings bloquean aunque las históricas se migren gradualmente.

### Orden sugerido

1. Documentar precondiciones de build Dev.
2. Crear job Windows y cache de dependencias.
3. Añadir smoke build.
4. Añadir test runner.
5. Incorporar checks de calidad.

### Exit gate

Dos PR consecutivos pasan build y tests en entorno limpio; el pipeline no requiere claves privadas, payload de release ni VPS.

---

## Fase 1 — Correctitud y lifetime

**Objetivo:** eliminar las rutas con mayor probabilidad de fallo difícil de reproducir.  
**Hallazgos:** AUD-004, AUD-005, AUD-006, AUD-011, AUD-012.  
**Dependencia:** Fase 0 mínima operativa.

### 1A. Config y JSON

- Crear tests de caracterización del parser actual.
- Decidir librería JSON o endurecimiento propio.
- Definir schema para envelope, payload, config local y public config.
- Rechazar un merge completo si los roots no tienen tipos correctos.
- Hacer el parser reentrante; diagnósticos por instancia/resultado.
- Versionar migraciones y probar round-trip.

**Motivo:** config firmada puede ser auténtica y aun así incompatible; hoy se degrada silenciosamente.

### 1B. Executor y shutdown

- Inventariar las 25 tareas detached por owner y propósito.
- Introducir un executor propiedad de `ApplicationRuntime` o, transitoriamente, un `TaskGroup` global con stop/join.
- Migrar primero tareas que capturan `this` y config.
- Añadir test de shutdown con red lenta/tareas pendientes.

**Motivo:** controla lifetime antes de tocar ownership de módulos.

### 1C. Boundary JNI

- Añadir `LocalRef<T>`/`GlobalRef<T>` RAII.
- Crear resultado de llamada con chequeo de excepción.
- Centralizar resolución de IDs gradualmente, empezando por hot paths.
- Añadir contadores/asserts de debug para refs y excepción pendiente.

### 1D. Health y seguridad

- Modelar un estado de salud único.
- Separar detectores de la política destructiva.
- Mantener comportamiento release detrás de una política explícita mientras tests usan una política no destructiva.

### Exit gate

- Cero `.detach()` no permitido.
- Parser/schema cubierto y reentrante.
- Shutdown espera tareas.
- Primeros wrappers JNI usados en loops críticos.
- Tests de health/security no terminan el proceso.

---

## Fase 2 — Ownership y dependencias

**Objetivo:** eliminar el grafo global que obliga a coordinar features manualmente.  
**Hallazgos:** AUD-002, AUD-003, AUD-009.  
**Dependencia:** executor/shutdown estabilizados.

### Entregables

- `ApplicationRuntime` propietario de Java, SDK, modules, hooks, executor y config.
- `ModuleManager` con `std::unique_ptr<Module>`.
- Registro declarativo de módulos: construcción, categoría, metadata y settings.
- Servicios explícitos:
  - `RotationArbiter`;
  - `PacketPolicy`;
  - `InventoryService`;
  - `RuntimeHealth`.
- Persistencia delegada por subsistema para HUD, theme y friends.
- API separada para toggle de usuario y force-disable de lifecycle.

### Estrategia de migración

1. Permitir lookup temporal mediante registry.
2. Migrar ciclos simples Timer/TimerRange y Blink/FakeLag a exclusión por policy.
3. Migrar cluster Opportunity/Clutch/AutoRod/AutoPot/ThrowPots a arbitraje.
4. Cambiar ownership a `unique_ptr`.
5. Eliminar globals restantes.

### Exit gate

- Cero punteros globales de módulos.
- `cobble.cpp` solo ensambla etapas de alto nivel.
- Agregar un módulo no exige código manual de delete ni lookup UI por nombre.

---

## Fase 3 — Consolidación y modularización

**Objetivo:** reducir duplicación y aislar subsistemas grandes sin cambiar comportamiento.  
**Hallazgos:** AUD-007, AUD-008, AUD-015, AUD-016, AUD-017.

### Entregables

- `util/time`, `util/angles`, cooldown/rate limiter.
- `InventoryService` adoptado por módulos de inventario.
- Class-file reader/writer con fixtures.
- Bytecode patches separados por target/feature.
- `menu.cpp` sin duplicación temporal y páginas especiales separadas.
- `widgets.cpp` dividido por widget/metadata.
- Seguridad movida de headers a `.cpp` con interfaces testeables.
- Documento de paths de paquetes y precedencia.

### Regla de extracción

Cada extracción debe incluir:

1. test de caracterización previo;
2. movimiento sin cambio funcional;
3. validación equivalente;
4. limpieza posterior en PR separado si es riesgosa.

### Exit gate

Las unidades tienen responsabilidad única, tests focalizados y dependencias dirigidas; no se considera éxito solo reducir líneas de un archivo.

---

## Fase 4 — Repositorio, loader y documentación

**Objetivo:** hacer que la estructura fuente describa el sistema real.  
**Hallazgos:** AUD-010, AUD-013, AUD-014.

### Entregables

- Rename semántico del loader con mapa de archivos viejo→nuevo.
- Actualización de Architecture, Development y Update Workflow.
- Assets originales versionados y blobs generados durante build.
- Decisiones de SDLCheck/CFG registradas en ADR.
- Build, pack y deploy separados y documentados.

### Exit gate

- Cero rutas inexistentes en documentación.
- Loader navegable por dominio.
- Headers generados grandes fuera de Git.
- Build Dev limpio y documentado desde checkout nuevo.

---

## Orden de PRs recomendado

1. CI Dev + test runner.
2. Tests JSON de caracterización.
3. Schema/resultado de parse reentrante.
4. Executor/TaskGroup y migración de config.
5. Migración del resto de detached tasks.
6. RAII JNI y primer hot path.
7. RuntimeHealth unificado.
8. SecurityDecision/policy.
9. Utilidades tiempo/ángulos.
10. Exclusión de módulos por policy.
11. RotationArbiter.
12. Ownership `unique_ptr`.
13. Persistencia HUD por subsistema.
14. Class-file extraction.
15. Rename loader/docs/assets.

## Trabajo que no debe mezclarse

- No renombrar todo el loader mientras se cambia autenticación/update.
- No reemplazar JSON y reescribir ConfigManager en el mismo PR.
- No dividir `bytecode_patcher.cpp` y cambiar bytecode generado a la vez.
- No activar todos los warnings como errors sin baseline.
- No eliminar globals antes de tener owner de runtime y política de shutdown.
