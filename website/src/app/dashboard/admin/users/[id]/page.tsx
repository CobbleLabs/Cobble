"use client";

import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { Button } from "@/components/ui/Button";
import { Badge } from "@/components/ui/Badge";
import { Modal } from "@/components/ui/Modal";
import { Input } from "@/components/ui/Input";
import { User, Shield, Activity, HardDrive, Ban, Undo2, Plus, AlertTriangle } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useParams, useRouter } from "next/navigation";
import { useState } from "react";
import { useToast } from "@/context/ToastContext";

export default function UserDetailPage() {
    const { showToast } = useToast();
    const params = useParams(); // params.id
    const router = useRouter();
    const [isBanModalOpen, setIsBanModalOpen] = useState(false);
    const [isExtendModalOpen, setIsExtendModalOpen] = useState(false);

    // Mock data
    const user = {
        username: "xac",
        email: "xac@example.com",
        discord: "xac#123",
        ip: "192.168.1.1",
        plan: "Lifetime",
        status: "Active",
        hwid: "HWID-1234-5678-90AB"
    };

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
                            <Badge variant="success">{user.status}</Badge>
                        </Heading>
                        <Text className="text-sm text-[var(--text-muted)]">ID: {params.id}</Text>
                    </div>
                </div>
                <div className="flex gap-3">
                    <Button variant="danger" icon={<Ban className="w-4 h-4" />} onClick={() => setIsBanModalOpen(true)}>
                        Ban User
                    </Button>
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
                                <div className="text-sm text-[var(--text-secondary)]">Discord</div>
                                <div className="text-white font-medium">{user.discord}</div>
                            </div>
                            <div>
                                <div className="text-sm text-[var(--text-secondary)]">Latest IP</div>
                                <div className="text-white text-sm">{user.ip}</div>
                            </div>
                        </div>
                    </Card>

                    <Card>
                        <div className="flex items-center gap-3 mb-6">
                            <Shield className="w-5 h-5 text-[var(--accent-primary)]" />
                            <Heading className="text-xl">License</Heading>
                        </div>
                        <div className="space-y-4">
                            <div className="flex justify-between items-center">
                                <span className="text-[var(--text-secondary)]">Plan</span>
                                <Badge variant="success">{user.plan}</Badge>
                            </div>
                            <Button
                                variant="secondary"
                                className="w-full"
                                size="sm"
                                icon={<Plus className="w-4 h-4" />}
                                onClick={() => setIsExtendModalOpen(true)}
                            >
                                Add/Extend License
                            </Button>
                        </div>
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
                            <Button variant="secondary" size="sm" onClick={() => showToast({ title: "HWID Reset", message: "User's HWID has been successfully reset.", type: "success" })}>Reset HWID</Button>
                        </div>

                        <div className="p-4 bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl text-sm mb-4 break-all text-[var(--text-secondary)]">
                            {user.hwid}
                        </div>

                        <div className="space-y-3">
                            <div className="text-sm font-semibold text-[var(--text-secondary)]">History</div>
                            <div className="flex items-center justify-between text-sm py-2 border-b border-[var(--border-color)]">
                                <span className="text-[var(--text-muted)]">HWID-OLD-111...</span>
                                <span className="text-[var(--text-muted)]">3 months ago</span>
                            </div>
                            <div className="flex items-center justify-between text-sm py-2 border-b border-[var(--border-color)]">
                                <span className="text-[var(--text-muted)]">HWID-OLD-222...</span>
                                <span className="text-[var(--text-muted)]">6 months ago</span>
                            </div>
                        </div>
                    </Card>

                    {/* Activity Log */}
                    <Card>
                        <div className="flex items-center gap-3 mb-6">
                            <Activity className="w-5 h-5 text-blue-500" />
                            <Heading className="text-xl">Recent Activity</Heading>
                        </div>

                        <div className="space-y-4">
                            {[1, 2, 3].map((i) => (
                                <div key={i} className="flex items-start gap-4 text-sm">
                                    <div className="w-2 h-2 rounded-full bg-[var(--accent-primary)] mt-1.5" />
                                    <div>
                                        <div className="text-white">Launched Client v2.1</div>
                                        <div className="text-[var(--text-muted)]">Today, 14:30</div>
                                    </div>
                                </div>
                            ))}
                        </div>
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
                        <Button variant="danger" onClick={() => {
                            setIsBanModalOpen(false);
                            showToast({ title: "User Banned", message: "The user has been banned successfully.", type: "error" });
                        }}>Ban User</Button>
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

                    <Input label="Reason" placeholder="e.g. Chargeback, Cheating" />
                </div>
            </Modal>

            {/* Extend License Modal */}
            <Modal
                isOpen={isExtendModalOpen}
                onClose={() => setIsExtendModalOpen(false)}
                title="Add/Extend License"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsExtendModalOpen(false)}>Cancel</Button>
                        <Button onClick={() => {
                            setIsExtendModalOpen(false);
                            showToast({ title: "License Updated", message: "User license has been extended.", type: "success" });
                        }}>Update License</Button>
                    </>
                }
            >
                <div>
                    <Input label="Days" type="number" placeholder="30" />
                    <div className="mt-4">
                        <label className="text-sm font-medium text-[var(--text-secondary)] mb-1.5 block">Update Plan (Optional)</label>
                        <select className="w-full bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl px-4 py-3 text-white outline-none focus:border-[var(--accent-primary)] transition-colors">
                            <option>No Change</option>
                            <option>Monthly</option>
                            <option>Lifetime</option>
                        </select>
                    </div>
                </div>
            </Modal>
        </motion.div>
    );
}
