"use client";

import { motion } from "framer-motion";
import { Check, Shield, Zap } from "lucide-react";
import clsx from "clsx";

export default function PurchasePage() {
    return (
        <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            className="max-w-5xl mx-auto"
        >
            <div className="text-center mb-12">
                <h1 className="text-4xl font-bold font-heading text-white mb-4">Choose Your Plan</h1>
                <p className="text-gray-400">Unlock the full power of Cobble today.</p>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
                <PlanCard
                    title="Monthly Access"
                    price="10"
                    period="/mo"
                    description="Flexible access for short-term domination."
                    features={["Full Module Access", "24/7 Support", "Cloud Configs", "HWID Resets"]}
                />
                <PlanCard
                    title="Lifetime Access"
                    price="50"
                    period="/life"
                    description="One-time payment. Own it forever."
                    features={["Full Module Access", "Priority Support", "Cloud Configs", "Unlimited HWID Resets", "Early Access Beta"]}
                    isPopular
                />
            </div>

            <div className="mt-16 bg-[#121212] border border-[#1E1E1E] rounded-3xl p-8">
                <h3 className="text-xl font-bold font-heading text-white mb-6">Frequently Asked Questions</h3>
                <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
                    <FAQItem q="Do you accept Crypto?" a="Yes, we accept Bitcoin, Ethereum, Litecoin and USDT." />
                    <FAQItem q="Is it refundable?" a="Digital goods are generally non-refundable, but contact support if you have issues." />
                    <FAQItem q="How instant is delivery?" a="You receive your key immediately after payment confirmation." />
                    <FAQItem q="Does it bypass screenshares?" a="Yes, Cobble is designed to be invisible to common SS tools." />
                </div>
            </div>
        </motion.div>
    );
}

function PlanCard({ title, price, period, description, features, isPopular }: any) {
    return (
        <div className={clsx(
            "relative p-8 rounded-3xl border transition-all duration-300 group hover:scale-[1.02]",
            isPopular ? "bg-[#121212] border-[#4A90E2] shadow-[0_0_30px_rgba(74,144,226,0.1)]" : "bg-[#121212] border-[#1E1E1E]"
        )}>
            {isPopular && (
                <div className="absolute -top-4 left-1/2 -translate-x-1/2 bg-[#4A90E2] text-white px-6 py-1.5 rounded-full text-sm font-bold tracking-widest shadow-lg">
                    BEST VALUE
                </div>
            )}

            <h3 className="text-2xl font-bold text-white mb-2">{title}</h3>
            <p className="text-gray-400 text-sm mb-8 h-10">{description}</p>

            <div className="flex items-baseline gap-1 mb-8">
                <span className="text-5xl font-black text-white">${price}</span>
                <span className="text-gray-500">{period}</span>
            </div>

            <button className={clsx(
                "w-full py-4 rounded-xl font-bold mb-8 transition-all",
                isPopular ? "bg-[#4A90E2] hover:bg-[#357ABD] text-white shadow-lg" : "bg-[#1E1E1E] hover:bg-[#252525] text-white border border-[#333]"
            )}>
                Purchase Now
            </button>

            <div className="space-y-4">
                {features.map((f: string, i: number) => (
                    <div key={i} className="flex items-center gap-4 text-gray-300">
                        <div className={clsx("rounded-full p-1", isPopular ? "bg-[#4A90E2]/20 text-[#4A90E2]" : "bg-[#1E1E1E] text-gray-400")}>
                            <Check className="w-4 h-4" />
                        </div>
                        {f}
                    </div>
                ))}
            </div>
        </div>
    );
}

function FAQItem({ q, a }: { q: string, a: string }) {
    return (
        <div>
            <h4 className="font-bold text-white mb-2">{q}</h4>
            <p className="text-gray-400 text-sm leading-relaxed">{a}</p>
        </div>
    );
}
