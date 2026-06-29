// ─── Admin Routes ───────────────────────────────────────────────────────────
const express = require('express');
const router = express.Router();
const adminAuth = require('../middleware/adminAuth');
const { queries } = require('../database');
const crypto = require('crypto');

router.use(adminAuth);

// ─── GET /api/admin/users ───────────────────────────────────────────────────
router.get('/users', (req, res) => {
    try {
        const users = queries.getAllUsers();
        res.json({ success: true, users });
    } catch (err) {
        console.error('Error listing users:', err);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ─── POST /api/admin/users/:id/ban ──────────────────────────────────────────
router.post('/users/:id/ban', (req, res) => {
    try {
        const userId = parseInt(req.params.id);
        const { banned } = req.body;
        const result = queries.banUser(banned ? 1 : 0, userId);
        if (result.changes === 0) {
            return res.status(404).json({ success: false, message: 'User not found' });
        }
        queries.createLog(banned ? 'USER_BANNED' : 'USER_UNBANNED', `User ID: ${userId}`, `Admin ${banned ? 'banned' : 'unbanned'} user`);
        res.json({ success: true, message: `User ${banned ? 'banned' : 'unbanned'}` });
    } catch (err) {
        console.error('Error banning user:', err);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ─── POST /api/admin/users/:id/reset-hwid ───────────────────────────────────
router.post('/users/:id/reset-hwid', (req, res) => {
    try {
        const userId = parseInt(req.params.id);
        const result = queries.resetUserHwid(userId);
        if (result.changes === 0) {
            return res.status(404).json({ success: false, message: 'User not found' });
        }
        queries.createLog('HWID_RESET', `User ID: ${userId}`, 'HWID reset by admin');
        res.json({ success: true, message: 'HWID reset. User can login from a new machine.' });
    } catch (err) {
        console.error('Error resetting HWID:', err);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ─── DELETE /api/admin/users/:id ────────────────────────────────────────────
router.delete('/users/:id', (req, res) => {
    try {
        const userId = parseInt(req.params.id);
        const result = queries.deleteUser(userId);
        if (result.changes === 0) {
            return res.status(404).json({ success: false, message: 'User not found' });
        }
        queries.createLog('USER_DELETED', `User ID: ${userId}`, 'User deleted by admin');
        res.json({ success: true, message: 'User deleted' });
    } catch (err) {
        console.error('Error deleting user:', err);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ─── GET /api/admin/keys ────────────────────────────────────────────────────
router.get('/keys', (req, res) => {
    try {
        const keys = queries.getAllKeys();
        res.json({ success: true, keys });
    } catch (err) {
        console.error('Error listing keys:', err);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ─── POST /api/admin/keys ──────────────────────────────────────────────────
router.post('/keys', (req, res) => {
    try {
        const { key, type, note } = req.body;
        if (!key || !type) {
            return res.status(400).json({ success: false, message: 'Key and type are required' });
        }

        const validTypes = ['3_days', '1_week', '1_month', 'lifetime'];
        if (!validTypes.includes(type)) {
            return res.status(400).json({ success: false, message: `Invalid type. Must be one of: ${validTypes.join(', ')}` });
        }

        let durationDays = null;
        switch (type) {
            case '3_days':  durationDays = 3; break;
            case '1_week':  durationDays = 7; break;
            case '1_month': durationDays = 30; break;
            case 'lifetime': durationDays = null; break;
        }

        const existing = queries.findKeyByValue(key.trim());
        if (existing) {
            return res.status(409).json({ success: false, message: 'A key with this value already exists' });
        }

        queries.createKey(key.trim(), type, durationDays, note || '');
        queries.createLog('KEY_CREATED', null, `Key "${key}" created (${type})${note ? ' — ' + note : ''}`);

        res.json({
            success: true,
            message: `Key created: ${key} (${type})`,
            key: key.trim(),
            type,
            duration_days: durationDays
        });
    } catch (err) {
        console.error('Error creating key:', err);
        if (err.message && err.message.includes('UNIQUE')) {
            return res.status(409).json({ success: false, message: 'A key with this value already exists' });
        }
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ─── POST /api/admin/keys/generate — Auto-generate a key ───────────────────
router.post('/keys/generate', (req, res) => {
    try {
        const { type, note } = req.body;
        if (!type) {
            return res.status(400).json({ success: false, message: 'Type is required' });
        }

        const validTypes = ['3_days', '1_week', '1_month', 'lifetime'];
        if (!validTypes.includes(type)) {
            return res.status(400).json({ success: false, message: `Invalid type` });
        }

        // Auto-generate key in format PULSE-XXXX-XXXX-XXXX
        const chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ';
        const genSegment = () => {
            let seg = '';
            const bytes = crypto.randomBytes(4);
            for (let i = 0; i < 4; i++) {
                seg += chars[bytes[i] % chars.length];
            }
            return seg;
        };

        const generatedKey = `PULSE-${genSegment()}-${genSegment()}-${genSegment()}`;

        let durationDays = null;
        switch (type) {
            case '3_days':  durationDays = 3; break;
            case '1_week':  durationDays = 7; break;
            case '1_month': durationDays = 30; break;
            case 'lifetime': durationDays = null; break;
        }

        queries.createKey(generatedKey, type, durationDays, note || '');
        queries.createLog('KEY_GENERATED', null, `Key "${generatedKey}" auto-generated (${type})${note ? ' — For: ' + note : ''}`);

        res.json({
            success: true,
            message: `Key generated!`,
            key: generatedKey,
            type,
            duration_days: durationDays
        });
    } catch (err) {
        console.error('Error generating key:', err);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ─── DELETE /api/admin/keys/:id ─────────────────────────────────────────────
router.delete('/keys/:id', (req, res) => {
    try {
        const keyId = parseInt(req.params.id);
        const result = queries.deleteKey(keyId);
        if (result.changes === 0) {
            return res.status(404).json({ success: false, message: 'Key not found' });
        }
        queries.createLog('KEY_DELETED', null, `Key ID ${keyId} deleted`);
        res.json({ success: true, message: 'Key deleted' });
    } catch (err) {
        console.error('Error deleting key:', err);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ─── GET /api/admin/logs ────────────────────────────────────────────────────
router.get('/logs', (req, res) => {
    try {
        const logs = queries.getLogs();
        res.json({ success: true, logs });
    } catch (err) {
        console.error('Error fetching logs:', err);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ─── GET /api/admin/stats ───────────────────────────────────────────────────
router.get('/stats', (req, res) => {
    try {
        const totalUsers = queries.getCount('SELECT COUNT(*) as count FROM users');
        const activeUsers = queries.getCount("SELECT COUNT(*) as count FROM users WHERE is_banned = 0");
        const bannedUsers = queries.getCount("SELECT COUNT(*) as count FROM users WHERE is_banned = 1");
        const totalKeys = queries.getCount('SELECT COUNT(*) as count FROM license_keys');
        const unusedKeys = queries.getCount('SELECT COUNT(*) as count FROM license_keys WHERE is_redeemed = 0');
        const redeemedKeys = queries.getCount('SELECT COUNT(*) as count FROM license_keys WHERE is_redeemed = 1');
        const expiringSoon = queries.getCount(`
            SELECT COUNT(*) as count FROM licenses
            WHERE is_active = 1
            AND expires_at IS NOT NULL
            AND datetime(expires_at) <= datetime('now', '+3 days')
            AND datetime(expires_at) > datetime('now')
        `);

        res.json({
            success: true,
            stats: { totalUsers, activeUsers, bannedUsers, totalKeys, unusedKeys, redeemedKeys, expiringSoon }
        });
    } catch (err) {
        console.error('Error fetching stats:', err);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

module.exports = router;
