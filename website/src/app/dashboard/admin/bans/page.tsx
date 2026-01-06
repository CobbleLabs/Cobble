"use client";

import { useState, useEffect, useCallback } from "react";
import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Table } from "@/components/ui/Table";
import { Badge } from "@/components/ui/Badge";
import { Button } from "@/components/ui/Button";
import { Modal } from "@/components/ui/Modal";
import { Ban, CheckCircle, Search, ChevronLeft, ChevronRight } from "lucide-react";
import { Input } from "@/components/ui/Input";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useToast } from "@/context/ToastContext";
import { useRequireAdmin } from "@/context/AuthContext";
import * as api from "@/lib/api";

export default function AdminBansPage() {
    const { isAdmin, isLoading: authLoading } = useRequireAdmin();
    const { showToast } = useToast();

    const [users, setUsers] = useState<api.AdminUser[]>([]);
    const [loading, setLoading] = useState(true);
    const [search, setSearch] = useState("");
    const [page, setPage] = useState(1);
    const [total, setTotal] = useState(0);
    const limit = 20;

    const [selectedUser, setSelectedUser] = useState<api.AdminUser | null>(null);
    const [modalLoading, setModalLoading] = useState(false);

    // Fetch only banned users
    const fetchBannedUsers = useCallback(async () => {
        setLoading(true);
        try {
            // Fetch all users and filter banned ones (backend should ideally have a filter)
            const data = await api.admin.getUsers({ search: search || undefined, page, limit });
            const bannedUsers = (data.users || []).filter(u => u.banned_at);
            setUsers(bannedUsers);
            setTotal(bannedUsers.length);
        } catch (error) {
            console.error("Failed to fetch bans:", error);
            showToast({ title: "Error", message: "Failed to load bans", type: "error" });
        } finally {
            setLoading(false);
        }
    }, [search, page, showToast]);

    useEffect(() => {
        if (isAdmin) {
            fetchBannedUsers();
        }
    }, [isAdmin, fetchBannedUsers]);

    // Debounced search
    useEffect(() => {
        const timer = setTimeout(() => {
            if (isAdmin) {
                setPage(1);
                fetchBannedUsers();
            }
        }, 300);
        return () => clearTimeout(timer);
    }, [search]);

    const handleUnban = async () => {
        if (!selectedUser) return;

        setModalLoading(true);
        try {
            await api.admin.unbanUser(selectedUser.id);
            showToast({ title: "User Unbanned", message: `${selectedUser.username} has been unbanned`, type: "success" });
            setSelectedUser(null);
            fetchBannedUsers();
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to unban";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
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
            <motion.div variants={fadeInUp} className="flex flex-col md:flex-row md:items-center justify-between gap-4">
                <div>
                    <Heading className="text-3xl">Bans</Heading>
                    <Text>Manage banned users.</Text>
                </div>
                <div className="relative">
                    <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-secondary)]" />
                    <Input
                        className="pl-10 w-64"
                        placeholder="Search bans..."
                        value={search}
                        onChange={(e) => setSearch(e.target.value)}
                    />
                </div>
            </motion.div>

            <motion.div variants={fadeInUp}>
                {loading ? (
                    <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8">
                        <div className="animate-pulse space-y-4">
                            {[1, 2, 3].map((i) => (
                                <div key={i} className="h-12 bg-[var(--bg-tertiary)] rounded" />
                            ))}
                        </div>
                    </div>
                ) : users.length > 0 ? (
                    <Table
                        data={users}
                        columns={[
                            { header: "User", accessorKey: "username", className: "font-semibold text-white" },
                            { header: "Email", accessorKey: "email" },
                            {
                                header: "Reason",
                                accessorKey: "ban_reason",
                                cell: (item) => (
                                    <span className="text-[var(--text-secondary)]">{item.ban_reason || "No reason"}</span>
                                )
                            },
                            {
                                header: "Date",
                                accessorKey: "banned_at",
                                cell: (item) => item.banned_at ? new Date(item.banned_at).toLocaleDateString() : "-"
                            },
                            {
                                header: "Actions",
                                cell: (item) => (
                                    <Button
                                        size="sm"
                                        variant="secondary"
                                        onClick={() => setSelectedUser(item)}
                                    >
                                        Unban
                                    </Button>
                                )
                            }
                        ]}
                    />
                ) : (
                    <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-12 text-center">
                        <Ban className="w-12 h-12 text-[var(--text-muted)] mx-auto mb-4" />
                        <Heading className="text-xl mb-2">No Banned Users</Heading>
                        <Text>All users are in good standing.</Text>
                    </div>
                )}

                {/* Pagination */}
                {totalPages > 1 && (
                    <div className="flex items-center justify-between mt-6">
                        <Text className="text-sm">{total} banned users</Text>
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

            {/* Unban Modal */}
            <Modal
                isOpen={!!selectedUser}
                onClose={() => setSelectedUser(null)}
                title="Confirm Unban"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setSelectedUser(null)}>Cancel</Button>
                        <Button onClick={handleUnban} loading={modalLoading}>Confirm Unban</Button>
                    </>
                }
            >
                <div className="text-center py-4">
                    <CheckCircle className="w-12 h-12 text-[var(--success)] mx-auto mb-4" />
                    <Heading className="text-xl mb-2">Unban {selectedUser?.username}?</Heading>
                    <Text>Are you sure you want to revoke this ban? The user will regain access immediately.</Text>
                    {selectedUser?.ban_reason && (
                        <div className="mt-4 p-3 bg-[var(--bg-tertiary)] rounded-xl text-sm">
                            <span className="text-[var(--text-secondary)]">Ban reason: </span>
                            <span className="text-white">{selectedUser.ban_reason}</span>
                        </div>
                    )}
                </div>
            </Modal>
        </motion.div>
    );
}
