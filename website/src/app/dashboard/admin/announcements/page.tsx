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
import { Plus, Edit2, Trash2, Megaphone, Calendar, Pin } from "lucide-react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useToast } from "@/context/ToastContext";
import { useRequireAdmin } from "@/context/AuthContext";
import * as api from "@/lib/api";

export default function AdminAnnouncementsPage() {
    const { isAdmin, isLoading: authLoading } = useRequireAdmin();
    const { showToast } = useToast();

    const [announcements, setAnnouncements] = useState<api.Announcement[]>([]);
    const [loading, setLoading] = useState(true);

    const [isCreateModalOpen, setIsCreateModalOpen] = useState(false);
    const [isEditModalOpen, setIsEditModalOpen] = useState(false);
    const [isDeleteModalOpen, setIsDeleteModalOpen] = useState(false);
    const [selectedAnnouncement, setSelectedAnnouncement] = useState<api.Announcement | null>(null);
    const [modalLoading, setModalLoading] = useState(false);

    // Form states
    const [formTitle, setFormTitle] = useState("");
    const [formContent, setFormContent] = useState("");
    const [formType, setFormType] = useState<"info" | "warning" | "update">("info");
    const [formPinned, setFormPinned] = useState(false);

    const fetchAnnouncements = useCallback(async () => {
        setLoading(true);
        try {
            const data = await api.admin.getAnnouncements();
            const announcementsData = data || [];
            setAnnouncements(announcementsData);
        } catch (error) {
            console.error("Failed to fetch announcements:", error);
            showToast({ title: "Error", message: "Failed to load announcements", type: "error" });
        } finally {
            setLoading(false);
        }
    }, [showToast]);

    useEffect(() => {
        if (isAdmin) {
            fetchAnnouncements();
        }
    }, [isAdmin, fetchAnnouncements]);

    const resetForm = () => {
        setFormTitle("");
        setFormContent("");
        setFormType("info");
        setFormPinned(false);
    };

    const openCreateModal = () => {
        resetForm();
        setIsCreateModalOpen(true);
    };

    const openEditModal = (announcement: api.Announcement) => {
        setSelectedAnnouncement(announcement);
        setFormTitle(announcement.title);
        setFormContent(announcement.content);
        setFormType(announcement.type);
        setFormPinned(announcement.pinned);
        setIsEditModalOpen(true);
    };

    const openDeleteModal = (announcement: api.Announcement) => {
        setSelectedAnnouncement(announcement);
        setIsDeleteModalOpen(true);
    };

    const handleCreate = async () => {
        if (!formTitle.trim() || !formContent.trim()) {
            showToast({ title: "Error", message: "Title and content are required", type: "error" });
            return;
        }

        setModalLoading(true);
        try {
            await api.admin.createAnnouncement({
                title: formTitle,
                content: formContent,
                type: formType,
                pinned: formPinned,
            });
            showToast({ title: "Success", message: "Announcement created", type: "success" });
            setIsCreateModalOpen(false);
            fetchAnnouncements();
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to create";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleUpdate = async () => {
        if (!selectedAnnouncement) return;

        setModalLoading(true);
        try {
            await api.admin.updateAnnouncement(selectedAnnouncement.id, {
                title: formTitle,
                content: formContent,
                type: formType,
                pinned: formPinned,
            });
            showToast({ title: "Success", message: "Announcement updated", type: "success" });
            setIsEditModalOpen(false);
            fetchAnnouncements();
        } catch (error) {
            const msg = error instanceof Error ? error.message : "Failed to update";
            showToast({ title: "Error", message: msg, type: "error" });
        } finally {
            setModalLoading(false);
        }
    };

    const handleDelete = async () => {
        if (!selectedAnnouncement) return;

        setModalLoading(true);
        try {
            await api.admin.deleteAnnouncement(selectedAnnouncement.id);
            showToast({ title: "Success", message: "Announcement deleted", type: "success" });
            setIsDeleteModalOpen(false);
            fetchAnnouncements();
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
                    <Heading className="text-3xl mb-2">Announcements</Heading>
                    <Text>Manage system-wide announcements and notifications.</Text>
                </div>
                <Button icon={<Plus className="w-5 h-5" />} onClick={openCreateModal}>
                    Post Announcement
                </Button>
            </motion.div>

            <motion.div variants={fadeInUp}>
                <LoadingTransition loading={loading} skeleton={<Loader />}>
                    {announcements.length > 0 ? (
                        <Card className="p-0 overflow-hidden">
                            <Table
                                data={announcements}
                                columns={[
                                    {
                                        header: "Title",
                                        accessorKey: "title",
                                        cell: (item) => (
                                            <div className="flex items-center gap-3">
                                                <div className="p-2 bg-[var(--accent-primary)]/10 rounded-lg text-[var(--accent-primary)]">
                                                    <Megaphone className="w-4 h-4" />
                                                </div>
                                                <span className="font-medium text-white">{item.title}</span>
                                                {item.pinned && <Pin className="w-4 h-4 text-[var(--accent-primary)]" />}
                                            </div>
                                        )
                                    },
                                    {
                                        header: "Type",
                                        accessorKey: "type",
                                        cell: (item) => {
                                            const variant = item.type === "warning" ? "warning" : item.type === "update" ? "info" : "secondary";
                                            return <Badge variant={variant}>{item.type}</Badge>;
                                        }
                                    },
                                    {
                                        header: "Date",
                                        accessorKey: "created_at",
                                        cell: (item) => (
                                            <div className="flex items-center gap-2 text-[var(--text-secondary)]">
                                                <Calendar className="w-4 h-4" />
                                                {new Date(item.created_at).toLocaleDateString()}
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
                            <Megaphone className="w-12 h-12 text-[var(--text-muted)] mx-auto mb-4" />
                            <Text>No announcements yet.</Text>
                            <Button className="mt-4" onClick={openCreateModal}>Create First Announcement</Button>
                        </Card>
                    )}
                </LoadingTransition>
            </motion.div>

            {/* Create Modal */}
            <Modal
                isOpen={isCreateModalOpen}
                onClose={() => setIsCreateModalOpen(false)}
                title="Post Announcement"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsCreateModalOpen(false)}>Cancel</Button>
                        <Button onClick={handleCreate} loading={modalLoading}>Post Now</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input
                        label="Title"
                        placeholder="Enter announcement title"
                        value={formTitle}
                        onChange={(e) => setFormTitle(e.target.value)}
                    />
                    <div>
                        <label className="block text-sm font-medium text-[var(--text-secondary)] mb-2">Type</label>
                        <select
                            className="w-full bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl p-3 text-white outline-none focus:border-[var(--accent-primary)]"
                            value={formType}
                            onChange={(e) => setFormType(e.target.value as typeof formType)}
                        >
                            <option value="info">Info</option>
                            <option value="warning">Warning</option>
                            <option value="update">Update</option>
                        </select>
                    </div>
                    <div>
                        <label className="block text-sm font-medium text-[var(--text-secondary)] mb-2">Content</label>
                        <textarea
                            className="w-full bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl p-3 text-white placeholder-[var(--text-muted)] focus:border-[var(--accent-primary)] outline-none min-h-[120px] resize-y"
                            placeholder="Write your announcement content..."
                            value={formContent}
                            onChange={(e) => setFormContent(e.target.value)}
                        />
                    </div>
                    <div className="flex items-center gap-2">
                        <input
                            type="checkbox"
                            id="pin"
                            checked={formPinned}
                            onChange={(e) => setFormPinned(e.target.checked)}
                            className="rounded border-[var(--border-color)] bg-[var(--bg-secondary)] text-[var(--accent-primary)]"
                        />
                        <label htmlFor="pin" className="text-sm text-[var(--text-secondary)]">Pin to top</label>
                    </div>
                </div>
            </Modal>

            {/* Edit Modal */}
            <Modal
                isOpen={isEditModalOpen}
                onClose={() => setIsEditModalOpen(false)}
                title="Edit Announcement"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsEditModalOpen(false)}>Cancel</Button>
                        <Button onClick={handleUpdate} loading={modalLoading}>Save Changes</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input
                        label="Title"
                        placeholder="Enter announcement title"
                        value={formTitle}
                        onChange={(e) => setFormTitle(e.target.value)}
                    />
                    <div>
                        <label className="block text-sm font-medium text-[var(--text-secondary)] mb-2">Type</label>
                        <select
                            className="w-full bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl p-3 text-white outline-none focus:border-[var(--accent-primary)]"
                            value={formType}
                            onChange={(e) => setFormType(e.target.value as typeof formType)}
                        >
                            <option value="info">Info</option>
                            <option value="warning">Warning</option>
                            <option value="update">Update</option>
                        </select>
                    </div>
                    <div>
                        <label className="block text-sm font-medium text-[var(--text-secondary)] mb-2">Content</label>
                        <textarea
                            className="w-full bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl p-3 text-white placeholder-[var(--text-muted)] focus:border-[var(--accent-primary)] outline-none min-h-[120px] resize-y"
                            placeholder="Write your announcement content..."
                            value={formContent}
                            onChange={(e) => setFormContent(e.target.value)}
                        />
                    </div>
                    <div className="flex items-center gap-2">
                        <input
                            type="checkbox"
                            id="edit-pin"
                            checked={formPinned}
                            onChange={(e) => setFormPinned(e.target.checked)}
                            className="rounded border-[var(--border-color)] bg-[var(--bg-secondary)] text-[var(--accent-primary)]"
                        />
                        <label htmlFor="edit-pin" className="text-sm text-[var(--text-secondary)]">Pin to top</label>
                    </div>
                </div>
            </Modal>

            {/* Delete Modal */}
            <Modal
                isOpen={isDeleteModalOpen}
                onClose={() => setIsDeleteModalOpen(false)}
                title="Delete Announcement"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsDeleteModalOpen(false)}>Cancel</Button>
                        <Button variant="danger" onClick={handleDelete} loading={modalLoading}>Delete</Button>
                    </>
                }
            >
                <div className="p-4 bg-red-500/10 border border-red-500/20 rounded-xl text-red-400 text-sm">
                    Are you sure you want to delete <strong>{selectedAnnouncement?.title}</strong>? This action cannot be undone.
                </div>
            </Modal>
        </motion.div>
    );
}
