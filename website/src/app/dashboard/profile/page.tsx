"use client";

import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { Button } from "@/components/ui/Button";
import { Input } from "@/components/ui/Input";
import { User as UserIcon, Shield, Key, Save, Crown, Calendar } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { Modal } from "@/components/ui/Modal";
import { useState } from "react";
import { useToast } from "@/context/ToastContext";

export default function ProfilePage() {
    const { showToast } = useToast();
    const [isPasswordModalOpen, setIsPasswordModalOpen] = useState(false);

    return (
        <motion.div
            variants={staggerContainer}
            initial="initial"
            animate="animate"
            className="space-y-6"
        >
            <motion.div variants={fadeInUp}>
                <Heading className="text-3xl">My Profile</Heading>
                <Text>Manage your account settings and preferences.</Text>
            </motion.div>

            {/* Profile Overview Banner */}
            <motion.div variants={fadeInUp}>
                <div className="bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-2xl p-6 md:p-8 flex flex-col md:flex-row items-center gap-6 md:gap-8">
                    <div className="flex-shrink-0">
                        <div className="w-24 h-24 bg-[var(--accent-primary)]/10 rounded-full flex items-center justify-center border-2 border-[var(--accent-primary)] text-4xl font-bold text-[var(--accent-primary)]">
                            Q
                        </div>
                    </div>
                    <div className="flex-1 text-center md:text-left space-y-2">
                        <div>
                            <Heading className="text-2xl">qspec</Heading>
                            <Text className="text-sm">qspec@example.com</Text>
                        </div>
                        <div className="flex flex-wrap items-center justify-center md:justify-start gap-3">
                            <div className="flex items-center gap-2 px-3 py-1.5 bg-[var(--bg-tertiary)] rounded-full text-xs font-medium text-[var(--accent-primary)] border border-[var(--border-color)]">
                                <Crown className="w-3 h-3" />
                                Lifetime Plan
                            </div>
                            <div className="flex items-center gap-2 px-3 py-1.5 bg-[var(--bg-tertiary)] rounded-full text-xs font-medium text-[var(--text-secondary)] border border-[var(--border-color)]">
                                <Calendar className="w-3 h-3" />
                                Joined Jan 5, 2026
                            </div>
                        </div>
                    </div>
                </div>
            </motion.div>

            <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
                {/* Account Information */}
                <motion.div variants={fadeInUp} className="space-y-6">
                    <Card className="h-full">
                        <div className="flex items-center gap-3 mb-6">
                            <div className="p-2 bg-[var(--bg-tertiary)] rounded-lg">
                                <UserIcon className="w-5 h-5 text-[var(--accent-primary)]" />
                            </div>
                            <Heading className="text-xl">Account Details</Heading>
                        </div>
                        <div className="space-y-4">
                            <div className="grid grid-cols-2 gap-4">
                                <Input label="Username" defaultValue="qspec" disabled />
                                <Input label="Email" defaultValue="qspec@example.com" disabled />
                            </div>
                            <Input label="Discord ID" placeholder="Enter Discord ID" />
                            <Input label="In-Game Name" placeholder="Enter Minecraft IGN" />
                            <div className="pt-2 flex justify-end">
                                <Button icon={<Save className="w-4 h-4" />} onClick={() => showToast({ title: "Settings Saved", message: "Your profile information has been updated.", type: "success" })}>Save Changes</Button>
                            </div>
                        </div>
                    </Card>
                </motion.div>

                {/* Security */}
                <motion.div variants={fadeInUp} className="space-y-6">
                    <Card className="h-full">
                        <div className="flex items-center gap-3 mb-6">
                            <div className="p-2 bg-[var(--bg-tertiary)] rounded-lg">
                                <Shield className="w-5 h-5 text-[var(--accent-primary)]" />
                            </div>
                            <Heading className="text-xl">Security</Heading>
                        </div>
                        <div className="space-y-4">
                            <div className="p-5 bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl flex items-center justify-between">
                                <div>
                                    <div className="font-medium text-white mb-1">Password</div>
                                    <div className="text-sm text-[var(--text-secondary)]">Last changed 3 months ago</div>
                                </div>
                                <Button variant="secondary" size="sm" icon={<Key className="w-4 h-4" />} onClick={() => setIsPasswordModalOpen(true)}>
                                    Change
                                </Button>
                            </div>

                            {/* 2FA Placeholder (Future) */}
                            <div className="p-5 bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-xl flex items-center justify-between opacity-50 cursor-not-allowed">
                                <div>
                                    <div className="font-medium text-white mb-1">Two-Factor Authentication</div>
                                    <div className="text-sm text-[var(--text-secondary)]">Coming soon</div>
                                </div>
                                <Button variant="secondary" size="sm" disabled>Enable</Button>
                            </div>
                        </div>
                    </Card>
                </motion.div>
            </div>

            {/* Change Password Modal */}
            <Modal
                isOpen={isPasswordModalOpen}
                onClose={() => setIsPasswordModalOpen(false)}
                title="Change Password"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsPasswordModalOpen(false)}>Cancel</Button>
                        <Button onClick={() => {
                            setIsPasswordModalOpen(false);
                            showToast({ title: "Password Updated", message: "Your password has been changed successfully.", type: "success" });
                        }}>Update Password</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input type="password" label="Current Password" placeholder="********" />
                    <Input type="password" label="New Password" placeholder="********" />
                    <Input type="password" label="Confirm New Password" placeholder="********" />
                </div>
            </Modal>
        </motion.div>
    );
}
