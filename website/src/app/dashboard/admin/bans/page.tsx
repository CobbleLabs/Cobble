"use client";

import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Table } from "@/components/ui/Table";
import { Badge } from "@/components/ui/Badge";
import { Button } from "@/components/ui/Button";
import { Modal } from "@/components/ui/Modal";
import { Ban, CheckCircle, Search } from "lucide-react";
import { Input } from "@/components/ui/Input";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useState } from "react";
import { useToast } from "@/context/ToastContext";

const mockBans = [
    { id: 1, user: "xac", reason: "Chargeback", date: "Jan 5", by: "spexey" },
    { id: 2, user: "cheater123", reason: "Detected", date: "Jan 3", by: "anticheat" },
];

export default function AdminBansPage() {
    const { showToast } = useToast();
    const [selectedBan, setSelectedBan] = useState<any>(null);

    return (
        <motion.div
            variants={staggerContainer}
            initial="initial"
            animate="animate"
            className="space-y-8"
        >
            <motion.div variants={fadeInUp} className="flex items-center justify-between">
                <div>
                    <Heading className="text-3xl">Bans</Heading>
                    <Text>Manage banned users.</Text>
                </div>
                <div className="relative">
                    <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-secondary)]" />
                    <Input className="pl-10 w-64" placeholder="Search bans..." />
                </div>
            </motion.div>

            <motion.div variants={fadeInUp}>
                <Table
                    data={mockBans}
                    columns={[
                        { header: "User", accessorKey: "user", className: "font-semibold text-white" },
                        { header: "Reason", accessorKey: "reason" },
                        { header: "Date", accessorKey: "date" },
                        {
                            header: "By",
                            accessorKey: "by",
                            cell: (item) => (
                                <Badge variant="default">{item.by}</Badge>
                            )
                        },
                        {
                            header: "Actions",
                            cell: (item) => (
                                <Button
                                    size="sm"
                                    variant="secondary"
                                    onClick={() => setSelectedBan(item)}
                                >
                                    Unban
                                </Button>
                            )
                        }
                    ]}
                />
            </motion.div>

            <Modal
                isOpen={!!selectedBan}
                onClose={() => setSelectedBan(null)}
                title="Confirm Unban"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setSelectedBan(null)}>Cancel</Button>
                        <Button variant="primary" onClick={() => {
                            setSelectedBan(null);
                            showToast({ title: "User Unbanned", message: `Successfully revoked ban for ${selectedBan?.user}`, type: "success" });
                        }}>Confirm Unban</Button>
                    </>
                }
            >
                <div className="text-center py-4">
                    <CheckCircle className="w-12 h-12 text-[var(--success)] mx-auto mb-4" />
                    <Heading className="text-xl mb-2">Unban {selectedBan?.user}?</Heading>
                    <Text>Are you sure you want to revoke this ban? The user will regain access immediately.</Text>
                </div>
            </Modal>
        </motion.div>
    );
}
