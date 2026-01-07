"use client";

import { useState, useEffect, useCallback } from "react";
import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { Button } from "@/components/ui/Button";
import { Table } from "@/components/ui/Table";
import { Loader } from "@/components/ui/Loader";
import { LoadingTransition } from "@/components/ui/LoadingTransition";
import { Modal } from "@/components/ui/Modal";
import { Input } from "@/components/ui/Input";
import { Badge } from "@/components/ui/Badge";
import { Plus, Edit2, Trash2, ScrollText, Calendar, X } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useToast } from "@/context/ToastContext";
import { useRequireAdmin } from "@/context/AuthContext";
import * as api from "@/lib/api";

export default function AdminChangelogsPage() {
    const { isAdmin, isLoading: authLoading } = useRequireAdmin();
    const { showToast } = useToast();

    const [changelogs, setChangelogs] = useState<api.Changelog[]>([]);
    const [loading, setLoading] = useState(true);

    const [isCreateModalOpen, setIsCreateModalOpen] = useState(false);
    const [isEditModalOpen, setIsEditModalOpen] = useState(false);
    const [isDeleteModalOpen, setIsDeleteModalOpen] = useState(false);
    const [selectedChangelog, setSelectedChangelog] = useState<api.Changelog | null>(null);
    const [modalLoading, setModalLoading] = useState(false);

    // Form states
    const [formVersion, setFormVersion] = useState("");
    const [formChanges, setFormChanges] = useState<string[]>([""]);

    const fetchChangelogs = useCallback(async () => {
        setLoading(true);
        try {
            const data = await api.admin.getChangelogs();
            setChangelogs(data || []);
        } catch (error) {
            console.error("Failed to fetch changelogs:", error);
            showToast({ title: "Error", message: "Failed to load changelogs", type: "error" });
        } finally {
            setLoading(false);
        }
    }, [showToast]);

    useEffect(() => {
        if (isAdmin) {
            fetchChangelogs();
        }
    }, [isAdmin, fetchChangelogs]);

    const resetForm = () => {
        setFormVersion("");
        setFormChanges([""]);
    };

    const addChangeField = () => {
        setFormChanges([...formChanges, ""]);
    };

    const removeChangeField = (index: number) => {
        if (formChanges.length > 1) {
            setFormChanges(formChanges.filter((_, i) => i !== index));
        }
    };

    const updateChangeField = (index: number, value: string) => {
        const updated = [...formChanges];
        updated[index] = value;
        setFormChanges(updated);
    };

    const openCreateModal = () => {
        resetForm();
        setIsCreateModalOpen(true);
    };

    const openEditModal = (changelog: api.Changelog) => {
        setSelectedChangelog(changelog);
        setFormVersion(changelog.version);
        setFormChanges(changelog.changes.length > 0 ? changelog.changes : [""]);
        setIsEditModalOpen(true);
    };

    const openDeleteModal = (changelog: api.Changelog) => {
        setSelectedChangelog(changelog);
        setIsDeleteModalOpen(true);
    };

    const handleCreate = async () => {
        const validChanges = formChanges.filter(c => c.trim() !== "");
        if (!formVersion.trim() || validChanges.length === 0) {
            showToast({ title: "Error", message: "Version and at least one change required", type: "error" });
            return;
        }

        setModalLoading(true);
        try {
            await api.admin.createChangelog({
                version: formVersion,
                changes: validChanges,
            });
            showToast({ title: "Success", message: "Changelog created", type: "success" });
            setIsCreateModalOpen(false);
            fetchChangelogs();
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to create";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleUpdate = async () => {
        if (!selectedChangelog) return;
        const validChanges = formChanges.filter(c => c.trim() !== "");

        setModalLoading(true);
        try {
            await api.admin.updateChangelog(selectedChangelog.id, {
                version: formVersion,
                changes: validChanges,
            });
            showToast({ title: "Success", message: "Changelog updated", type: "success" });
            setIsEditModalOpen(false);
            fetchChangelogs();
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to update";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleDelete = async () => {
        if (!selectedChangelog) return;

        setModalLoading(true);
        try {
            await api.admin.deleteChangelog(selectedChangelog.id);
            showToast({ title: "Success", message: "Changelog deleted", type: "success" });
            setIsDeleteModalOpen(false);
            fetchChangelogs();
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to delete";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

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
                    <Heading className="text-3xl mb-2">Changelogs</Heading>
                    <Text>Manage version changelogs displayed to users.</Text>
                </div>
                <Button icon={<Plus className="w-5 h-5" />} onClick={openCreateModal}>
                    New Changelog
                </Button>
            </motion.div>

            <motion.div variants={fadeInUp}>
                <LoadingTransition loading={loading} skeleton={<Loader />}>
                    {changelogs.length > 0 ? (
                        <Card className="p-0 overflow-hidden">
                            <Table
                                data={changelogs}
                                columns={[
                                    {
                                        header: "Version",
                                        accessorKey: "version",
                                        cell: (item) => (
                                            <div className="flex items-center gap-3">
                                                <div className="p-2 bg-[var(--accent-primary)]/10 rounded-lg text-[var(--accent-primary)]">
                                                    <ScrollText className="w-4 h-4" />
                                                </div>
                                                <Badge variant="info">{item.version}</Badge>
                                            </div>
                                        )
                                    },
                                    {
                                        header: "Changes",
                                        accessorKey: "changes",
                                        cell: (item) => (
                                            <span className="text-[var(--text-secondary)]">
                                                {item.changes.length} change{item.changes.length !== 1 ? "s" : ""}
                                            </span>
                                        )
                                    },
                                    {
                                        header: "Released",
                                        accessorKey: "released_at",
                                        cell: (item) => (
                                            <div className="flex items-center gap-2 text-[var(--text-secondary)]">
                                                <Calendar className="w-4 h-4" />
                                                {new Date(item.released_at).toLocaleDateString()}
                                            </div>
                                        )
                                    },
                                    {
                                        header: "Actions",
                                        accessorKey: "id",
                                        cell: (item) => (
                                            <div className="flex items-center gap-2">
                                                <Button
                                                    size="sm"
                                                    variant="secondary"
                                                    icon={<Edit2 className="w-4 h-4" />}
                                                    onClick={() => openEditModal(item)}
                                                >
                                                    Edit
                                                </Button>
                                                <Button
                                                    size="sm"
                                                    variant="danger"
                                                    icon={<Trash2 className="w-4 h-4" />}
                                                    onClick={() => openDeleteModal(item)}
                                                >
                                                    Delete
                                                </Button>
                                            </div>
                                        )
                                    }
                                ]}
                            />
                        </Card>
                    ) : (
                        <Card className="p-12 text-center">
                            <ScrollText className="w-12 h-12 text-[var(--text-muted)] mx-auto mb-4" />
                            <Text>No changelogs yet.</Text>
                            <Button className="mt-4" onClick={openCreateModal}>Create First Changelog</Button>
                        </Card>
                    )}
                </LoadingTransition>
            </motion.div>

            {/* Create Modal */}
            <Modal
                isOpen={isCreateModalOpen}
                onClose={() => setIsCreateModalOpen(false)}
                title="New Changelog"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsCreateModalOpen(false)}>Cancel</Button>
                        <Button onClick={handleCreate} loading={modalLoading}>Create</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input
                        label="Version"
                        placeholder="e.g. 1.2.0"
                        value={formVersion}
                        onChange={(e) => setFormVersion(e.target.value)}
                    />
                    <div>
                        <label className="block text-sm font-medium text-[var(--text-secondary)] mb-2">Changes</label>
                        <div className="space-y-2">
                            {formChanges.map((change, index) => (
                                <div key={index} className="flex gap-2">
                                    <input
                                        className="flex-1 bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl px-4 py-3 text-white placeholder-[var(--text-muted)] focus:border-[var(--accent-primary)] outline-none"
                                        placeholder={`Change ${index + 1}`}
                                        value={change}
                                        onChange={(e) => updateChangeField(index, e.target.value)}
                                    />
                                    {formChanges.length > 1 && (
                                        <button
                                            onClick={() => removeChangeField(index)}
                                            className="p-3 text-red-400 hover:bg-red-500/10 rounded-xl transition-colors"
                                        >
                                            <X className="w-5 h-5" />
                                        </button>
                                    )}
                                </div>
                            ))}
                        </div>
                        <Button variant="ghost" size="sm" className="mt-2" onClick={addChangeField}>
                            + Add Change
                        </Button>
                    </div>
                </div>
            </Modal>

            {/* Edit Modal */}
            <Modal
                isOpen={isEditModalOpen}
                onClose={() => setIsEditModalOpen(false)}
                title="Edit Changelog"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsEditModalOpen(false)}>Cancel</Button>
                        <Button onClick={handleUpdate} loading={modalLoading}>Save Changes</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input
                        label="Version"
                        placeholder="e.g. 1.2.0"
                        value={formVersion}
                        onChange={(e) => setFormVersion(e.target.value)}
                    />
                    <div>
                        <label className="block text-sm font-medium text-[var(--text-secondary)] mb-2">Changes</label>
                        <div className="space-y-2">
                            {formChanges.map((change, index) => (
                                <div key={index} className="flex gap-2">
                                    <input
                                        className="flex-1 bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl px-4 py-3 text-white placeholder-[var(--text-muted)] focus:border-[var(--accent-primary)] outline-none"
                                        placeholder={`Change ${index + 1}`}
                                        value={change}
                                        onChange={(e) => updateChangeField(index, e.target.value)}
                                    />
                                    {formChanges.length > 1 && (
                                        <button
                                            onClick={() => removeChangeField(index)}
                                            className="p-3 text-red-400 hover:bg-red-500/10 rounded-xl transition-colors"
                                        >
                                            <X className="w-5 h-5" />
                                        </button>
                                    )}
                                </div>
                            ))}
                        </div>
                        <Button variant="ghost" size="sm" className="mt-2" onClick={addChangeField}>
                            + Add Change
                        </Button>
                    </div>
                </div>
            </Modal>

            {/* Delete Modal */}
            <Modal
                isOpen={isDeleteModalOpen}
                onClose={() => setIsDeleteModalOpen(false)}
                title="Delete Changelog"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsDeleteModalOpen(false)}>Cancel</Button>
                        <Button variant="danger" onClick={handleDelete} loading={modalLoading}>Delete</Button>
                    </>
                }
            >
                <div className="p-4 bg-red-500/10 border border-red-500/20 rounded-xl text-red-400 text-sm">
                    Are you sure you want to delete version <strong>{selectedChangelog?.version}</strong>? This action cannot be undone.
                </div>
            </Modal>
        </motion.div>
    );
}
