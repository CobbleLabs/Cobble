# Cobble Backend Integration Specification

> **Purpose**: This document specifies all backend systems, APIs, and integrations required to fully connect the Cobble web frontend. It is intended for backend developers who have not seen the frontend code.

---

## 1. High-Level Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              CLIENT                                      │
│  Next.js 16 (App Router) + TypeScript + Tailwind                        │
│  SSR for public pages, CSR for dashboard                                │
└─────────────────────────────┬───────────────────────────────────────────┘
                              │ HTTPS (REST JSON)
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          API GATEWAY / BACKEND                           │
│  Recommended: Go                           │
│  Authentication: JWT (Access + Refresh tokens, HttpOnly cookies)        │
│  Rate Limiting: Per-IP + Per-User                                       │
└─────────┬───────────────────┬───────────────────┬───────────────────────┘
          │                   │                   │
          ▼                   ▼                   ▼
   ┌──────────────┐   ┌──────────────┐   ┌──────────────────────┐
   │  PostgreSQL  │   │    Redis     │   │  External Services   │
   │  (Primary)   │   │  (Sessions/  │   │  - Stripe/Crypto     │
   │              │   │   Cache)     │   │  - HWID Validation   │
   │              │   │              │   │  - Discord Webhooks  │
   └──────────────┘   └──────────────┘   └──────────────────────┘
```

### Stack Recommendations

| Layer | Technology | Rationale |
|-------|------------|-----------|
| API | Node.js (Fastify) or Go | JSON APIs, JWT handling, async I/O |
| Database | PostgreSQL | Relational data, ACID compliance, JSON support |
| Cache/Sessions | Redis | Token blacklisting, rate limiting, session store |
| Payments | Stripe + Crypto (manual) | Recurring payments + privacy option |
| File Storage | S3-compatible | Downloadable client binaries |

---

## 2. Authentication & Authorization

### 2.1 Token Strategy

- **Access Token**: Short-lived JWT (15 min), sent in `Authorization: Bearer <token>` header
- **Refresh Token**: Long-lived (7 days), stored in HttpOnly cookie with `SameSite=Strict`
- **Token Blacklist**: Store invalidated tokens in Redis until expiry

### 2.2 JWT Payload

```json
{
  "sub": "uuid",
  "email": "user@example.com",
  "role": "user|admin",
  "plan": "none|trial|monthly|lifetime",
  "iat": 1704067200,
  "exp": 1704068100
}
```

### 2.3 Role Model

| Role | Permissions |
|------|-------------|
| `user` | Access dashboard, download client, manage own profile/HWID |
| `admin` | All user permissions + access admin panel, manage users/licenses/bans/announcements |

### 2.4 Required Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/auth/register` | Create account (username, email, password) |
| POST | `/api/auth/login` | Authenticate, return tokens |
| POST | `/api/auth/refresh` | Refresh access token using refresh cookie |
| POST | `/api/auth/logout` | Invalidate tokens, clear cookie |
| GET | `/api/auth/me` | Get current user from token |

### 2.5 Registration Flow

```
Client                          Server
  │                               │
  │─── POST /auth/register ──────▶│
  │    {username, email, pass}    │
  │                               │──▶ Validate inputs
  │                               │──▶ Check email uniqueness
  │                               │──▶ Hash password (argon2)
  │                               │──▶ Create user (plan: none)
  │◀── 201 {user} ────────────────│
  │                               │
  │─── POST /auth/login ─────────▶│
  │    {email, password}          │
  │                               │──▶ Verify password
  │                               │──▶ Generate tokens
  │◀── 200 + Set-Cookie ──────────│
  │    {accessToken, user}        │
```

### 2.6 Protected Route Middleware

Every protected endpoint must:
1. Extract `Authorization` header
2. Verify JWT signature and expiry
3. Check token not in blacklist
4. Attach `user` to request context
5. For admin routes: verify `role === 'admin'`

---

## 3. User Account System

### 3.1 User Data Model

```sql
CREATE TABLE users (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  username VARCHAR(32) UNIQUE NOT NULL,
  email VARCHAR(255) UNIQUE NOT NULL,
  password_hash VARCHAR(255) NOT NULL,
  role VARCHAR(16) DEFAULT 'user',
  plan VARCHAR(16) DEFAULT 'none',
  plan_expires_at TIMESTAMP,
  hwid VARCHAR(64),
  hwid_last_reset TIMESTAMP,
  avatar_url VARCHAR(512),
  discord_id VARCHAR(32),
  created_at TIMESTAMP DEFAULT NOW(),
  updated_at TIMESTAMP DEFAULT NOW(),
  banned_at TIMESTAMP,
  ban_reason TEXT
);
```

### 3.2 Profile Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/users/me` | Get full profile |
| PATCH | `/api/users/me` | Update profile (avatar, discord) |
| PUT | `/api/users/me/password` | Change password |
| POST | `/api/users/me/hwid/reset` | Reset HWID (rate-limited: 1/month) |

### 3.3 Password Change Request

