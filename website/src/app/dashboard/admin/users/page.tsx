"use client";

import { useState, useEffect, useCallback, useRef } from "react";
import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Table } from "@/components/ui/Table";
import { Loader } from "@/components/ui/Loader";
import { LoadingTransition } from "@/components/ui/LoadingTransition";
import { Badge } from "@/components/ui/Badge";
import { Button } from "@/components/ui/Button";
import { Input } from "@/components/ui/Input";
import { Modal } from "@/components/ui/Modal";
import { Search, Eye, Plus, Timer, ChevronLeft, ChevronRight } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import Link from "next/link";
import { useToast } from "@/context/ToastContext";
import { useRequireAdmin } from "@/context/AuthContext";
import * as api from "@/lib/api";

export default function AdminUsersPage() {
    const { isAdmin, isLoading: authLoading } = useRequireAdmin();
    const { showToast } = useToast();

    const [users, setUsers] = useState<api.AdminUser[]>([]);
    const [loading, setLoading] = useState(true);
    const [search, setSearch] = useState("");
    const [page, setPage] = useState(1);
    const [total, setTotal] = useState(0);
    const limit = 20;

    const [selectedUser, setSelectedUser] = useState<api.AdminUser | null>(null);
    const [modalType, setModalType] = useState<"extend" | "ban" | "unban" | "license" | null>(null);
    const [modalLoading, setModalLoading] = useState(false);

    // Form states
    const [banReason, setBanReason] = useState("");
    const [extendDays, setExtendDays] = useState("30");
    const [licensePlan, setLicensePlan] = useState("monthly");
    const [licenseDays, setLicenseDays] = useState("30");

    const fetchUsers = useCallback(async (searchTerm: string, pageNum: number) => {
        setLoading(true);
        try {
            const data = await api.admin.getUsers({ search: searchTerm || undefined, page: pageNum, limit });
            const usersData = data.users || [];
            setUsers(usersData);
            setTotal(data.total || 0);
        } catch (error) {
            console.error("Failed to fetch users:", error);
            showToast({ title: "Error", message: "Failed to load users", type: "error" });
        } finally {
            setLoading(false);
        }
    }, [showToast]);

    // Initial fetch when admin status is confirmed
    useEffect(() => {
        if (isAdmin) {
            fetchUsers(search, page);
        }
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [isAdmin, page]); // Only fetch on mount and page change

    // Debounced search - only triggers new search, not initial load
    useEffect(() => {
        if (!isAdmin) return;
        const timer = setTimeout(() => {
            setPage(1); // Reset to page 1 on search
            fetchUsers(search, 1);
        }, 300);
        return () => clearTimeout(timer);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [search]); // Only when search changes

    const openModal = (type: typeof modalType, user?: api.AdminUser) => {
        setSelectedUser(user || null);
        setModalType(type);
        setBanReason("");
        setExtendDays("30");
    };

    const closeModal = () => {
        setSelectedUser(null);
        setModalType(null);
    };

    const handleBan = async () => {
        if (!selectedUser) return;
        setModalLoading(true);
        try {
            await api.admin.banUser(selectedUser.id, banReason);
            showToast({ title: "User Banned", message: `${selectedUser.username} has been banned`, type: "success" });
            closeModal();
            fetchUsers(search, page);
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to ban user";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleUnban = async () => {
        if (!selectedUser) return;
        setModalLoading(true);
        try {
            await api.admin.unbanUser(selectedUser.id);
            showToast({ title: "User Unbanned", message: `${selectedUser.username} has been unbanned`, type: "success" });
            closeModal();
            fetchUsers(search, page);
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to unban user";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleExtend = async () => {
        if (!selectedUser?.license) return;
        setModalLoading(true);
        try {
            await api.admin.extendLicense(selectedUser.license.id, parseInt(extendDays));
            showToast({ title: "License Extended", message: `Added ${extendDays} days`, type: "success" });
            closeModal();
            fetchUsers(search, page);
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to extend license";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleCreateLicense = async () => {
        if (!selectedUser) return;
        setModalLoading(true);
        try {
            // Monthly = 30 days, Lifetime = no expiry, Custom = user input
            const duration = licensePlan === "lifetime" ? undefined :
                licensePlan === "monthly" ? 30 :
                    parseInt(licenseDays);

            await api.admin.createLicense(
                selectedUser.id,
                licensePlan,
                duration
            );
            showToast({ title: "License Created", message: `${licensePlan} license created`, type: "success" });
            closeModal();
            fetchUsers(search, page);
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to create license";
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
                    <Heading className="text-3xl">Users</Heading>
                    <Text>Manage all registered users and their subscriptions.</Text>
                </div>
                <div className="flex gap-4">
                    <div className="relative">
                        <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-secondary)]" />
                        <Input
                            className="pl-10 w-64"
                            placeholder="Search users..."
                            value={search}
                            onChange={(e) => setSearch(e.target.value)}
                        />
                    </div>
                </div>
            </motion.div>

            <motion.div variants={fadeInUp}>
                <LoadingTransition loading={loading} skeleton={<Loader />}>
                    {users.length > 0 ? (
                        <Table
                            data={users}
                            columns={[
                                { header: "Username", accessorKey: "username", className: "font-semibold text-white" },
                                { header: "Email", accessorKey: "email" },
                                {
                                    header: "Plan",
                                    accessorKey: "license",
                                    cell: (item) => {
                                        if (!item.license) return <Badge variant="secondary">No License</Badge>;

                                        // Check if license is expired (not lifetime and has expiry in the past)
                                        const isExpired = item.license.plan !== "lifetime" &&
                                            item.license.expires_at &&
                                            new Date(item.license.expires_at) < new Date();

                                        if (isExpired) {
                                            return <Badge variant="warning">Expired</Badge>;
                                        }

                                        const variant = item.license.plan === "lifetime" ? "success" : "info";
                                        return <Badge variant={variant}>{item.license.plan}</Badge>;
                                    }
                                },
                                {
                                    header: "Expires",
                                    accessorKey: "license",
                                    cell: (item) => {
                                        if (!item.license) return <span className="text-[var(--text-muted)]">-</span>;
                                        if (item.license.plan === "lifetime") return <span className="text-[var(--success)]">Never</span>;
                                        if (!item.license.expires_at) return <span className="text-[var(--text-muted)]">-</span>;
                                        return (
                                            <div className="flex items-center gap-2">
                                                <Timer className="w-4 h-4 text-[var(--text-secondary)]" />
                                                {new Date(item.license.expires_at).toLocaleDateString()}
                                            </div>
                                        );
                                    }
                                },
                                {
                                    header: "Status",
                                    cell: (item) => (
                                        <Badge variant={item.banned_at ? "error" : "success"}>
                                            {item.banned_at ? "Banned" : "Active"}
                                        </Badge>
                                    )
                                },
                                {
                                    header: "Actions",
                                    cell: (item) => (
                                        <div className="flex gap-2">
                                            <Link href={`/dashboard/admin/users/${item.id}`}>
                                                <Button size="sm" variant="secondary" icon={<Eye className="w-3 h-3" />}>View</Button>
                                            </Link>
                                            {item.license ? (
                                                <Button size="sm" variant="secondary" onClick={() => openModal("extend", item)}>
                                                    Extend
                                                </Button>
                                            ) : (
                                                <Button size="sm" variant="secondary" icon={<Plus className="w-3 h-3" />} onClick={() => openModal("license", item)}>
                                                    License
                                                </Button>
                                            )}
                                            {item.banned_at ? (
                                                <Button size="sm" variant="secondary" onClick={() => openModal("unban", item)}>
                                                    Unban
                                                </Button>
                                            ) : (
                                                <Button size="sm" variant="danger" onClick={() => openModal("ban", item)}>
                                                    Ban
                                                </Button>
                                            )}
                                        </div>
                                    )
                                }
                            ]}
                        />
                    ) : (
                        <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-12 text-center">
                            <Text>No users found.</Text>
                        </div>
                    )}
                </LoadingTransition>

                {/* Pagination */}
                {!loading && totalPages > 1 && (
                    <div className="flex items-center justify-between mt-6">
                        <Text className="text-sm">{total} users total</Text>
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

            {/* Create License Modal */}
            <Modal
                isOpen={modalType === "license"}
                onClose={closeModal}
                title="Create License"
                footer={
                    <>
                        <Button variant="ghost" onClick={closeModal}>Cancel</Button>
                        <Button onClick={handleCreateLicense} loading={modalLoading}>Create License</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Text>
                        Creating license for <strong>{selectedUser?.username}</strong>.
                    </Text>
                    <div>
                        <label className="text-sm font-medium text-[var(--text-secondary)] mb-1.5 block">Plan Type</label>
                        <select
                            className="w-full bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl px-4 py-3 text-white outline-none focus:border-[var(--accent-primary)] transition-colors"
                            value={licensePlan}
                            onChange={(e) => setLicensePlan(e.target.value)}
                        >
                            <option value="monthly">Monthly (30 days)</option>
                            <option value="lifetime">Lifetime</option>
                            <option value="custom">Custom</option>
                        </select>
                    </div>
                    {licensePlan === "custom" && (
                        <Input
                            label="Duration (Days)"
                            type="number"
                            value={licenseDays}
                            onChange={(e) => setLicenseDays(e.target.value)}
                        />
                    )}
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
                        <Button onClick={handleExtend} loading={modalLoading}>Confirm Extension</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Text>
                        Extending subscription for <strong>{selectedUser?.username}</strong>.
                    </Text>
                    <Input
                        label="Days to Add"
                        type="number"
                        value={extendDays}
                        onChange={(e) => setExtendDays(e.target.value)}
                    />
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
                        <Button variant="danger" onClick={handleBan} loading={modalLoading}>Confirm Ban</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <div className="p-4 bg-red-500/10 border border-red-500/20 rounded-xl text-red-400 text-sm">
                        Are you sure you want to ban <strong>{selectedUser?.username}</strong>? This will immediately revoke their access.
                    </div>
                    <Input
                        label="Reason"
                        placeholder="e.g. Chargeback, TOS violation"
                        value={banReason}
                        onChange={(e) => setBanReason(e.target.value)}
                    />
                </div>
            </Modal>

            {/* Unban Modal */}
            <Modal
                isOpen={modalType === "unban"}
                onClose={closeModal}
                title="Unban User"
                footer={
                    <>
                        <Button variant="ghost" onClick={closeModal}>Cancel</Button>
                        <Button onClick={handleUnban} loading={modalLoading}>Confirm Unban</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Text>
                        Are you sure you want to unban <strong>{selectedUser?.username}</strong>?
                    </Text>
                    {selectedUser?.ban_reason && (
                        <div className="p-4 bg-[var(--bg-tertiary)] rounded-xl text-sm">
                            <span className="text-[var(--text-secondary)]">Ban reason: </span>
                            <span className="text-white">{selectedUser.ban_reason}</span>
                        </div>
                    )}
                </div>
            </Modal>
        </motion.div>
    );
}
