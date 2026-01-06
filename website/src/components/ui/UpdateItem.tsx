"use client";

import { Badge } from "./Badge";
import { Text } from "./Typography";

interface UpdateItemProps {
    version: string;
    date: string;
    description: string;
}

export function UpdateItem({ version, date, description }: UpdateItemProps) {
    return (
        <div className="flex gap-4 group">
            <div className="flex flex-col items-center">
                <div className="w-2 h-2 rounded-full bg-[var(--accent-primary)] ring-4 ring-[var(--bg-primary)] group-hover:ring-[var(--accent-primary)]/20 transition-all" />
                <div className="w-0.5 flex-1 bg-[var(--border-color)] mt-1" />
            </div>
            <div className="pb-6">
                <div className="flex items-center gap-2 mb-1">
                    <Badge variant="default">{version}</Badge>
                    <span className="text-xs text-[var(--text-muted)]">{date}</span>
                </div>
                <Text className="text-sm">{description}</Text>
            </div>
        </div>
    );
}
