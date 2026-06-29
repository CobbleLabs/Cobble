// ─── Pulse Backend - Server Entry Point ─────────────────────────────────────
require('dotenv').config();
const express = require('express');
const cors = require('cors');
const path = require('path');
const { initDatabase } = require('./database');

const app = express();
const PORT = process.env.PORT || 3000;

// ─── Middleware ─────────────────────────────────────────────────────────────
app.use(cors());
app.use(express.json());

// Bloqueo Anti-Reconocimiento: El panel de control SOLO es accesible desde tu PC local.
// Si alguien intenta entrar desde el túnel de Cloudflare o internet, devuelve 404 Not Found.
const localOnly = (req, res, next) => {
    if (req.headers['cf-connecting-ip'] || req.headers['cf-ray'] || req.headers['x-forwarded-for']) {
        return res.status(404).send('Not Found');
    }
    next();
};

// Serve admin panel static files (protegido)
app.use('/admin', localOnly, express.static(path.join(__dirname, '..', 'PulseAdmin')));

// ─── Routes ─────────────────────────────────────────────────────────────────
app.use('/api/auth', require('./routes/auth'));
app.use('/api/admin', localOnly, require('./routes/admin'));

// ─── Health Check ───────────────────────────────────────────────────────────
app.get('/api/health', (req, res) => {
    res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

// ─── Root redirect to admin ─────────────────────────────────────────────────
app.get('/', (req, res) => {
    if (req.headers['cf-connecting-ip'] || req.headers['cf-ray'] || req.headers['x-forwarded-for']) {
        return res.status(404).send('Pulse License Server Active.');
    }
    res.redirect('/admin');
});

// ─── Start Server (async for sql.js init) ───────────────────────────────────
async function start() {
    try {
        await initDatabase();

        app.listen(PORT, () => {
            console.log('');
            console.log('  ╔══════════════════════════════════════╗');
            console.log('  ║     PULSE LICENSE SERVER              ║');
            console.log(`  ║     Running on port ${PORT}              ║`);
            console.log('  ╠══════════════════════════════════════╣');
            console.log(`  ║  API:   http://localhost:${PORT}/api     ║`);
            console.log(`  ║  Admin: http://localhost:${PORT}/admin   ║`);
            console.log('  ╚══════════════════════════════════════╝');
            console.log('');
        });
    } catch (err) {
        console.error('Failed to start server:', err);
        process.exit(1);
    }
}

start();
