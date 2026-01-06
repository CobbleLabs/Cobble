"use client";

import React, { createContext, useContext, useState, useEffect, useCallback, ReactNode } from "react";
import { useRouter, usePathname } from "next/navigation";
import * as api from "@/lib/api";

interface AuthContextType {
    user: api.UserWithLicense | null;
    isLoading: boolean;
    isAuthenticated: boolean;
    login: (email: string, password: string) => Promise<void>;
    register: (username: string, email: string, password: string) => Promise<void>;
    logout: () => Promise<void>;
    refreshUser: () => Promise<void>;
}

const AuthContext = createContext<AuthContextType | undefined>(undefined);

// Routes that don't require authentication
const PUBLIC_ROUTES = ['/login', '/register', '/', '/pricing', '/features'];

export function AuthProvider({ children }: { children: ReactNode }) {
    const [user, setUser] = useState<api.UserWithLicense | null>(null);
    const [isLoading, setIsLoading] = useState(true);
    const router = useRouter();
    const pathname = usePathname();

    const isAuthenticated = !!user;

    // Try to restore session on mount
    useEffect(() => {
        const initAuth = async () => {
            try {
                // First try to use existing token from localStorage
                const existingToken = api.getAccessToken();
                if (existingToken) {
                    try {
                        const userData = await api.getMe();
                        setUser(userData);
                        setIsLoading(false);
                        return;
                    } catch {
                        // Token invalid, try refresh below
                    }
                }

                // Fallback: Try to refresh token using HttpOnly cookie
                const refreshed = await api.refreshToken();
                if (refreshed) {
                    const userData = await api.getMe();
                    setUser(userData);
                }
            } catch {
                // No valid session
                api.clearAuth();
            } finally {
                setIsLoading(false);
            }
        };

        initAuth();
    }, []);

    // Redirect logic for protected routes
    useEffect(() => {
        if (isLoading) return;

        const isPublicRoute = PUBLIC_ROUTES.some(route =>
            pathname === route || pathname.startsWith('/(landing)')
        );
        const isDashboardRoute = pathname.startsWith('/dashboard');

        if (!isAuthenticated && isDashboardRoute) {
            router.replace('/login');
        }
    }, [isLoading, isAuthenticated, pathname, router]);

    const login = useCallback(async (email: string, password: string) => {
        const response = await api.login(email, password);
        // Login already returns user data, use it directly
        // Then fetch full profile with license info
        if (response.user) {
            // Set basic user first
            setUser(response.user as api.UserWithLicense);
            // Then try to get full data with license
            try {
                const fullUser = await api.getMe();
                setUser(fullUser);
            } catch {
                // If getMe fails, we still have basic user data
                console.log('Could not fetch full user data, using login response');
            }
        }
    }, []);

    const register = useCallback(async (username: string, email: string, password: string) => {
        await api.register(username, email, password);
        // Don't auto-login, redirect to login page
    }, []);

    const logout = useCallback(async () => {
        await api.logout();
        setUser(null);
        router.replace('/login');
    }, [router]);

    const refreshUser = useCallback(async () => {
        try {
            const userData = await api.getMe();
            setUser(userData);
        } catch {
            setUser(null);
        }
    }, []);

    return (
        <AuthContext.Provider value={{
            user,
            isLoading,
            isAuthenticated,
            login,
            register,
            logout,
            refreshUser,
        }}>
            {children}
        </AuthContext.Provider>
    );
}

export function useAuth() {
    const context = useContext(AuthContext);
    if (!context) {
        throw new Error("useAuth must be used within an AuthProvider");
    }
    return context;
}

// Higher-order component for protected pages
export function withAuth<P extends object>(Component: React.ComponentType<P>) {
    return function ProtectedComponent(props: P) {
        const { isLoading, isAuthenticated } = useAuth();
        const router = useRouter();

        useEffect(() => {
            if (!isLoading && !isAuthenticated) {
                router.replace('/login');
            }
        }, [isLoading, isAuthenticated, router]);

        if (isLoading) {
            return (
                <div className="min-h-screen flex items-center justify-center bg-[#0A0A0A]">
                    <div className="w-8 h-8 border-2 border-[var(--accent-primary)] border-t-transparent rounded-full animate-spin" />
                </div>
            );
        }

        if (!isAuthenticated) {
            return null;
        }

        return <Component {...props} />;
    };
}

// Hook for requiring admin role
export function useRequireAdmin() {
    const { user, isLoading } = useAuth();
    const router = useRouter();

    useEffect(() => {
        if (!isLoading && user?.role !== 'admin') {
            router.replace('/dashboard');
        }
    }, [isLoading, user, router]);

    return { isAdmin: user?.role === 'admin', isLoading };
}
