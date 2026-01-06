"use client";

import Link from "next/link";

export function AuthNavbar() {
    return (
        <nav className="fixed top-0 left-0 w-full z-50 py-4 px-6 md:px-12 flex items-center justify-between bg-transparent">
            <Link href="/" className="text-xl font-heading font-bold tracking-widest text-white uppercase flex items-center gap-2 hover:opacity-80 transition-opacity">
                <div className="w-8 h-8 bg-[var(--accent-primary)] rounded flex items-center justify-center">
                    <span className="text-white text-lg font-black font-heading">C</span>
                </div>
                COBBLE
            </Link>
        </nav>
    );
}