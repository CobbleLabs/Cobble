"use client";

import { useState } from "react";
import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { PayPalButtons, usePayPalScriptReducer } from "@paypal/react-paypal-js";
import { useRequireAdmin } from "@/context/AuthContext";
import { Check, Loader2, AlertCircle, TestTube } from "lucide-react";

type TestStatus = "idle" | "processing" | "success" | "error";

export default function PayPalTestPage() {
    const { isAdmin, isLoading: authLoading } = useRequireAdmin();
    const [{ isPending, isRejected }] = usePayPalScriptReducer();
    const [status, setStatus] = useState<TestStatus>("idle");
    const [orderId, setOrderId] = useState<string | null>(null);
    const [errorMessage, setErrorMessage] = useState<string | null>(null);

    if (authLoading) {
        return (
            <div className="flex items-center justify-center py-12">
                <Loader2 className="w-8 h-8 animate-spin text-[var(--accent-primary)]" />
            </div>
        );
    }

    if (!isAdmin) {
        return null;
    }

    // Success state
    if (status === "success") {
        return (
            <motion.div
                initial={{ opacity: 0, scale: 0.95 }}
                animate={{ opacity: 1, scale: 1 }}
                className="max-w-xl mx-auto text-center py-12"
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
                    PayPal is working correctly in <span className="text-green-400 font-semibold">production mode</span>.
                </Text>
                <Text className="text-[var(--text-muted)] mb-6">
                    Order ID: <code className="bg-[var(--bg-tertiary)] px-2 py-1 rounded">{orderId}</code>
                </Text>
                <button
                    onClick={() => {
                        setStatus("idle");
                        setOrderId(null);
                    }}
                    className="px-6 py-3 rounded-xl bg-[var(--bg-tertiary)] text-white hover:bg-[var(--bg-secondary)] transition-colors"
                >
                    Test Again
                </button>
            </motion.div>
        );
    }

    return (
        <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            className="max-w-xl mx-auto"
        >
            {/* Header */}
            <div className="text-center mb-8">
                <div className="w-16 h-16 rounded-full bg-red-500/10 flex items-center justify-center mx-auto mb-4">
                    <TestTube className="w-8 h-8 text-red-500" />
                </div>
                <Heading className="text-3xl mb-2">PayPal Test</Heading>
                <Text className="text-[var(--text-muted)]">
                    Test $1.00 payment to verify PayPal production integration
                </Text>
            </div>

            {/* Warning */}
            <div className="bg-yellow-500/10 border border-yellow-500/30 rounded-xl p-4 mb-6">
                <div className="flex items-start gap-3">
                    <AlertCircle className="w-5 h-5 text-yellow-500 mt-0.5 flex-shrink-0" />
                    <div>
                        <Text className="text-yellow-400 font-semibold text-sm">Real Payment Warning</Text>
                        <Text className="text-yellow-400/80 text-sm">
                            This is a real $1.00 payment. It will be charged to the PayPal account used.
                            Use this only to verify production PayPal is working.
                        </Text>
                    </div>
                </div>
            </div>

            {/* PayPal Card */}
            <Card variant="default" className="p-6">
                <div className="text-center mb-6">
                    <div className="text-4xl font-black text-white mb-2">$1.00</div>
                    <Text className="text-sm text-[var(--text-muted)]">Test Payment</Text>
                </div>

                {/* Loading state */}
                {isPending && (
                    <div className="flex items-center justify-center py-4">
                        <Loader2 className="w-6 h-6 text-[var(--accent-primary)] animate-spin" />
                        <span className="ml-2 text-sm text-[var(--text-muted)]">Loading PayPal...</span>
                    </div>
                )}

                {/* Error state */}
                {isRejected && (
                    <div className="flex flex-col items-center justify-center py-4 gap-2">
                        <AlertCircle className="w-6 h-6 text-red-500" />
                        <span className="text-sm text-red-400">Failed to load PayPal SDK</span>
                        <button
                            onClick={() => window.location.reload()}
                            className="text-xs text-[var(--accent-primary)] hover:underline"
                        >
                            Try again
                        </button>
                    </div>
                )}

                {/* Processing state */}
                {status === "processing" && (
                    <div className="flex items-center justify-center py-4 gap-2">
                        <Loader2 className="w-5 h-5 animate-spin text-[var(--accent-primary)]" />
                        <span className="text-sm text-[var(--text-muted)]">Processing payment...</span>
                    </div>
                )}

                {/* Error message */}
                {status === "error" && errorMessage && (
                    <div className="bg-red-500/10 border border-red-500/30 rounded-lg p-3 mb-4">
                        <Text className="text-red-400 text-sm">{errorMessage}</Text>
                    </div>
                )}

                {/* PayPal Button */}
                {!isPending && !isRejected && status !== "processing" && (
                    <motion.div
                        initial={{ opacity: 0 }}
                        animate={{ opacity: 1 }}
                        className="w-full rounded-xl overflow-hidden"
                    >
                        <PayPalButtons
                            style={{
                                layout: "vertical",
                                color: "gold",
                                shape: "rect",
                                label: "pay",
                                height: 45,
                            }}
                            createOrder={(_data, actions) => {
                                return actions.order.create({
                                    intent: "CAPTURE",
                                    purchase_units: [
                                        {
                                            description: "Cobble PayPal Test Payment",
                                            amount: {
                                                currency_code: "USD",
                                                value: "1.00",
                                            },
                                            custom_id: "test_payment",
                                        },
                                    ],
                                });
                            }}
                            onApprove={async (data, actions) => {
                                if (!actions.order) return;
                                setStatus("processing");

                                try {
                                    const details = await actions.order.capture();
                                    console.log("Test payment captured:", details);
                                    setOrderId(data.orderID);
                                    setStatus("success");
                                } catch (err) {
                                    console.error("Capture error:", err);
                                    setErrorMessage("Payment capture failed");
                                    setStatus("error");
                                }
                            }}
                            onError={(err) => {
                                console.error("PayPal error:", err);
                                setErrorMessage("Payment failed. Check console for details.");
                                setStatus("error");
                            }}
                            onCancel={() => {
                                console.log("Test payment cancelled");
                                setStatus("idle");
                            }}
                        />
                    </motion.div>
                )}
            </Card>

            {/* Info */}
            <div className="mt-6 text-center">
                <Text className="text-xs text-[var(--text-muted)]">
                    This payment does NOT create a license. It's only to verify PayPal connectivity.
                </Text>
            </div>
        </motion.div>
    );
}
