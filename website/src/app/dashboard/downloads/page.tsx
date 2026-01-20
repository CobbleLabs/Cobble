"use client";

import { useEffect, useState, useRef } from "react";
import { motion, AnimatePresence } from "framer-motion";
import { Download, AlertTriangle, Terminal, Lock, X } from "lucide-react";
import { Button } from "@/components/ui/Button";
import { Heading, Text } from "@/components/ui/Typography";
import { Loader } from "@/components/ui/Loader";
import { LoadingTransition } from "@/components/ui/LoadingTransition";
import { useToast } from "@/context/ToastContext";
import { useAuth } from "@/context/AuthContext";
import * as api from "@/lib/api";

interface DownloadItem {
    id: string;
    version: string;
    name: string;
    download_url: string;
}

// Random app names to disguise the loader
const DISGUISE_NAMES = [
    "AnyDesk",
    "OBS Studio",
    "Spotify",
    "Discord",
    "Lunar Client",
    "Steam",
    "Notion",
    "Slack",
    "Figma",
    "VS Code",
    "Chrome",
    "Firefox",
    "Zoom",
    "Teams",
    "Skype",
    "Blender",
    "Unity Hub",
    "Epic Games",
    "Overwolf",
    "Razer Synapse"
];

function getRandomDisguiseName(): string {
    return DISGUISE_NAMES[Math.floor(Math.random() * DISGUISE_NAMES.length)];
}

