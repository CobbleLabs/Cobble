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

export default function RegisterPage() {
    const { showToast } = useToast();
    const { register, isAuthenticated, isLoading: authLoading } = useAuth();
    const router = useRouter();
    const [formData, setFormData] = useState({
        username: "",
        email: "",
        password: "",
        confirmPassword: ""
    });
    const [isLoading, setIsLoading] = useState(false);

    // Redirect if already logged in (in useEffect to avoid setState during render)
    useEffect(() => {
        if (!authLoading && isAuthenticated) {
            router.replace("/dashboard");
        }
    }, [isAuthenticated, authLoading, router]);

    const handleSubmit = async (e: React.FormEvent) => {
        e.preventDefault();

        // Validation
        if (!formData.username || !formData.email || !formData.password) {
            showToast({ title: "Error", message: "Please fill in all fields", type: "error" });
            return;
        }

        if (formData.username.length < 3 || formData.username.length > 32) {
            showToast({ title: "Error", message: "Username must be 3-32 characters", type: "error" });
            return;
        }

        if (formData.password.length < 8) {
            showToast({ title: "Error", message: "Password must be at least 8 characters", type: "error" });
            return;
        }

        if (formData.password !== formData.confirmPassword) {
            showToast({ title: "Error", message: "Passwords do not match", type: "error" });
            return;
        }

        setIsLoading(true);
        try {
            await register(formData.username, formData.email, formData.password);
            showToast({
                title: "Account Created",
                message: "Please log in with your new account",
                type: "success"
            });
            router.push("/login");
        } catch (error) {
            const message = error instanceof Error ? error.message : "Registration failed";
            showToast({ title: "Registration Failed", message, type: "error" });
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
                    <Heading className="text-4xl mb-2">Register</Heading>
                    <Text>Create your Cobble account</Text>
                </div>

                <div className="bg-[var(--bg-secondary)] border border-[var(--border-color)] p-8 rounded-2xl shadow-2xl backdrop-blur-sm">
                    <form onSubmit={handleSubmit} className="space-y-4">
                        <Input
                            label="Username"
                            type="text"
                            value={formData.username}
                            onChange={(e) => setFormData({ ...formData, username: e.target.value })}
                            required
                            autoComplete="username"
                            minLength={3}
                            maxLength={32}
                        />

                        <Input
                            label="Email"
                            type="email"
                            value={formData.email}
                            onChange={(e) => setFormData({ ...formData, email: e.target.value })}
                            required
                            autoComplete="email"
                        />

                        <div className="grid grid-cols-2 gap-4">
                            <Input
                                label="Password"
                                type="password"
                                value={formData.password}
                                onChange={(e) => setFormData({ ...formData, password: e.target.value })}
                                required
                                autoComplete="new-password"
                                minLength={8}
                            />
                            <Input
                                label="Confirm"
                                type="password"
                                value={formData.confirmPassword}
                                onChange={(e) => setFormData({ ...formData, confirmPassword: e.target.value })}
                                required
                                autoComplete="new-password"
                            />
                        </div>

                        <Button
                            type="submit"
                            loading={isLoading}
                            className="w-full mt-4"
                            icon={<ArrowRight className="w-5 h-5" />}
                        >
                            Create Account
                        </Button>
                    </form>

                    <div className="mt-6 text-center text-sm text-[var(--text-muted)]">
                        Already have an account?{" "}
                        <Link href="/login" className="text-[var(--accent-primary)] hover:underline">
                            Sign in
                        </Link>
                    </div>
                </div>
            </motion.div>
        </div>
    );
}
