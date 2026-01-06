"use client";

import { motion, AnimatePresence } from "framer-motion";
import { X, CheckCircle, AlertCircle, Info, AlertTriangle } from "lucide-react";
import { Toast, ToastType } from "@/context/ToastContext";
import clsx from "clsx";

const toastIcons = {
    success: CheckCircle,
    error: AlertCircle,
    warning: AlertTriangle,
    info: Info,
};

const toastStyles = {
    success: "bg-[#052e16] text-green-500",
    error: "bg-[#450a0a] text-red-500",
    warning: "bg-[#422006] text-yellow-500",
    info: "bg-[#0A0A0A] text-[var(--accent-primary)]",
};

export function ToastContainer({ toasts, removeToast }: { toasts: Toast[]; removeToast: (id: string) => void }) {
    return (
        <div className="fixed bottom-4 right-4 z-50 flex flex-col gap-2 w-full max-w-sm pointer-events-none p-4">
            <AnimatePresence mode="popLayout">
                {toasts.map((toast) => (
                    <ToastItem key={toast.id} toast={toast} onClose={() => removeToast(toast.id)} />
                ))}
            </AnimatePresence>
        </div>
    );
}

function ToastItem({ toast, onClose }: { toast: Toast; onClose: () => void }) {
    const Icon = toastIcons[toast.type];

    return (
        <motion.div
            layout
            initial={{ opacity: 0, y: 20, scale: 0.95 }}
            animate={{ opacity: 1, y: 0, scale: 1 }}
            exit={{ opacity: 0, scale: 0.95, transition: { duration: 0.2 } }}
            className={clsx(
                "pointer-events-auto relative overflow-hidden rounded-xl border border-white/5 p-4 shadow-lg",
                toastStyles[toast.type]
            )}
        >
            <div className="flex items-start gap-3">
                <div className={clsx("mt-0.5 rounded-full p-1",
                    toast.type === 'success' && "bg-green-500/10",
                    toast.type === 'error' && "bg-red-500/10",
                    toast.type === 'warning' && "bg-yellow-500/10",
                    toast.type === 'info' && "bg-[var(--accent-primary)]/10"
                )}>
                    <Icon className="w-5 h-5" />
                </div>
                <div className="flex-1">
                    <h3 className="font-semibold text-white text-sm">{toast.title}</h3>
                    {toast.message && <p className="mt-1 text-sm text-gray-300/90">{toast.message}</p>}
                </div>
                <button
                    onClick={onClose}
                    className="text-gray-400 transition-colors hover:text-white"
                >
                    <X className="w-4 h-4" />
                </button>
            </div>

            {/* Progress bar effect (optional, could be added later) */}
            <div className="absolute bottom-0 left-0 h-0.5 w-full bg-gradient-to-r from-transparent via-white/10 to-transparent opacity-50" />
        </motion.div>
    );
}