function sanitizeFilename(name: string): string {
    // Remove invalid filename characters and trim
    return name.replace(/[<>:"/\\|?*]/g, '').trim().slice(0, 50);
}

export default function DownloadsPage() {
    const { showToast } = useToast();
    const { user } = useAuth();
    const [downloads, setDownloads] = useState<DownloadItem[]>([]);
    const [loading, setLoading] = useState(true);
    const [downloading, setDownloading] = useState<string | null>(null);

    // Modal state
    const [showModal, setShowModal] = useState(false);
    const [pendingDownload, setPendingDownload] = useState<DownloadItem | null>(null);
    const [customFilename, setCustomFilename] = useState("");
    const inputRef = useRef<HTMLInputElement>(null);

    const hasLicense = !!user?.license;

    useEffect(() => {
        async function fetchDownloads() {
            if (!hasLicense) {
                setLoading(false);
                return;
            }
            try {
                const data = await api.getDownloads();
                setDownloads(data || []);
            } catch (error) {
                console.error("Failed to fetch downloads:", error);
            } finally {
                setLoading(false);
            }
        }
        fetchDownloads();
    }, [hasLicense]);

    // Focus input when modal opens
    useEffect(() => {
        if (showModal && inputRef.current) {
            inputRef.current.focus();
        }
    }, [showModal]);

    const openDownloadModal = (item: DownloadItem) => {
        setPendingDownload(item);
        setCustomFilename("");
        setShowModal(true);
    };

    const handleConfirmDownload = async () => {
        if (!pendingDownload) return;

        // Determine filename: use custom input or generate random
        let filename = sanitizeFilename(customFilename);
        if (!filename) {
            filename = getRandomDisguiseName();
        }
        filename = `${filename}.exe`;

        setShowModal(false);
        setDownloading(pendingDownload.id);
        showToast({ title: "Downloading", message: `Downloading as "${filename}"...`, type: "info" });

        try {
            const response = await api.downloadFile(pendingDownload.download_url);
            const blob = await response.blob();
            const url = window.URL.createObjectURL(blob);
            const link = document.createElement('a');
            link.href = url;
            link.download = filename;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
            window.URL.revokeObjectURL(url);

            showToast({ title: "Download Complete", message: `Saved as "${filename}"`, type: "success" });
        } catch (error) {
            console.error("Download failed:", error);
            showToast({ title: "Download Failed", message: "Please try again", type: "error" });
        } finally {
            setDownloading(null);
            setPendingDownload(null);
        }
    };

    const handleKeyDown = (e: React.KeyboardEvent) => {
        if (e.key === 'Enter') {
            handleConfirmDownload();
        } else if (e.key === 'Escape') {
            setShowModal(false);
        }
    };

    return (
        <>
            {/* Filename Modal */}
            <AnimatePresence>
                {showModal && (
                    <motion.div
                        initial={{ opacity: 0 }}
                        animate={{ opacity: 1 }}
                        exit={{ opacity: 0 }}
                        className="fixed inset-0 z-50 flex items-center justify-center p-4"
                        style={{ backgroundColor: 'rgba(0, 0, 0, 0.8)' }}
                        onClick={() => setShowModal(false)}
                    >
                        <motion.div
                            initial={{ opacity: 0, scale: 0.9, y: 20 }}
                            animate={{ opacity: 1, scale: 1, y: 0 }}
                            exit={{ opacity: 0, scale: 0.9, y: 20 }}
                            onClick={(e) => e.stopPropagation()}
                            className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-2xl p-6 w-full max-w-md shadow-2xl"
                        >
                            <div className="flex items-center justify-between mb-4">
                                <h3 className="text-xl font-bold text-white">Choose Filename</h3>
                                <button
                                    onClick={() => setShowModal(false)}
                                    className="p-1 hover:bg-[var(--bg-secondary)] rounded-lg transition-colors"
                                >
                                    <X className="w-5 h-5 text-[var(--text-muted)]" />
                                </button>
                            </div>

                            <p className="text-[var(--text-secondary)] text-sm mb-4">
                                Enter a custom name for the file, or leave blank for a random disguised name.
                            </p>

                            <input
                                ref={inputRef}
                                type="text"
                                value={customFilename}
                                onChange={(e) => setCustomFilename(e.target.value)}
                                onKeyDown={handleKeyDown}
                                placeholder={`e.g. ${getRandomDisguiseName()}`}
                                className="w-full px-4 py-3 bg-[var(--bg-secondary)] border border-[var(--border-color)] rounded-xl text-white placeholder:text-[var(--text-muted)] focus:outline-none focus:border-[var(--accent-primary)] transition-colors mb-4"
                            />

                            <div className="flex gap-3">
                                <Button
                                    variant="secondary"
                                    onClick={() => setShowModal(false)}
                                    className="flex-1"
                                >
                                    Cancel
                                </Button>
                                <Button
                                    onClick={handleConfirmDownload}
                                    icon={<Download className="w-4 h-4" />}
                                    className="flex-1"
                                >
                                    Download
                                </Button>
                            </div>
                        </motion.div>
                    </motion.div>
                )}
            </AnimatePresence>

            <motion.div
                initial={{ opacity: 0, scale: 0.95 }}
                animate={{ opacity: 1, scale: 1 }}
                className="max-w-4xl mx-auto space-y-8"
            >
                <div>
                    <Heading className="text-3xl mb-2">Downloads</Heading>
                    <Text>Get the latest version of the Cobble client.</Text>
                </div>

                {/* Warning Card */}
                <div className="bg-[var(--warning)]/10 p-6 rounded-2xl flex items-start gap-4" style={{ backgroundColor: 'rgba(242, 153, 74, 0.15)' }}>
                    <AlertTriangle className="w-6 h-6 text-[var(--warning)] flex-shrink-0 mt-1" />
                    <div>
                        <h3 className="font-bold text-[var(--warning)] mb-1">Important Security Notice</h3>
                        <p className="text-[var(--warning)]/80 text-sm">
                            Please disable your antivirus before downloading. The loader is heavily obfuscated and may be flagged as a false positive.
                            We recommend adding an exclusion folder for Cobble.
                        </p>
                    </div>
                </div>

                {!hasLicense ? (
                    /* No License Card */
                    <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-12 text-center">
                        <div className="w-20 h-20 bg-[var(--bg-secondary)] rounded-full mx-auto flex items-center justify-center mb-6">
                            <Lock className="w-10 h-10 text-[var(--text-muted)]" />
                        </div>
                        <Heading className="text-2xl mb-2">No Active License</Heading>
                        <Text className="mb-6">Purchase a license to access downloads.</Text>
                        <a href="/dashboard/purchase">
                            <Button>Get a License</Button>
                        </a>
                    </div>

                ) : (
                    <LoadingTransition loading={loading} skeleton={<Loader centered />}>
                        {downloads.length > 0 ? (
                            /* Downloads List */
                            <div className="space-y-4">
                                {downloads.map((item) => (
                                    <div
                                        key={item.id}
                                        className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8 hover:border-[var(--accent-primary)]/50 transition-colors group"
                                    >
                                        <div className="flex items-start gap-6">
                                            <div className="w-16 h-16 bg-[var(--accent-primary)]/10 rounded-2xl flex items-center justify-center flex-shrink-0 group-hover:scale-110 transition-transform">
                                                <Terminal className="w-8 h-8 text-[var(--accent-primary)]" />
                                            </div>
                                            <div className="flex-1">
                                                <h3 className="text-2xl font-bold text-white mb-1">{item.name}</h3>
                                                <Text className="mb-4">Version {item.version}</Text>
                                                <Button
                                                    icon={<Download className="w-5 h-5" />}
                                                    onClick={() => openDownloadModal(item)}
                                                    loading={downloading === item.id}
                                                >
                                                    Download
                                                </Button>
                                            </div>
                                        </div>
                                    </div>
                                ))}
                            </div>

                        ) : (
                            /* Fallback - Direct Loader Download */
                            <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8 hover:border-[var(--accent-primary)]/50 transition-colors group">
                                <div className="flex items-start gap-6">
                                    <div className="w-16 h-16 bg-[var(--accent-primary)]/10 rounded-2xl flex items-center justify-center flex-shrink-0 group-hover:scale-110 transition-transform">
                                        <Terminal className="w-8 h-8 text-[var(--accent-primary)]" />
                                    </div>
                                    <div className="flex-1">
                                        <h3 className="text-2xl font-bold text-white mb-2">Cobble Loader</h3>
                                        <Text className="mb-4">
                                            The official auto-updating loader. Simply run it and log in.
                                        </Text>
                                        <Button
                                            icon={<Download className="w-5 h-5" />}
                                            onClick={() => showToast({ title: "Coming Soon", message: "Download will be available shortly.", type: "info" })}
                                        >
                                            Download
                                        </Button>
                                    </div>
                                </div>
                            </div>
                        )}
                    </LoadingTransition>
                )
                }

                <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8">
                    <h3 className="text-xl font-bold font-heading text-white mb-4">Installation Instructions</h3>
                    <ol className="list-decimal list-inside space-y-4 text-[var(--text-secondary)]">
                        <li>Create a folder on your desktop named <code className="bg-[var(--bg-secondary)] px-2 py-1 rounded text-white">Cobble</code></li>
                        <li>Add this folder to your Antivirus exclusions.</li>
                        <li>Download the Loader and move it into the folder.</li>
                        <li>Run as Administrator.</li>
                    </ol>
                </div>
            </motion.div >
        </>
    );
}
