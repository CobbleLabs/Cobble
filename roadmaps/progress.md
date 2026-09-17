# Progreso de remediación

Última actualización: 2026-09-07.

## Resumen

| Fase | Estado | Completado | Próximo paso |
|---|---|---:|---|
| 0. Baseline automatizado | NOT_STARTED | 0% | Definir build Dev sin secretos y crear CI Windows. |
| 1. Correctitud/lifetime | NOT_STARTED | 0% | Tests de caracterización JSON y censo de detached tasks. |
| 2. Ownership/dependencias | BLOCKED | 0% | Espera estabilización de shutdown/executor. |
| 3. Consolidación | BLOCKED | 0% | Espera tests y boundaries estables. |
| 4. Repositorio/docs | NOT_STARTED | 0% | Puede iniciar con correcciones documentales pequeñas. |

## Estado por hallazgo

| ID | Estado | Baseline | Objetivo | Evidencia de cierre |
|---|---|---:|---:|---|
| AUD-001 | OPEN | 0 pipelines / 0 suites | CI + tests obligatorios | Pendiente |
| AUD-002 | OPEN | 53 `new`, 53 `delete` en `cobble.cpp` | 0 para módulos | Pendiente |
| AUD-003 | OPEN | 51 globales; ≥39 accesos cruzados | 0 globals | Pendiente |
| AUD-004 | OPEN | 25 `.detach()` | 0 fuera de allowlist | Pendiente |
| AUD-005 | OPEN | parser propio, schema parcial | parser reentrante + schema/tests | Pendiente |
| AUD-006 | OPEN | 307 resoluciones; 10 en cache | allowlist mínima fuera del registry | Pendiente |
| AUD-007 | OPEN | patcher 2.952 líneas | componentes testeables | Pendiente |
| AUD-008 | OPEN | 12 `nowMs` + helpers repetidos | APIs compartidas | Pendiente |
| AUD-009 | OPEN | 209 strings `watermark*` en config | persistencia por subsistema | Pendiente |
| AUD-010 | OPEN | 4 headers / 15.318.001 bytes | assets generados fuera de Git | Pendiente |
| AUD-011 | OPEN | ExitProcess/memset/null crash | detector + policy testeable | Pendiente |
| AUD-012 | OPEN | 2 modelos; 320000/318000 ms | una state machine | Pendiente |
| AUD-013 | OPEN | loader `test*`; docs rotas | nombres/rutas semánticos | Pendiente |
| AUD-014 | OPEN | quality no integrado; W3 | quality gate + baseline | Pendiente |
| AUD-015 | OPEN | 6 hotspots >1.100 líneas | responsabilidades separadas | Pendiente |
| AUD-016 | OPEN | 95 inline funcs / 12 estados | contracts en headers | Pendiente |
| AUD-017 | OPEN | 3 paths dispersos | contrato y owner documentados | Pendiente |

## Trabajo ya existente que se preserva

Estos puntos no cierran hallazgos, pero son baseline positivo y no deberían perderse:

- [x] Los 93 `.cpp` propios de `src/` están incluidos en `Cobble.vcxproj`.
- [x] `Module` ofrece lifecycle y settings virtuales.
- [x] Gameplay modules delegan settings específicos con `saveSettings/loadSettings`.
- [x] `is_running` es atómico.
- [x] Shutdown evita descargar si PacketInterceptor/Backtrack no pueden desmontarse de forma segura.
- [x] Build verifica existencia y mutación de artefactos protegidos.
- [x] Headers generados de config se reutilizan en Dev si no cambió la entrada.
- [x] Existe tooling de format/lint/dead-code/orphans, aunque aún no es gate.
- [x] La UI tiene una regla explícita de no hacer JNI en el render thread y usa caches en varios visuales.

## Plantilla de actualización

Al iniciar trabajo:

```text
ID: AUD-xxx
Estado: IN_PROGRESS
Owner:
Branch/PR:
Baseline revalidado:
Plan corto:
Riesgos:
```

Al cerrar:

```text
ID: AUD-xxx
Estado: CLOSED o MITIGATED
Fecha:
PR/commit:
Tests ejecutados:
Build ejecutado:
Métrica anterior:
Métrica nueva:
Criterios de cierre cumplidos:
Documentación actualizada:
```

## Definition of done

Un ítem solo está `CLOSED` cuando:

- se cumple cada criterio de cierre de `audit/findings.md`;
- hay test o evidencia reproducible;
- pasa build relevante;
- no quedan rutas legacy activas sin ticket/fecha;
- las métricas y documentación se actualizaron;
- si afecta threading/JNI/hooks, existe smoke test en el host real.

## Historial

| Fecha | Cambio | Evidencia |
|---|---|---|
| 2026-09-07 | Baseline inicial y 17 hallazgos creados. | `audit/evidence.md` |
