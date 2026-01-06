import clsx from "clsx";
import { ButtonHTMLAttributes, ReactNode } from "react";
import { Loader2 } from "lucide-react";

interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
    variant?: "primary" | "secondary" | "ghost" | "danger";
    size?: "sm" | "md" | "lg";
    loading?: boolean;
    icon?: ReactNode;
    children: ReactNode;
}

export function Button({
    variant = "primary",
    size = "md",
    loading = false,
    icon,
    children,
    className,
    disabled,
    ...props
}: ButtonProps) {

    const variants = {
        primary: "bg-[var(--accent-primary)] hover:bg-[var(--accent-secondary)] text-white shadow-lg shadow-[var(--accent-primary)]/20 hover:shadow-[var(--accent-primary)]/40",
        secondary: "bg-[var(--bg-secondary)] text-white border border-[var(--border-color)] hover:border-white/20 hover:bg-[var(--bg-tertiary)]",
        ghost: "text-gray-400 hover:text-white hover:bg-white/5",
        danger: "bg-red-500/10 text-red-500 hover:bg-red-500/20 border border-red-500/20"
    };

    const sizes = {
        sm: "px-4 py-2 text-sm",
        md: "px-6 py-3 text-base",
        lg: "px-8 py-4 text-lg"
    };

    return (
        <button
            disabled={disabled || loading}
            className={clsx(
                "rounded-xl font-bold transition-all flex items-center justify-center gap-2 active:scale-95 disabled:opacity-50 disabled:cursor-not-allowed disabled:active:scale-100",
                variants[variant],
                sizes[size],
                className
            )}
            {...props}
        >
            {loading ? <Loader2 className="w-5 h-5 animate-spin" /> : icon}
            {children}
        </button>
    );
}
