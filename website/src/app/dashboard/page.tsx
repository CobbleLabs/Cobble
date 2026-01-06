"use client";

import { motion } from "framer-motion";
import { Activity, Clock, ShieldCheck, Download, ExternalLink } from "lucide-react";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { BentoGrid, BentoItem } from "@/components/ui/BentoGrid";
import { ActionCard } from "@/components/ui/ActionCard";
import { UpdateItem } from "@/components/ui/UpdateItem";
import { Badge } from "@/components/ui/Badge";
import { staggerContainer, fadeInUp } from "@/lib/motion";

export default function DashboardPage() {
    return (
        <motion.div
            variants={staggerContainer}
            initial="initial"
            animate="animate"
            className="space-y-8"
        >
            <motion.div variants={fadeInUp}>
                <Heading className="text-4xl mb-2">Welcome back, User</Heading>
                <Text>Here&apos;s an overview of your Cobble status.</Text>
            </motion.div>

            {/* Bento Grid Stats */}
            <BentoGrid className="lg:grid-cols-4">

                {/* License Status - Big Card */}
                <BentoItem colSpan={2} className="group">
                    <div className="absolute top-0 right-0 w-32 h-32 bg-[var(--accent-primary)]/10 rounded-full blur-[50px] group-hover:bg-[var(--accent-primary)]/20 transition-all" />

                    <div className="flex justify-between items-start mb-6">
                        <div className="p-3 bg-[var(--accent-primary)]/10 rounded-xl text-[var(--accent-primary)]">
                            <ShieldCheck className="w-8 h-8" />
                        </div>
                        <Badge variant="success">ACTIVE</Badge>
                    </div>

                    <h3 className="text-[var(--text-secondary)] text-sm font-medium mb-1">Current Plan</h3>
                    <div className="text-3xl font-bold text-white mb-6">Lifetime Access</div>

                    <div className="h-1.5 w-full bg-[var(--bg-tertiary)] rounded-full overflow-hidden">
                        <div className="h-full bg-[var(--accent-primary)] w-full shadow-[0_0_10px_var(--accent-primary)]" />
                    </div>
                </BentoItem>

                {/* HWID Status */}
                <BentoItem>
                    <div className="flex items-center gap-4 mb-4">
                        <div className="p-2.5 bg-purple-500/10 rounded-lg text-purple-500">
                            <Activity className="w-6 h-6" />
                        </div>
                        <div className="text-sm font-medium text-[var(--text-secondary)]">HWID Status</div>
                    </div>
                    <div className="text-2xl font-bold text-white mb-2">Locked</div>
                    <Text className="text-xs">Reset available in 14 days</Text>
                </BentoItem>

                {/* Session Time */}
                <BentoItem>
                    <div className="flex items-center gap-4 mb-4">
                        <div className="p-2.5 bg-yellow-500/10 rounded-lg text-yellow-500">
                            <Clock className="w-6 h-6" />
                        </div>
                        <div className="text-sm font-medium text-[var(--text-secondary)]">Last Seen</div>
                    </div>
                    <div className="text-2xl font-bold text-white mb-2">2h ago</div>
                    <Text className="text-xs">v2.0.1</Text>
                </BentoItem>
            </BentoGrid>

            <div className="grid grid-cols-1 lg:grid-cols-3 gap-8">
                {/* Quick Actions */}
                <motion.div variants={fadeInUp} className="lg:col-span-2">
                    <Heading className="text-xl mb-6">Quick Actions</Heading>
                    <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
                        <ActionCard
                            icon={Download}
                            title="Download Loader"
                            description="Get the latest client version"
                            color="blue"
                        />
                        <ActionCard
                            icon={ExternalLink}
                            title="Join Discord"
                            description="Get help & community updates"
                            color="purple"
                        />
                    </div>

                    <Heading className="text-xl mb-6 mt-8">Announcements</Heading>
                    <Card className="p-6">
                        <div className="flex items-start gap-4">
                            <div className="p-2 bg-[var(--accent-primary)]/10 rounded-lg text-[var(--accent-primary)] mt-1">
                                <Activity className="w-5 h-5" />
                            </div>
                            <div>
                                <h3 className="text-lg font-bold text-white mb-2">Welcome to Cobble v2.0</h3>
                                <Text className="text-sm leading-relaxed mb-4">
                                    We&apos;re excited to announce the release of our biggest update yet! Enjoy the new UI, improved performance, and enhanced security features.
                                    Join our Discord for more details and giveaways.
                                </Text>
                                <div className="flex items-center gap-2 text-xs font-medium text-[var(--text-secondary)]">
                                    <span className="text-[var(--accent-primary)]">Administrator</span>
                                    <span>•</span>
                                    <span>Posted just now</span>
                                </div>
                            </div>
                        </div>
                    </Card>
                </motion.div>

                {/* Recent News */}
                <motion.div variants={fadeInUp}>
                    <Heading className="text-xl mb-6">Latest Updates</Heading>
                    <Card className="space-y-6">
                        <UpdateItem version="v2.0.2" date="Today" description="Fixed movement lag on Hypixel" />
                        <UpdateItem version="v2.0.1" date="Yesterday" description="Improved AimAssist randomization" />
                        <UpdateItem version="v2.0.0" date="2 days ago" description="Major UI overhaul released" />
                    </Card>
                </motion.div>
            </div>
        </motion.div>
    );
}
