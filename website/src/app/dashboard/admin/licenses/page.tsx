"use client";

import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Table } from "@/components/ui/Table";
import { Badge } from "@/components/ui/Badge";
import { Button } from "@/components/ui/Button";
import { Input } from "@/components/ui/Input";
import { Modal } from "@/components/ui/Modal";
import { Plus, Timer } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useState } from "react";
import { useToast } from "@/context/ToastContext";

const mockLicenses = [
    { id: 1, user: "darling", plan: "Lifetime", expires: "Never", status: "Active" },
    { id: 2, user: "user2", plan: "Monthly", expires: "2024-02-15", status: "Active" },
    { id: 3, user: "user3", plan: "Monthly", expires: "2023-12-01", status: "Expired" },
];

export default function AdminLicensesPage() {
    const { showToast } = useToast();
    const [isAddModalOpen, setIsAddModalOpen] = useState(false);
    const [selectedLicense, setSelectedLicense] = useState<any>(null);
    const [modalType, setModalType] = useState<"extend" | "revoke" | null>(null);

    const openModal = (type: "extend" | "revoke", license: any) => {
        setSelectedLicense(license);
        setModalType(type);
    };

    const closeModal = () => {
        setSelectedLicense(null);
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
                    <Heading className="text-3xl">Licenses</Heading>
                    <Text>Manage user licenses and subscriptions.</Text>
                </div>
                <Button icon={<Plus className="w-4 h-4" />} onClick={() => setIsAddModalOpen(true)}>
                    Add License
                </Button>
            </motion.div>

            <motion.div variants={fadeInUp}>
                <Table
                    data={mockLicenses}
                    columns={[
                        { header: "User", accessorKey: "user", className: "font-semibold text-white" },
                        { header: "Plan", accessorKey: "plan" },
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
                        {
                            header: "Actions",
                            cell: (item) => (
                                <div className="flex gap-2">
                                    <Button size="sm" variant="secondary" onClick={() => openModal("extend", item)}>Extend</Button>
                                    <Button size="sm" variant="danger" onClick={() => openModal("revoke", item)}>Revoke</Button>
                                </div>
                            )
                        }
                    ]}
                />
            </motion.div>

            <Modal
                isOpen={isAddModalOpen}
                onClose={() => setIsAddModalOpen(false)}
                title="Add New License"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsAddModalOpen(false)}>Cancel</Button>
                        <Button onClick={() => {
                            setIsAddModalOpen(false);
                            showToast({ title: "License Created", message: "New license generated successfully.", type: "success" });
                        }}>Create License</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input label="Username" placeholder="Enter username" />
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
                title="Extend License"
                footer={
                    <>
                        <Button variant="ghost" onClick={closeModal}>Cancel</Button>
                        <Button onClick={() => {
                            closeModal();
                            showToast({ title: "License Extended", message: "Subscription duration updated.", type: "success" });
                        }}>Confirm Extension</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Text>
                        Extending license for <strong>{selectedLicense?.user}</strong>.
                    </Text>
                    <Input label="Days to Add" type="number" placeholder="30" />
                </div>
            </Modal>

            {/* Revoke Modal */}
            <Modal
                isOpen={modalType === "revoke"}
                onClose={closeModal}
                title="Revoke License"
                footer={
                    <>
                        <Button variant="ghost" onClick={closeModal}>Cancel</Button>
                        <Button variant="danger" onClick={() => {
                            closeModal();
                            showToast({ title: "License Revoked", message: "User license has been terminated.", type: "error" });
                        }}>Confirm Revoke</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <div className="p-4 bg-red-500/10 border border-red-500/20 rounded-xl text-red-400 text-sm">
                        Are you sure you want to revoke the <strong>{selectedLicense?.plan}</strong> license for <strong>{selectedLicense?.user}</strong>? This action cannot be undone.
                    </div>
                </div>
            </Modal>
        </motion.div>
    );
}
