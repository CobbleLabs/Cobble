"use client";

import Link from "next/link";

export function Footer() {
    return (
        <footer className="bg-[#050505] py-12 border-t border-[var(--border-color)]">
            <div className="max-w-7xl mx-auto px-6 flex flex-col md:flex-row justify-between items-center gap-6">
                <div className="text-2xl font-bold text-white tracking-widest flex items-center gap-2">
                    <div className="w-6 h-6 bg-[var(--accent-primary)] rounded flex items-center justify-center">
                        <span className="text-white text-sm font-black font-heading">C</span>
                    </div>
                    COBBLE
                </div>
                <div className="text-[var(--text-muted)] text-sm">© 2026 Cobble Client. All rights reserved.</div>
                <div className="flex gap-6">
                    <Link href="#" className="text-[var(--text-secondary)] hover:text-white transition-colors">Terms</Link>
                    <Link href="#" className="text-[var(--text-secondary)] hover:text-white transition-colors">Privacy</Link>
                    <Link href="#" className="text-[var(--text-secondary)] hover:text-white transition-colors">Discord</Link>
                </div>
            </div>
        </footer>
    );
}
