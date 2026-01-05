"use client";

import { motion } from "framer-motion";
import { Activity, Clock, ShieldCheck, Download, ExternalLink } from "lucide-react";
import clsx from "clsx";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { BentoGrid, BentoItem } from "@/components/ui/BentoGrid";
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
                    <div className="absolute top-0 right-0 w-32 h-32 bg-[#4A90E2]/10 rounded-full blur-[50px] group-hover:bg-[#4A90E2]/20 transition-all" />

                    <div className="flex justify-between items-start mb-6">
                        <div className="p-3 bg-[#4A90E2]/10 rounded-xl text-[#4A90E2]">
                            <ShieldCheck className="w-8 h-8" />
                        </div>
                        <div className="px-3 py-1 bg-green-500/10 text-green-500 rounded-full text-xs font-bold border border-green-500/20">
                            ACTIVE
                        </div>
                    </div>

                    <h3 className="text-gray-400 text-sm font-medium mb-1">Current Plan</h3>
                    <div className="text-3xl font-bold text-white mb-6">Lifetime Access</div>

                    <div className="h-1.5 w-full bg-[#1E1E1E] rounded-full overflow-hidden">
                        <div className="h-full bg-[#4A90E2] w-full shadow-[0_0_10px_#4A90E2]" />
                    </div>
                </BentoItem>

                {/* HWID Status */}
                <BentoItem>
                    <div className="flex items-center gap-4 mb-4">
                        <div className="p-2.5 bg-purple-500/10 rounded-lg text-purple-500">
                            <Activity className="w-6 h-6" />
                        </div>
                        <div className="text-sm font-medium text-gray-300">HWID Status</div>
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
                        <div className="text-sm font-medium text-gray-300">Last Seen</div>
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
                            desc="Get the latest client version"
                            color="blue"
                        />
                        <ActionCard
                            icon={ExternalLink}
                            title="Join Discord"
                            desc="Get help & community updates"
                            color="purple"
                        />
                    </div>
                </motion.div>

                {/* Recent News */}
                <motion.div variants={fadeInUp}>
                    <Heading className="text-xl mb-6">Latest Updates</Heading>
                    <Card className="space-y-6">
                        <UpdateItem version="v2.0.2" date="Today" desc="Fixed movement lag on Hypixel" />
                        <UpdateItem version="v2.0.1" date="Yesterday" desc="Improved AimAssist randomization" />
                        <UpdateItem version="v2.0.0" date="2 days ago" desc="Major UI overhaul released" />
                    </Card>
                </motion.div>
            </div>
        </motion.div>
    );
}

function ActionCard({ icon: Icon, title, desc, color }: any) {
    const colors = {
        blue: "text-[#4A90E2] bg-[#4A90E2]/10 group-hover:bg-[#4A90E2]/20",
        purple: "text-purple-500 bg-purple-500/10 group-hover:bg-purple-500/20"
    };

    return (
        <button className="flex items-center gap-6 bg-[#121212] border border-[#1E1E1E] rounded-2xl p-6 hover:border-[#333] transition-all text-left group w-full">
            <div className={clsx("p-4 rounded-xl transition-colors", colors[color as keyof typeof colors])}>
                <Icon className="w-8 h-8" />
            </div>
            <div>
                <div className="font-bold text-lg text-white group-hover:text-[#4A90E2] transition-colors">{title}</div>
                <div className="text-sm text-gray-500">{desc}</div>
            </div>
        </button>
    );
}

function UpdateItem({ version, date, desc }: any) {
    return (
        <div className="flex gap-4">
            <div className="flex flex-col items-center">
                <div className="w-2 h-2 rounded-full bg-[#4A90E2]" />
                <div className="w-0.5 flex-1 bg-[#1E1E1E] mt-1" />
            </div>
            <div className="pb-4">
                <div className="flex items-center gap-2 mb-1">
                    <span className="font-mono text-xs font-bold text-white bg-[#1E1E1E] px-2 py-0.5 rounded border border-[#333]">{version}</span>
                    <span className="text-xs text-gray-500">{date}</span>
                </div>
                <p className="text-sm text-gray-400">{desc}</p>
            </div>
        </div>
    );
}
