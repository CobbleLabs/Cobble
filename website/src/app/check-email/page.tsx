"use client";

import { Suspense, useState } from "react";
import { useRouter, useSearchParams } from "next/navigation";
import { motion } from "framer-motion";
import { Mail, ArrowRight, Loader2 } from "lucide-react";
import { Button } from "@/components/ui/Button";

const API_URL = process.env.NEXT_PUBLIC_API_URL || "https://cobble.vip";

function CheckEmailContent() {
    const router = useRouter();
    const searchParams = useSearchParams();
    const email = searchParams.get("email") || "";

    const [isResending, setIsResending] = useState(false);
    const [resendMessage, setResendMessage] = useState("");

    const handleResend = async () => {
        if (!email) return;
        setIsResending(true);
        setResendMessage("");

        try {
            const response = await fetch(`${API_URL}/api/auth/resend-verification`, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ email }),
            });
            const data = await response.json();
            setResendMessage(data.data?.message || "Verification email sent!");
        } catch {
            setResendMessage("Failed to resend. Please try again.");
        } finally {
            setIsResending(false);
        }
    };

    return (
        <motion.div
            initial={{ opacity: 0, y: 20, filter: "blur(4px)" }}
            animate={{ opacity: 1, y: 0, filter: "blur(0px)" }}
            transition={{ duration: 0.6, ease: [0.16, 1, 0.3, 1] }}
            className="w-full max-w-md"
        >
            <div className="bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-2xl p-8 text-center">
                <div className="w-16 h-16 bg-[var(--accent-primary)]/10 rounded-full flex items-center justify-center mx-auto mb-6">
                    <Mail className="w-8 h-8 text-[var(--accent-primary)]" />
                </div>

                <h1 className="text-2xl font-bold text-white mb-2">Check your email</h1>
                <p className="text-gray-400 mb-6">
                    We sent a verification link to{" "}
                    {email ? <span className="text-white font-medium">{email}</span> : "your email"}.
                    Click the link to verify your account.
                </p>

                <div className="space-y-3">
                    <Button
                        onClick={() => router.push("/login")}
                        variant="primary"
                        className="w-full flex items-center justify-center gap-2"
                    >
                        Continue to Login <ArrowRight className="w-4 h-4" />
                    </Button>

                    <button
                        onClick={handleResend}
                        disabled={isResending || !email}
                        className="text-gray-400 hover:text-white text-sm transition-colors disabled:opacity-50"
                    >
                        {isResending ? "Sending..." : "Didn't receive it? Resend email"}
                    </button>

                    {resendMessage && (
                        <p className="text-sm text-[var(--accent-primary)]">{resendMessage}</p>
                    )}
                </div>
            </div>
        </motion.div>
    );
}

export default function CheckEmailPage() {
    return (
        <div className="min-h-screen flex items-center justify-center bg-[var(--bg-primary)] p-4">
            <Suspense fallback={
                <div className="flex items-center justify-center">
                    <Loader2 className="w-8 h-8 text-[var(--accent-primary)] animate-spin" />
                </div>
            }>
                <CheckEmailContent />
            </Suspense>
        </div>
    );
}