```json
// PUT /api/users/me/password
{
  "currentPassword": "...",
  "newPassword": "...",
  "confirmPassword": "..."
}
```

**Validation**:
- Verify current password
- Ensure new ≠ current
- Minimum 8 characters
- Passwords match

---

## 4. Feature-by-Feature Integration

### 4.1 Landing Page (Public)

**Data Required**: None (static content)

**Note**: Pricing values are hardcoded in frontend. If dynamic pricing is needed later, add:
```
GET /api/pricing → [{ id, name, price, interval, features: string[] }]
```

---

### 4.2 Dashboard Home

**Data Required**:
- Current user profile
- Active announcements
- License status

**Endpoints**:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/announcements` | Public announcements for dashboard |
| GET | `/api/users/me` | User profile + license info |

**Announcement Response**:
```json
{
  "id": "uuid",
  "title": "Maintenance Notice",
  "content": "Servers offline 2am-4am UTC",
  "type": "info|warning|update",
  "pinned": false,
  "createdAt": "2026-01-06T00:00:00Z"
}
```

---

### 4.3 Downloads Page

**Data Required**:
- Available client versions
- User must have active license

**Endpoint**:
```
GET /api/downloads
Authorization: Bearer <token>
```

**Response**:
```json
[
  {
    "id": "uuid",
    "version": "2.4.1",
    "name": "Cobble Loader",
    "size": "12.4 MB",
    "releasedAt": "2026-01-05T00:00:00Z",
    "downloadUrl": "presigned-s3-url",
    "changelog": "- Fixed crash on 1.8.9\n- Added new bypass"
  }
]
```

**Security**:
- Verify user has valid license (`plan !== 'none'` OR `plan_expires_at > NOW()`)
- Verify user is not banned
- Generate short-lived presigned S3 URL (5 min expiry)
- Log download event

---

### 4.4 HWID Manager

**Data Required**:
- User's current HWID (masked)
- Last reset date
- Whether reset is available

**Endpoints**:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/users/me/hwid` | Get HWID status |
| POST | `/api/users/me/hwid/reset` | Reset HWID |

**GET Response**:
```json
{
  "hwid": "A1B2...****",
  "lastReset": "2025-12-01T00:00:00Z",
  "canReset": true,
  "nextResetAvailable": null
}
```

**Reset Logic**:
- Check `hwid_last_reset < NOW() - INTERVAL '30 days'`
- If false: return 429 with `nextResetAvailable` date
- If true: set `hwid = NULL`, update `hwid_last_reset`

---

### 4.5 Changelogs

**Data Required**: List of version changelogs

**Endpoint**:
```
GET /api/changelogs?limit=10&offset=0
```

**Response**:
```json
{
  "data": [
    {
      "id": "uuid",
      "version": "2.4.1",
      "date": "2026-01-05",
      "changes": ["Fixed X", "Added Y"]
    }
  ],
  "total": 50
}
```

---

### 4.6 Profile Page

See Section 3.2. Uses `/api/users/me` and related endpoints.

---

### 4.7 Purchase Page

**Flow**:
1. User selects plan (Monthly $10 / Lifetime $50)
2. Frontend redirects to payment provider
3. Provider sends webhook on success
4. Backend updates user's `plan` and `plan_expires_at`

**Endpoints**:

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/checkout/create` | Create Stripe checkout session |
| POST | `/api/webhooks/stripe` | Handle Stripe events |

**Checkout Create**:
```json
// Request
{ "planId": "monthly|lifetime" }

// Response
{ "checkoutUrl": "https://checkout.stripe.com/..." }
```

**Webhook Events to Handle**:
- `checkout.session.completed` → Activate license
- `invoice.paid` → Extend monthly subscription
- `customer.subscription.deleted` → Expire license

---

## 5. Admin Panel

### 5.1 Users Management

**Endpoints**:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/admin/users` | List all users (paginated, filterable) |
| GET | `/api/admin/users/:id` | Get user detail |
| PATCH | `/api/admin/users/:id` | Update user (role, plan, ban status) |
| DELETE | `/api/admin/users/:id` | Delete user |

**List Query Params**:
- `search`: Filter by username/email
- `plan`: Filter by plan type
- `status`: Filter by banned/active
- `page`, `limit`: Pagination

---

### 5.2 Licenses Management

**Endpoints**:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/admin/licenses` | List all licenses |
| POST | `/api/admin/licenses` | Create license for user |
| PATCH | `/api/admin/licenses/:id/extend` | Extend license |
| DELETE | `/api/admin/licenses/:id` | Revoke license |

**Create License**:
```json
// Request
{
  "userId": "uuid",
  "plan": "monthly|lifetime",
  "duration": 30 // days, null for lifetime
}
```

---

### 5.3 Bans Management

**Endpoints**:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/admin/bans` | List banned users |
| POST | `/api/admin/users/:id/ban` | Ban user |
| DELETE | `/api/admin/users/:id/ban` | Unban user |

**Ban Request**:
```json
{ "reason": "Chargeback" }
```

---

### 5.4 Announcements Management

