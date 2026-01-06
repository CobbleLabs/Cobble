"use client";

import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Table } from "@/components/ui/Table";
import { Badge } from "@/components/ui/Badge";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { Terminal } from "lucide-react";

const mockLogs = [
    { id: 1, time: "01:43", actor: "spexey", action: "UNBAN", target: "xac", meta: "hwid cleared" },
    { id: 2, time: "22:38", actor: "064933", action: "ADD_LICENSE", target: "darling", meta: "360 days" },
    { id: 3, time: "18:12", actor: "system", action: "FLAG", target: "user_99", meta: "High CPS" },
    { id: 4, time: "15:30", actor: "xac", action: "HWID_RESET", target: "xac", meta: "self-service" },
    { id: 5, time: "14:22", actor: "darling", action: "LOGIN", target: "darling", meta: "IP: 192.168.1.1" },
    { id: 6, time: "12:05", actor: "system", action: "SECURITY_FLAG", target: "noob123", meta: "VPN detected" },
];

export default function AdminLogsPage() {
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
                <Table
                    data={mockLogs}
                    columns={[
                        { header: "Time", accessorKey: "time", className: "text-[var(--text-muted)]" },
                        {
                            header: "Actor",
                            accessorKey: "actor",
                            cell: (item) => <span className="text-white font-medium">{item.actor}</span>
                        },
                        {
                            header: "Action",
                            accessorKey: "action",
                            cell: (item) => {
                                let colorClass = "text-white";
                                if (item.action === "UNBAN") colorClass = "text-[var(--success)]";
                                if (item.action.includes("BAN")) colorClass = "text-[var(--error)]";
                                if (item.action === "ADD_LICENSE") colorClass = "text-[var(--warning)]";
                                if (item.action === "LOGIN") colorClass = "text-blue-400";
                                if (item.action === "HWID_RESET") colorClass = "text-purple-400";
                                if (item.action === "FLAG" || item.action === "SECURITY_FLAG") colorClass = "text-[var(--error)]";

                                return (
                                    <Badge variant="default" className={colorClass}>
                                        {item.action}
                                    </Badge>
                                );
                            }
                        },
                        {
                            header: "Target",
                            accessorKey: "target",
                            cell: (item) => <span>{item.target}</span>
                        },
                        {
                            header: "Meta",
                            accessorKey: "meta",
                            cell: (item) => <span className="text-[var(--text-muted)]">{item.meta}</span>
                        },
                    ]}
                />
            </motion.div>
        </motion.div>
    );
}
