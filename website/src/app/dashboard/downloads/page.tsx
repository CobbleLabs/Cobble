"use client";

import { useEffect, useState } from "react";
import { motion } from "framer-motion";
import { Download, AlertTriangle, Terminal, Lock } from "lucide-react";
import { Button } from "@/components/ui/Button";
import { Heading, Text } from "@/components/ui/Typography";
import { useToast } from "@/context/ToastContext";
import { useAuth } from "@/context/AuthContext";
import * as api from "@/lib/api";

interface DownloadItem {
    id: string;
    version: string;
    name: string;
    download_url: string;
}

export default function DownloadsPage() {
    const { showToast } = useToast();
    const { user } = useAuth();
    const [downloads, setDownloads] = useState<DownloadItem[]>([]);
    const [loading, setLoading] = useState(true);
    const [downloading, setDownloading] = useState<string | null>(null);

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

    const handleDownload = async (item: DownloadItem) => {
        setDownloading(item.id);
        showToast({ title: "Download Started", message: `${item.name} is downloading...`, type: "info" });

        try {
            // Create a link and trigger download
            const link = document.createElement('a');
            link.href = item.download_url;
            link.download = item.name;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
        } catch (error) {
            showToast({ title: "Download Failed", message: "Please try again", type: "error" });
        } finally {
            setDownloading(null);
        }
    };

    return (
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
            ) : loading ? (
                /* Loading State */
                <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8 animate-pulse">
                    <div className="flex items-start gap-6">
                        <div className="w-16 h-16 bg-[var(--bg-tertiary)] rounded-2xl" />
                        <div className="flex-1 space-y-4">
                            <div className="h-6 bg-[var(--bg-tertiary)] rounded w-1/3" />
                            <div className="h-4 bg-[var(--bg-tertiary)] rounded w-2/3" />
                        </div>
                    </div>
                </div>
            ) : downloads.length > 0 ? (
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
                                        onClick={() => handleDownload(item)}
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

            <div className="bg-[var(--bg-primary)] border border-[var(--border-color)] rounded-3xl p-8">
                <h3 className="text-xl font-bold font-heading text-white mb-4">Installation Instructions</h3>
                <ol className="list-decimal list-inside space-y-4 text-[var(--text-secondary)]">
                    <li>Create a folder on your desktop named <code className="bg-[var(--bg-secondary)] px-2 py-1 rounded text-white">Cobble</code></li>
                    <li>Add this folder to your Antivirus exclusions.</li>
                    <li>Download the Loader and move it into the folder.</li>
                    <li>Run as Administrator.</li>
                </ol>
            </div>
        </motion.div>
    );
}
