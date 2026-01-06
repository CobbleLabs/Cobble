"use client";

import { motion } from "framer-motion";
import { FAQItem } from "@/components/ui/FAQItem";
import { PricingCard } from "@/components/ui/PricingCard";
import { Heading, Text } from "@/components/ui/Typography";

export default function PurchasePage() {
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

            <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
                <PricingCard
                    title="Monthly Access"
                    price="10"
                    period="/mo"
                    description="Flexible access for short-term domination."
                    features={["Full Module Access", "24/7 Support", "Cloud Configs", "HWID Resets"]}
                    buttonText="Purchase Now"
                />
                <PricingCard
                    title="Lifetime Access"
                    price="50"
                    period="/life"
                    description="One-time payment. Own it forever."
                    features={["Full Module Access", "Priority Support", "Cloud Configs", "Unlimited HWID Resets", "Early Access Beta"]}
                    isPopular
                    buttonText="Purchase Now"
                />
            </div>

            <div className="mt-16 bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8">
                <Heading className="text-xl mb-6">Frequently Asked Questions</Heading>
                <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
                    <FAQItem question="Do you accept Crypto?" answer="Yes, we accept Bitcoin, Ethereum, Litecoin and USDT." />
                    <FAQItem question="Is it refundable?" answer="Digital goods are generally non-refundable, but contact support if you have issues." />
                    <FAQItem question="How instant is delivery?" answer="You receive your key immediately after payment confirmation." />
                    <FAQItem question="Does it bypass screenshares?" answer="Yes, Cobble is designed to be invisible to common SS tools." />
                </div>
            </div>
        </motion.div>
    );
}
