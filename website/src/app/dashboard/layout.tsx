"use client";

import { LayoutDashboard, ShoppingCart, Download, Shield, FileText, Menu, X, LogOut, User, ChevronDown } from "lucide-react";
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
    const [isAdminOpen, setIsAdminOpen] = useState(false);

    return (
        <div className="min-h-screen bg-[#0A0A0A] text-white font-sans flex overflow-hidden">

            {/* Sidebar (Desktop) */}
            <aside className="hidden md:flex flex-col w-72 h-screen fixed left-0 top-0 border-r border-[var(--border-color)] bg-[#0F0F0F] z-20">
                <div className="p-8">
                    <Link href="/" className="text-2xl font-bold tracking-widest text-white uppercase flex items-center gap-2">
                        <div className="w-8 h-8 bg-[var(--accent-primary)] rounded flex items-center justify-center">
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
                                        ? "bg-[var(--accent-primary)]/10 text-[var(--accent-primary)] font-semibold"
                                        : "text-[var(--text-secondary)] hover:text-white hover:bg-[var(--bg-tertiary)]"
                                )}>
                                    <item.icon className={clsx("w-5 h-5 transition-colors", isActive ? "text-[var(--accent-primary)]" : "text-gray-500 group-hover:text-white")} />
                                    {item.name}
                                    {isActive && (
                                        <motion.div
                                            layoutId="activeTab"
                                            className="ml-auto w-1.5 h-1.5 rounded-full bg-[var(--accent-primary)]"
                                        />
                                    )}
                                </div>
                            </Link>
                        );
                    })}

                    {/* Admin Divider */}
                    <div className="my-4 border-t border-[var(--border-color)]" />

                    {/* Admin Menu */}
                    <div>
                        <button
                            onClick={() => setIsAdminOpen(!isAdminOpen)}
                            className={clsx(
                                "w-full flex items-center justify-between px-4 py-3.5 rounded-xl transition-all duration-300 group",
                                pathname.startsWith("/dashboard/admin") ? "text-white" : "text-[var(--text-secondary)] hover:text-white hover:bg-[var(--bg-tertiary)]"
                            )}
                        >
                            <div className="flex items-center gap-4">
                                <Shield className={clsx("w-5 h-5", pathname.startsWith("/dashboard/admin") ? "text-red-500" : "text-gray-500 group-hover:text-red-500")} />
                                <span className="font-medium">Admin</span>
                            </div>
                            <ChevronDown className={clsx("w-4 h-4 transition-transform", isAdminOpen ? "rotate-180" : "")} />
                        </button>

                        <AnimatePresence>
                            {(isAdminOpen || pathname.startsWith("/dashboard/admin")) && (
                                <motion.div
                                    initial={{ height: 0, opacity: 0 }}
                                    animate={{ height: "auto", opacity: 1 }}
                                    exit={{ height: 0, opacity: 0 }}
                                    className="overflow-hidden"
                                >
                                    <div className="pl-12 pr-4 space-y-1 py-1">
                                        {[
                                            { name: "Users", href: "/dashboard/admin/users" },
                                            { name: "Bans", href: "/dashboard/admin/bans" },
                                            { name: "Logs", href: "/dashboard/admin/logs" },
                                            { name: "Announcements", href: "/dashboard/admin/announcements" },
                                        ].map((subItem) => (
                                            <Link key={subItem.href} href={subItem.href}>
                                                <div className={clsx(
                                                    "py-2 px-3 rounded-lg text-sm transition-colors block",
                                                    pathname === subItem.href
                                                        ? "text-red-500 bg-red-500/10 font-medium"
                                                        : "text-[var(--text-secondary)] hover:text-white hover:bg-[var(--bg-tertiary)]"
                                                )}>
                                                    {subItem.name}
                                                </div>
                                            </Link>
                                        ))}
                                    </div>
                                </motion.div>
                            )}
                        </AnimatePresence>
                    </div>

                </nav>

                <div className="p-4 mt-auto space-y-3">
                    {/* My Profile Link */}
                    <Link href="/dashboard/profile">
                        <div className={clsx(
                            "flex items-center gap-4 px-4 py-3 rounded-xl transition-all duration-300",
                            pathname === "/dashboard/profile"
                                ? "bg-[var(--accent-primary)]/10 text-[var(--accent-primary)] font-semibold"
                                : "text-[var(--text-secondary)] hover:text-white hover:bg-[var(--bg-tertiary)]"
                        )}>
                            <User className="w-5 h-5" />
                            My Profile
                        </div>
                    </Link>

                    {/* User Card */}
                    <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl p-4 flex items-center gap-3">
                        <div className="w-10 h-10 rounded-full bg-[var(--bg-secondary)] flex items-center justify-center text-gray-400">
                            <User className="w-5 h-5" />
                        </div>
                        <div className="flex-1 min-w-0">
                            <div className="text-sm font-semibold truncate">User123</div>
                            <div className="text-xs text-[var(--text-muted)] truncate">Lifetime Plan</div>
                        </div>
                        <button className="text-[var(--text-secondary)] hover:text-[var(--error)] transition-colors">
                            <LogOut className="w-5 h-5" />
                        </button>
                    </div>
                </div>
            </aside>

            {/* Main Content */}
            <div className="flex-1 md:ml-72 flex flex-col min-h-screen overflow-hidden">

                {/* Mobile Header */}
                <header className="md:hidden h-16 border-b border-[var(--border-color)] bg-[#0F0F0F] flex items-center justify-between px-4 z-20">
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
                                            pathname === item.href ? "bg-[var(--accent-primary)]/10 text-[var(--accent-primary)]" : "text-[var(--text-secondary)]"
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
                    <div className="absolute top-0 left-0 w-full h-[300px] bg-gradient-to-b from-[var(--accent-primary)]/5 to-transparent pointer-events-none" />
                    {children}
                </main>
            </div>
        </div>
    );
}
