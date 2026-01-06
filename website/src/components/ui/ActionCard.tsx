"use client";

import clsx from "clsx";
import { LucideIcon } from "lucide-react";

interface ActionCardProps {
    icon: LucideIcon;
    title: string;
    description: string;
    color?: "blue" | "purple" | "green" | "yellow";
    onClick?: () => void;
    href?: string;
}

export function ActionCard({ icon: Icon, title, description, color = "blue", onClick }: ActionCardProps) {
    const colors = {
        blue: "text-[#4A90E2] bg-[#4A90E2]/10 group-hover:bg-[#4A90E2]/20",
        purple: "text-purple-500 bg-purple-500/10 group-hover:bg-purple-500/20",
        green: "text-[var(--success)] bg-[var(--success)]/10 group-hover:bg-[var(--success)]/20",
        yellow: "text-[var(--warning)] bg-[var(--warning)]/10 group-hover:bg-[var(--warning)]/20"
    };

    return (
        <button
            onClick={onClick}
            className="flex items-center gap-6 bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-2xl p-6 hover:border-[#333] transition-all text-left group w-full"
        >
            <div className={clsx("p-4 rounded-xl transition-colors", colors[color])}>
                <Icon className="w-8 h-8" />
            </div>
            <div>
                <div className="font-bold text-lg text-white group-hover:text-[var(--accent-primary)] transition-colors">{title}</div>
                <div className="text-sm text-[var(--text-muted)]">{description}</div>
            </div>
        </button>
    );
}
