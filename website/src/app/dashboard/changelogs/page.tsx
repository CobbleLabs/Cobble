"use client";

import { motion } from "framer-motion";
import { GitCommit, Tag } from "lucide-react";
import clsx from "clsx";
import { Heading, Text } from "@/components/ui/Typography";

const changes = [
    {
        version: "2.0.2",
        date: "Jan 05, 2026",
        type: "update",
        items: [
            "Fixed movement lag on Hypixel",
            "Optimized Killaura rotation logic",
            "Added new Velocity mode (Grim)",
            "Fixed UI crash on inject"
        ]
    },
    {
        version: "2.0.1",
        date: "Jan 03, 2026",
        type: "fix",
        items: [
            "Patched random disconnects",
            "Improved AimAssist randomization",
            "Updated config system cloud sync"
        ]
    },
    {
        version: "2.0.0",
        date: "Jan 01, 2026",
        type: "release",
        items: [
            "Initial V2 Release",
            "Complete code rewrite",
            "New UI implementation",
            "New Cloud Loader"
        ]
    }
];

export default function ChangelogsPage() {
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

            <div className="relative border-l border-[var(--border-color)] ml-4 md:ml-8 space-y-12">
                {changes.map((change, i) => (
                    <motion.div
                        key={change.version}
                        initial={{ opacity: 0, x: -20 }}
                        animate={{ opacity: 1, x: 0 }}
                        transition={{ delay: i * 0.1 }}
                        className="relative pl-8 md:pl-12"
                    >
                        {/* Timeline Dot */}
                        <div className={clsx(
                            "absolute -left-1.5 md:-left-2 top-2 w-3 h-3 md:w-4 md:h-4 rounded-full border-2 border-[var(--bg-primary)]",
                            change.type === "release" ? "bg-[var(--accent-primary)]" : "bg-gray-600"
                        )} />

                        <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8 hover:border-[var(--accent-primary)]/30 transition-colors">
                            <div className="flex flex-col md:flex-row md:items-center gap-4 mb-6">
                                <div className="flex items-center gap-3">
                                    <span className={clsx(
                                        "px-3 py-1 rounded-full text-xs font-bold uppercase tracking-wider",
                                        change.type === "release" ? "bg-[var(--accent-primary)]/10 text-[var(--accent-primary)]" : "bg-gray-800 text-[var(--text-secondary)]"
                                    )}>
                                        {change.type}
                                    </span>
                                    <span className="text-2xl font-bold text-white flex items-center gap-2">
                                        {change.version}
                                    </span>
                                </div>
                                <span className="text-[var(--text-muted)] text-sm flex items-center gap-2 md:ml-auto">
                                    <Tag className="w-4 h-4" /> {change.date}
                                </span>
                            </div>

                            <ul className="space-y-3">
                                {change.items.map((item, idx) => (
                                    <li key={idx} className="flex items-start gap-3 text-gray-300">
                                        <GitCommit className="w-5 h-5 text-[var(--text-muted)] flex-shrink-0 mt-0.5" />
                                        <span>{item}</span>
                                    </li>
                                ))}
                            </ul>
                        </div>
                    </motion.div>
                ))}
            </div>
        </motion.div>
    );
}
