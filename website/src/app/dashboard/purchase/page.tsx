"use client";

import { useState } from "react";
import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { Badge } from "@/components/ui/Badge";
import { PayPalCheckoutButton } from "@/components/ui/PayPalButton";
import { useAuth } from "@/context/AuthContext";
import { activatePayment } from "@/lib/api";
import { Crown, Zap, Shield, Cloud, Headphones, Sparkles, Check, Loader2 } from "lucide-react";
import clsx from "clsx";

const MONTHLY_FEATURES = [
    { icon: Zap, text: "Full Module Access" },
    { icon: Headphones, text: "24/7 Support" },
    { icon: Cloud, text: "Cloud Configs" },
    { icon: Shield, text: "HWID Resets" },
];

const LIFETIME_FEATURES = [
    { icon: Zap, text: "Full Module Access" },
    { icon: Crown, text: "Priority Support" },
    { icon: Cloud, text: "Cloud Configs" },
    { icon: Shield, text: "Unlimited HWID Resets" },
    { icon: Sparkles, text: "Early Access Beta" },
];

type PaymentState = "idle" | "processing" | "success" | "error";

export default function PurchasePage() {
    const { user, refreshUser } = useAuth();
    const [paymentState, setPaymentState] = useState<PaymentState>("idle");
    const [activatingPlan, setActivatingPlan] = useState<string | null>(null);

    const hasLicense = user?.license && user.license.plan !== "none";
    const hasMonthly = user?.license?.plan === "monthly";
    const hasLifetime = user?.license?.plan === "lifetime";

    const handlePaymentSuccess = async (orderId: string, planType: "monthly" | "lifetime") => {
        setPaymentState("processing");
        setActivatingPlan(planType);

        try {
            // Call backend to verify and activate license
            await activatePayment(orderId, planType);

            setPaymentState("success");
            // Refresh user to get updated license
            await refreshUser();
        } catch (error) {
            console.error("Activation error:", error);
            setPaymentState("error");
        } finally {
            setActivatingPlan(null);
        }
    };

    // Already has lifetime - show message
    if (hasLifetime) {
        return (
            <motion.div
                initial={{ opacity: 0, y: 20 }}
                animate={{ opacity: 1, y: 0 }}
                className="max-w-2xl mx-auto text-center py-12"
            >
                <div className="w-20 h-20 rounded-full bg-[var(--accent-primary)]/10 flex items-center justify-center mx-auto mb-6">
                    <Crown className="w-10 h-10 text-[var(--accent-primary)]" />
                </div>
                <Heading className="text-3xl mb-4">You're All Set!</Heading>
                <Text className="text-lg mb-2">
                    You already have <span className="text-[var(--accent-primary)] font-semibold">Lifetime Access</span>.
                </Text>
                <Text className="text-[var(--text-muted)]">
                    Thank you for your support! Enjoy all features forever.
                </Text>
            </motion.div>
        );
    }

    // Payment success state
    if (paymentState === "success") {
        return (
            <motion.div
                initial={{ opacity: 0, scale: 0.95 }}
                animate={{ opacity: 1, scale: 1 }}
                className="max-w-2xl mx-auto text-center py-12"
            >
                <motion.div
                    initial={{ scale: 0 }}
                    animate={{ scale: 1 }}
                    transition={{ type: "spring", delay: 0.2 }}
                    className="w-20 h-20 rounded-full bg-green-500/20 flex items-center justify-center mx-auto mb-6"
                >
                    <Check className="w-10 h-10 text-green-500" />
                </motion.div>
                <Heading className="text-3xl mb-4">Payment Successful!</Heading>
                <Text className="text-lg mb-2">
                    Your license has been <span className="text-green-400 font-semibold">activated</span>.
                </Text>
                <Text className="text-[var(--text-muted)]">
                    You can now download Cobble from the Downloads page.
                </Text>
            </motion.div>
        );
    }

    return (
        <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            className="max-w-5xl mx-auto"
        >
            {/* Header */}
            <div className="text-center mb-12">
                <Heading className="text-4xl mb-4">Choose Your Plan</Heading>
                <Text className="text-lg">Unlock the full power of Cobble today.</Text>
            </div>

            {/* Pricing Cards Grid */}
            <div className={clsx(
                "grid gap-8",
                hasMonthly ? "grid-cols-1 max-w-lg mx-auto" : "grid-cols-1 md:grid-cols-2"
            )}>
                {/* Monthly Card - Hide if user already has monthly */}
                {!hasMonthly && (
                    <motion.div
                        initial={{ opacity: 0, y: 30 }}
                        whileInView={{ opacity: 1, y: 0 }}
                        viewport={{ once: true }}
                        transition={{ duration: 0.5 }}
                    >
                        <Card variant="default" className="h-full flex flex-col hover:border-[var(--border-hover)] transition-all">
                            <div className="flex-1">
                                <Heading className="text-2xl mb-2">Monthly Access</Heading>
                                <Text className="text-sm mb-6 text-[var(--text-muted)]">
                                    Flexible access for short-term domination.
                                </Text>

                                <div className="flex items-baseline gap-1 mb-8">
                                    <span className="text-white text-5xl font-black">$10</span>
                                    <span className="text-[var(--text-muted)]">/mo</span>
                                </div>

                                {/* Features */}
                                <div className="space-y-3 mb-8">
                                    {MONTHLY_FEATURES.map((feature, i) => (
                                        <div key={i} className="flex items-center gap-3 text-gray-300">
                                            <div className="w-8 h-8 rounded-lg bg-[var(--bg-tertiary)] flex items-center justify-center flex-shrink-0">
                                                <feature.icon className="w-4 h-4 text-[var(--text-muted)]" />
                                            </div>
                                            <span className="text-sm">{feature.text}</span>
                                        </div>
                                    ))}
                                </div>
                            </div>

                            {/* PayPal Button or Processing State */}
                            {activatingPlan === "monthly" ? (
                                <div className="flex items-center justify-center py-4 gap-2">
                                    <Loader2 className="w-5 h-5 animate-spin text-[var(--accent-primary)]" />
                                    <span className="text-sm text-[var(--text-muted)]">Activating license...</span>
                                </div>
                            ) : (
                                <PayPalCheckoutButton
                                    planType="monthly"
                                    onSuccess={(orderId) => handlePaymentSuccess(orderId, "monthly")}
                                    onError={(err) => console.error(err)}
                                    disabled={paymentState === "processing"}
                                />
                            )}
                        </Card>
                    </motion.div>
                )}

                {/* Lifetime Card */}
                <motion.div
                    initial={{ opacity: 0, y: 30 }}
                    whileInView={{ opacity: 1, y: 0 }}
                    viewport={{ once: true }}
                    transition={{ duration: 0.5, delay: hasMonthly ? 0 : 0.1 }}
                >
                    <Card
                        variant="premium"
                        className={clsx(
                            "h-full flex flex-col relative",
                            "border-[var(--accent-primary)]/50 hover:border-[var(--accent-primary)] transition-all"
                        )}
                    >
                        {/* Popular Badge */}
                        <div className="absolute -top-4 left-1/2 -translate-x-1/2">
                            <Badge
                                variant="info"
                                className="bg-[var(--accent-primary)] text-white border-none shadow-lg shadow-[var(--accent-primary)]/30 px-4 py-1.5 tracking-widest"
                            >
                                {hasMonthly ? "UPGRADE" : "MOST POPULAR"}
                            </Badge>
                        </div>

                        <div className="flex-1 pt-4">
                            <Heading className="text-2xl mb-2">Lifetime Access</Heading>
                            <Text className="text-sm mb-6 text-[var(--text-muted)]">
                                One-time payment. Own it forever.
                            </Text>

                            <div className="flex items-baseline gap-1 mb-8">
                                <span className="text-white text-5xl font-black">$45</span>
                                <span className="text-[var(--text-muted)]">/lifetime</span>
                            </div>

                            {/* Features */}
                            <div className="space-y-3 mb-8">
                                {LIFETIME_FEATURES.map((feature, i) => (
                                    <div key={i} className="flex items-center gap-3 text-gray-300">
                                        <div className="w-8 h-8 rounded-lg bg-[var(--accent-primary)]/10 flex items-center justify-center flex-shrink-0">
                                            <feature.icon className="w-4 h-4 text-[var(--accent-primary)]" />
                                        </div>
                                        <span className="text-sm">{feature.text}</span>
                                    </div>
                                ))}
                            </div>
                        </div>

                        {/* PayPal Button or Processing State */}
                        {activatingPlan === "lifetime" ? (
                            <div className="flex items-center justify-center py-4 gap-2">
                                <Loader2 className="w-5 h-5 animate-spin text-[var(--accent-primary)]" />
                                <span className="text-sm text-[var(--text-muted)]">Activating license...</span>
                            </div>
                        ) : (
                            <PayPalCheckoutButton
                                planType="lifetime"
                                onSuccess={(orderId) => handlePaymentSuccess(orderId, "lifetime")}
                                onError={(err) => console.error(err)}
                                disabled={paymentState === "processing"}
                            />
                        )}
                    </Card>
                </motion.div>
            </div>

            {/* Info Section */}
            <motion.div
                initial={{ opacity: 0 }}
                whileInView={{ opacity: 1 }}
                viewport={{ once: true }}
                className="mt-16 text-center"
            >
                <div className="max-w-2xl mx-auto p-6 rounded-2xl bg-[var(--bg-secondary)] border border-[var(--border-color)]">
                    <div className="flex items-center justify-center gap-2 mb-3">
                        <Shield className="w-5 h-5 text-[var(--accent-primary)]" />
                        <span className="font-semibold text-white">Secure Purchase</span>
                    </div>
                    <Text className="text-sm text-[var(--text-muted)]">
                        All payments are processed securely through PayPal. Your license will be activated automatically after payment confirmation.
                    </Text>
                </div>
            </motion.div>
        </motion.div>
    );
}
