"use client";

import { motion } from "framer-motion";
import { RefreshCcw, Monitor, ShieldAlert } from "lucide-react";
import { useState } from "react";

export default function HWIDPage() {
    const [loading, setLoading] = useState(false);

    const handleReset = () => {
        setLoading(true);
        setTimeout(() => setLoading(false), 2000);
    };

    return (
        <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            className="max-w-3xl mx-auto space-y-8"
        >
            <div>
                <h1 className="text-3xl font-bold font-heading text-white mb-2">HWID Manager</h1>
                <p className="text-gray-400">Manage your Hardware ID binding.</p>
            </div>

            <div className="bg-[#121212] border border-[#1E1E1E] rounded-3xl p-10 text-center relative overflow-hidden">
                <div className="absolute top-0 left-0 w-full h-1 bg-gradient-to-r from-transparent via-[#4A90E2] to-transparent opacity-50" />

                <div className="w-20 h-20 bg-[#1E1E1E] rounded-full mx-auto flex items-center justify-center mb-6">
                    <Monitor className="w-10 h-10 text-gray-400" />
                </div>

                <h2 className="text-gray-400 mb-2 font-mono text-sm uppercase tracking-widest">CURRENT HWID</h2>
                <div className="text-3xl md:text-5xl font-black text-white font-mono tracking-tighter mb-8 break-all">
                    A1B2-C3D4-E5F6-G7H8
                </div>

                <div className="flex justify-center gap-4">
                    <button
                        onClick={handleReset}
                        disabled={loading}
                        className="bg-[#4A90E2] hover:bg-[#357ABD] disabled:opacity-50 disabled:cursor-not-allowed text-white px-8 py-3 rounded-xl font-bold flex items-center gap-2 transition-all shadow-lg shadow-[#4A90E2]/20"
                    >
                        <RefreshCcw className={`w-5 h-5 ${loading ? "animate-spin" : ""}`} />
                        {loading ? "Resetting..." : "Reset HWID"}
                    </button>
                </div>

                <p className="text-gray-500 text-sm mt-6">
                    You have <span className="text-white font-bold">1</span> reset remaining this month.
                </p>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
                <div className="bg-[#121212] border border-[#1E1E1E] rounded-2xl p-6">
                    <h3 className="font-bold text-white mb-4 flex items-center gap-2">
                        <ShieldAlert className="w-5 h-5 text-red-500" /> Rules
                    </h3>
                    <ul className="space-y-2 text-sm text-gray-400">
                        <li>• Sharing your account is strictly prohibited.</li>
                        <li>• Resets are manually reviewed if flagged.</li>
                        <li>• Spoofers may cause HWID mismatch.</li>
                    </ul>
                </div>

                <div className="bg-[#121212] border border-[#1E1E1E] rounded-2xl p-6">
                    <h3 className="font-bold text-white mb-4">History</h3>
                    <div className="space-y-4">
                        <div className="flex justify-between text-sm">
                            <span className="text-gray-400">Jan 01, 2026</span>
                            <span className="text-green-500">Reset Approved</span>
                        </div>
                        <div className="flex justify-between text-sm">
                            <span className="text-gray-400">Dec 15, 2025</span>
                            <span className="text-green-500">Reset Approved</span>
                        </div>
                    </div>
                </div>
            </div>
        </motion.div>
    );
}
