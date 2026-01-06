"use client";

import { useState } from "react";
import { motion, AnimatePresence } from "framer-motion";
import { ChevronDown, HelpCircle } from "lucide-react";
import { Heading, Text } from "./Typography";
import { fadeInUp } from "@/lib/motion";
import clsx from "clsx";

interface FAQItemProps {
    question: string;
    answer: string;
    isOpen: boolean;
    onClick: () => void;
}

const FAQItem = ({ question, answer, isOpen, onClick }: FAQItemProps) => {
    return (
        <motion.div
            initial={false}
            className={clsx(
                "border rounded-2xl overflow-hidden transition-colors duration-200",
                isOpen
                    ? "bg-[var(--accent-primary)]/5 border-white/5"
                    : "bg-[#0A0A0A] border-white/5 hover:border-[var(--accent-primary)]/30"
            )}
        >
            <button
                onClick={onClick}
                className="w-full px-6 py-5 flex items-center justify-between gap-4 text-left"
            >
                <span className={clsx(
                    "font-bold text-lg transition-colors",
                    isOpen ? "text-[var(--accent-primary)]" : "text-white"
                )}>
                    {question}
                </span>
                <ChevronDown
                    className={clsx(
                        "w-5 h-5 text-[var(--text-secondary)] transition-transform duration-300",
                        isOpen && "rotate-180 text-[var(--accent-primary)]"
                    )}
                />
            </button>
            <AnimatePresence initial={false}>
                {isOpen && (
                    <motion.div
                        initial={{ height: 0, opacity: 0 }}
                        animate={{ height: "auto", opacity: 1 }}
                        exit={{ height: 0, opacity: 0 }}
                        transition={{ duration: 0.3, ease: "easeInOut" }}
                    >
                        <div className="px-6 pb-5">
                            <Text className="text-[var(--text-secondary)] leading-relaxed">
                                {answer}
                            </Text>
                        </div>
                    </motion.div>
                )}
            </AnimatePresence>
        </motion.div>
    );
};

const faqs = [
    {
        question: "Is Cobble really undetectable?",
        answer: "Yes. Cobble utilizes advanced kernel-level injection methods that hide the client from task manager, file system scans, and memory dumps. We have maintained a 99.9% undetected rate on major servers for over 2 years."
    },
    {
        question: "What versions of Minecraft are supported?",
        answer: "We currently support Minecraft Java Edition versions 1.7.10, 1.8.9, and 1.12.2. Support for 1.19+ is currently in beta testing for lifetime users."
    },
    {
        question: "Do I need to disable my antivirus?",
        answer: "Due to the nature of how our injection works (interacting with game memory), some antiviruses may flag the loader as a false positive. We recommend adding an exclusion for the Cobble folder."
    },
    {
        question: "Can I transfer my license to another PC?",
        answer: "Yes! Your license is bound to your HWID. You can reset your HWID once per month directly from the dashboard to move your license to a new computer."
    },
    {
        question: "What payment methods do you accept?",
        answer: "We accept all major credit cards (via Stripe), PayPal, and various cryptocurrencies including Bitcoin, Ethereum, and Litecoin for maximum privacy."
    }
];

export function FAQ() {
    const [openIndex, setOpenIndex] = useState<number | null>(0);

    return (
        <section id="faq" className="py-24 px-6 relative overflow-hidden">
            {/* Background decoration */}
            <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-[600px] h-[600px] bg-[var(--accent-primary)]/5 blur-[100px] rounded-full pointer-events-none" />

            <div className="max-w-3xl mx-auto relative z-10">
                <motion.div
                    initial="initial"
                    whileInView="animate"
                    viewport={{ once: true }}
                    variants={fadeInUp}
                    className="text-center mb-16"
                >
                    <div className="inline-flex items-center gap-2 px-3 py-1 rounded-full bg-[var(--accent-primary)]/10 text-[var(--accent-primary)] text-sm font-medium mb-4">
                        <HelpCircle className="w-4 h-4" />
                        <span>Support Center</span>
                    </div>
                    <Heading className="text-4xl md:text-5xl mb-6">Frequently Asked Questions</Heading>
                    <Text className="text-lg">
                        Have questions? We have answers. If you need more help, join our Discord.
                    </Text>
                </motion.div>

                <div className="space-y-4">
                    {faqs.map((faq, index) => (
                        <motion.div
                            key={index}
                            initial={{ opacity: 0, y: 20 }}
                            whileInView={{ opacity: 1, y: 0 }}
                            viewport={{ once: true }}
                            transition={{ delay: index * 0.1 }}
                        >
                            <FAQItem
                                question={faq.question}
                                answer={faq.answer}
                                isOpen={openIndex === index}
                                onClick={() => setOpenIndex(openIndex === index ? null : index)}
                            />
                        </motion.div>
                    ))}
                </div>
            </div>
        </section>
    );
}
