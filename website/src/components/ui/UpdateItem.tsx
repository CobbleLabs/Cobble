"use client";

import { Badge } from "./Badge";
import { Text } from "./Typography";

interface UpdateItemProps {
    version: string;
    date: string;
    changes: string[];  // Array of changes instead of single description
    maxChanges?: number; // Max changes to show (default 3)
}

export function UpdateItem({ version, date, changes, maxChanges = 3 }: UpdateItemProps) {
    const displayChanges = changes.slice(0, maxChanges);
    const remaining = changes.length - maxChanges;

    return (
        <div className="flex gap-4 group">
            <div className="flex flex-col items-center">
                <div className="w-2 h-2 rounded-full bg-[var(--accent-primary)] ring-4 ring-[var(--bg-primary)] group-hover:ring-[var(--accent-primary)]/20 transition-all" />
                <div className="w-0.5 flex-1 bg-[var(--border-color)] mt-1" />
            </div>
            <div className="pb-6 flex-1">
                <div className="flex items-center gap-2 mb-2">
                    <Badge variant="default">{version}</Badge>
                    <span className="text-xs text-[var(--text-muted)]">{date}</span>
                </div>
                <ul className="space-y-1">
                    {displayChanges.map((change, index) => (
                        <li key={index} className="text-sm text-[var(--text-secondary)] flex items-start gap-2">
                            <span className="text-[var(--text-muted)] mt-0.5">•</span>
                            <span>{change}</span>
                        </li>
                    ))}
                    {remaining > 0 && (
                        <li className="text-xs text-[var(--text-muted)] ml-4">
                            +{remaining} more changes
                        </li>
                    )}
                </ul>
            </div>
        </div>
    );
}
