"use client";

import { motion } from "framer-motion";
import { Download, AlertTriangle, FileBox, Terminal } from "lucide-react";
import { Button } from "@/components/ui/Button";
import { Heading, Text } from "@/components/ui/Typography";
import { useToast } from "@/context/ToastContext";

export default function DownloadsPage() {
    const { showToast } = useToast();
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

            {/* Loader Card */}
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
                        <Button icon={<Download className="w-5 h-5" />} onClick={() => showToast({ title: "Download Started", message: "Cobble loader is downloading...", type: "info" })}>
                            Download
                        </Button>
                    </div>
                </div>
            </div>

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
