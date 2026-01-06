"use client";

import { useEffect, useState } from "react";
import { motion } from "framer-motion";
import { RefreshCcw, Monitor, ShieldAlert, Clock } from "lucide-react";
import { useToast } from "@/context/ToastContext";
import { Button } from "@/components/ui/Button";
import { Heading, Text } from "@/components/ui/Typography";
import { useAuth } from "@/context/AuthContext";
import * as api from "@/lib/api";

export default function HWIDPage() {
    const { showToast } = useToast();
    const { user, refreshUser } = useAuth();
    const [loading, setLoading] = useState(false);
    const [hwidStatus, setHwidStatus] = useState<api.HWIDStatus | null>(null);
    const [fetchingStatus, setFetchingStatus] = useState(true);

    useEffect(() => {
        async function fetchHWID() {
            try {
                const status = await api.getHWIDStatus();
                setHwidStatus(status);
            } catch (error) {
                console.error("Failed to fetch HWID status:", error);
            } finally {
                setFetchingStatus(false);
            }
        }
        fetchHWID();
    }, []);

    const handleReset = async () => {
        if (!hwidStatus?.can_reset) {
            showToast({ title: "Cannot Reset", message: "HWID reset not available yet", type: "error" });
            return;
        }

        setLoading(true);
        try {
            await api.resetHWID();
            await refreshUser();
            const newStatus = await api.getHWIDStatus();
            setHwidStatus(newStatus);
            showToast({ title: "HWID Reset", message: "Your Hardware ID has been reset.", type: "success" });
        } catch (error) {
            const message = error instanceof Error ? error.message : "Failed to reset HWID";
            showToast({ title: "Error", message, type: "error" });
        } finally {
            setLoading(false);
        }
    };

    // Format HWID for display (show partial)
    const displayHWID = hwidStatus?.hwid
        ? `${hwidStatus.hwid.slice(0, 8)}...${hwidStatus.hwid.slice(-8)}`
        : "Not Set";

    const resetAvailableText = hwidStatus?.can_reset
        ? "Reset available now"
        : hwidStatus?.next_reset_available
            ? `Available ${new Date(hwidStatus.next_reset_available).toLocaleDateString()}`
            : "No reset info";

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

                {fetchingStatus ? (
                    <div className="text-3xl font-medium text-[var(--text-secondary)] mb-8">Loading...</div>
                ) : (
                    <div className="text-3xl md:text-4xl font-black text-white tracking-tight mb-8 font-mono">
                        {displayHWID}
                    </div>
                )}

                <div className="flex justify-center gap-4">
                    <Button
                        onClick={handleReset}
                        loading={loading}
                        disabled={!hwidStatus?.can_reset}
                        icon={<RefreshCcw className="w-5 h-5" />}
                    >
                        {loading ? "Resetting..." : "Reset HWID"}
                    </Button>
                </div>

                <p className="text-[var(--text-muted)] text-sm mt-6 flex items-center justify-center gap-2">
                    <Clock className="w-4 h-4" />
                    {resetAvailableText}
                </p>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
                <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-2xl p-6">
                    <h3 className="font-bold text-white mb-4 flex items-center gap-2">
                        <ShieldAlert className="w-5 h-5 text-[var(--error)]" /> Rules
                    </h3>
                    <ul className="space-y-2 text-sm text-[var(--text-secondary)]">
                        <li>• Sharing your account is strictly prohibited.</li>
                        <li>• HWID resets are limited to once per 30 days.</li>
                        <li>• Spoofers may cause HWID mismatch issues.</li>
                    </ul>
                </div>

                <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-2xl p-6">
                    <h3 className="font-bold text-white mb-4">Status</h3>
                    <div className="space-y-4">
                        <div className="flex justify-between text-sm">
                            <span className="text-[var(--text-secondary)]">HWID Bound</span>
                            <span className={hwidStatus?.hwid ? "text-[var(--success)]" : "text-[var(--text-muted)]"}>
                                {hwidStatus?.hwid ? "Yes" : "Not yet"}
                            </span>
                        </div>
                        {hwidStatus?.last_reset && (
                            <div className="flex justify-between text-sm">
                                <span className="text-[var(--text-secondary)]">Last Reset</span>
                                <span className="text-[var(--text-muted)]">
                                    {new Date(hwidStatus.last_reset).toLocaleDateString()}
                                </span>
                            </div>
                        )}
                        <div className="flex justify-between text-sm">
                            <span className="text-[var(--text-secondary)]">Reset Available</span>
                            <span className={hwidStatus?.can_reset ? "text-[var(--success)]" : "text-[var(--warning)]"}>
                                {hwidStatus?.can_reset ? "Yes" : "No"}
                            </span>
                        </div>
                    </div>
                </div>
            </div>
        </motion.div>
    );
}
