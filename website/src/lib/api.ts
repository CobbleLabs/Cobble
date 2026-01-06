// API Client for Cobble Backend
// DEV MODE: Token in localStorage (persists across page loads)
// PROD MODE: Access token in memory, refresh via HttpOnly cookie

const API_URL = process.env.NEXT_PUBLIC_API_URL || 'https://syra.club';

// Use localStorage in development to persist tokens across page loads
// This is needed because cross-domain cookies don't work without same domain
const TOKEN_STORAGE_KEY = 'cobble_access_token';

function getStoredToken(): string | null {
    if (typeof window === 'undefined') return null;
    return localStorage.getItem(TOKEN_STORAGE_KEY);
}

function storeToken(token: string | null) {
    if (typeof window === 'undefined') return;
    if (token) {
        localStorage.setItem(TOKEN_STORAGE_KEY, token);
    } else {
        localStorage.removeItem(TOKEN_STORAGE_KEY);
    }
}

// In-memory fallback
let accessToken: string | null = null;

export interface User {
    id: string;
    username: string;
    email: string;
    role: 'user' | 'admin';
    avatar_url?: string;
    discord_id?: string;
    created_at: string;
    updated_at: string;
}

export interface License {
    id: number;
    plan: 'monthly' | 'lifetime' | 'trial';
    expires_at?: string;
    hwid?: string;
    hwid_last_reset?: string;
    created_at: string;
}

export interface UserWithLicense extends User {
    license?: License;
}

export interface AuthResponse {
    access_token: string;
    user: User;
}

export interface Announcement {
    id: string;
    title: string;
    content: string;
    type: 'info' | 'warning' | 'update';
    pinned: boolean;
    created_at: string;
}

export interface Changelog {
    id: string;
    version: string;
    changes: string[];
    released_at: string;
}

export interface HWIDStatus {
    hwid?: string;
    last_reset?: string;
    next_reset_available?: string;
    can_reset: boolean;
}

export interface ApiError {
    error: string;
}

// Token management - uses localStorage for persistence
export function setAccessToken(token: string | null) {
    accessToken = token;
    storeToken(token);
}

export function getAccessToken(): string | null {
    // Try memory first, then localStorage
    if (accessToken) return accessToken;
    const stored = getStoredToken();
    if (stored) {
        accessToken = stored;
        return stored;
    }
    return null;
}

export function clearAuth() {
    accessToken = null;
    storeToken(null);
}

// Base fetch with auth and error handling
async function apiFetch<T>(
    endpoint: string,
    options: RequestInit = {}
): Promise<T> {
    const url = `${API_URL}${endpoint}`;

    const headers: HeadersInit = {
        'Content-Type': 'application/json',
        ...options.headers,
    };

    // Add auth header if we have a token
    const token = getAccessToken();
    if (token) {
        (headers as Record<string, string>)['Authorization'] = `Bearer ${token}`;
    }

    const response = await fetch(url, {
        ...options,
        headers,
        credentials: 'include', // Important: send cookies for refresh token
    });

    // Handle 401 - try to refresh
    if (response.status === 401 && endpoint !== '/api/auth/refresh') {
        const refreshed = await refreshToken();
        if (refreshed) {
            // Retry original request with new token
            (headers as Record<string, string>)['Authorization'] = `Bearer ${accessToken}`;
            const retryResponse = await fetch(url, {
                ...options,
                headers,
                credentials: 'include',
            });
            if (!retryResponse.ok) {
                const error = await retryResponse.json().catch(() => ({ error: 'Request failed' }));
                throw new Error(error.error || 'Request failed');
            }
            return retryResponse.json();
        }
        throw new Error('Session expired');
    }

    if (!response.ok) {
        const error = await response.json().catch(() => ({ error: 'Request failed' }));
        throw new Error(error.error || `HTTP ${response.status}`);
    }

    // Handle empty responses
    const text = await response.text();
    if (!text) return {} as T;
    return JSON.parse(text);
}

// Auth endpoints
export async function login(email: string, password: string): Promise<AuthResponse> {
    const data = await apiFetch<AuthResponse>('/api/auth/login', {
        method: 'POST',
        body: JSON.stringify({ email, password }),
    });
    setAccessToken(data.access_token);
    return data;
}

export async function register(username: string, email: string, password: string): Promise<{ success: boolean }> {
    return apiFetch('/api/auth/register', {
        method: 'POST',
        body: JSON.stringify({ username, email, password }),
    });
}

let refreshPromise: Promise<boolean> | null = null;

export async function refreshToken(): Promise<boolean> {
    if (refreshPromise) return refreshPromise;

    refreshPromise = (async () => {
        try {
            const data = await apiFetch<{ access_token: string }>('/api/auth/refresh', {
                method: 'POST',
            });
            setAccessToken(data.access_token);
            return true;
        } catch {
            clearAuth();
            return false;
        } finally {
            refreshPromise = null;
        }
    })();

    return refreshPromise;
}

