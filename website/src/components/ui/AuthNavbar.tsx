"use client";

import Link from "next/link";

export function AuthNavbar() {
    return (
        <nav className="fixed top-0 left-0 w-full z-50 py-4 px-6 md:px-12 flex items-center justify-between bg-transparent">
            <Link href="/" className="text-xl font-heading font-bold tracking-wide text-white flex items-center gap-2 hover:opacity-80 transition-opacity">
                Cobble
            </Link>
        </nav>
    );
}