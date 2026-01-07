import clsx from "clsx";
import { HTMLAttributes, ReactNode } from "react";

interface CardProps extends HTMLAttributes<HTMLDivElement> {
    variant?: "default" | "interactive" | "premium" | "glass";
    children: ReactNode;
}

export function Card({ variant = "default", children, className, ...props }: CardProps) {
    const variants = {
        default: "bg-[#121212] border border-[#1E1E1E]",
        interactive: "bg-[#121212] border border-[#1E1E1E] hover:border-[var(--accent-primary)]/30 transition-all cursor-default",
        premium: "bg-[#121212] border border-[var(--accent-primary)]/50 shadow-[0_0_30px_rgba(215,96,20,0.1)]",
        glass: "bg-[#121212]/80 backdrop-blur-md border border-[#1E1E1E]"
    };

    return (
        <div
            className={clsx(
                "rounded-3xl p-6 md:p-8",
                variants[variant],
                className
            )}
            {...props}
        >
            {children}
        </div>
    );
}
