"use client";

import { motion } from "framer-motion";
import { Download, AlertTriangle, FileBox, Terminal } from "lucide-react";

export default function DownloadsPage() {
    return (
        <motion.div
            initial={{ opacity: 0, scale: 0.95 }}
            animate={{ opacity: 1, scale: 1 }}
            className="max-w-4xl mx-auto space-y-8"
        >
            <div>
                <h1 className="text-3xl font-bold font-heading text-white mb-2">Downloads</h1>
                <p className="text-gray-400">Get the latest version of the Cobble client.</p>
            </div>

            {/* Warning Card */}
            <div className="bg-yellow-500/10 border border-yellow-500/20 p-6 rounded-2xl flex items-start gap-4">
                <AlertTriangle className="w-6 h-6 text-yellow-500 flex-shrink-0 mt-1" />
                <div>
                    <h3 className="font-bold text-yellow-500 mb-1">Important Security Notice</h3>
                    <p className="text-yellow-500/80 text-sm">
                        Please disable your antivirus before downloading. The loader is heavily obfuscated and may be flagged as a false positive.
                        We recommend adding an exclusion folder for Cobble.
                    </p>
                </div>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
                {/* Loader Card */}
                <div className="bg-[#121212] border border-[#1E1E1E] rounded-3xl p-8 hover:border-[#4A90E2]/50 transition-colors group">
                    <div className="w-16 h-16 bg-[#4A90E2]/10 rounded-2xl flex items-center justify-center mb-6 group-hover:scale-110 transition-transform">
                        <Terminal className="w-8 h-8 text-[#4A90E2]" />
                    </div>
                    <h3 className="text-2xl font-bold text-white mb-2">Cobble Loader</h3>
                    <p className="text-gray-400 mb-8">
                        The official auto-updating loader. Simply run it and log in.
                    </p>
                    <div className="flex items-center justify-between text-xs text-gray-500 mb-6 font-mono">
                        <span>v2.0.2</span>
                        <span>3.5 MB</span>
                        <span>.exe</span>
                    </div>
                    <button className="w-full bg-[#4A90E2] hover:bg-[#357ABD] text-white py-4 rounded-xl font-bold flex items-center justify-center gap-2 transition-all shadow-lg shadow-[#4A90E2]/20">
                        <Download className="w-5 h-5" /> Download
                    </button>
                </div>

                {/* DLL Card */}
                <div className="bg-[#121212] border border-[#1E1E1E] rounded-3xl p-8 hover:border-purple-500/50 transition-colors group">
                    <div className="w-16 h-16 bg-purple-500/10 rounded-2xl flex items-center justify-center mb-6 group-hover:scale-110 transition-transform">
                        <FileBox className="w-8 h-8 text-purple-500" />
                    </div>
                    <h3 className="text-2xl font-bold text-white mb-2">Manual DLL</h3>
                    <p className="text-gray-400 mb-8">
                        For manual injection. Only use if the loader fails.
                    </p>
                    <div className="flex items-center justify-between text-xs text-gray-500 mb-6 font-mono">
                        <span>v2.0.2</span>
                        <span>12.4 MB</span>
                        <span>.dll</span>
                    </div>
                    <button className="w-full bg-[#1A1A1A] hover:bg-[#252525] text-white border border-[#333] py-4 rounded-xl font-bold flex items-center justify-center gap-2 transition-all">
                        <Download className="w-5 h-5" /> Download
                    </button>
                </div>
            </div>

            <div className="bg-[#121212] border border-[#1E1E1E] rounded-3xl p-8">
                <h3 className="text-xl font-bold font-heading text-white mb-4">Installation Instructions</h3>
                <ol className="list-decimal list-inside space-y-4 text-gray-400">
                    <li>Create a folder on your desktop named <code className="bg-[#1E1E1E] px-2 py-1 rounded text-white">Cobble</code></li>
                    <li>Add this folder to your Antivirus exclusions.</li>
                    <li>Download the Loader and move it into the folder.</li>
                    <li>Run as Administrator.</li>
                </ol>
            </div>
        </motion.div>
    );
}
