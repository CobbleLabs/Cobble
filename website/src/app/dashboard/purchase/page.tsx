"use client";

import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { Badge } from "@/components/ui/Badge";
import { useAuth } from "@/context/AuthContext";
import { Crown, Zap, Shield, Cloud, Headphones, Sparkles, Bitcoin } from "lucide-react";
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

// Coinbase Commerce checkout URLs
const COINBASE_MONTHLY_URL = "https://commerce.coinbase.com/checkout/6c10e1ba-b95e-490d-b2f8-0d216ccbac8f";
const COINBASE_LIFETIME_URL = "https://commerce.coinbase.com/checkout/3aea7796-3498-4b95-a1fa-3e9877a82230";

export default function PurchasePage() {
    const { user, isAuthenticated, isLoading } = useAuth();

    const hasLicense = user?.license && user.license.plan !== "none";
    const hasMonthly = user?.license?.plan === "monthly";
    const hasLifetime = user?.license?.plan === "lifetime";

    // Must be authenticated to purchase
    if (!isLoading && !isAuthenticated) {
        return (
            <motion.div
                initial={{ opacity: 0, y: 20 }}
                animate={{ opacity: 1, y: 0 }}
                className="max-w-2xl mx-auto text-center py-12"
            >
                <div className="w-20 h-20 rounded-full bg-[var(--accent-primary)]/10 flex items-center justify-center mx-auto mb-6">
                    <Shield className="w-10 h-10 text-[var(--accent-primary)]" />
                </div>
                <Heading className="text-3xl mb-4">Login Required</Heading>
                <Text className="text-lg mb-6">
                    Please log in to purchase a license.
                </Text>
                <a
                    href="/login"
                    className="inline-flex items-center gap-2 px-6 py-3 rounded-xl bg-[var(--accent-primary)] text-white font-semibold hover:bg-[var(--accent-primary)]/90 transition-colors"
                >
                    Go to Login
                </a>
            </motion.div>
        );
    }

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

    const handleCheckout = (url: string) => {
        // Open Coinbase checkout in new tab
        window.open(url, "_blank");
    };

    return (
        <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            className="max-w-5xl mx-auto"
        >
            {/* Header */}
            <div className="text-center mb-12">
                <Heading className="text-4xl mb-4">Choose Your Plan</Heading>
                <Text className="text-lg">Unlock the full power of Cobble with crypto.</Text>
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

                            {/* Crypto Button */}
                            <button
                                onClick={() => handleCheckout(COINBASE_MONTHLY_URL)}
                                className="w-full py-4 px-6 rounded-xl bg-gradient-to-r from-orange-500 to-yellow-500 text-white font-bold flex items-center justify-center gap-3 hover:from-orange-600 hover:to-yellow-600 transition-all shadow-lg shadow-orange-500/20"
                            >
                                <Bitcoin className="w-5 h-5" />
                                Pay with Crypto
                            </button>
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

                        {/* Crypto Button */}
                        <button
                            onClick={() => handleCheckout(COINBASE_LIFETIME_URL)}
                            className="w-full py-4 px-6 rounded-xl bg-gradient-to-r from-orange-500 to-yellow-500 text-white font-bold flex items-center justify-center gap-3 hover:from-orange-600 hover:to-yellow-600 transition-all shadow-lg shadow-orange-500/20"
                        >
                            <Bitcoin className="w-5 h-5" />
                            Pay with Crypto
                        </button>
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
                        <Bitcoin className="w-5 h-5 text-orange-500" />
                        <span className="font-semibold text-white">Crypto Payments</span>
                    </div>
                    <Text className="text-sm text-[var(--text-muted)]">
                        We accept Bitcoin, Ethereum, USDC, and other cryptocurrencies via Coinbase Commerce.
                        Your license will be activated automatically after payment confirmation.
                    </Text>
                </div>
            </motion.div>
        </motion.div>
    );
}
