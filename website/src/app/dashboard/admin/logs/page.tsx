"use client";

import { useState, useEffect, useCallback, useRef } from "react";
import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Table } from "@/components/ui/Table";
import { Loader } from "@/components/ui/Loader";
import { LoadingTransition } from "@/components/ui/LoadingTransition";
import { Badge } from "@/components/ui/Badge";
import { Button } from "@/components/ui/Button";
import { Terminal, ChevronLeft, ChevronRight } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useRequireAdmin } from "@/context/AuthContext";
import * as api from "@/lib/api";

interface LogEntry {
    id: number;
    actor?: string;
    action: string;
    target_type?: string;
    target?: string;
    details?: string;
    created_at: string;
}

export default function AdminLogsPage() {
    const { isAdmin, isLoading: authLoading } = useRequireAdmin();

    const [logs, setLogs] = useState<LogEntry[]>([]);
    const [loading, setLoading] = useState(true);
    const [page, setPage] = useState(1);
    const [total, setTotal] = useState(0);
    const limit = 50;

    const fetchLogs = useCallback(async () => {
        setLoading(true);
        try {
            const data = await api.admin.getLogs({ page, limit });
            const logsData = data.logs || [];
            setLogs(logsData);
            setTotal(data.total || 0);
        } catch (error) {
            console.error("Failed to fetch logs:", error);
        } finally {
            setLoading(false);
        }
    }, [page]);

    useEffect(() => {
        if (isAdmin) {
            fetchLogs();
        }
    }, [isAdmin, fetchLogs]);

    const getActionColor = (action: string) => {
        if (action.includes("BAN")) return "text-[var(--error)]";
        if (action.includes("UNBAN")) return "text-[var(--success)]";
        if (action.includes("LICENSE")) return "text-[var(--warning)]";
        if (action.includes("LOGIN")) return "text-blue-400";
        if (action.includes("HWID")) return "text-purple-400";
        if (action.includes("FLAG")) return "text-[var(--error)]";
        return "text-white";
    };

    const totalPages = Math.ceil(total / limit);

    if (authLoading) {
        return (
            <div className="flex items-center justify-center min-h-[50vh]">
                <div className="w-8 h-8 border-2 border-[var(--accent-primary)] border-t-transparent rounded-full animate-spin" />
            </div>
        );
    }

    return (
        <motion.div
            variants={staggerContainer}
            initial="initial"
            animate="animate"
            className="space-y-8"
        >
            <motion.div variants={fadeInUp}>
                <div className="flex items-center gap-3 mb-2">
                    <div className="p-2 bg-[var(--bg-tertiary)] rounded-lg">
                        <Terminal className="w-6 h-6 text-[var(--accent-primary)]" />
                    </div>
                    <Heading className="text-3xl">Audit Logs</Heading>
                </div>
                <Text>Track all system, admin, and user actions.</Text>
            </motion.div>

            <motion.div variants={fadeInUp}>
                <LoadingTransition loading={loading} skeleton={<Loader />}>
                    {logs.length > 0 ? (
                        <Table
                            data={logs}
                            columns={[
                                {
                                    header: "Time",
                                    accessorKey: "created_at",
                                    className: "text-[var(--text-muted)] whitespace-nowrap",
                                    cell: (item) => {
                                        const date = new Date(item.created_at);
                                        return `${date.toLocaleDateString()} ${date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}`;
                                    }
                                },
                                {
                                    header: "Actor",
                                    accessorKey: "actor",
                                    cell: (item) => <span className="text-white font-medium">{item.actor || 'System'}</span>
                                },
                                {
                                    header: "Action",
                                    accessorKey: "action",
                                    cell: (item) => (
                                        <Badge variant="default" className={getActionColor(item.action)}>
                                            {item.action}
                                        </Badge>
                                    )
                                },
                                {
                                    header: "Target",
                                    accessorKey: "target",
                                    cell: (item) => {
                                        if (!item.target) return <span className="text-[var(--text-muted)]">-</span>;
                                        return <span>{item.target}</span>;
                                    }
                                },
                                {
                                    header: "Details",
                                    accessorKey: "details",
                                    cell: (item) => <span className="text-[var(--text-muted)] truncate max-w-[200px] block">{item.details || "-"}</span>
                                },
                            ]}
                        />
                    ) : (
                        <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-12 text-center">
                            <Terminal className="w-12 h-12 text-[var(--text-muted)] mx-auto mb-4" />
                            <Text>No logs found.</Text>
                        </div>
                    )}
                </LoadingTransition>

                {/* Pagination */}
                {totalPages > 1 && (
                    <div className="flex items-center justify-between mt-6">
                        <Text className="text-sm">{total} log entries</Text>
                        <div className="flex items-center gap-2">
                            <Button
                                size="sm"
                                variant="secondary"
                                disabled={page === 1}
                                onClick={() => setPage(p => p - 1)}
                                icon={<ChevronLeft className="w-4 h-4" />}
                            >
                                Prev
                            </Button>
                            <span className="text-sm text-[var(--text-secondary)]">
                                Page {page} of {totalPages}
                            </span>
                            <Button
                                size="sm"
                                variant="secondary"
                                disabled={page >= totalPages}
                                onClick={() => setPage(p => p + 1)}
                                icon={<ChevronRight className="w-4 h-4" />}
                            >
                                Next
                            </Button>
                        </div>
                    </div>
                )}
            </motion.div>
        </motion.div>
    );
}
