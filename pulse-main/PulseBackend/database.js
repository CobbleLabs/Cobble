// ─── Pulse Backend - Database Setup (sql.js) ────────────────────────────────
// Uses sql.js (WebAssembly SQLite) — no native compilation needed.
const initSqlJs = require('sql.js');
const fs = require('fs');
const path = require('path');

const DB_PATH = path.resolve(__dirname, process.env.DB_PATH || './pulse.db');

let db = null;

// ─── Save DB to disk ────────────────────────────────────────────────────────
function saveDb() {
    if (db) {
        const data = db.export();
        const buffer = Buffer.from(data);
        fs.writeFileSync(DB_PATH, buffer);
    }
}

// Auto-save every 30 seconds
setInterval(saveDb, 30000);

// ─── Initialize Database ────────────────────────────────────────────────────
async function initDatabase() {
    const SQL = await initSqlJs();

    // Load existing DB or create new one
    if (fs.existsSync(DB_PATH)) {
        const fileBuffer = fs.readFileSync(DB_PATH);
        db = new SQL.Database(fileBuffer);
    } else {
        db = new SQL.Database();
    }

    // Create tables
    db.run(`
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            hwid TEXT UNIQUE DEFAULT NULL,
            is_banned INTEGER DEFAULT 0,
            created_at TEXT DEFAULT (datetime('now')),
            last_login TEXT DEFAULT NULL
        )
    `);

    db.run(`
        CREATE TABLE IF NOT EXISTS licenses (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            license_key TEXT NOT NULL,
            type TEXT NOT NULL,
            expires_at TEXT DEFAULT NULL,
            activated_at TEXT DEFAULT (datetime('now')),
            is_active INTEGER DEFAULT 1,
            FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
        )
    `);

    db.run(`
        CREATE TABLE IF NOT EXISTS license_keys (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            key TEXT UNIQUE NOT NULL,
            type TEXT NOT NULL,
            duration_days INTEGER DEFAULT NULL,
            note TEXT DEFAULT '',
            is_redeemed INTEGER DEFAULT 0,
            redeemed_by INTEGER DEFAULT NULL,
            created_at TEXT DEFAULT (datetime('now')),
            redeemed_at TEXT DEFAULT NULL
        )
    `);

    db.run(`
        CREATE TABLE IF NOT EXISTS admin_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            action TEXT NOT NULL,
            target_user TEXT DEFAULT NULL,
            details TEXT DEFAULT NULL,
            timestamp TEXT DEFAULT (datetime('now'))
        )
    `);

    saveDb();
    console.log('  [DB] SQLite database initialized at:', DB_PATH);
    return db;
}

// ─── Query Helpers ──────────────────────────────────────────────────────────
// sql.js returns arrays, these helpers convert to objects

function queryOne(sql, params = []) {
    const stmt = db.prepare(sql);
    stmt.bind(params);
    let result = null;
    if (stmt.step()) {
        const columns = stmt.getColumnNames();
        const values = stmt.get();
        result = {};
        columns.forEach((col, i) => result[col] = values[i]);
    }
    stmt.free();
    return result;
}

function queryAll(sql, params = []) {
    const stmt = db.prepare(sql);
    stmt.bind(params);
    const results = [];
    const columns = stmt.getColumnNames();
    while (stmt.step()) {
        const values = stmt.get();
        const row = {};
        columns.forEach((col, i) => row[col] = values[i]);
        results.push(row);
    }
    stmt.free();
    return results;
}

function runSql(sql, params = []) {
    db.run(sql, params);
    const changes = db.getRowsModified();
    const lastId = queryOne("SELECT last_insert_rowid() as id");
    saveDb();
    return { changes, lastInsertRowid: lastId ? lastId.id : 0 };
}

// ─── Exported Query Functions ───────────────────────────────────────────────
const queries = {
    // Users — identified by HWID (no username needed)
    findUserByHwid: (hwid) =>
        queryOne('SELECT * FROM users WHERE hwid = ?', [hwid]),

    createUser: (hwid) =>
        runSql('INSERT INTO users (hwid) VALUES (?)', [hwid]),

    updateUserLastLogin: (userId) =>
        runSql("UPDATE users SET last_login = datetime('now') WHERE id = ?", [userId]),

    resetUserHwid: (userId) =>
        runSql('UPDATE users SET hwid = NULL WHERE id = ?', [userId]),

    banUser: (banned, userId) =>
        runSql('UPDATE users SET is_banned = ? WHERE id = ?', [banned, userId]),

    deleteUser: (userId) =>
        runSql('DELETE FROM users WHERE id = ?', [userId]),

    getAllUsers: () =>
        queryAll(`
            SELECT u.*,
                   l.license_key, l.type as license_type, l.expires_at, l.is_active as license_active,
                   lk.note as key_note
            FROM users u
            LEFT JOIN licenses l ON l.user_id = u.id AND l.is_active = 1
            LEFT JOIN license_keys lk ON lk.key = l.license_key
            ORDER BY u.created_at DESC
        `),

    // License Keys
    findKeyByValue: (key) =>
        queryOne('SELECT * FROM license_keys WHERE key = ?', [key]),

    createKey: (key, type, durationDays, note) =>
        runSql('INSERT INTO license_keys (key, type, duration_days, note) VALUES (?, ?, ?, ?)', [key, type, durationDays, note || '']),

    redeemKey: (userId, keyId) =>
        runSql("UPDATE license_keys SET is_redeemed = 1, redeemed_by = ?, redeemed_at = datetime('now') WHERE id = ?", [userId, keyId]),

    getAllKeys: () =>
        queryAll('SELECT * FROM license_keys ORDER BY created_at DESC'),

    deleteKey: (keyId) =>
        runSql('DELETE FROM license_keys WHERE id = ?', [keyId]),

    // Licenses
    createLicense: (userId, licenseKey, type, expiresAt) =>
        runSql('INSERT INTO licenses (user_id, license_key, type, expires_at) VALUES (?, ?, ?, ?)', [userId, licenseKey, type, expiresAt]),

    getActiveLicense: (userId) =>
        queryOne('SELECT * FROM licenses WHERE user_id = ? AND is_active = 1 ORDER BY id DESC LIMIT 1', [userId]),

    deactivateUserLicenses: (userId) =>
        runSql('UPDATE licenses SET is_active = 0 WHERE user_id = ?', [userId]),

    // Admin Logs
    createLog: (action, targetUser, details) =>
        runSql("INSERT INTO admin_logs (action, target_user, details) VALUES (?, ?, ?)", [action, targetUser, details]),

    getLogs: () =>
        queryAll('SELECT * FROM admin_logs ORDER BY timestamp DESC LIMIT 100'),

    // Stats
    getCount: (sql) => {
        const row = queryOne(sql);
        return row ? row.count : 0;
    }
};

module.exports = { initDatabase, queries, queryOne, queryAll, runSql };
