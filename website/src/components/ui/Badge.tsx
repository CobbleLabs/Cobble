"use client";

import clsx from "clsx";
import { ReactNode } from "react";

interface BadgeProps {
    children: ReactNode;
    variant?: "success" | "warning" | "error" | "info" | "default";
    className?: string;
}

export function Badge({ children, variant = "default", className }: BadgeProps) {
    const variants = {
        success: "bg-[var(--success)] text-white border-transparent",
        warning: "bg-[var(--warning)] text-white border-transparent",
        error: "bg-[var(--error)] text-white border-transparent",
        info: "bg-[var(--accent-primary)] text-white border-transparent",
        default: "bg-[var(--bg-tertiary)] text-[var(--text-secondary)] border-[var(--border-color)]"
    };

    return (
        <span className={clsx(
            "px-2.5 py-0.5 rounded-full text-xs font-bold border uppercase tracking-wide",
            variants[variant],
            className
        )}>
            {children}
        </span>
    );
}
