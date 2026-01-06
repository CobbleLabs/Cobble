"use client";

import clsx from "clsx";
import { InputHTMLAttributes } from "react";

interface InputProps extends InputHTMLAttributes<HTMLInputElement> {
    label?: string;
    error?: string;
}

export function Input({ label, error, className, ...props }: InputProps) {
    return (
        <div className="w-full">
            {label && (
                <label className="block text-sm font-medium text-[var(--text-secondary)] mb-2">
                    {label}
                </label>
            )}
            <input
                className={clsx(
                    "input w-full transition-all focus:ring-2 focus:ring-[var(--accent-primary)]/20 disabled:opacity-50 disabled:cursor-not-allowed disabled:bg-[var(--bg-secondary)]",
                    error && "border-[var(--error)] focus:border-[var(--error)] focus:ring-[var(--error)]/20",
                    className
                )}
                {...props}
            />
            {error && <p className="mt-1 text-xs text-[var(--error)]">{error}</p>}
        </div>
    );
}
