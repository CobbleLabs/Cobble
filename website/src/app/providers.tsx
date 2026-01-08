"use client";

import { ToastProvider } from "@/context/ToastContext";
import { AuthProvider } from "@/context/AuthContext";
import { PayPalScriptProvider } from "@paypal/react-paypal-js";

// Use environment variable for PayPal Client ID
// Sandbox vs Live is determined by which client ID you use
const PAYPAL_CLIENT_ID = process.env.NEXT_PUBLIC_PAYPAL_CLIENT_ID || "";

export function Providers({ children }: { children: React.ReactNode }) {
    return (
        <AuthProvider>
            <ToastProvider>
                <PayPalScriptProvider options={{
                    clientId: PAYPAL_CLIENT_ID,
                    currency: "USD",
                    intent: "capture",
                }}>
                    {children}
                </PayPalScriptProvider>
            </ToastProvider>
        </AuthProvider>
    );
}
