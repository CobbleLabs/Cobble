# Cobble Security Documentation

Este documento describe todos los sistemas de seguridad implementados para proteger el cliente contra reversers, crackers y loaders falsos.

---

## Tabla de Contenidos

1. [Resumen de Fases](#resumen-de-fases)
2. [Phase 1: Dead Man Switches & JNI Poisoning](#phase-1-dead-man-switches--jni-poisoning)
3. [Phase 2: Server Challenge Validation](#phase-2-server-challenge-validation)
4. [Phase 3: Honeypot Decoy System](#phase-3-honeypot-decoy-system)
5. [Phase 4: Enhanced Loader Validation](#phase-4-enhanced-loader-validation) ✅
6. [Endpoints de Seguridad](#endpoints-de-seguridad)
7. [Archivos Clave](#archivos-clave)

---

## Resumen de Fases

| Phase | Nombre | Objetivo | Estado |
|-------|--------|----------|--------|
| 1 | Dead Man Switches | Detectar tampering/debugging, corromper JNI cache | ✅ |
| 2 | Server Challenge | Validar que el loader es legítimo via sync tokens | ✅ |
| 3 | Honeypots | Detectar reversers que llaman funciones señuelo | ✅ |
| 4 | Enhanced Loader Validation | Múltiples validadores + audit logs detallados | ✅ |

---

## Phase 1: Dead Man Switches & JNI Poisoning

### Ubicación
`src/util/state.hpp`

### Concepto
El sistema mantiene checksums de la JNI cache. Si detecta tampering o si pasa demasiado tiempo sin validación, "envenena" el cache JNI causando crashes "naturales".

### Triggers

| Trigger | Descripción |
|---------|-------------|
| **Heartbeat fails 3x** | `g_stateCorrupted = true` |
| **Dead man switch** | 5.3 minutos sin validación → `isStateStale()` |
| **Checksum mismatch** | JNI cache modificado externamente |

### Acciones de Poisoning

```cpp
// attemptRecovery() - Parece que intenta recuperar, pero en realidad:
JniCache::cls_Minecraft = nullptr;
JniCache::cls_EntityPlayer = nullptr;
// ... todos los jclass quedan en nullptr

// attemptRecoveryAlt() - Alternativa más sutil:
// Intercambia method IDs para que llamen funciones incorrectas
std::swap(JniCache::mid_getHealth, JniCache::mid_getName);
```

### Por qué funciona
- El crash aparece como memory corruption, no como seguridad
- No hay strings como "banned" o "security" en el crash
- El reverser no puede encontrar un trigger obvio

### Integración

```cpp
// En cobble.cpp main loop:
if (SessionValidator::isStateCorrupted() || SessionValidator::isStateStale()) {
    sdk::internal::attemptRecovery();  // Poison
}

// En Module::shouldUpdate():
if (!sdk::internal::isOperational()) {
    return false;  // Módulos dejan de funcionar silenciosamente
}
```

---

## Phase 2: Server Challenge Validation

### Concepto
El loader recibe un `sync_token` del server. El DLL debe calcular un hash con ese token y enviarlo en cada heartbeat. Si el hash es incorrecto, el server retorna `sync_error`.

### Flujo

```
┌─────────────────────────────────────────────────────────────┐
│                    SYNC TOKEN FLOW                           │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. /api/verify                                              │
│     Server genera sync_token (32 bytes, 64 hex)              │
│     Guarda en active_sessions                                │
│     Retorna en respuesta firmada                             │
│                                                              │
│  2. Loader                                                   │
│     Copia sync_token a InjectionData en shared memory        │
│                                                              │
│  3. DLL startup                                              │
│     Lee sync_token desde shared memory                       │
│     Lo guarda en g_SyncToken                                 │
│                                                              │
│  4. /api/heartbeat                                           │
│     DLL calcula: sync_hash = SHA256(sync_token + hwid +      │
│                                      time_bucket)            │
│     Server valida hash (±1 min tolerance)                    │
│     Server rota: new_sync_token en respuesta                 │
│     DLL actualiza g_SyncToken                                │
│                                                              │
│  ON FAILURE:                                                 │
│     Server: { ok: false, reason: "sync_error" }              │
│     DLL: g_stateCorrupted = true → JNI poison                │
└─────────────────────────────────────────────────────────────┘
```

### Time Bucket
```cpp
int64_t bucket = unix_timestamp / 60;  // 1 min intervals
// Server acepta: bucket-1, bucket, bucket+1 (tolerancia a latencia)
```

### Por qué fake loaders fallan
- Sin handshake real → no tienen sync_token válido
- Sync_token hardcodeado → falla después de primera rotación
- HWID incorrecto en hash → sync_error
- Time bucket incorrecto → sync_error

### Archivos

| Archivo | Función |
|---------|---------|
| `verify.go` | Genera sync_token, lo firma y retorna |
| `heartbeat.go` | Valida sync_hash, rota token |
| `db.go` | Migration 14: `sync_token`, `prev_sync_token` |
| `loader/main.cpp` | Copia sync_token a shared memory |
| `session_validator.hpp` | `computeSyncHash()`, validación |

---

## Phase 3: Honeypot Decoy System

### Concepto
Funciones y archivos que parecen valiosos para reversers. Cuando se acceden, reportan silenciosamente a Discord y al admin panel.

### Ubicación
- `src/security/honeypot.hpp` - Sistema principal
- `src/security/internal/internal_keys.h` - Archivo señuelo

### Señuelos

| Señuelo | Tipo | Descripción |
|---------|------|-------------|
| `GetLicenseKey()` | Función | Parece retornar la license key |
| `DecryptPayloadKey()` | Función | Parece desencriptar algo importante |
| `GetRawSessionData()` | Función | Parece dar data de sesión |
| `g_DevOverride` | Variable | Parece un bypass flag |
| `internal_keys.h` | Archivo | "Keys olvidadas" de desarrollo |
| `GetDevKey()` | Función | Retorna "dev key" |
| `GetMasterKey()` | Función | Retorna "master key" |

### Características
- **Delay random 2-5s**: No es obvio que algo se triggereó
- **Report-once**: Solo reporta una vez por tipo por sesión
- **Discord embed**: Notificación inmediata con HWID, user, timestamp
- **Admin panel**: Log persistente en base de datos

### Discord Webhook
```
https://discordapp.com/api/webhooks/[REDACTED]
```

### Embed de Discord
```json
{
  "title": "🍯 Honeypot Triggered",
  "color": 16711680,
  "fields": [
    { "name": "Event", "value": "license_key_accessed" },
    { "name": "HWID", "value": "abc123..." },
    { "name": "User", "value": "reverser123" },
    { "name": "Details", "value": "GetLicenseKey() called" },
    { "name": "Time", "value": "2026-01-13 01:30:00" }
  ]
}
```

### Archivo Señuelo: internal_keys.h

```cpp
// Parece un archivo olvidado con keys de testing
// DO NOT COMMIT - internal testing keys
static const char* DEV_LICENSE_KEY = "COBBLE-DEV-A7B3-F9E2-K4M1";
static const char* MASTER_OVERRIDE_KEY = "COBBLE-MASTER-BYPASS-2026";
static const char* PAYLOAD_DECRYPTION_KEY = "4a7f2c9e8b1d...";

// Static initializer - reporta cuando el archivo es incluido
struct KeysInitializer {
    KeysInitializer() {
        Honeypot::Report("internal_keys_accessed", ...);
    }
};
static KeysInitializer __keys_init;
```

---

## Phase 4: Enhanced Loader Validation ✅

> **Estado:** Implementado

### Objetivo
Agregar múltiples capas de validación entre Loader y DLL para detectar inyección directa sin pasar por el loader legítimo.

### 4.1 Múltiples Validadores JNI

**Problema actual:** Un reverser podría inyectar directamente al JNI sin usar el loader.

**Solución:** Agregar validadores redundantes que verifican:

| Validador | Qué verifica | Ubicación |
|-----------|--------------|-----------|
| `LoaderSignatureValidator` | Firma del loader en memoria del proceso padre | DLL startup |
| `SharedMemoryValidator` | Presencia de `Local\CobbleInjection` | DLL startup |
| `ProcessChainValidator` | Cadena de procesos padre (loader → javaw) | DLL startup |
| `MemoryRegionValidator` | Que el loader tenga regiones de memoria esperadas | DLL periódico |
| `TimestampValidator` | Que la inyección ocurrió en tiempo razonable | DLL startup |

```cpp
// Ejemplo de validación múltiple:
bool ValidateLoaderPresence() {
    bool valid = true;
    valid &= ValidateLoaderSignature();      // Firma en memoria
    valid &= ValidateSharedMemory();         // Shared memory existe
    valid &= ValidateProcessChain();         // Padre es loader legítimo
    valid &= ValidateInjectionTiming();      // Timing correcto
    return valid;
}
```

### 4.2 Webhook + Audit Logs en Triggers

**Objetivo:** Cuando cualquier validador falla, reportar inmediatamente con toda la info disponible.

**Datos a reportar:**
```json
{
  "event_type": "loader_validation_failed",
  "validator": "MemoryRegionValidator",
  "hwid": "abc123...",
  "username": "user123",
  "license_key": "COBBLE-XXXX-XXXX",
  "license_expires": "2026-06-15",
  "pc_username": "xac",
  "ip": "190.123.45.67",
  "port": "54321",
  "os_version": "Windows 10 Pro 22H2",
  "cpu": "Intel i7-12700K",
  "ram_gb": 32,
  "gpu": "NVIDIA RTX 3080",
  "process_list": ["javaw.exe", "CobbleLoader.exe", ...],
  "parent_process": "explorer.exe",
  "injection_method_detected": "manual_map",
  "timestamp": "2026-01-13T01:38:00Z"
}
```

### 4.3 Audit Logs Mejorados

**Tabla: security_audit_logs (Migration 16)**
```sql
CREATE TABLE security_audit_logs (
    id SERIAL PRIMARY KEY,
    
    -- Identidad
    hwid VARCHAR(64) NOT NULL,
    username VARCHAR(64),          -- Cobble username
    pc_username VARCHAR(64),       -- Windows username (GetUserName)
    license_key VARCHAR(64),
    license_expires TIMESTAMP,
    
    -- Red
    ip VARCHAR(45) NOT NULL,
    port INTEGER,
    country VARCHAR(2),
    isp VARCHAR(128),
    
    -- Sistema
    os_version VARCHAR(128),
    cpu VARCHAR(128),
    ram_gb INTEGER,
    gpu VARCHAR(128),
    disk_serial VARCHAR(64),
    mac_address VARCHAR(17),
    
    -- Evento
    event_type VARCHAR(64) NOT NULL,
    validator VARCHAR(64),
    severity VARCHAR(16),  -- 'info', 'warning', 'critical'
    details JSONB,
    
    -- Contexto
    process_list TEXT[],
    parent_process VARCHAR(128),
    injection_method VARCHAR(32),
    
    -- Metadata
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE INDEX idx_audit_hwid ON security_audit_logs(hwid);
CREATE INDEX idx_audit_type ON security_audit_logs(event_type);
CREATE INDEX idx_audit_severity ON security_audit_logs(severity);
CREATE INDEX idx_audit_created ON security_audit_logs(created_at);
```

### 4.4 Verificadores de Firma en Memoria del Loader

**Concepto:** El DLL verifica que el loader legítimo está corriendo y tiene las firmas correctas en memoria.

```cpp
struct LoaderMemoryCheck {
    // Buscar patrones en el proceso loader:
    BYTE signature_cobble[16] = "COBBLE_LOADER_V2";
    BYTE code_hash[32];        // Hash de secciones críticas
    DWORD expected_imagebase;
    SIZE_T expected_size;
    
    bool Validate(HANDLE hLoaderProcess) {
        // 1. Leer memoria del loader
        // 2. Verificar firma
        // 3. Calcular hash de secciones
        // 4. Comparar con valores esperados
        return matches;
    }
};
```

### 4.5 Nuevos Endpoints

| Endpoint | Método | Descripción |
|----------|--------|-------------|
| `/api/internal/audit` | POST | Recibe audit logs detallados |
| `/api/admin/audit-logs` | GET | Lista audit logs en admin panel |

### 4.6 Discord Webhook Mejorado

```json
{
  "embeds": [{
    "title": "🚨 Security Alert: Loader Validation Failed",
    "color": 16711680,
    "fields": [
      { "name": "Validator", "value": "MemoryRegionValidator", "inline": true },
      { "name": "Severity", "value": "CRITICAL", "inline": true },
      { "name": "HWID", "value": "abc123...", "inline": true },
      { "name": "User", "value": "reverser123", "inline": true },
      { "name": "PC Username", "value": "xac", "inline": true },
      { "name": "License", "value": "COBBLE-XXXX-XXXX", "inline": true },
      { "name": "IP", "value": "190.123.45.67:54321", "inline": true },
      { "name": "OS", "value": "Windows 10 Pro 22H2", "inline": true },
      { "name": "CPU", "value": "Intel i7-12700K", "inline": true },
      { "name": "Parent Process", "value": "explorer.exe ⚠️", "inline": true }
    ],
    "footer": { "text": "Injection without loader detected" }
  }]
}
```

### 4.7 Implementación Propuesta

| Archivo | Cambios |
|---------|---------|
| `src/security/loader_validator.hpp` | [NEW] Múltiples validadores |
| `src/security/audit_reporter.hpp` | [NEW] Recopilación de info de PC |
| `src/security/system_info.hpp` | [NEW] CPU, GPU, RAM, OS, procesos |
| `handlers/audit.go` | [NEW] Endpoint /api/internal/audit |
| `database/db.go` | Migration 16: security_audit_logs |
| `cobble.cpp` | Llamar validadores en startup |

---

## Endpoints de Seguridad

### Server License (Go)

| Endpoint | Método | Descripción |
|----------|--------|-------------|
| `/api/verify` | POST | Verificación + genera sync_token |
| `/api/heartbeat` | POST | Valida session + sync_hash |
| `/api/internal/telemetry` | POST | Recibe reportes honeypot |
| `/api/dll/validate` | POST | Valida injection secret |

### Request/Response

**Heartbeat Request:**
```
POST /api/heartbeat
Content-Type: application/x-www-form-urlencoded

session_token=xxx&hwid=xxx&username=xxx&version=1.0.30&sync_hash=sha256hex
```

**Heartbeat Response (OK):**
```json
{
  "ok": true,
  "new_token": "rotated_session_token",
  "new_sync_token": "rotated_sync_token"
}
```

**Heartbeat Response (Sync Error):**
```json
{
  "ok": false,
  "reason": "sync_error"
}
```

**Telemetry Request:**
```
POST /api/internal/telemetry
Content-Type: application/x-www-form-urlencoded

hwid=xxx&username=xxx&event_type=license_key_accessed&details=...
```

---

## Archivos Clave

### DLL (`src/`)

| Archivo | Responsabilidad |
|---------|-----------------|
| `security/session_validator.hpp` | Validación, heartbeat, sync hash |
| `security/honeypot.hpp` | Sistema honeypot + Discord webhook |
| `security/internal/internal_keys.h` | Archivo señuelo |
| `security/dll_integrity.hpp` | SHA256 anti-patch |
| `security/loader_verify.hpp` | Anti-LoadLibrary |
| `util/state.hpp` | Dead man switch, JNI poisoning |

### Loader (`loader/`)

| Archivo | Responsabilidad |
|---------|-----------------|
| `main.cpp` | Shared memory con sync_token |
| `src/license/license.cpp` | Recibe sync_token del server |
| `src/security/` | Anti-debug, anti-VM |

### Server (`license-server/`)

| Archivo | Responsabilidad |
|---------|-----------------|
| `handlers/verify.go` | Genera sync_token |
| `handlers/heartbeat.go` | Valida sync_hash |
| `handlers/telemetry.go` | Recibe honeypot reports |
| `database/db.go` | Migrations 14, 15 |

---

## Base de Datos

### Tabla: active_sessions (Migration 14)
```sql
sync_token VARCHAR(64)      -- Token actual
prev_sync_token VARCHAR(64) -- Token anterior (race conditions)
```

### Tabla: security_events (Migration 15)
```sql
id SERIAL PRIMARY KEY
hwid VARCHAR(64)
username VARCHAR(64)
ip VARCHAR(45)
event_type VARCHAR(64)
details TEXT
created_at TIMESTAMP
```

---

## Diagrama de Flujo Completo

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   Loader    │────▶│    Server    │────▶│     DLL     │
└─────────────┘     └──────────────┘     └─────────────┘
      │                    │                    │
      │  1. /api/verify    │                    │
      │ ──────────────────▶│                    │
      │                    │                    │
      │  sync_token +      │                    │
      │  session_token     │                    │
      │ ◀──────────────────│                    │
      │                    │                    │
      │  2. Shared Memory  │                    │
      │ ───────────────────────────────────────▶│
      │                    │                    │
      │                    │  3. /api/heartbeat │
      │                    │ ◀──────────────────│
      │                    │    + sync_hash     │
      │                    │                    │
      │                    │  Validate hash     │
      │                    │  Rotate token      │
      │                    │                    │
      │                    │  new_sync_token    │
      │                    │ ──────────────────▶│
      │                    │                    │
      │                    │  If sync_error:    │
      │                    │ ──────────────────▶│──▶ JNI Poison
      │                    │                    │
      │                    │                    │
      │                    │  Honeypot trigger? │
      │                    │ ◀──────────────────│
      │                    │  (async, delayed)  │
      │                    │                    │
      │                    │  + Discord webhook │
      │                    │ ──────────────────▶│ 🔔
```

---

## Notas de Seguridad

1. **Nunca loguear** sync_token, session_token, o injection_secret en producción
2. **Webhook de Discord** es para alertas inmediatas, no reemplaza logs del server
3. **Dead man switch** tiene 5.3 min timeout - suficiente para latencia de red
4. **Honeypots** solo reportan una vez por tipo para evitar flood
5. **El delay random** (2-5s) hace difícil correlacionar acción → reporte

---

*Última actualización: 2026-01-13*
