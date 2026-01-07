"use client";

import { motion } from "framer-motion";
import { PricingCard } from "@/components/ui/PricingCard";
import { Heading, Text } from "@/components/ui/Typography";

import { useAuth } from "@/context/AuthContext";

export default function PurchasePage() {
    const { user } = useAuth();
    const hasMonthly = user?.license?.plan === 'monthly';

    return (
        <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            className="max-w-5xl mx-auto"
        >
            <div className="text-center mb-12">
                <Heading className="text-4xl mb-4">Choose Your Plan</Heading>
                <Text>Unlock the full power of Cobble today.</Text>
            </div>

            <div className={`grid grid-cols-1 ${hasMonthly ? 'place-items-center' : 'md:grid-cols-2'} gap-8`}>
                {!hasMonthly && (
                    <PricingCard
                        title="Monthly Access"
                        price="10"
                        period="/mo"
                        description="Flexible access for short-term domination."
                        features={["Full Module Access", "24/7 Support", "Cloud Configs", "HWID Resets"]}
                        buttonText="Purchase Now"
                    />
                )}
                <PricingCard
                    title="Lifetime Access"
                    price="45"
                    period="/life"
                    description="One-time payment. Own it forever."
                    features={["Full Module Access", "Priority Support", "Cloud Configs", "Unlimited HWID Resets", "Early Access Beta"]}
                    isPopular
                    buttonText="Purchase Now"
                    className={hasMonthly ? "max-w-md w-full" : ""}
                />
            </div>
        </motion.div>
    );
}

