"use client";

import { PayPalButtons, usePayPalScriptReducer } from "@paypal/react-paypal-js";
import { motion } from "framer-motion";
import { Loader2, AlertCircle } from "lucide-react";
import clsx from "clsx";

interface PayPalCheckoutButtonProps {
    planType: "monthly" | "lifetime";
    onSuccess: (orderId: string) => void;
    onError?: (error: string) => void;
    disabled?: boolean;
    className?: string;
}

const PLAN_CONFIG = {
    monthly: {
        amount: "10.00",
        description: "Cobble Monthly Access",
    },
    lifetime: {
        amount: "45.00",
        description: "Cobble Lifetime Access",
    },
} as const;

export function PayPalCheckoutButton({
    planType,
    onSuccess,
    onError,
    disabled = false,
    className,
}: PayPalCheckoutButtonProps) {
    const [{ isPending, isRejected }] = usePayPalScriptReducer();
    const plan = PLAN_CONFIG[planType];

    // Loading state
    if (isPending) {
        return (
            <div className={clsx("flex items-center justify-center py-4", className)}>
                <Loader2 className="w-6 h-6 text-[var(--accent-primary)] animate-spin" />
                <span className="ml-2 text-sm text-[var(--text-muted)]">Loading PayPal...</span>
            </div>
        );
    }

    // Error state
    if (isRejected) {
        return (
            <div className={clsx("flex flex-col items-center justify-center py-4 gap-2", className)}>
                <AlertCircle className="w-6 h-6 text-red-500" />
                <span className="text-sm text-red-400">Failed to load PayPal</span>
                <button
                    onClick={() => window.location.reload()}
                    className="text-xs text-[var(--accent-primary)] hover:underline"
                >
                    Try again
                </button>
            </div>
        );
    }

    return (
        <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            className={clsx(
                "w-full rounded-xl overflow-hidden",
                disabled && "opacity-50 pointer-events-none",
                className
            )}
        >
            <PayPalButtons
                style={{
                    layout: "vertical",
                    color: "gold",
                    shape: "rect",
                    label: "pay",
                    height: 45,
                }}
                disabled={disabled}
                createOrder={(_data, actions) => {
                    return actions.order.create({
                        intent: "CAPTURE",
                        purchase_units: [
                            {
                                description: plan.description,
                                amount: {
                                    currency_code: "USD",
                                    value: plan.amount,
                                },
                                custom_id: planType, // Used by backend to identify plan
                            },
                        ],
                    });
                }}
                onApprove={async (data, actions) => {
                    if (!actions.order) return;

                    try {
                        const details = await actions.order.capture();
                        console.log("Payment captured:", details);
                        onSuccess(data.orderID);
                    } catch (err) {
                        console.error("Capture error:", err);
                        onError?.("Payment capture failed");
                    }
                }}
                onError={(err) => {
                    console.error("PayPal error:", err);
                    onError?.("Payment failed");
                }}
                onCancel={() => {
                    console.log("Payment cancelled");
                }}
            />
        </motion.div>
    );
}

export default PayPalCheckoutButton;
