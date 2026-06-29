// ─── Pulse Admin Panel — Application Logic ──────────────────────────────────
// Handles API communication, UI updates, and user interactions.

const API_BASE = window.location.origin;
let ADMIN_KEY = '';

// ─── API Helper ─────────────────────────────────────────────────────────────
async function api(method, path, body = null) {
    const opts = {
        method,
        headers: {
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${ADMIN_KEY}`
        }
    };
    if (body) opts.body = JSON.stringify(body);

    try {
        const res = await fetch(`${API_BASE}${path}`, opts);
        const data = await res.json();
        if (!res.ok) throw new Error(data.message || 'Request failed');
        return data;
    } catch (err) {
        if (err.message === 'Failed to fetch') {
            throw new Error('Cannot connect to server');
        }
        throw err;
    }
}

// ─── Toast Notifications ────────────────────────────────────────────────────
function showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.textContent = message;
    container.appendChild(toast);
    setTimeout(() => toast.remove(), 4000);
}

// ─── Navigation ─────────────────────────────────────────────────────────────
document.querySelectorAll('.nav-item').forEach(btn => {
    btn.addEventListener('click', () => {
        const page = btn.dataset.page;

        // Update nav active state
        document.querySelectorAll('.nav-item').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');

        // Show page
        document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
        document.getElementById(`page-${page}`).classList.add('active');

        // Update title
        document.getElementById('page-title').textContent = 
            btn.querySelector('span').textContent;

        // Load data for the page
        if (ADMIN_KEY) loadPageData(page);
    });
});

// ─── Connect Button ─────────────────────────────────────────────────────────
document.getElementById('btn-connect').addEventListener('click', connect);
document.getElementById('admin-key').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') connect();
});

async function connect() {
    ADMIN_KEY = document.getElementById('admin-key').value.trim();
    if (!ADMIN_KEY) {
        showToast('Enter admin secret key', 'error');
        return;
    }

    const statusEl = document.getElementById('server-status');
    statusEl.className = 'server-status';
    statusEl.querySelector('.status-text').textContent = 'Connecting...';

    try {
        const data = await api('GET', '/api/admin/stats');
        statusEl.className = 'server-status connected';
        statusEl.querySelector('.status-text').textContent = 'Connected';
        showToast('Connected to Pulse server', 'success');

        // Load current page data
        const activePage = document.querySelector('.nav-item.active').dataset.page;
        loadPageData(activePage);
        updateStats(data.stats);
    } catch (err) {
        statusEl.className = 'server-status error';
        statusEl.querySelector('.status-text').textContent = 'Failed';
        showToast(err.message, 'error');
    }
}

// ─── Load Page Data ─────────────────────────────────────────────────────────
async function loadPageData(page) {
    try {
        switch (page) {
            case 'dashboard': await loadDashboard(); break;
            case 'users':     await loadUsers(); break;
            case 'keys':      await loadKeys(); break;
            case 'logs':      await loadLogs(); break;
        }
    } catch (err) {
        showToast(err.message, 'error');
    }
}

// ─── Dashboard ──────────────────────────────────────────────────────────────
async function loadDashboard() {
    const data = await api('GET', '/api/admin/stats');
    updateStats(data.stats);
}

function updateStats(stats) {
    document.getElementById('stat-total-users').textContent = stats.totalUsers;
    document.getElementById('stat-active-users').textContent = stats.activeUsers;
    document.getElementById('stat-banned').textContent = stats.bannedUsers;
    document.getElementById('stat-unused-keys').textContent = stats.unusedKeys;
    document.getElementById('stat-expiring').textContent = stats.expiringSoon;
}

// ─── Users ──────────────────────────────────────────────────────────────────
async function loadUsers() {
    const data = await api('GET', '/api/admin/users');
    const tbody = document.getElementById('users-tbody');

    if (!data.users || data.users.length === 0) {
        tbody.innerHTML = '<tr><td colspan="7" class="empty-state">No users registered yet</td></tr>';
        return;
    }

    tbody.innerHTML = data.users.map(u => {
        const isBanned = u.is_banned === 1;
        const hwid = u.hwid 
            ? `<span class="hwid-cell" title="${u.hwid}" onclick="copyText('${u.hwid}')">${u.hwid}</span>` 
            : '<span class="badge badge-muted">None</span>';

        const note = u.key_note 
            ? escapeHtml(u.key_note) 
            : '<span class="badge badge-muted">—</span>';

        const licType = u.license_type 
            ? `<span class="badge badge-accent">${formatLicenseType(u.license_type)}</span>` 
            : '<span class="badge badge-muted">None</span>';

        const expires = u.expires_at 
            ? (u.expires_at === 'never' || !u.expires_at
                ? '<span class="badge badge-green">Lifetime</span>'
                : formatExpiry(u.expires_at))
            : '—';

        const status = isBanned 
            ? '<span class="badge badge-red">Banned</span>' 
            : '<span class="badge badge-green">Active</span>';

        return `<tr>
            <td>${u.id}</td>
            <td>${hwid}</td>
            <td style="color:var(--text-primary);font-weight:500">${note}</td>
            <td>${licType}</td>
            <td>${expires}</td>
            <td>${status}</td>
            <td>
                <div class="action-btns">
                    <button class="btn btn-xs ${isBanned ? 'btn-success' : 'btn-danger'}" 
                            onclick="toggleBan(${u.id}, ${!isBanned})">
                        ${isBanned ? 'Unban' : 'Ban'}
                    </button>
                    <button class="btn btn-xs btn-warning" onclick="resetHwid(${u.id})">
                        Reset HWID
                    </button>
                    <button class="btn btn-xs btn-danger" onclick="deleteUser(${u.id})">
                        Delete
                    </button>
                </div>
            </td>
        </tr>`;
    }).join('');
}

// ─── Keys ───────────────────────────────────────────────────────────────────
async function loadKeys() {
    const data = await api('GET', '/api/admin/keys');
    const tbody = document.getElementById('keys-tbody');

    if (!data.keys || data.keys.length === 0) {
        tbody.innerHTML = '<tr><td colspan="7" class="empty-state">No keys created yet. Generate one above!</td></tr>';
        return;
    }

    tbody.innerHTML = data.keys.map(k => {
        const status = k.is_redeemed 
            ? '<span class="badge badge-blue">Redeemed</span>' 
            : '<span class="badge badge-green">Available</span>';

        const type = `<span class="badge badge-accent">${formatLicenseType(k.type)}</span>`;
        const note = k.note ? escapeHtml(k.note) : '<span class="badge badge-muted">—</span>';

        return `<tr>
            <td>${k.id}</td>
            <td><span class="key-cell" onclick="copyText('${escapeHtml(k.key)}')" title="Click to copy">${escapeHtml(k.key)}</span></td>
            <td>${type}</td>
            <td style="color:var(--text-primary)">${note}</td>
            <td>${status}</td>
            <td>${formatDate(k.created_at)}</td>
            <td>
                <div class="action-btns">
                    <button class="btn btn-xs btn-outline" onclick="copyText('${escapeHtml(k.key)}')">Copy</button>
                    ${!k.is_redeemed ? `<button class="btn btn-xs btn-danger" onclick="deleteKey(${k.id})">Delete</button>` : ''}
                </div>
            </td>
        </tr>`;
    }).join('');
}

// Generate Key
document.getElementById('btn-generate-key').addEventListener('click', async () => {
    const keyType = document.getElementById('new-key-type').value;
    const keyNote = document.getElementById('new-key-note').value.trim();

    if (!ADMIN_KEY) {
        showToast('Connect first', 'error');
        return;
    }

    try {
        const data = await api('POST', '/api/admin/keys/generate', { type: keyType, note: keyNote });
        showToast(`Key generated!`, 'success');
        
        // Show the generated key
        const display = document.getElementById('generated-key-display');
        const value = document.getElementById('generated-key-value');
        value.textContent = data.key;
        display.style.display = 'flex';

        document.getElementById('new-key-note').value = '';
        loadKeys();
    } catch (err) {
        showToast(err.message, 'error');
    }
});

// Copy generated key
document.getElementById('btn-copy-generated').addEventListener('click', () => {
    const key = document.getElementById('generated-key-value').textContent;
    copyText(key);
});

// ─── Logs ───────────────────────────────────────────────────────────────────
async function loadLogs() {
    const data = await api('GET', '/api/admin/logs');
    const tbody = document.getElementById('logs-tbody');

    if (!data.logs || data.logs.length === 0) {
        tbody.innerHTML = '<tr><td colspan="4" class="empty-state">No activity yet</td></tr>';
        return;
    }

    tbody.innerHTML = data.logs.map(l => `<tr>
        <td>${formatDate(l.timestamp)}</td>
        <td><span class="badge ${getLogBadgeClass(l.action)}">${l.action}</span></td>
        <td>${escapeHtml(l.target_user || '—')}</td>
        <td style="max-width:300px;overflow:hidden;text-overflow:ellipsis">${escapeHtml(l.details || '—')}</td>
    </tr>`).join('');
}

// ─── User Actions ───────────────────────────────────────────────────────────
async function toggleBan(userId, ban) {
    if (!confirm(`Are you sure you want to ${ban ? 'ban' : 'unban'} this user?`)) return;
    try {
        await api('POST', `/api/admin/users/${userId}/ban`, { banned: ban });
        showToast(`User ${ban ? 'banned' : 'unbanned'}`, 'success');
        loadUsers();
    } catch (err) {
        showToast(err.message, 'error');
    }
}

async function resetHwid(userId) {
    if (!confirm('Reset this user\'s HWID? They will be able to login from a new machine.')) return;
    try {
        await api('POST', `/api/admin/users/${userId}/reset-hwid`);
        showToast('HWID reset', 'success');
        loadUsers();
    } catch (err) {
        showToast(err.message, 'error');
    }
}

async function deleteUser(userId) {
    if (!confirm('DELETE this user? This action cannot be undone!')) return;
    try {
        await api('DELETE', `/api/admin/users/${userId}`);
        showToast('User deleted', 'success');
        loadUsers();
    } catch (err) {
        showToast(err.message, 'error');
    }
}

async function deleteKey(keyId) {
    if (!confirm('Delete this key?')) return;
    try {
        await api('DELETE', `/api/admin/keys/${keyId}`);
        showToast('Key deleted', 'success');
        loadKeys();
    } catch (err) {
        showToast(err.message, 'error');
    }
}

// ─── Utility Functions ──────────────────────────────────────────────────────
function copyText(text) {
    navigator.clipboard.writeText(text).then(() => {
        showToast('Copied to clipboard!', 'info');
    });
}

function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
              .replace(/"/g,'&quot;').replace(/'/g,'&#39;');
}

function formatLicenseType(type) {
    switch (type) {
        case '3_days':   return '3 Days';
        case '1_week':   return '1 Week';
        case '1_month':  return '1 Month';
        case 'lifetime': return 'Lifetime';
        default: return type;
    }
}

function formatDate(dateStr) {
    if (!dateStr) return '—';
    try {
        const d = new Date(dateStr);
        return d.toLocaleDateString('en-US', { 
            month: 'short', day: 'numeric', year: 'numeric',
            hour: '2-digit', minute: '2-digit'
        });
    } catch {
        return dateStr;
    }
}

function formatExpiry(dateStr) {
    if (!dateStr || dateStr === 'never') return '<span class="badge badge-green">Lifetime</span>';
    try {
        const d = new Date(dateStr);
        const now = new Date();
        if (d < now) return '<span class="badge badge-red">Expired</span>';

        const diffMs = d - now;
        const diffDays = Math.floor(diffMs / (1000 * 60 * 60 * 24));
        const diffHours = Math.floor(diffMs / (1000 * 60 * 60));

        if (diffDays <= 0) {
            return `<span class="badge badge-yellow">${diffHours}h left</span>`;
        } else if (diffDays <= 3) {
            return `<span class="badge badge-yellow">${diffDays}d left</span>`;
        }
        return `<span class="badge badge-green">${diffDays}d left</span>`;
    } catch {
        return dateStr;
    }
}

function getLogBadgeClass(action) {
    if (action.includes('BANNED') || action.includes('DELETED')) return 'badge-red';
    if (action.includes('CREATED') || action.includes('REDEEMED') || action.includes('GENERATED')) return 'badge-green';
    if (action.includes('RESET') || action.includes('UNBANNED')) return 'badge-yellow';
    return 'badge-muted';
}

// Make functions available globally for inline onclick handlers
window.toggleBan = toggleBan;
window.resetHwid = resetHwid;
window.deleteUser = deleteUser;
window.deleteKey = deleteKey;
window.copyText = copyText;
