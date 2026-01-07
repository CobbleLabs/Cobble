"use client";

import { useState, useEffect, useCallback } from "react";
import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { Button } from "@/components/ui/Button";
import { Badge } from "@/components/ui/Badge";
import { Modal } from "@/components/ui/Modal";
import { Input } from "@/components/ui/Input";
import { User, Shield, HardDrive, Ban, Undo2, Plus, AlertTriangle, CheckCircle, Timer, RefreshCw, Trash2 } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useParams, useRouter } from "next/navigation";
import { useToast } from "@/context/ToastContext";
import { useRequireAdmin } from "@/context/AuthContext";
import * as api from "@/lib/api";

export default function UserDetailPage() {
    const { isAdmin, isLoading: authLoading } = useRequireAdmin();
    const { showToast } = useToast();
    const params = useParams();
    const router = useRouter();
    const userId = params.id as string;

    const [user, setUser] = useState<api.AdminUser | null>(null);
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState<string | null>(null);

    // Modal states
    const [isBanModalOpen, setIsBanModalOpen] = useState(false);
    const [isUnbanModalOpen, setIsUnbanModalOpen] = useState(false);
    const [isExtendModalOpen, setIsExtendModalOpen] = useState(false);
    const [isCreateLicenseModalOpen, setIsCreateLicenseModalOpen] = useState(false);
    const [isResetHwidModalOpen, setIsResetHwidModalOpen] = useState(false);
    const [isDeleteUserModalOpen, setIsDeleteUserModalOpen] = useState(false);
    const [modalLoading, setModalLoading] = useState(false);

    // Form states
    const [banReason, setBanReason] = useState("");
    const [extendDays, setExtendDays] = useState("30");
    const [licensePlan, setLicensePlan] = useState("monthly");
    const [licenseDays, setLicenseDays] = useState("30");

    const fetchUser = useCallback(async () => {
        setLoading(true);
        setError(null);
        try {
            const data = await api.admin.getUser(userId);
            setUser(data);
        } catch (err) {
            console.error("Failed to fetch user:", err);
            setError("Failed to load user");
        } finally {
            setLoading(false);
        }
    }, [userId]);

    useEffect(() => {
        if (isAdmin && userId) {
            fetchUser();
        }
    }, [isAdmin, userId, fetchUser]);

    const handleBan = async () => {
        if (!user) return;
        setModalLoading(true);
        try {
            await api.admin.banUser(user.id, banReason);
            showToast({ title: "User Banned", message: `${user.username} has been banned`, type: "success" });
            setIsBanModalOpen(false);
            setBanReason("");
            fetchUser();
        } catch (err) {
            const msg = err instanceof Error ? err.message : "Failed to ban user";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleUnban = async () => {
        if (!user) return;
        setModalLoading(true);
        try {
            await api.admin.unbanUser(user.id);
            showToast({ title: "User Unbanned", message: `${user.username} has been unbanned`, type: "success" });
            setIsUnbanModalOpen(false);
            fetchUser();
        } catch (err) {
            const msg = err instanceof Error ? err.message : "Failed to unban user";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleExtendLicense = async () => {
        if (!user?.license) return;
        setModalLoading(true);
        try {
            await api.admin.extendLicense(user.license.id, parseInt(extendDays));
            showToast({ title: "License Extended", message: `Added ${extendDays} days`, type: "success" });
            setIsExtendModalOpen(false);
            setExtendDays("30");
            fetchUser();
        } catch (err) {
            const msg = err instanceof Error ? err.message : "Failed to extend license";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleCreateLicense = async () => {
        if (!user) return;
        setModalLoading(true);
        try {
            await api.admin.createLicense(
                user.id,
                licensePlan,
                licensePlan === "lifetime" ? undefined : parseInt(licenseDays)
            );
            showToast({ title: "License Created", message: `${licensePlan} license created`, type: "success" });
            setIsCreateLicenseModalOpen(false);
            setLicensePlan("monthly");
            setLicenseDays("30");
            fetchUser();
        } catch (err) {
            const msg = err instanceof Error ? err.message : "Failed to create license";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleResetHwid = async () => {
        if (!user) return;
        setModalLoading(true);
        try {
            await api.admin.resetHwid(user.id);
            showToast({ title: "HWID Reset", message: `HWID has been reset for ${user.username}`, type: "success" });
            setIsResetHwidModalOpen(false);
            fetchUser();
        } catch (err) {
            const msg = err instanceof Error ? err.message : "Failed to reset HWID";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleDeleteUser = async () => {
        if (!user) return;
        setModalLoading(true);
        try {
            await api.admin.deleteUser(user.id);
            showToast({ title: "User Deleted", message: `${user.username} has been permanently deleted`, type: "success" });
            router.push("/dashboard/admin/users");
        } catch (err) {
            const msg = err instanceof Error ? err.message : "Failed to delete user";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    if (authLoading || loading) {
        return (
            <div className="flex items-center justify-center min-h-[50vh]">
                <div className="w-8 h-8 border-2 border-[var(--accent-primary)] border-t-transparent rounded-full animate-spin" />
            </div>
        );
    }

    if (error || !user) {
        return (
            <div className="flex flex-col items-center justify-center min-h-[50vh] gap-4">
                <Text className="text-[var(--error)]">{error || "User not found"}</Text>
                <Button variant="secondary" onClick={() => router.back()}>Go Back</Button>
            </div>
        );
    }

    const isBanned = !!user.banned_at;

    return (
        <motion.div
            variants={staggerContainer}
            initial="initial"
            animate="animate"
            className="space-y-8"
        >
            {/* Header */}
            <motion.div variants={fadeInUp} className="flex items-center justify-between">
                <div className="flex items-center gap-4">
                    <Button variant="ghost" size="sm" onClick={() => router.back()} icon={<Undo2 className="w-4 h-4" />}>
                        Back
                    </Button>
                    <div>
                        <Heading className="text-3xl flex items-center gap-3">
                            {user.username}
                            <Badge variant={isBanned ? "error" : "success"}>
                                {isBanned ? "Banned" : "Active"}
                            </Badge>
                        </Heading>
                        <Text className="text-sm text-[var(--text-muted)]">ID: {user.id}</Text>
                    </div>
                </div>
                <div className="flex gap-3">
                    {isBanned ? (
                        <Button variant="secondary" icon={<CheckCircle className="w-4 h-4" />} onClick={() => setIsUnbanModalOpen(true)}>
                            Unban User
                        </Button>
                    ) : (
                        <Button variant="danger" icon={<Ban className="w-4 h-4" />} onClick={() => setIsBanModalOpen(true)}>
                            Ban User
                        </Button>
                    )}
                </div>
            </motion.div>

            <div className="grid grid-cols-1 lg:grid-cols-3 gap-8">
                {/* Info Card */}
                <motion.div variants={fadeInUp} className="lg:col-span-1 space-y-6">
                    <Card>
                        <div className="flex items-center gap-3 mb-6">
                            <User className="w-5 h-5 text-[var(--accent-primary)]" />
                            <Heading className="text-xl">Basic Info</Heading>
                        </div>
                        <div className="space-y-4">
                            <div>
                                <div className="text-sm text-[var(--text-secondary)]">Email</div>
                                <div className="text-white font-medium">{user.email}</div>
                            </div>
                            <div>
                                <div className="text-sm text-[var(--text-secondary)]">Role</div>
                                <Badge variant={user.role === "admin" ? "warning" : "secondary"}>{user.role}</Badge>
                            </div>
                            <div>
                                <div className="text-sm text-[var(--text-secondary)]">Registered</div>
                                <div className="text-white text-sm">{new Date(user.created_at).toLocaleDateString()}</div>
                            </div>
                            {isBanned && user.ban_reason && (
                                <div>
                                    <div className="text-sm text-[var(--text-secondary)]">Ban Reason</div>
                                    <div className="text-[var(--error)] text-sm">{user.ban_reason}</div>
                                </div>
                            )}
                        </div>
                    </Card>

                    <Card>
                        <div className="flex items-center gap-3 mb-6">
                            <Shield className="w-5 h-5 text-[var(--accent-primary)]" />
                            <Heading className="text-xl">License</Heading>
                        </div>
                        {user.license ? (
                            <div className="space-y-4">
                                <div className="flex justify-between items-center">
                                    <span className="text-[var(--text-secondary)]">Plan</span>
                                    <Badge variant={user.license.plan === "lifetime" ? "success" : "info"}>
                                        {user.license.plan}
                                    </Badge>
                                </div>
                                {user.license.plan !== "lifetime" && user.license.expires_at && (
                                    <div className="flex justify-between items-center">
                                        <span className="text-[var(--text-secondary)]">Expires</span>
                                        <div className="flex items-center gap-2 text-sm">
                                            <Timer className="w-4 h-4" />
                                            {new Date(user.license.expires_at).toLocaleDateString()}
                                        </div>
                                    </div>
                                )}
                                <Button
                                    variant="secondary"
                                    className="w-full"
                                    size="sm"
                                    icon={<Plus className="w-4 h-4" />}
                                    onClick={() => setIsExtendModalOpen(true)}
                                >
                                    Extend License
                                </Button>
                            </div>
                        ) : (
                            <div className="space-y-4">
                                <Text className="text-[var(--text-muted)]">No license</Text>
                                <Button
                                    variant="primary"
                                    className="w-full"
                                    size="sm"
                                    icon={<Plus className="w-4 h-4" />}
                                    onClick={() => setIsCreateLicenseModalOpen(true)}
                                >
                                    Create License
                                </Button>
                            </div>
                        )}
                    </Card>
                </motion.div>

                {/* Main Content */}
                <motion.div variants={fadeInUp} className="lg:col-span-2 space-y-6">
                    {/* HWID Section */}
                    <Card>
                        <div className="flex items-center justify-between mb-6">
                            <div className="flex items-center gap-3">
                                <HardDrive className="w-5 h-5 text-purple-500" />
                                <Heading className="text-xl">Hardware ID</Heading>
                            </div>
                        </div>

                        {user.license?.hwid ? (
                            <div className="p-4 bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl text-sm mb-4 break-all text-[var(--text-secondary)] font-mono">
                                {user.license.hwid}
                            </div>
                        ) : (
                            <Text className="text-[var(--text-muted)]">No HWID bound</Text>
                        )}

                        {user.license?.hwid_last_reset && (
                            <div className="text-sm text-[var(--text-muted)] mt-2">
                                Last reset: {new Date(user.license.hwid_last_reset).toLocaleDateString()}
                            </div>
                        )}
                        {user.license?.hwid && (
                            <Button
                                variant="secondary"
                                className="w-full mt-4"
                                size="sm"
                                icon={<RefreshCw className="w-4 h-4" />}
                                onClick={() => setIsResetHwidModalOpen(true)}
                            >
                                Reset HWID
                            </Button>
                        )}
                    </Card>

                    {/* Danger Zone */}
                    <Card className="border-red-500/30">
                        <div className="flex items-center gap-3 mb-6">
                            <AlertTriangle className="w-5 h-5 text-red-500" />
                            <Heading className="text-xl text-red-400">Danger Zone</Heading>
                        </div>
                        <Text className="text-[var(--text-muted)] mb-4 text-sm">
                            Permanently delete this user account and all associated data.
                        </Text>
                        <Button
                            variant="danger"
                            className="w-full"
                            size="sm"
                            icon={<Trash2 className="w-4 h-4" />}
                            onClick={() => setIsDeleteUserModalOpen(true)}
                        >
                            Delete User
                        </Button>
                    </Card>
                </motion.div>
            </div>

            {/* Ban Modal */}
            <Modal
                isOpen={isBanModalOpen}
                onClose={() => setIsBanModalOpen(false)}
                title="Ban User"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsBanModalOpen(false)}>Cancel</Button>
                        <Button variant="danger" onClick={handleBan} loading={modalLoading}>Ban User</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <div className="p-4 bg-red-500/10 border border-red-500/20 rounded-xl flex items-start gap-3 text-red-400">
                        <AlertTriangle className="w-5 h-5 shrink-0 mt-0.5" />
                        <div className="text-sm">
                            This action will immediately revoke access for <strong>{user.username}</strong>.
                        </div>
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
                isOpen={isUnbanModalOpen}
                onClose={() => setIsUnbanModalOpen(false)}
                title="Unban User"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsUnbanModalOpen(false)}>Cancel</Button>
                        <Button onClick={handleUnban} loading={modalLoading}>Unban User</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Text>
                        Are you sure you want to unban <strong>{user.username}</strong>?
                    </Text>
                    {user.ban_reason && (
                        <div className="p-4 bg-[var(--bg-tertiary)] rounded-xl text-sm">
                            <span className="text-[var(--text-secondary)]">Ban reason: </span>
                            <span className="text-white">{user.ban_reason}</span>
                        </div>
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
                    <Text>
                        Extending license for <strong>{user.username}</strong>.
                    </Text>
                    <Input
                        label="Days to Add"
                        type="number"
                        value={extendDays}
                        onChange={(e) => setExtendDays(e.target.value)}
                    />
                </div>
            </Modal>

            {/* Create License Modal */}
            <Modal
                isOpen={isCreateLicenseModalOpen}
                onClose={() => setIsCreateLicenseModalOpen(false)}
                title="Create License"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsCreateLicenseModalOpen(false)}>Cancel</Button>
                        <Button onClick={handleCreateLicense} loading={modalLoading}>Create License</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Text>
                        Creating license for <strong>{user.username}</strong>.
                    </Text>
                    <div>
                        <label className="text-sm font-medium text-[var(--text-secondary)] mb-1.5 block">Plan Type</label>
                        <select
                            className="w-full bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl px-4 py-3 text-white outline-none focus:border-[var(--accent-primary)] transition-colors"
                            value={licensePlan}
                            onChange={(e) => setLicensePlan(e.target.value)}
                        >
                            <option value="monthly">Monthly</option>
                            <option value="lifetime">Lifetime</option>
                        </select>
                    </div>
                    {licensePlan !== "lifetime" && (
                        <Input
                            label="Duration (Days)"
                            type="number"
                            value={licenseDays}
                            onChange={(e) => setLicenseDays(e.target.value)}
                        />
                    )}
                </div>
            </Modal>

            {/* Reset HWID Modal */}
            <Modal
                isOpen={isResetHwidModalOpen}
                onClose={() => setIsResetHwidModalOpen(false)}
                title="Reset HWID"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsResetHwidModalOpen(false)}>Cancel</Button>
                        <Button onClick={handleResetHwid} loading={modalLoading}>Reset HWID</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Text>
                        Are you sure you want to reset the Hardware ID for <strong>{user.username}</strong>?
                    </Text>
                    <div className="p-4 bg-yellow-500/10 border border-yellow-500/20 rounded-xl text-yellow-400 text-sm">
                        This will allow them to bind a new device. Their current HWID will be cleared.
                    </div>
                </div>
            </Modal>

            {/* Delete User Modal */}
            <Modal
                isOpen={isDeleteUserModalOpen}
                onClose={() => setIsDeleteUserModalOpen(false)}
                title="Delete User"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsDeleteUserModalOpen(false)}>Cancel</Button>
                        <Button variant="danger" onClick={handleDeleteUser} loading={modalLoading}>Delete Permanently</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <div className="p-4 bg-red-500/10 border border-red-500/20 rounded-xl flex items-start gap-3 text-red-400">
                        <AlertTriangle className="w-5 h-5 shrink-0 mt-0.5" />
                        <div className="text-sm">
                            <strong>This action cannot be undone.</strong> This will permanently delete the account for <strong>{user.username}</strong> including their license, sessions, and all associated data.
                        </div>
                    </div>
                </div>
            </Modal>
        </motion.div>
    );
}
