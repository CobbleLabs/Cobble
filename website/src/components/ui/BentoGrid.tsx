import { ReactNode } from "react";
import { motion } from "framer-motion";
import { Card } from "./Card";
import clsx from "clsx";

export function BentoGrid({ children, className }: { children: ReactNode, className?: string }) {
    return (
        <div className={clsx("grid grid-cols-1 md:grid-cols-3 gap-6", className)}>
            {children}
        </div>
    );
}

export function BentoItem({
    children,
    className,
    colSpan = 1,
    rowSpan = 1,
    delay = 0
}: {
    children: ReactNode,
    className?: string,
    colSpan?: number,
    rowSpan?: number,
    delay?: number
}) {
    return (
        <motion.div
            initial={{ opacity: 0, y: 20 }}
            whileInView={{ opacity: 1, y: 0 }}
            viewport={{ once: true }}
            transition={{ delay, duration: 0.5 }}
            className={clsx(
                colSpan === 2 ? "md:col-span-2" : "md:col-span-1",
                rowSpan === 2 ? "md:row-span-2" : "md:row-span-1",
                className
            )}
        >
            <Card variant="interactive" className="h-full relative overflow-hidden group">
                {children}
            </Card>
        </motion.div>
    );
}
