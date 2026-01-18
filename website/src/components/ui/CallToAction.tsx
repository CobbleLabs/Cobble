"use client";

import { motion } from "framer-motion";
import { FaDiscord } from "react-icons/fa";
import Link from "next/link";
import { Button } from "./Button";
import { Heading, Text } from "./Typography";

export function CallToAction() {
    return (
        <section id="community" className="pt-0 pb-24 px-6 relative overflow-hidden">
            {/* Background Glow */}
            <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-[600px] h-[400px] bg-[var(--accent-primary)] opacity-10 blur-[100px] rounded-full pointer-events-none" />

            <motion.div
                initial={{ opacity: 0, scale: 0.95 }}
                whileInView={{ opacity: 1, scale: 1 }}
                viewport={{ once: true }}
                transition={{ duration: 0.8 }}
                className="max-w-4xl mx-auto relative z-10"
            >
                <div className="bg-[#0A0A0A] border border-[var(--border-color)] rounded-3xl p-8 md:p-16 text-center shadow-2xl overflow-hidden hover:border-[var(--accent-primary)]/30 transition-colors duration-500 group">

                    {/* Decorative Elements inside card */}
                    <div className="absolute top-0 right-0 w-64 h-64 bg-gradient-to-br from-[var(--accent-primary)]/10 to-transparent blur-3xl opacity-0 group-hover:opacity-100 transition-opacity duration-700 pointer-events-none" />

                    <Heading className="text-4xl md:text-5xl mb-6">Stay connected</Heading>
                    <Text className="text-lg max-w-2xl mx-auto mb-8">
                        Updates, support and releases, directly from the source.
                    </Text>

                    <div className="flex justify-center">
                        <Link href="https://discord.gg/frHmJ6TbMF" target="_blank">
                            <Button
                                size="lg"
                                className="bg-[var(--accent-primary)] hover:bg-[var(--accent-secondary)] text-white border-none shadow-lg shadow-[var(--accent-primary)]/20 hover:shadow-[var(--accent-primary)]/40 transition-all px-12 py-6 text-xl h-auto"
                                icon={<FaDiscord className="w-6 h-6 mr-2" />}
                            >
                                Join Discord Server
                            </Button>
                        </Link>
                    </div>
                </div>
            </motion.div>
        </section>
    );
}