**Endpoints**:

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/admin/announcements` | List all (incl. unpublished) |
| POST | `/api/admin/announcements` | Create |
| PATCH | `/api/admin/announcements/:id` | Update |
| DELETE | `/api/admin/announcements/:id` | Delete |

**Create/Update**:
```json
{
  "title": "...",
  "content": "...",
  "type": "info|warning|update",
  "pinned": false,
  "published": true
}
```

---

### 5.5 Audit Logs

**Purpose**: Track admin actions for accountability

**Data Model**:
```sql
CREATE TABLE audit_logs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  admin_id UUID REFERENCES users(id),
  action VARCHAR(64) NOT NULL,
  target_type VARCHAR(32),
  target_id UUID,
  details JSONB,
  ip_address INET,
  created_at TIMESTAMP DEFAULT NOW()
);
```

**Endpoint**:
```
GET /api/admin/logs?limit=50&offset=0&action=user.ban
```

**Actions to Log**:
- `user.ban`, `user.unban`
- `license.create`, `license.extend`, `license.revoke`
- `announcement.create`, `announcement.update`, `announcement.delete`
- `user.role_change`

---

## 6. Security Considerations

### 6.1 Rate Limiting

| Endpoint Pattern | Limit | Window |
|------------------|-------|--------|
| `/api/auth/login` | 5 | 15 min |
| `/api/auth/register` | 3 | 1 hour |
| `/api/users/me/hwid/reset` | 1 | 30 days |
| `/api/downloads` | 10 | 1 min |
| `/api/admin/*` | 100 | 1 min |
| Default | 60 | 1 min |

### 6.2 Input Validation

- **All endpoints**: Validate with schema (Zod/Joi/jsonschema)
- **Username**: 3-32 chars, alphanumeric + underscore
- **Email**: RFC 5322 compliant
- **Password**: Minimum 8 chars
- **UUIDs**: Validate format before DB queries

### 6.3 Authentication Enforcement

```
PUBLIC:
  - GET /api/pricing
  - GET /api/announcements
  - POST /api/auth/*
  - POST /api/webhooks/*

USER:
  - GET/PATCH /api/users/me
  - GET /api/downloads
  - GET /api/changelogs

ADMIN:
  - /api/admin/*
```

### 6.4 Anti-Abuse

- HWID binding: Client sends HWID on launch, backend validates against stored
- Download logging: Track IP, timestamp, user for abuse detection
- Ban propagation: Banned users rejected at auth middleware level
- Webhook signature verification (Stripe)

---

## 7. Deployment Considerations

### 7.1 Environment Variables

```env
# Database
DATABASE_URL=postgres://user:pass@host:5432/cobble

# Redis
REDIS_URL=redis://host:6379

# JWT
JWT_SECRET=<32+ char random string>
JWT_ACCESS_EXPIRY=15m
JWT_REFRESH_EXPIRY=7d

# Stripe
STRIPE_SECRET_KEY=sk_live_...
STRIPE_WEBHOOK_SECRET=whsec_...

# S3/Storage
S3_BUCKET=cobble-downloads
S3_ACCESS_KEY=...
S3_SECRET_KEY=...
S3_ENDPOINT=https://s3.amazonaws.com

# App
API_URL=https://api.cobble.gg
FRONTEND_URL=https://cobble.gg
```

### 7.2 CORS Configuration

```javascript
cors({
  origin: process.env.FRONTEND_URL,
  credentials: true, // for cookies
  methods: ['GET', 'POST', 'PATCH', 'PUT', 'DELETE'],
  allowedHeaders: ['Content-Type', 'Authorization']
})
```

### 7.3 Secrets Management

- Never commit secrets to version control
- Use platform secrets (Vercel, Railway, etc.) or vault
- Rotate `JWT_SECRET` periodically (requires token blacklist + forced re-login)

---

## 8. Assumptions

1. Single-tenant system (one organization)
2. Users purchase licenses directly (no resellers)
3. Monthly plans auto-renew via Stripe subscriptions
4. Lifetime plans are one-time payments
5. HWID is provided by the desktop client (not web)
6. Admin role is assigned manually via database or seed
7. No 2FA initially (can be added later)
8. No email verification initially (can be added)

---

## Appendix: Quick Reference

### Minimal Viable Backend

To get a working prototype:

1. **Auth**: Register, Login, Logout, Me
2. **Profile**: Get/Update profile, Change password
3. **Downloads**: List with presigned URLs
4. **Announcements**: CRUD (admin) + List (public)
5. **Users** (admin): List, View, Ban/Unban

### Technology Checklist

- [ ] PostgreSQL + migrations (Prisma/Drizzle)
- [ ] Redis for rate limiting + token blacklist
- [ ] JWT library (jose/jsonwebtoken)
- [ ] Password hashing (argon2)
- [ ] S3 SDK for presigned URLs
- [ ] Stripe SDK + webhook handling
- [ ] Request validation (Zod)
- [ ] Structured logging (pino)
- [ ] Error handling middleware
- [ ] CORS + Helmet security headers
