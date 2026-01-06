"use client";

import { motion } from "framer-motion";
import { Check } from "lucide-react";
import Link from "next/link";
import clsx from "clsx";
import { Card } from "./Card";
import { Button } from "./Button";
import { Heading, Text } from "./Typography";
import { Badge } from "./Badge";

interface PricingCardProps {
    title: string;
    price: string;
    period?: string;
    description: string;
    features: string[];
    isPopular?: boolean;
    delay?: number;
    buttonText?: string;
    href?: string;
    className?: string;
}

export function PricingCard({
    title,
    price,
    period = "/USD",
    description,
    features,
    isPopular = false,
    delay = 0,
    buttonText = "Get Started",
    href = "/register",
    className
}: PricingCardProps) {
    return (
        <motion.div
            initial={{ opacity: 0, y: 30 }}
            whileInView={{ opacity: 1, y: 0 }}
            viewport={{ once: true }}
            transition={{ duration: 0.5, delay }}
            className={clsx("h-full", className)}
        >
            <Card
                variant={isPopular ? "premium" : "default"}
                className={clsx("h-full hover:scale-[1.02] transition-transform flex flex-col relative", isPopular && "border-[var(--accent-primary)]")}
            >
                {isPopular && (
                    <div className="absolute -top-4 left-1/2 -translate-x-1/2">
                        <Badge variant="info" className="bg-[var(--accent-primary)] text-white border-none shadow-lg px-4 py-1.5 tracking-widest">
                            MOST POPULAR
                        </Badge>
                    </div>
                )}

                <Heading className="text-2xl mb-2">{title}</Heading>
                <Text className="text-sm mb-8 min-h-[40px]">{description}</Text>

                <div className="flex items-baseline gap-1 mb-8">
                    <span className="text-white text-5xl md:text-6xl font-black font-heading">${price}</span>
                    <span className="text-[var(--text-muted)]">{period}</span>
                </div>

                <Link href={href} className="w-full">
                    <Button
                        variant={isPopular ? "primary" : "secondary"}
                        className="w-full mb-10"
                        size="lg"
                    >
                        {buttonText}
                    </Button>
                </Link>

                <div className="space-y-4 mt-auto">
                    {features.map((f, i) => (
                        <div key={i} className="flex items-center gap-4 text-gray-300">
                            <div className={clsx("rounded-full p-1 flex-shrink-0", isPopular ? "bg-[var(--accent-primary)]/20 text-[var(--accent-primary)]" : "bg-[var(--bg-elevated)] text-[var(--text-muted)]")}>
                                <Check className="w-4 h-4" />
                            </div>
                            <span className="text-sm">{f}</span>
                        </div>
                    ))}
                </div>
            </Card>
        </motion.div>
    );
}
