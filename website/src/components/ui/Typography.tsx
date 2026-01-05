import { ReactNode } from "react";
import clsx from "clsx";

interface TypographyProps {
    children: ReactNode;
    className?: string;
}

export function Heading({ children, className }: TypographyProps) {
    return <h2 className={clsx("font-heading font-bold text-white tracking-tight", className)}>{children}</h2>
}

export function Text({ children, className }: TypographyProps) {
    return <p className={clsx("text-gray-400 font-sans", className)}>{children}</p>
}
