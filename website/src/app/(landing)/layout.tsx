"use client";

import Link from "next/link";
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
                    "fixed top-0 left-0 w-full z-50 py-4 px-6 md:px-12 flex items-center justify-between transition-all duration-300 border-b",
                    scrolled ? "bg-[#0A0A0A]/80 backdrop-blur-md border-[var(--border-color)]" : "bg-transparent border-transparent"
                )}
            >
                <div className="text-xl font-heading font-bold tracking-wide text-white flex items-center gap-2">
                    Cobble
                </div>

                <div className="hidden md:flex items-center gap-8 text-sm font-medium text-gray-300">
                    <Link href="/" className="hover:text-white transition-colors">Home</Link>
                    <Link href="#features" className="hover:text-white transition-colors">Features</Link>
                    <Link href="#pricing" className="hover:text-white transition-colors">Pricing</Link>
                    <Link href="#faq" className="hover:text-white transition-colors">FAQ</Link>
                    <Link href="#community" className="hover:text-white transition-colors">Support</Link>
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
        </>
    );
}
