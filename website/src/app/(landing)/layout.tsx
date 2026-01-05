"use client";

import Link from "next/link";
import { ChevronUp } from "lucide-react";
import { Button } from "@/components/ui/Button";
import clsx from "clsx";
import { useState, useEffect } from "react";
import { motion } from "framer-motion";

export default function LandingLayout({
    children,
}: Readonly<{
    children: React.ReactNode;
}>) {
    const [scrolled, setScrolled] = useState(false);

    useEffect(() => {
        const handleScroll = () => setScrolled(window.scrollY > 50);
        window.addEventListener("scroll", handleScroll);
        return () => window.removeEventListener("scroll", handleScroll);
    }, []);

    return (
        <>
            <motion.nav
                initial={{ y: -100 }}
                animate={{ y: 0 }}
                transition={{ duration: 0.5 }}
                className={clsx(
                    "fixed top-0 left-0 w-full z-50 py-4 px-6 md:px-12 flex items-center justify-between transition-all duration-300",
                    scrolled ? "bg-[#0A0A0A]/80 backdrop-blur-md border-b border-[#1E1E1E]" : "bg-transparent"
                )}
            >
                <div className="text-xl font-heading font-bold tracking-widest text-white uppercase flex items-center gap-2">
                    <div className="w-8 h-8 bg-[#4A90E2] rounded flex items-center justify-center">
                        <span className="text-white text-lg font-black font-heading">C</span>
                    </div>
                    COBBLE
                </div>

                <div className="hidden md:flex items-center gap-8 text-sm font-medium text-gray-300">
                    <Link href="/" className="hover:text-white transition-colors">Home</Link>
                    <Link href="#features" className="hover:text-white transition-colors">Features</Link>
                    <Link href="#pricing" className="hover:text-white transition-colors">Pricing</Link>
                    <Link href="#" className="hover:text-white transition-colors">Support</Link>
                </div>

                <div className="flex items-center gap-4">
                    <Link href="/login" className="text-sm font-medium text-gray-300 hover:text-white transition-colors hidden md:block">
                        Login
                    </Link>
                    <Link href="/register">
                        <Button size="sm">Get Started</Button>
                    </Link>
                </div>
            </motion.nav>

            {children}

            <div className="fixed bottom-8 right-8 z-50">
                <button
                    onClick={() => window.scrollTo({ top: 0, behavior: 'smooth' })}
                    className="bg-[#4A90E2] p-3 rounded-full text-white shadow-lg hover:bg-[#357ABD] transition-all hover:scale-110"
                >
                    <ChevronUp className="w-5 h-5" />
                </button>
            </div>
        </>
    );
}
