"use client";

import Link from "next/link";

export function Footer() {
    return (
        <footer className="bg-[#050505] py-12 border-t border-[var(--border-color)]">
            <div className="max-w-7xl mx-auto px-6 flex flex-col md:flex-row justify-between items-center gap-6">
                <div className="text-2xl font-bold text-white tracking-wide flex items-center gap-2">
                    Cobble
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
