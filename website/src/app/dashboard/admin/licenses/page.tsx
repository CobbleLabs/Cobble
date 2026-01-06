"use client";

import { useState, useEffect, useCallback } from "react";
import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Table } from "@/components/ui/Table";
import { Loader } from "@/components/ui/Loader";
import { LoadingTransition } from "@/components/ui/LoadingTransition";
import { Badge } from "@/components/ui/Badge";
import { Button } from "@/components/ui/Button";
import { Input } from "@/components/ui/Input";
import { Modal } from "@/components/ui/Modal";
import { Plus, Timer, Search, ChevronLeft, ChevronRight } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useToast } from "@/context/ToastContext";
import { useRequireAdmin } from "@/context/AuthContext";
import * as api from "@/lib/api";
import Link from "next/link";

interface LicenseWithUser {
    id: number;
    user_id: string;
    username: string;
    email: string;
    plan: string;
    expires_at: string | null;
    hwid: string | null;
    created_at: string;
}

export default function AdminLicensesPage() {
    const { isAdmin, isLoading: authLoading } = useRequireAdmin();
    const { showToast } = useToast();

    const [licenses, setLicenses] = useState<LicenseWithUser[]>([]);
    const [loading, setLoading] = useState(true);
    const [search, setSearch] = useState("");
    const [page, setPage] = useState(1);
    const [total, setTotal] = useState(0);
    const limit = 20;



    // Modal states
    const [isCreateModalOpen, setIsCreateModalOpen] = useState(false);
    const [isExtendModalOpen, setIsExtendModalOpen] = useState(false);
    const [selectedLicense, setSelectedLicense] = useState<LicenseWithUser | null>(null);
    const [modalLoading, setModalLoading] = useState(false);

    // Create modal form
    const [createUserId, setCreateUserId] = useState("");
    const [createPlan, setCreatePlan] = useState("monthly");
    const [createDays, setCreateDays] = useState("30");

    // Extend modal form
    const [extendDays, setExtendDays] = useState("30");

    const fetchLicenses = useCallback(async (searchTerm: string, pageNum: number) => {
        setLoading(true);
        try {
            // Fetch all users and filter those with licenses
            const data = await api.admin.getUsers({ search: searchTerm || undefined, page: pageNum, limit });
            const usersWithLicenses: LicenseWithUser[] = (data.users || [])
                .filter(u => u.license)
                .map(u => ({
                    id: u.license!.id,
                    user_id: u.id,
                    username: u.username,
                    email: u.email,
                    plan: u.license!.plan,
                    expires_at: u.license!.expires_at || null,
                    hwid: u.license!.hwid || null,
                    created_at: u.license!.created_at,
                }));
            setLicenses(usersWithLicenses);
            setTotal(usersWithLicenses.length);
        } catch (error) {
            console.error("Failed to fetch licenses:", error);
            showToast({ title: "Error", message: "Failed to load licenses", type: "error" });
        } finally {
            setLoading(false);
        }
    }, [showToast]);

    useEffect(() => {
        if (isAdmin) {
            fetchLicenses(search, page);
        }
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [isAdmin, page]);

    // Debounced search
    useEffect(() => {
        if (!isAdmin) return;
        const timer = setTimeout(() => {
            setPage(1);
            fetchLicenses(search, 1);
        }, 300);
        return () => clearTimeout(timer);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [search]);

    const handleCreateLicense = async () => {
        if (!createUserId.trim()) {
            showToast({ title: "Error", message: "User ID is required", type: "error" });
            return;
        }

        setModalLoading(true);
        try {
            await api.admin.createLicense(
                createUserId.trim(),
                createPlan,
                createPlan === "lifetime" ? undefined : parseInt(createDays)
            );
            showToast({ title: "License Created", message: `${createPlan} license created`, type: "success" });
            setIsCreateModalOpen(false);
            setCreateUserId("");
            setCreatePlan("monthly");
            setCreateDays("30");
            fetchLicenses(search, page);
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to create license";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleExtendLicense = async () => {
        if (!selectedLicense) return;

        setModalLoading(true);
        try {
            await api.admin.extendLicense(selectedLicense.id, parseInt(extendDays));
            showToast({ title: "License Extended", message: `Added ${extendDays} days to ${selectedLicense.username}`, type: "success" });
            setIsExtendModalOpen(false);
            setSelectedLicense(null);
            setExtendDays("30");
            fetchLicenses(search, page);
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to extend license";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const openExtendModal = (license: LicenseWithUser) => {
        setSelectedLicense(license);
        setExtendDays("30");
        setIsExtendModalOpen(true);
    };

    const isExpired = (expiresAt: string | null) => {
        if (!expiresAt) return false;
        return new Date(expiresAt) < new Date();
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
            <motion.div variants={fadeInUp} className="flex items-center justify-between">
                <div>
                    <Heading className="text-3xl">Licenses</Heading>
                    <Text>Manage user licenses and subscriptions.</Text>
                </div>
                <Button icon={<Plus className="w-4 h-4" />} onClick={() => setIsCreateModalOpen(true)}>
                    Add License
                </Button>
            </motion.div>

            {/* Search */}
            <motion.div variants={fadeInUp}>
                <div className="relative max-w-md">
                    <Search className="absolute left-4 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-muted)]" />
                    <input
                        type="text"
                        placeholder="Search by username or email..."
                        className="w-full bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl pl-11 pr-4 py-3 text-white placeholder-[var(--text-muted)] outline-none focus:border-[var(--accent-primary)] transition-colors"
                        value={search}
                        onChange={(e) => setSearch(e.target.value)}
                    />
                </div>
            </motion.div>

            <motion.div variants={fadeInUp}>
                <LoadingTransition loading={loading} skeleton={<Loader />}>
                    <Table
                        data={licenses}
                        columns={[
                            {
                                header: "User",
                                accessorKey: "username",
                                cell: (item) => (
                                    <Link href={`/dashboard/admin/users/${item.user_id}`} className="font-semibold text-white hover:text-[var(--accent-primary)] transition-colors">
                                        {item.username}
                                    </Link>
                                )
                            },
                            {
                                header: "Plan",
                                accessorKey: "plan",
                                cell: (item) => (
                                    <Badge variant={item.plan === "lifetime" ? "success" : "info"}>
                                        {item.plan}
                                    </Badge>
                                )
                            },
                            {
                                header: "Expires",
                                accessorKey: "expires_at",
                                cell: (item) => (
                                    <div className="flex items-center gap-2">
                                        <Timer className="w-4 h-4 text-[var(--text-secondary)]" />
                                        {item.plan === "lifetime"
                                            ? "Never"
                                            : item.expires_at
                                                ? new Date(item.expires_at).toLocaleDateString()
                                                : "N/A"}
                                    </div>
                                )
                            },
                            {
                                header: "Status",
                                cell: (item) => (
                                    <Badge variant={item.plan === "lifetime" || !isExpired(item.expires_at) ? "success" : "error"}>
                                        {item.plan === "lifetime" || !isExpired(item.expires_at) ? "Active" : "Expired"}
                                    </Badge>
                                )
                            },
                            {
                                header: "Actions",
                                cell: (item) => (
                                    <div className="flex gap-2">
                                        <Button
                                            variant="ghost"
                                            size="sm"
                                            onClick={() => openExtendModal(item)}
                                        >
                                            Extend
                                        </Button>
                                    </div>
                                )
                            }
                        ]}
                    />
                </LoadingTransition>

                {/* Pagination */}
                {!loading && totalPages > 1 && (
                    <div className="flex items-center justify-center gap-4 pt-6">
                        <Button
                            variant="ghost"
                            size="sm"
                            onClick={() => setPage(p => Math.max(1, p - 1))}
                            disabled={page === 1}
                            icon={<ChevronLeft className="w-4 h-4" />}
                        >
                            Previous
                        </Button>
                        <Text className="text-sm">
                            Page {page} of {totalPages}
                        </Text>
                        <Button
                            variant="ghost"
                            size="sm"
                            onClick={() => setPage(p => Math.min(totalPages, p + 1))}
                            disabled={page === totalPages}
                            icon={<ChevronRight className="w-4 h-4" />}
                        >
                            Next
                        </Button>
                    </div>
                )}
            </motion.div>

            {/* Create License Modal */}
            <Modal
                isOpen={isCreateModalOpen}
                onClose={() => setIsCreateModalOpen(false)}
                title="Create License"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsCreateModalOpen(false)}>Cancel</Button>
                        <Button onClick={handleCreateLicense} loading={modalLoading}>Create License</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input
                        label="User ID"
                        placeholder="Enter user UUID"
                        value={createUserId}
                        onChange={(e) => setCreateUserId(e.target.value)}
                    />
                    <div>
                        <label className="text-sm font-medium text-[var(--text-secondary)] mb-1.5 block">Plan Type</label>
                        <select
                            className="w-full bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl px-4 py-3 text-white outline-none focus:border-[var(--accent-primary)] transition-colors"
                            value={createPlan}
                            onChange={(e) => setCreatePlan(e.target.value)}
                        >
                            <option value="monthly">Monthly</option>
                            <option value="lifetime">Lifetime</option>
                        </select>
                    </div>
                    {createPlan !== "lifetime" && (
                        <Input
                            label="Duration (Days)"
                            type="number"
                            value={createDays}
                            onChange={(e) => setCreateDays(e.target.value)}
                        />
                    )}
                </div>
            </Modal>

            {/* Extend License Modal */}
            <Modal
                isOpen={isExtendModalOpen}
                onClose={() => setIsExtendModalOpen(false)}
                title="Extend License"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsExtendModalOpen(false)}>Cancel</Button>
                        <Button onClick={handleExtendLicense} loading={modalLoading}>Extend License</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    {selectedLicense && (
                        <Text>
                            Extending license for <strong>{selectedLicense.username}</strong>.
                            Current plan: <Badge variant="info">{selectedLicense.plan}</Badge>
                        </Text>
                    )}
                    <Input
                        label="Days to Add"
                        type="number"
                        value={extendDays}
                        onChange={(e) => setExtendDays(e.target.value)}
                    />
                </div>
            </Modal>
        </motion.div>
    );
}
