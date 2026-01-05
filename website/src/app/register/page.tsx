"use client";

import Link from "next/link";
import { useState } from "react";
import { Loader2 } from "lucide-react";

export default function RegisterPage() {
    const [formData, setFormData] = useState({
        username: "",
        email: "",
        password: "",
        confirmPassword: "",
        license: ""
    });
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
                    <h1 className="text-4xl font-bold text-white mb-2">Register</h1>
                    <p className="text-white/80">Create your Cobble account</p>
                </div>

                <div className="bg-[#1E1E1E] p-8 rounded shadow-2xl">
                    <form onSubmit={handleSubmit} className="space-y-4">
                        <div>
                            <label className="block text-sm font-medium text-gray-400 mb-1">License Key</label>
                            <input
                                type="text"
                                value={formData.license}
                                onChange={(e) => setFormData({ ...formData, license: e.target.value })}
                                className="input font-mono text-center tracking-widest"
                                placeholder="XXXX-XXXX-XXXX"
                                required
                            />
                        </div>

                        <div>
                            <label className="block text-sm font-medium text-gray-400 mb-1">Username</label>
                            <input
                                type="text"
                                value={formData.username}
                                onChange={(e) => setFormData({ ...formData, username: e.target.value })}
                                className="input"
                                required
                            />
                        </div>

                        <div>
                            <label className="block text-sm font-medium text-gray-400 mb-1">Email</label>
                            <input
                                type="email"
                                value={formData.email}
                                onChange={(e) => setFormData({ ...formData, email: e.target.value })}
                                className="input"
                                required
                            />
                        </div>

                        <div className="grid grid-cols-2 gap-4">
                            <div>
                                <label className="block text-sm font-medium text-gray-400 mb-1">Password</label>
                                <input
                                    type="password"
                                    value={formData.password}
                                    onChange={(e) => setFormData({ ...formData, password: e.target.value })}
                                    className="input"
                                    required
                                />
                            </div>
                            <div>
                                <label className="block text-sm font-medium text-gray-400 mb-1">Confirm</label>
                                <input
                                    type="password"
                                    value={formData.confirmPassword}
                                    onChange={(e) => setFormData({ ...formData, confirmPassword: e.target.value })}
                                    className="input"
                                    required
                                />
                            </div>
                        </div>

                        <button
                            type="submit"
                            disabled={isLoading}
                            className="btn btn-primary w-full mt-4"
                        >
                            {isLoading ? <Loader2 className="animate-spin" /> : "Create Account"}
                        </button>
                    </form>

                    <div className="mt-6 text-center text-sm text-gray-500">
                        Already have an account?{" "}
                        <Link href="/login" className="text-[#4A90E2] hover:underline">
                            Sign in
                        </Link>
                    </div>
                </div>
            </div>
        </div>
    );
}
