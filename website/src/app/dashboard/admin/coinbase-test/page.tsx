"use client";

import { useState } from "react";
import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { admin } from "@/lib/api";
import { Bitcoin, Check, AlertCircle, Loader2, User } from "lucide-react";
import clsx from "clsx";

export default function CoinbaseTestPage() {
    const [userId, setUserId] = useState("");
    const [plan, setPlan] = useState<"monthly" | "lifetime">("monthly");
    const [isLoading, setIsLoading] = useState(false);
    const [result, setResult] = useState<{ success: boolean; message: string; chargeCode?: string } | null>(null);

    const handleTest = async () => {
        if (!userId.trim()) {
            setResult({ success: false, message: "Please enter a User ID" });
            return;
        }

        setIsLoading(true);
        setResult(null);

        try {
            const response = await admin.testCoinbase(userId.trim(), plan);
            setResult({
                success: true,
                message: `License activated for ${response.data.user} (${response.data.plan})`,
                chargeCode: response.data.charge_code,
            });
        } catch (error) {
            setResult({
                success: false,
                message: error instanceof Error ? error.message : "Test failed",
            });
        } finally {
            setIsLoading(false);
        }
    };

    return (
        <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            className="max-w-2xl mx-auto"
        >
            <div className="mb-8">
                <Heading className="text-3xl mb-2">Coinbase Test</Heading>
                <Text className="text-[var(--text-muted)]">
                    Simulate a Coinbase payment to test license activation without real payment.
                </Text>
            </div>

            <Card variant="default" className="mb-6">
                <div className="space-y-6">
                    {/* User ID Input */}
                    <div>
                        <label className="block text-sm font-medium text-white mb-2">
                            User ID (UUID)
                        </label>
                        <div className="relative">
                            <User className="absolute left-3 top-1/2 -translate-y-1/2 w-5 h-5 text-[var(--text-muted)]" />
                            <input
                                type="text"
                                value={userId}
                                onChange={(e) => setUserId(e.target.value)}
                                placeholder="e.g., 123e4567-e89b-12d3-a456-426614174000"
                                className="w-full pl-10 pr-4 py-3 bg-[var(--bg-tertiary)] border border-[var(--border-color)] rounded-xl text-white placeholder:text-[var(--text-muted)] focus:outline-none focus:border-[var(--accent-primary)]"
                            />
                        </div>
                        <p className="text-xs text-[var(--text-muted)] mt-1">
                            Get User ID from the Users admin page
                        </p>
                    </div>

                    {/* Plan Selection */}
                    <div>
                        <label className="block text-sm font-medium text-white mb-2">
                            Plan
                        </label>
                        <div className="grid grid-cols-2 gap-3">
                            <button
                                onClick={() => setPlan("monthly")}
                                className={clsx(
                                    "py-3 px-4 rounded-xl border-2 transition-all font-medium",
                                    plan === "monthly"
                                        ? "border-[var(--accent-primary)] bg-[var(--accent-primary)]/10 text-[var(--accent-primary)]"
                                        : "border-[var(--border-color)] text-[var(--text-muted)] hover:border-[var(--border-hover)]"
                                )}
                            >
                                Monthly ($10)
                            </button>
                            <button
                                onClick={() => setPlan("lifetime")}
                                className={clsx(
                                    "py-3 px-4 rounded-xl border-2 transition-all font-medium",
                                    plan === "lifetime"
                                        ? "border-[var(--accent-primary)] bg-[var(--accent-primary)]/10 text-[var(--accent-primary)]"
                                        : "border-[var(--border-color)] text-[var(--text-muted)] hover:border-[var(--border-hover)]"
                                )}
                            >
                                Lifetime ($45)
                            </button>
                        </div>
                    </div>

                    {/* Test Button */}
                    <button
                        onClick={handleTest}
                        disabled={isLoading}
                        className="w-full py-4 px-6 rounded-xl bg-gradient-to-r from-orange-500 to-yellow-500 text-white font-bold flex items-center justify-center gap-3 hover:from-orange-600 hover:to-yellow-600 transition-all shadow-lg shadow-orange-500/20 disabled:opacity-50"
                    >
                        {isLoading ? (
                            <>
                                <Loader2 className="w-5 h-5 animate-spin" />
                                Processing...
                            </>
                        ) : (
                            <>
                                <Bitcoin className="w-5 h-5" />
                                Simulate Payment
                            </>
                        )}
                    </button>
                </div>
            </Card>

            {/* Result */}
            {result && (
                <motion.div
                    initial={{ opacity: 0, y: 10 }}
                    animate={{ opacity: 1, y: 0 }}
                >
                    <Card
                        variant="default"
                        className={clsx(
                            "border-2",
                            result.success ? "border-green-500/50" : "border-red-500/50"
                        )}
                    >
                        <div className="flex items-start gap-4">
                            <div className={clsx(
                                "w-10 h-10 rounded-full flex items-center justify-center flex-shrink-0",
                                result.success ? "bg-green-500/20" : "bg-red-500/20"
                            )}>
                                {result.success ? (
                                    <Check className="w-5 h-5 text-green-500" />
                                ) : (
                                    <AlertCircle className="w-5 h-5 text-red-500" />
                                )}
                            </div>
                            <div className="flex-1">
                                <Text className={clsx(
                                    "font-semibold",
                                    result.success ? "text-green-400" : "text-red-400"
                                )}>
                                    {result.success ? "Success" : "Error"}
                                </Text>
                                <Text className="text-[var(--text-muted)]">
                                    {result.message}
                                </Text>
                                {result.chargeCode && (
                                    <Text className="text-xs text-[var(--text-muted)] mt-2">
                                        Charge Code: <code className="text-white">{result.chargeCode}</code>
                                    </Text>
                                )}
                            </div>
                        </div>
                    </Card>
                </motion.div>
            )}

            {/* Warning */}
            <div className="mt-6 p-4 rounded-xl bg-yellow-500/10 border border-yellow-500/30">
                <div className="flex items-start gap-3">
                    <AlertCircle className="w-5 h-5 text-yellow-500 flex-shrink-0 mt-0.5" />
                    <div>
                        <Text className="text-yellow-400 font-medium">Testing Only</Text>
                        <Text className="text-sm text-[var(--text-muted)]">
                            This creates a real license without payment. Use only for testing purposes.
                            Test payments are marked with "TEST_" prefix in charge codes.
                        </Text>
                    </div>
                </div>
            </div>
        </motion.div>
    );
}
