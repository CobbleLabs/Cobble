// ─── Auth Routes ────────────────────────────────────────────────────────────
// POST /api/auth/login — Client authentication: { key, hwid }
const express = require('express');
const router = express.Router();
const { queries } = require('../database');

router.post('/login', (req, res) => {
    try {
        const { key, hwid } = req.body;

        // ── Validate input ──────────────────────────────────────────────────
        if (!key || !hwid) {
            return res.status(400).json({
                success: false,
                message: 'Key and HWID are required.'
            });
        }

        const trimKey = key.trim();
        const trimHwid = hwid.trim();

        // ── Find the license key ────────────────────────────────────────────
        const licenseKey = queries.findKeyByValue(trimKey);
        if (!licenseKey) {
            return res.status(401).json({
                success: false,
                message: 'Invalid license key.'
            });
        }

        // ── Check if key is already redeemed ────────────────────────────────
        if (licenseKey.is_redeemed) {
            // Find the user who redeemed it
            const existingUser = licenseKey.redeemed_by
                ? queries.findUserByHwid(trimHwid)
                : null;

            // If same HWID user redeemed it, allow re-login
            if (existingUser && existingUser.id === licenseKey.redeemed_by) {
                // Check if banned
                if (existingUser.is_banned) {
                    return res.status(403).json({
                        success: false,
                        message: 'Account is banned. Contact admin.'
                    });
                }

                // Check active license
                const activeLicense = queries.getActiveLicense(existingUser.id);
                if (activeLicense) {
                    if (activeLicense.expires_at && new Date(activeLicense.expires_at) < new Date()) {
                        return res.status(403).json({
                            success: false,
                            message: 'License expired. Contact admin for renewal.'
                        });
                    }

                    queries.updateUserLastLogin(existingUser.id);
                    return res.json({
                        success: true,
                        message: 'Welcome back!',
                        license_type: activeLicense.type,
                        expires_at: activeLicense.expires_at || 'never',
                        hwid: trimHwid
                    });
                } else {
                    return res.status(403).json({
                        success: false,
                        message: 'No active license found.'
                    });
                }
            } else {
                // Different HWID or no user — key already used by someone else
                return res.status(401).json({
                    success: false,
                    message: 'This key has already been used.'
                });
            }
        }

        // ── Key is not redeemed yet — activate it ───────────────────────────

        // Find or create user by HWID
        let user = queries.findUserByHwid(trimHwid);

        if (user) {
            // Existing user with this HWID
            if (user.is_banned) {
                return res.status(403).json({
                    success: false,
                    message: 'This machine is banned. Contact admin.'
                });
            }

            // Deactivate old licenses, activate new one
            queries.deactivateUserLicenses(user.id);
        } else {
            // New user — create by HWID
            const result = queries.createUser(trimHwid);
            user = { id: result.lastInsertRowid };
        }

        const expiresAt = calculateExpiry(licenseKey.type, licenseKey.duration_days);
        queries.createLicense(user.id, trimKey, licenseKey.type, expiresAt);
        queries.redeemKey(user.id, licenseKey.id);
        queries.updateUserLastLogin(user.id);
        queries.createLog('KEY_REDEEMED', `HWID: ${trimHwid}`,
            `Key "${trimKey}" (${licenseKey.type}) activated`);

        return res.json({
            success: true,
            message: 'License activated!',
            license_type: licenseKey.type,
            expires_at: expiresAt || 'never',
            hwid: trimHwid
        });

    } catch (err) {
        console.error('Login error:', err);
        return res.status(500).json({
            success: false,
            message: 'Server error. Try again later.'
        });
    }
});

function calculateExpiry(type, durationDays) {
    if (type === 'lifetime') return null;

    let days = durationDays;
    if (!days) {
        switch (type) {
            case '3_days':  days = 3; break;
            case '1_week':  days = 7; break;
            case '1_month': days = 30; break;
            default: days = 30;
        }
    }

    const now = new Date();
    now.setDate(now.getDate() + days);
    return now.toISOString();
}

module.exports = router;
