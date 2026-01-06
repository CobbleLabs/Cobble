"use client";

import { motion } from "framer-motion";
import { RefreshCcw, Monitor, ShieldAlert } from "lucide-react";
import { useState } from "react";
import { useToast } from "@/context/ToastContext";
import { Button } from "@/components/ui/Button";
import { Heading, Text } from "@/components/ui/Typography";

export default function HWIDPage() {
    const { showToast } = useToast();
    const [loading, setLoading] = useState(false);

    const handleReset = () => {
        setLoading(true);
        setTimeout(() => {
            setLoading(false);
            showToast({ title: "HWID Reset Successful", message: "Your Hardware ID has been updated.", type: "success" });
        }, 2000);
    };

    return (
        <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            className="max-w-3xl mx-auto space-y-8"
        >
            <div>
                <Heading className="text-3xl mb-2">HWID Manager</Heading>
                <Text>Manage your Hardware ID binding.</Text>
            </div>

            <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-10 text-center relative overflow-hidden">
                <div className="absolute top-0 left-0 w-full h-1 bg-gradient-to-r from-transparent via-[var(--accent-primary)] to-transparent opacity-50" />

                <div className="w-20 h-20 bg-[var(--bg-secondary)] rounded-full mx-auto flex items-center justify-center mb-6">
                    <Monitor className="w-10 h-10 text-[var(--text-secondary)]" />
                </div>

                <h2 className="text-[var(--text-secondary)] mb-2 text-sm uppercase tracking-widest">CURRENT HWID</h2>
                <div className="text-3xl md:text-5xl font-black text-white tracking-tighter mb-8 break-all">
                    A1B2-C3D4-E5F6-G7H8
                </div>

                <div className="flex justify-center gap-4">
                    <Button
                        onClick={handleReset}
                        loading={loading}
                        icon={<RefreshCcw className="w-5 h-5" />}
                    >
                        {loading ? "Resetting..." : "Reset HWID"}
                    </Button>
                </div>

                <p className="text-[var(--text-muted)] text-sm mt-6">
                    You have <span className="text-white font-bold">1</span> reset remaining this month.
                </p>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
                <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-2xl p-6">
                    <h3 className="font-bold text-white mb-4 flex items-center gap-2">
                        <ShieldAlert className="w-5 h-5 text-[var(--error)]" /> Rules
                    </h3>
                    <ul className="space-y-2 text-sm text-[var(--text-secondary)]">
                        <li>• Sharing your account is strictly prohibited.</li>
                        <li>• Resets are manually reviewed if flagged.</li>
                        <li>• Spoofers may cause HWID mismatch.</li>
                    </ul>
                </div>

                <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-2xl p-6">
                    <h3 className="font-bold text-white mb-4">History</h3>
                    <div className="space-y-4">
                        <div className="flex justify-between text-sm">
                            <span className="text-[var(--text-secondary)]">Jan 01, 2026</span>
                            <span className="text-[var(--success)]">Reset Approved</span>
                        </div>
                        <div className="flex justify-between text-sm">
                            <span className="text-[var(--text-secondary)]">Dec 15, 2025</span>
                            <span className="text-[var(--success)]">Reset Approved</span>
                        </div>
                    </div>
                </div>
            </div>
        </motion.div>
    );
}
