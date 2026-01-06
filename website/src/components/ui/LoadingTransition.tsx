"use client";

import { AnimatePresence, motion } from "framer-motion";

interface LoadingTransitionProps {
    loading: boolean;
    skeleton: React.ReactNode;
    children: React.ReactNode;
}

/**
 * Smooth transition between skeleton loading state and actual content.
 * Skeleton appears immediately (no flicker), only fades out when content loads.
 */
export function LoadingTransition({ loading, skeleton, children }: LoadingTransitionProps) {
    return (
        <AnimatePresence mode="wait">
            {loading ? (
                <motion.div
                    key="skeleton"
                    initial={false}
                    animate={{ opacity: 1 }}
                    exit={{ opacity: 0 }}
                    transition={{ duration: 0.15, ease: "easeOut" }}
                >
                    {skeleton}
                </motion.div>
            ) : (
                <motion.div
                    key="content"
                    initial={{ opacity: 0 }}
                    animate={{ opacity: 1 }}
                    transition={{ duration: 0.2, ease: "easeIn" }}
                >
                    {children}
                </motion.div>
            )}
        </AnimatePresence>
    );
}
