"use client";

import Link from "next/link";
import { useRouter } from "next/navigation";
import { useState, useEffect } from "react";
import { useToast } from "@/context/ToastContext";
import { useAuth } from "@/context/AuthContext";
import { ArrowRight } from "lucide-react";
import { AuthNavbar } from "@/components/ui/AuthNavbar";
import { Button } from "@/components/ui/Button";
import { Heading, Text } from "@/components/ui/Typography";
import { Input } from "@/components/ui/Input";
import { motion } from "framer-motion";

export default function LoginPage() {
    const [identifier, setIdentifier] = useState("");
    const [password, setPassword] = useState("");
    const [isLoading, setIsLoading] = useState(false);
    const { showToast } = useToast();
    const { login, isAuthenticated, isLoading: authLoading } = useAuth();
    const router = useRouter();

    // Redirect if already logged in (in useEffect to avoid setState during render)
    useEffect(() => {
        if (!authLoading && isAuthenticated) {
            router.replace("/dashboard");
        }
    }, [isAuthenticated, authLoading, router]);

    const handleSubmit = async (e: React.FormEvent) => {
        e.preventDefault();

        if (!identifier || !password) {
            showToast({ title: "Error", message: "Please fill in all fields", type: "error" });
            return;
        }

        setIsLoading(true);
        try {
            await login(identifier, password);
            showToast({ title: "Welcome back!", message: "Successfully logged in.", type: "success" });
            router.push("/dashboard");
        } catch (error) {
            const message = error instanceof Error ? error.message : "Login failed";
            showToast({ title: "Login Failed", message, type: "error" });
        } finally {
            setIsLoading(false);
        }
    };

    return (
        <div className="min-h-screen flex items-center justify-center relative bg-[#0A0A0A] overflow-hidden">
            <AuthNavbar />

            {/* Background Gradients */}
            <div className="absolute top-0 left-1/2 -translate-x-1/2 w-[800px] h-[600px] bg-[var(--accent-primary)] opacity-10 blur-[120px] rounded-full pointer-events-none" />
            <div className="absolute bottom-0 right-0 w-[600px] h-[600px] bg-purple-600 opacity-10 blur-[120px] rounded-full pointer-events-none" />

            <motion.div
                initial={{ opacity: 0, y: 20, filter: "blur(4px)" }}
                animate={{ opacity: 1, y: 0, filter: "blur(0px)" }}
                transition={{ duration: 0.6, ease: [0.22, 1, 0.36, 1] }}
                className="w-full max-w-md relative z-10 px-6"
            >
                <div className="text-center mb-8">
                    <Heading className="text-4xl mb-2">Login</Heading>
                    <Text>Welcome back to Cobble</Text>
                </div>

                <div className="bg-[var(--bg-secondary)] border border-[var(--border-color)] p-8 rounded-2xl shadow-2xl backdrop-blur-sm">
                    <form onSubmit={handleSubmit} className="space-y-6">
                        <Input
                            label="Username or Email"
                            type="text"
                            value={identifier}
                            onChange={(e) => setIdentifier(e.target.value)}
                            placeholder="Username or Email"
                            required
                            autoComplete="username"
                        />

                        <Input
                            label="Password"
                            type="password"
                            value={password}
                            onChange={(e) => setPassword(e.target.value)}
                            placeholder="••••••••"
                            required
                            autoComplete="current-password"
                        />

                        <Button
                            type="submit"
                            loading={isLoading}
                            className="w-full"
                            icon={<ArrowRight className="w-5 h-5" />}
                        >
                            Sign In
                        </Button>
                    </form>

                    <div className="mt-6 text-center text-sm text-[var(--text-muted)]">
                        Don&apos;t have an account?{" "}
                        <Link href="/register" className="text-[var(--accent-primary)] hover:underline">
                            Sign up
                        </Link>
                    </div>
                </div>
            </motion.div>
        </div>
    );
}
