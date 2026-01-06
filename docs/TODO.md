# Cobble - TODO & Improvements

Last updated: January 6, 2026

---

## 🔴 Priority: High

### Loader C++ Updates
- [ ] Implement `/api/loader/login` first-run flow
  - Prompt username + password when no `hwid.dat` exists
  - POST to `/api/loader/login` with credentials + generated HWID
  - Store HWID on success
  - Fallback to credential prompt if `/api/verify` returns HWID mismatch

### Website Admin Panel
- [x] `/dashboard/admin/users` - User list with search/pagination
- [x] `/dashboard/admin/users/[id]` - User detail view + edit
- [x] `/dashboard/admin/licenses` - Create/extend licenses
- [x] `/dashboard/admin/announcements` - CRUD announcements
- [x] `/dashboard/admin/bans` - Ban management
- [x] `/dashboard/admin/logs` - Audit log viewer

### Website Purchase Page
- [ ] Integrate Sellix or Stripe for payments
- [ ] Auto-create license on successful payment webhook

---

## 🟡 Priority: Medium

### Backend Improvements
- [ ] Rate limiting on all sensitive endpoints
- [ ] Email verification for registration
- [ ] Password reset flow (forgot password)
- [ ] 2FA (TOTP) support
- [ ] Discord OAuth login

### Frontend Improvements
- [ ] Error boundary components
- [ ] Better loading skeletons
- [ ] Toast for session expiry with re-login prompt
- [ ] Remember me checkbox (longer refresh token)
- [ ] Mobile responsive polish for dashboard

### License Server
- [ ] Webhook for Discord notifications (new user, new license, ban)
- [ ] License expiration email reminders
- [ ] Detailed analytics (daily active users, injection count)

---

## 🟢 Priority: Low (Nice to Have)

### Security Enhancements
- [ ] IP-based anomaly detection
- [ ] Geo-blocking for license abuse
- [ ] Refresh token rotation on every use

### Developer Experience
- [ ] API documentation (OpenAPI/Swagger)
- [ ] Postman collection for testing
- [ ] Better error messages from API

### Client
- [ ] Config profiles (save/load different presets)
- [ ] Config sharing between users
- [ ] In-game changelog viewer

---

## ✅ Completed

### License Server v2.0 (Jan 5-6, 2026)
- [x] PostgreSQL migration (removed SQLite)
- [x] JWT authentication with refresh tokens
- [x] User registration/login system
- [x] HWID binding via loader login
- [x] Profile management (password, HWID reset)
- [x] Admin CRUD for users/licenses/announcements
- [x] Ed25519 signed verification responses
- [x] CORS configuration for frontend
- [x] Legacy API key endpoints for Discord bot

### Website Frontend (Jan 6, 2026)
- [x] API client with secure token storage
- [x] AuthContext for session management
- [x] Login/Register pages with validation
- [x] Dashboard with real user data
- [x] Profile page with password change
- [x] HWID manager with reset
- [x] Downloads page with license check
- [x] Changelogs page from API

---

## 💡 Ideas for Future

- **Config Marketplace**: Share configs with the community
- **Achievement System**: Badges for hours used, modules tried
- **Referral System**: Get free days for referring friends
- **Multi-game Support**: External overlay for other games
- **Club/Team System**: Shared configs for teams
