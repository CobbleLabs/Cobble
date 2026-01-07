"use client";

import { Suspense, useEffect, useState } from "react";
import { useSearchParams, useRouter } from "next/navigation";
import { motion } from "framer-motion";
import { CheckCircle, XCircle, Loader2 } from "lucide-react";
import { Button } from "@/components/ui/Button";

const API_URL = process.env.NEXT_PUBLIC_API_URL || "https://cobble.vip";

function VerifyEmailContent() {
    const searchParams = useSearchParams();
    const router = useRouter();
    const token = searchParams.get("token");

    const [status, setStatus] = useState<"loading" | "success" | "error">("loading");
    const [message, setMessage] = useState("");

    useEffect(() => {
        if (!token) {
            setStatus("error");
            setMessage("No verification token provided.");
            return;
        }

        const verifyEmail = async () => {
            try {
                const response = await fetch(`${API_URL}/api/auth/verify-email?token=${token}`, {
                    method: "GET",
                });
                const data = await response.json();

                if (response.ok) {
                    setStatus("success");
                    setMessage(data.data?.message || "Email verified successfully!");
                } else {
                    setStatus("error");
                    setMessage(data.error || "Verification failed.");
                }
            } catch {
                setStatus("error");
                setMessage("Failed to verify email. Please try again.");
            }
        };

        verifyEmail();
    }, [token]);

    return (
        <motion.div
            initial={{ opacity: 0, y: 20, filter: "blur(4px)" }}
            animate={{ opacity: 1, y: 0, filter: "blur(0px)" }}
            transition={{ duration: 0.6, ease: [0.16, 1, 0.3, 1] }}
            className="w-full max-w-md"
        >
            <div className="bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-2xl p-8 text-center">
                {status === "loading" && (
                    <>
                        <Loader2 className="w-16 h-16 text-[var(--accent-primary)] mx-auto mb-4 animate-spin" />
                        <h1 className="text-2xl font-bold text-white mb-2">Verifying your email...</h1>
                        <p className="text-gray-400">Please wait while we verify your email address.</p>
                    </>
                )}

                {status === "success" && (
                    <>
                        <CheckCircle className="w-16 h-16 text-green-500 mx-auto mb-4" />
                        <h1 className="text-2xl font-bold text-white mb-2">Email Verified!</h1>
                        <p className="text-gray-400 mb-6">{message}</p>
                        <Button onClick={() => router.push("/login")} variant="primary" className="w-full">
                            Continue to Login
                        </Button>
                    </>
                )}

                {status === "error" && (
                    <>
                        <XCircle className="w-16 h-16 text-red-500 mx-auto mb-4" />
                        <h1 className="text-2xl font-bold text-white mb-2">Verification Failed</h1>
                        <p className="text-gray-400 mb-6">{message}</p>
                        <div className="space-y-3">
                            <Button onClick={() => router.push("/login")} variant="primary" className="w-full">
                                Go to Login
                            </Button>
                            <Button onClick={() => router.push("/register")} variant="secondary" className="w-full">
                                Create New Account
                            </Button>
                        </div>
                    </>
                )}
            </div>
        </motion.div>
    );
}

export default function VerifyEmailPage() {
    return (
        <div className="min-h-screen flex items-center justify-center bg-[var(--bg-primary)] p-4">
            <Suspense fallback={
                <div className="flex items-center justify-center">
                    <Loader2 className="w-8 h-8 text-[var(--accent-primary)] animate-spin" />
                </div>
            }>
                <VerifyEmailContent />
            </Suspense>
        </div>
    );
}
