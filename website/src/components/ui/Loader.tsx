"use client";

import { cn } from "@/lib/utils";

interface LoaderProps {
    className?: string;
    size?: "sm" | "md" | "lg";
    centered?: boolean;
}

export function Loader({ className, size = "md", centered = true }: LoaderProps) {
    const sizeClasses = {
        sm: "w-5 h-5 border-2",
        md: "w-8 h-8 border-[3px]",
        lg: "w-12 h-12 border-4",
    };

    const loader = (
        <div
            className={cn(
                "rounded-full border-[var(--bg-tertiary)] border-t-[var(--accent-primary)] animate-spin",
                sizeClasses[size],
                className
            )}
        />
    );

    if (centered) {
        return (
            <div className="flex items-center justify-center p-8 w-full h-full min-h-[200px]">
                {loader}
            </div>
        );
    }

    return loader;
}
