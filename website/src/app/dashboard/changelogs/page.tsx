"use client";

import { useEffect, useState } from "react";
import { motion } from "framer-motion";
import { GitCommit, Tag } from "lucide-react";
import clsx from "clsx";
import { Heading, Text } from "@/components/ui/Typography";
import * as api from "@/lib/api";

export default function ChangelogsPage() {
    const [changelogs, setChangelogs] = useState<api.Changelog[]>([]);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        async function fetchChangelogs() {
            try {
                const data = await api.getChangelogs();
                setChangelogs(data || []);
            } catch (error) {
                console.error("Failed to fetch changelogs:", error);
            } finally {
                setLoading(false);
            }
        }
        fetchChangelogs();
    }, []);

    return (
        <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            className="max-w-4xl mx-auto"
        >
            <div className="mb-12">
                <Heading className="text-3xl mb-2">Changelogs</Heading>
                <Text>Track the evolution of Cobble.</Text>
            </div>

            {loading ? (
                <div className="space-y-8">
                    {[1, 2, 3].map((i) => (
                        <div key={i} className="animate-pulse bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8">
                            <div className="h-8 bg-[var(--bg-tertiary)] rounded w-1/4 mb-4" />
                            <div className="space-y-2">
                                <div className="h-4 bg-[var(--bg-tertiary)] rounded w-3/4" />
                                <div className="h-4 bg-[var(--bg-tertiary)] rounded w-2/3" />
                            </div>
                        </div>
                    ))}
                </div>
            ) : changelogs.length > 0 ? (
                <div className="relative border-l border-[var(--border-color)] ml-4 md:ml-8 space-y-12">
                    {changelogs.map((changelog, i) => {
                        const isLatest = i === 0;
                        const releaseDate = new Date(changelog.released_at).toLocaleDateString('en-US', {
                            month: 'short', day: 'numeric', year: 'numeric'
                        });

                        return (
                            <motion.div
                                key={changelog.id}
                                initial={{ opacity: 0, x: -20 }}
                                animate={{ opacity: 1, x: 0 }}
                                transition={{ delay: i * 0.1 }}
                                className="relative pl-8 md:pl-12"
                            >
                                {/* Timeline Dot */}
                                <div className={clsx(
                                    "absolute -left-1.5 md:-left-2 top-2 w-3 h-3 md:w-4 md:h-4 rounded-full border-2 border-[var(--bg-primary)]",
                                    isLatest ? "bg-[var(--accent-primary)]" : "bg-gray-600"
                                )} />

                                <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8 hover:border-[var(--accent-primary)]/30 transition-colors">
                                    <div className="flex flex-col md:flex-row md:items-center gap-4 mb-6">
                                        <div className="flex items-center gap-3">
                                            {isLatest && (
                                                <span className="px-3 py-1 rounded-full text-xs font-bold uppercase tracking-wider bg-[var(--accent-primary)]/10 text-[var(--accent-primary)]">
                                                    latest
                                                </span>
                                            )}
                                            <span className="text-2xl font-bold text-white flex items-center gap-2">
                                                {changelog.version}
                                            </span>
                                        </div>
                                        <span className="text-[var(--text-muted)] text-sm flex items-center gap-2 md:ml-auto">
                                            <Tag className="w-4 h-4" /> {releaseDate}
                                        </span>
                                    </div>

                                    <ul className="space-y-3">
                                        {changelog.changes?.map((item, idx) => (
                                            <li key={idx} className="flex items-start gap-3 text-gray-300">
                                                <GitCommit className="w-5 h-5 text-[var(--text-muted)] flex-shrink-0 mt-0.5" />
                                                <span>{item}</span>
                                            </li>
                                        ))}
                                    </ul>
                                </div>
                            </motion.div>
                        );
                    })}
                </div>
            ) : (
                <div className="text-center py-16 text-[var(--text-secondary)]">
                    <Text>No changelogs available yet.</Text>
                </div>
            )}
        </motion.div>
    );
}
