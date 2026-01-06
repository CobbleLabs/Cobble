"use client";

import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { Button } from "@/components/ui/Button";
import { Table } from "@/components/ui/Table";
import { Modal } from "@/components/ui/Modal";
import { Input } from "@/components/ui/Input";
import { Badge } from "@/components/ui/Badge";
import { Plus, Edit2, Trash2, Megaphone, Calendar } from "lucide-react";
import { useState } from "react";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { useToast } from "@/context/ToastContext";

// Mock Data
interface Announcement {
    id: string;
    title: string;
    content: string;
    author: string;
    date: string;
    status: "Active" | "Archived";
}

const mockAnnouncements: Announcement[] = [
    {
        id: "1",
        title: "Welcome to Cobble v2.0",
        content: "We're excited to announce the release of our biggest update yet! Enjoy the new UI, improved performance, and enhanced security features.",
        author: "Administrator",
        date: "Jan 5, 2026",
        status: "Active"
    },
    {
        id: "2",
        title: "Maintenance Scheduled",
        content: "Servers will be undergoing maintenance on Jan 10th for approximately 2 hours.",
        author: "System",
        date: "Jan 4, 2026",
        status: "Archived"
    }
];

export default function AdminAnnouncementsPage() {
    const { showToast } = useToast();
    const [isCreateModalOpen, setIsCreateModalOpen] = useState(false);

    const [isEditModalOpen, setIsEditModalOpen] = useState(false);
    const [isDeleteModalOpen, setIsDeleteModalOpen] = useState(false);
    const [selectedAnnouncement, setSelectedAnnouncement] = useState<Announcement | null>(null);

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
                <Button icon={<Plus className="w-5 h-5" />} onClick={() => setIsCreateModalOpen(true)}>
                    Post Announcement
                </Button>
            </motion.div>

            <motion.div variants={fadeInUp}>
                <Card className="p-0 overflow-hidden">
                    <Table
                        data={mockAnnouncements}
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
                                    </div>
                                )
                            },
                            {
                                header: "Author",
                                accessorKey: "author",
                                cell: (item) => <span className="text-[var(--text-secondary)]">{item.author}</span>
                            },
                            {
                                header: "Date",
                                accessorKey: "date",
                                cell: (item) => (
                                    <div className="flex items-center gap-2 text-[var(--text-secondary)]">
                                        <Calendar className="w-4 h-4" />
                                        {item.date}
                                    </div>
                                )
                            },
                            {
                                header: "Status",
                                accessorKey: "status",
                                cell: (item) => (
                                    <Badge variant={item.status === "Active" ? "success" : "default"}>
                                        {item.status}
                                    </Badge>
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
                                            onClick={() => {
                                                setSelectedAnnouncement(item);
                                                setIsEditModalOpen(true);
                                            }}
                                        >
                                            Edit
                                        </Button>
                                        <Button
                                            size="sm"
                                            variant="danger"
                                            icon={<Trash2 className="w-4 h-4" />}
                                            onClick={() => {
                                                setSelectedAnnouncement(item);
                                                setIsDeleteModalOpen(true);
                                            }}
                                        >
                                            Delete
                                        </Button>
                                    </div>
                                )
                            }
                        ]}
                    />
                </Card>
            </motion.div>

            {/* Create Announcement Modal */}
            <Modal
                isOpen={isCreateModalOpen}
                onClose={() => setIsCreateModalOpen(false)}
                title="Post Announcement"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsCreateModalOpen(false)}>Cancel</Button>
                        <Button onClick={() => {
                            setIsCreateModalOpen(false);
                            showToast({ title: "Announcement Posted", message: "Your announcement has been successfully published.", type: "success" });
                        }}>Post Now</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input label="Title" placeholder="Enter announcement title" />
                    <div>
                        <label className="block text-sm font-medium text-[var(--text-secondary)] mb-2">
                            Content
                        </label>
                        <textarea
                            className="w-full bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl p-3 text-white placeholder-[var(--text-muted)] focus:ring-2 focus:ring-[var(--accent-primary)]/20 focus:border-[var(--accent-primary)] outline-none min-h-[120px] resize-y"
                            placeholder="Write your announcement content..."
                        />
                    </div>
                    <div className="flex items-center gap-2">
                        <input type="checkbox" id="pin" className="rounded border-[var(--border-color)] bg-[var(--bg-secondary)] text-[var(--accent-primary)] focus:ring-[var(--accent-primary)]" />
                        <label htmlFor="pin" className="text-sm text-[var(--text-secondary)]">Pin to top</label>
                    </div>
                </div>
            </Modal>

            {/* Delete Confirmation Modal */}
            <Modal
                isOpen={isDeleteModalOpen}
                onClose={() => setIsDeleteModalOpen(false)}
                title="Delete Announcement"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsDeleteModalOpen(false)}>Cancel</Button>
                        <Button variant="danger" onClick={() => {
                            setIsDeleteModalOpen(false);
                            showToast({ title: "Announcement Deleted", message: "The announcement has been permanently removed.", type: "success" });
                        }}>Delete</Button>
                    </>
                }
            >
                <Text>
                    Are you sure you want to delete <span className="text-white font-bold">{selectedAnnouncement?.title}</span>?
                    This action cannot be undone.
                </Text>
            </Modal>


            {/* Edit Announcement Modal */}
            <Modal
                isOpen={isEditModalOpen}
                onClose={() => setIsEditModalOpen(false)}
                title="Edit Announcement"
                footer={
                    <>
                        <Button variant="ghost" onClick={() => setIsEditModalOpen(false)}>Cancel</Button>
                        <Button onClick={() => {
                            setIsEditModalOpen(false);
                            showToast({ title: "Changes Saved", message: "Announcement updated successfully.", type: "success" });
                        }}>Save Changes</Button>
                    </>
                }
            >
                <div className="space-y-4">
                    <Input
                        label="Title"
                        defaultValue={selectedAnnouncement?.title}
                        placeholder="Enter announcement title"
                    />
                    <div>
                        <label className="block text-sm font-medium text-[var(--text-secondary)] mb-2">
                            Content
                        </label>
                        <textarea
                            className="w-full bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl p-3 text-white placeholder-[var(--text-muted)] focus:ring-2 focus:ring-[var(--accent-primary)]/20 focus:border-[var(--accent-primary)] outline-none min-h-[120px] resize-y"
                            placeholder="Write your announcement content..."
                            defaultValue={selectedAnnouncement?.content}
                        />
                    </div>
                    <div className="flex items-center gap-2">
                        <input type="checkbox" id="edit-pin" className="rounded border-[var(--border-color)] bg-[var(--bg-secondary)] text-[var(--accent-primary)] focus:ring-[var(--accent-primary)]" />
                        <label htmlFor="edit-pin" className="text-sm text-[var(--text-secondary)]">Pin to top</label>
                    </div>
                    <div className="flex items-center gap-2">
                        <input
                            type="checkbox"
                            id="edit-status"
                            defaultChecked={selectedAnnouncement?.status === "Active"}
                            className="rounded border-[var(--border-color)] bg-[var(--bg-secondary)] text-[var(--accent-primary)] focus:ring-[var(--accent-primary)]"
                        />
                        <label htmlFor="edit-status" className="text-sm text-[var(--text-secondary)]">Active</label>
                    </div>
                </div>
            </Modal>
        </motion.div >
    );
}
