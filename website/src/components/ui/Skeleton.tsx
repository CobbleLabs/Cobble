"use client";

import { cn } from "@/lib/utils";

interface SkeletonProps {
    className?: string;
    variant?: "default" | "circular" | "rounded";
    animation?: "pulse" | "shimmer" | "none";
    style?: React.CSSProperties;
}

export function Skeleton({
    className,
    variant = "default",
    animation = "shimmer",
    style,
}: SkeletonProps) {
    return (
        <div
            style={style}
            className={cn(
                "bg-[var(--bg-tertiary)]",
                variant === "circular" && "rounded-full",
                variant === "rounded" && "rounded-xl",
                variant === "default" && "rounded-lg",
                animation === "pulse" && "animate-pulse",
                animation === "shimmer" && "skeleton-shimmer",
                className
            )}
        />
    );
}

// Table skeleton for admin pages
export function TableSkeleton({ rows = 5, columns = 5 }: { rows?: number; columns?: number }) {
    return (
        <div className="bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-2xl overflow-hidden">
            {/* Header */}
            <div className="border-b border-[var(--border-color)] px-6 py-4 flex gap-4">
                {Array.from({ length: columns }).map((_, i) => (
                    <Skeleton key={i} className="h-4 flex-1" />
                ))}
            </div>
            {/* Rows */}
            {Array.from({ length: rows }).map((_, rowIndex) => (
                <div
                    key={rowIndex}
                    className="border-b border-[var(--border-color)] last:border-b-0 px-6 py-5 flex items-center gap-4"
                >
                    {Array.from({ length: columns }).map((_, colIndex) => (
                        <Skeleton
                            key={colIndex}
                            className={cn(
                                "h-4 flex-1",
                                colIndex === 0 && "max-w-[140px]",
                                colIndex === columns - 1 && "max-w-[100px]"
                            )}
                        />
                    ))}
                </div>
            ))}
        </div>
    );
}

// Card skeleton for dashboard
export function CardSkeleton({ className }: { className?: string }) {
    return (
        <div className={cn(
            "bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-2xl p-6",
            className
        )}>
            <div className="flex items-start justify-between mb-4">
                <Skeleton className="w-12 h-12" variant="rounded" />
                <Skeleton className="w-16 h-6" variant="rounded" />
            </div>
            <Skeleton className="h-4 w-24 mb-2" />
            <Skeleton className="h-8 w-32" />
        </div>
    );
}

// Stats grid skeleton
export function StatsGridSkeleton() {
    return (
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6">
            {Array.from({ length: 4 }).map((_, i) => (
                <CardSkeleton key={i} />
            ))}
        </div>
    );
}

// Page loading skeleton with header
export function PageSkeleton({ title = true, table = true }: { title?: boolean; table?: boolean }) {
    return (
        <div className="space-y-8 animate-in fade-in duration-300">
            {title && (
                <div className="flex items-center justify-between">
                    <div>
                        <Skeleton className="h-8 w-48 mb-2" />
                        <Skeleton className="h-4 w-72" />
                    </div>
                    <Skeleton className="h-10 w-32" variant="rounded" />
                </div>
            )}
            {table && <TableSkeleton />}
        </div>
    );
}

// Announcement skeleton
export function AnnouncementSkeleton() {
    return (
        <div className="bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-2xl p-6">
            <div className="flex items-start gap-4">
                <Skeleton className="w-10 h-10 flex-shrink-0" variant="rounded" />
                <div className="flex-1 space-y-3">
                    <div className="flex items-center gap-2">
                        <Skeleton className="h-5 w-40" />
                        <Skeleton className="h-5 w-16" variant="rounded" />
                    </div>
                    <Skeleton className="h-4 w-full" />
                    <Skeleton className="h-4 w-3/4" />
                    <div className="flex items-center gap-2 pt-2">
                        <Skeleton className="h-3 w-20" />
                        <Skeleton className="h-3 w-24" />
                    </div>
                </div>
            </div>
        </div>
    );
}
