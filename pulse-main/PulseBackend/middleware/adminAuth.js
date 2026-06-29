// ─── Admin Auth Middleware ───────────────────────────────────────────────────
// Protects admin routes with a secret key passed in the Authorization header.

function adminAuth(req, res, next) {
    const authHeader = req.headers['authorization'];
    
    if (!authHeader) {
        return res.status(401).json({ 
            success: false, 
            message: 'Authorization header required' 
        });
    }

    // Expect: "Bearer <admin_secret>"
    const token = authHeader.startsWith('Bearer ') 
        ? authHeader.slice(7) 
        : authHeader;

    if (token !== process.env.ADMIN_SECRET) {
        return res.status(403).json({ 
            success: false, 
            message: 'Invalid admin credentials' 
        });
    }

    next();
}

module.exports = adminAuth;
