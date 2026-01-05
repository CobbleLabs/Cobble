"use client";

import Link from "next/link";
import { useState } from "react";
import { Loader2, ArrowRight } from "lucide-react";

export default function LoginPage() {
    const [email, setEmail] = useState("");
    const [password, setPassword] = useState("");
    const [isLoading, setIsLoading] = useState(false);

    const handleSubmit = async (e: React.FormEvent) => {
        e.preventDefault();
        setIsLoading(true);
        // TODO: API
        setTimeout(() => {
            setIsLoading(false);
            window.location.href = "/dashboard";
        }, 1000);
    };

    return (
        <div className="min-h-screen flex items-center justify-center relative bg-[#121212]">
            {/* Blue Header Background */}
            <div className="absolute top-0 left-0 w-full h-[50vh] bg-[#4A90E2] z-0" />

            <div className="w-full max-w-md relative z-10 px-6">
                <div className="text-center mb-8">
                    <h1 className="text-4xl font-bold text-white mb-2">Login</h1>
                    <p className="text-white/80">Welcome back to Cobble</p>
                </div>

                <div className="bg-[#1E1E1E] p-8 rounded shadow-2xl">
                    <form onSubmit={handleSubmit} className="space-y-6">
                        <div>
                            <label className="block text-sm font-medium text-gray-400 mb-2">Email Address</label>
                            <input
                                type="email"
                                value={email}
                                onChange={(e) => setEmail(e.target.value)}
                                className="input"
                                placeholder="name@example.com"
                                required
                            />
                        </div>

                        <div>
                            <label className="block text-sm font-medium text-gray-400 mb-2">Password</label>
                            <input
                                type="password"
                                value={password}
                                onChange={(e) => setPassword(e.target.value)}
                                className="input"
                                placeholder="••••••••"
                                required
                            />
                        </div>

                        <button
                            type="submit"
                            disabled={isLoading}
                            className="btn btn-primary w-full"
                        >
                            {isLoading ? <Loader2 className="animate-spin" /> : "Sign In"}
                        </button>
                    </form>

                    <div className="mt-6 text-center text-sm text-gray-500">
                        Don&apos;t have an account?{" "}
                        <Link href="/register" className="text-[#4A90E2] hover:underline">
                            Sign up
                        </Link>
                    </div>
                </div>
            </div>
        </div>
    );
}
