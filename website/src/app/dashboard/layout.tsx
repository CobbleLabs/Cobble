"use client";

import { LayoutDashboard, ShoppingCart, Download, Shield, FileText, Menu, X, LogOut, User } from "lucide-react";
import Link from "next/link";
import { usePathname } from "next/navigation";
import { useState } from "react";
import { motion, AnimatePresence } from "framer-motion";
import clsx from "clsx";

const navItems = [
    { name: "Overview", href: "/dashboard", icon: LayoutDashboard },
    { name: "Purchase", href: "/dashboard/purchase", icon: ShoppingCart },
    { name: "Downloads", href: "/dashboard/downloads", icon: Download },
    { name: "HWID Manager", href: "/dashboard/hwid", icon: Shield },
    { name: "Changelogs", href: "/dashboard/changelogs", icon: FileText },
];

export default function DashboardLayout({ children }: { children: React.ReactNode }) {
    const pathname = usePathname();
    const [isMobileMenuOpen, setIsMobileMenuOpen] = useState(false);

    return (
        <div className="min-h-screen bg-[#0A0A0A] text-white font-sans flex overflow-hidden">

            {/* Sidebar (Desktop) */}
            <aside className="hidden md:flex flex-col w-72 h-screen fixed left-0 top-0 border-r border-[#1E1E1E] bg-[#0F0F0F] z-20">
                <div className="p-8">
                    <Link href="/" className="text-2xl font-bold tracking-widest text-white uppercase flex items-center gap-2">
                        <div className="w-8 h-8 bg-[#4A90E2] rounded flex items-center justify-center">
                            <span className="text-white text-lg font-black">C</span>
                        </div>
                        COBBLE
                    </Link>
                </div>

                <nav className="flex-1 px-4 space-y-2 overflow-y-auto custom-scrollbar">
                    {navItems.map((item) => {
                        const isActive = pathname === item.href;
                        return (
                            <Link key={item.href} href={item.href}>
                                <div className={clsx(
                                    "flex items-center gap-4 px-4 py-3.5 rounded-xl transition-all duration-300 group",
                                    isActive
                                        ? "bg-[#4A90E2]/10 text-[#4A90E2] font-semibold"
                                        : "text-gray-400 hover:text-white hover:bg-[#1E1E1E]"
                                )}>
                                    <item.icon className={clsx("w-5 h-5 transition-colors", isActive ? "text-[#4A90E2]" : "text-gray-500 group-hover:text-white")} />
                                    {item.name}
                                    {isActive && (
                                        <motion.div
                                            layoutId="activeTab"
                                            className="ml-auto w-1.5 h-1.5 rounded-full bg-[#4A90E2]"
                                        />
                                    )}
                                </div>
                            </Link>
                        );
                    })}
                </nav>

                <div className="p-4 mt-auto">
                    <div className="bg-[#121212] border border-[#1E1E1E] rounded-xl p-4 flex items-center gap-3">
                        <div className="w-10 h-10 rounded-full bg-[#1E1E1E] flex items-center justify-center text-gray-400">
                            <User className="w-5 h-5" />
                        </div>
                        <div className="flex-1 min-w-0">
                            <div className="text-sm font-semibold truncate">User123</div>
                            <div className="text-xs text-gray-500 truncate">Lifetime Plan</div>
                        </div>
                        <button className="text-gray-400 hover:text-red-500 transition-colors">
                            <LogOut className="w-5 h-5" />
                        </button>
                    </div>
                </div>
            </aside>

            {/* Main Content */}
            <div className="flex-1 md:ml-72 flex flex-col min-h-screen overflow-hidden">

                {/* Mobile Header */}
                <header className="md:hidden h-16 border-b border-[#1E1E1E] bg-[#0F0F0F] flex items-center justify-between px-4 z-20">
                    <span className="font-bold text-lg">COBBLE</span>
                    <button onClick={() => setIsMobileMenuOpen(!isMobileMenuOpen)}>
                        {isMobileMenuOpen ? <X /> : <Menu />}
                    </button>
                </header>

                {/* Mobile Sidebar */}
                <AnimatePresence>
                    {isMobileMenuOpen && (
                        <motion.div
                            initial={{ x: "-100%" }}
                            animate={{ x: 0 }}
                            exit={{ x: "-100%" }}
                            className="fixed inset-0 z-50 bg-[#0F0F0F] md:hidden pt-20 px-4"
                        >
                            <nav className="space-y-2">
                                {navItems.map((item) => (
                                    <Link key={item.href} href={item.href} onClick={() => setIsMobileMenuOpen(false)}>
                                        <div className={clsx(
                                            "flex items-center gap-4 px-4 py-4 rounded-xl text-lg font-medium",
                                            pathname === item.href ? "bg-[#4A90E2]/10 text-[#4A90E2]" : "text-gray-400"
                                        )}>
                                            <item.icon className="w-6 h-6" />
                                            {item.name}
                                        </div>
                                    </Link>
                                ))}
                            </nav>
                        </motion.div>
                    )}
                </AnimatePresence>

                <main className="flex-1 overflow-y-auto p-6 md:p-12 relative">
                    {/* Background Glow */}
                    <div className="absolute top-0 left-0 w-full h-[300px] bg-gradient-to-b from-[#4A90E2]/5 to-transparent pointer-events-none" />
                    {children}
                </main>
            </div>
        </div>
    );
}
