# Roadmaps de Cobble

Este directorio contiene planes ejecutables, no una segunda copia de la auditoría.

- [`technical-debt-remediation.md`](technical-debt-remediation.md): fases, dependencias, entregables y exit gates.
- [`progress.md`](progress.md): estado vivo, métricas baseline y evidencia de cierre.
- [`../audit/findings.md`](../audit/findings.md): por qué existe cada iniciativa y cómo se verifica.

## Flujo de uso

1. Elegir el siguiente ítem no bloqueado de `technical-debt-remediation.md`.
2. Revalidar su baseline en `audit/evidence.md`.
3. Marcarlo `IN_PROGRESS` en `progress.md` y agregar owner/PR.
4. Implementar sin mezclar áreas no relacionadas.
5. Ejecutar tests/build/smoke correspondientes.
6. Comparar métricas y cumplir el exit gate.
7. Marcar `MITIGATED` o `CLOSED`; nunca borrar el historial.

## Prioridad actual

1. Fase 0: CI y tests.
2. Fase 1A: config/JSON.
3. Fase 1B: executor y shutdown.
4. Fase 1C: boundary JNI.
5. Fase 1D: health/security testeable.

Ownership global, split de monolitos y rename del loader vienen después porque son cambios amplios y necesitan primero una red de seguridad.
