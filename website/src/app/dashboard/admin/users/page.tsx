"use client";

import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Table } from "@/components/ui/Table";
import { Badge } from "@/components/ui/Badge";
import { Button } from "@/components/ui/Button";
import { Input } from "@/components/ui/Input";
import { Modal } from "@/components/ui/Modal";
import { Search, Eye, Ban, Plus, Timer } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import Link from "next/link";
import { useState } from "react";

const mockUsers = [
    { id: 1, username: "xac", discord: "xac#123", plan: "Lifetime", expires: "Never", status: "Active", lastSeen: "2h ago" },
    { id: 2, username: "spexey", discord: "spexey#999", plan: "Monthly", expires: "2025-06-01", status: "Active", lastSeen: "5m ago" },
    { id: 3, username: "darling", discord: "darling#001", plan: "Trial", expires: "2024-02-15", status: "Active", lastSeen: "1d ago" },
    { id: 4, username: "banned_guy", discord: "noob#000", plan: "Monthly", expires: "Expired", status: "Banned", lastSeen: "1d ago" },
];

export default function AdminUsersPage() {
    const [selectedUser, setSelectedUser] = useState<any>(null);
    const [modalType, setModalType] = useState<"extend" | "revoke" | "ban" | "add" | null>(null);

    const openModal = (type: "extend" | "revoke" | "ban" | "add", user?: any) => {
        setSelectedUser(user || null);
        setModalType(type);
    };

    const closeModal = () => {
        setSelectedUser(null);
        setModalType(null);
    };

    return (
        <motion.div
            variants={staggerContainer}
            initial="initial"
            animate="animate"
            className="space-y-8"
        >
            <motion.div variants={fadeInUp} className="flex items-center justify-between">
                <div>
                    <Heading className="text-3xl">Users</Heading>
                    <Text>Manage all registered users and their subscriptions.</Text>
                </div>
                <div className="flex gap-4">
                    <div className="relative">
                        <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-secondary)]" />
                        <Input className="pl-10 w-64" placeholder="Search users..." />
                    </div>
                    <Button icon={<Plus className="w-4 h-4" />} onClick={() => openModal("add")}>
                        Add User
                    </Button>
                </div>
            </motion.div>

            <motion.div variants={fadeInUp}>
                <Table
                    data={mockUsers}
                    columns={[
                        { header: "Username", accessorKey: "username", className: "font-semibold text-white" },
                        { header: "Discord", accessorKey: "discord" },
                        {
                            header: "Plan",
                            accessorKey: "plan",
                            cell: (item) => {
                                const variant = item.plan === "Lifetime" ? "success" : item.plan === "Monthly" ? "info" : "warning";
                                return <Badge variant={variant}>{item.plan}</Badge>;
                            }
                        },
                        {
                            header: "Expires",
                            accessorKey: "expires",
                            cell: (item) => (
                                <div className="flex items-center gap-2">
                                    <Timer className="w-4 h-4 text-[var(--text-secondary)]" />
                                    {item.expires}
                                </div>
                            )
                        },
                        {
                            header: "Status",
                            accessorKey: "status",
                            cell: (item) => (
                                <Badge variant={item.status === "Active" ? "success" : "error"}>{item.status}</Badge>
                            )
                        },
                        { header: "Last Seen", accessorKey: "lastSeen" },
                        {
                            header: "Actions",
                            cell: (item) => (
                                <div className="flex gap-2">
                                    <Link href={`/dashboard/admin/users/${item.id}`}>
                                        <Button size="sm" variant="secondary" icon={<Eye className="w-3 h-3" />}>View</Button>
                                    </Link>
                                    <Button size="sm" variant="secondary" onClick={() => openModal("extend", item)}>Extend</Button>
                                    {item.status === "Active" ? (
                                        <Button size="sm" variant="danger" onClick={() => openModal("ban", item)}>Ban</Button>
                                    ) : (
                                        <Button size="sm" variant="secondary" onClick={() => openModal("revoke", item)}>Revoke</Button>
                                    )}
                                </div>
                            )
                        }
                    ]}
                />
            </motion.div>

            {/* Add User Modal */}
            <Modal
                isOpen={modalType === "add"}
                onClose={closeModal}
                title="Add New User"
                footer={
                    <>
                        <Button variant="ghost" onClick={closeModal}>Cancel</Button>
                        <Button onClick={closeModal}>Create User</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input label="Username" placeholder="Enter username" />
                    <Input label="Discord ID" placeholder="Enter Discord ID" />
                    <Input label="Duration (Days)" type="number" placeholder="30" />
                    <div>
                        <label className="text-sm font-medium text-[var(--text-secondary)] mb-1.5 block">Plan Type</label>
                        <select className="w-full bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl px-4 py-3 text-white outline-none focus:border-[var(--accent-primary)] transition-colors">
                            <option>Monthly</option>
                            <option>Lifetime</option>
                            <option>Trial</option>
                        </select>
                    </div>
                </div>
            </Modal>

            {/* Extend Modal */}
            <Modal
                isOpen={modalType === "extend"}
                onClose={closeModal}
                title="Extend Subscription"
                footer={
                    <>
                        <Button variant="ghost" onClick={closeModal}>Cancel</Button>
                        <Button onClick={closeModal}>Confirm Extension</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Text>
                        Extending subscription for <strong>{selectedUser?.username}</strong>.
                    </Text>
                    <Input label="Days to Add" type="number" placeholder="30" />
                </div>
            </Modal>

            {/* Ban Modal */}
            <Modal
                isOpen={modalType === "ban"}
                onClose={closeModal}
                title="Ban User"
                footer={
                    <>
                        <Button variant="ghost" onClick={closeModal}>Cancel</Button>
                        <Button variant="danger" onClick={closeModal}>Confirm Ban</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <div className="p-4 bg-red-500/10 border border-red-500/20 rounded-xl text-red-400 text-sm">
                        Are you sure you want to ban <strong>{selectedUser?.username}</strong>? This will immediately revoke their access.
                    </div>
                    <Input label="Reason" placeholder="e.g. Chargeback, Cheating" />
                </div>
            </Modal>

            {/* Revoke Modal */}
            <Modal
                isOpen={modalType === "revoke"}
                onClose={closeModal}
                title="Revoke Access"
                footer={
                    <>
                        <Button variant="ghost" onClick={closeModal}>Cancel</Button>
                        <Button variant="danger" onClick={closeModal}>Confirm Revoke</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <div className="p-4 bg-red-500/10 border border-red-500/20 rounded-xl text-red-400 text-sm">
                        This will permanently remove <strong>{selectedUser?.username}</strong>&apos;s subscription. This action cannot be undone.
                    </div>
                </div>
            </Modal>
        </motion.div>
    );
}