export async function logout(): Promise<void> {
    try {
        await apiFetch('/api/auth/logout', { method: 'POST' });
    } finally {
        clearAuth();
    }
}

export async function getMe(): Promise<UserWithLicense> {
    return apiFetch('/api/auth/me');
}

// Profile endpoints
export async function updateProfile(data: { avatar_url?: string; discord_id?: string }): Promise<void> {
    await apiFetch('/api/users/me', {
        method: 'PATCH',
        body: JSON.stringify(data),
    });
}

export async function changePassword(currentPassword: string, newPassword: string): Promise<void> {
    await apiFetch('/api/users/me/password', {
        method: 'PUT',
        body: JSON.stringify({
            current_password: currentPassword,
            new_password: newPassword,
            confirm_password: newPassword,
        }),
    });
}

export async function getHWIDStatus(): Promise<HWIDStatus> {
    return apiFetch('/api/users/me/hwid');
}

export async function resetHWID(): Promise<void> {
    await apiFetch('/api/users/me/hwid/reset', { method: 'POST' });
}

// Dashboard endpoints
export async function getAnnouncements(): Promise<Announcement[]> {
    return apiFetch('/api/announcements');
}

export async function getChangelogs(): Promise<Changelog[]> {
    return apiFetch('/api/changelogs');
}

export async function getDownloads(): Promise<{ id: string; version: string; name: string; download_url: string }[]> {
    return apiFetch('/api/downloads');
}

// Admin endpoints (require admin role)
export interface AdminUser {
    id: string;
    username: string;
    email: string;
    role: 'user' | 'admin';
    discord_id?: string;
    banned_at?: string;
    ban_reason?: string;
    created_at: string;
    license?: License;
}

export interface AdminUsersResponse {
    users: AdminUser[];
    total: number;
    page: number;
    limit: number;
}

export const admin = {
    async getUsers(params?: { search?: string; page?: number; limit?: number }): Promise<AdminUsersResponse> {
        const query = new URLSearchParams();
        if (params?.search) query.set('search', params.search);
        if (params?.page) query.set('page', params.page.toString());
        if (params?.limit) query.set('limit', params.limit.toString());
        return apiFetch(`/api/admin/users?${query}`);
    },

    async getUser(userId: string): Promise<AdminUser> {
        return apiFetch(`/api/admin/users/${userId}`);
    },

    async updateUser(userId: string, data: { role?: string; discord_id?: string }): Promise<void> {
        await apiFetch(`/api/admin/users/${userId}`, {
            method: 'PATCH',
            body: JSON.stringify(data),
        });
    },

    async banUser(userId: string, reason: string): Promise<void> {
        await apiFetch(`/api/admin/users/${userId}/ban`, {
            method: 'POST',
            body: JSON.stringify({ reason }),
        });
    },

    async unbanUser(userId: string): Promise<void> {
        await apiFetch(`/api/admin/users/${userId}/ban`, { method: 'DELETE' });
    },

    async createLicense(userId: string, plan: string, days?: number): Promise<void> {
        await apiFetch('/api/admin/licenses', {
            method: 'POST',
            body: JSON.stringify({ user_id: userId, plan, days }),
        });
    },

    async extendLicense(licenseId: number, days: number): Promise<void> {
        await apiFetch(`/api/admin/licenses/${licenseId}`, {
            method: 'PATCH',
            body: JSON.stringify({ days }),
        });
    },

    async getAnnouncements(): Promise<Announcement[]> {
        return apiFetch('/api/admin/announcements');
    },

    async createAnnouncement(data: { title: string; content: string; type: string; pinned?: boolean }): Promise<void> {
        await apiFetch('/api/admin/announcements', {
            method: 'POST',
            body: JSON.stringify(data),
        });
    },

    async updateAnnouncement(id: string, data: { title?: string; content?: string; type?: string; pinned?: boolean }): Promise<void> {
        await apiFetch(`/api/admin/announcements/${id}`, {
            method: 'PATCH',
            body: JSON.stringify(data),
        });
    },

    async deleteAnnouncement(id: string): Promise<void> {
        await apiFetch(`/api/admin/announcements/${id}`, { method: 'DELETE' });
    },

    async getLogs(params?: { page?: number; limit?: number }): Promise<{ logs: any[]; total: number }> {
        const query = new URLSearchParams();
        if (params?.page) query.set('page', params.page.toString());
        if (params?.limit) query.set('limit', params.limit.toString());
        return apiFetch(`/api/admin/logs?${query}`);
    },

    async getStats(): Promise<{ total_users: number; active_licenses: number; total_lifetime: number; total_monthly: number }> {
        return apiFetch('/api/admin/stats');
    },
};
