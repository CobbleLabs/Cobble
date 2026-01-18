"use client";

import { useEffect, useState } from "react";
import { motion } from "framer-motion";
import { Activity, Clock, ShieldCheck, Download, ExternalLink } from "lucide-react";
import { Heading, Text } from "@/components/ui/Typography";
import { Loader } from "@/components/ui/Loader";
import { LoadingTransition } from "@/components/ui/LoadingTransition";
import { BentoGrid, BentoItem } from "@/components/ui/BentoGrid";
import { ActionCard } from "@/components/ui/ActionCard";
import { UpdateItem } from "@/components/ui/UpdateItem";
import { Badge } from "@/components/ui/Badge";
import { Card } from "@/components/ui/Card";
import { staggerContainer, fadeInUp } from "@/lib/motion";
import { useAuth } from "@/context/AuthContext";
import * as api from "@/lib/api";

export default function DashboardPage() {
    const { user } = useAuth();
    const [announcements, setAnnouncements] = useState<api.Announcement[]>([]);
    const [changelogs, setChangelogs] = useState<api.Changelog[]>([]);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        async function fetchData() {
            try {
                const [announcementsData, changelogsData] = await Promise.all([
                    api.getAnnouncements(),
                    api.getChangelogs(),
                ]);
                setAnnouncements(announcementsData || []);
                setChangelogs(changelogsData || []);
            } catch (error) {
                console.error("Failed to fetch dashboard data:", error);
            } finally {
                setLoading(false);
            }
        }
        fetchData();
    }, []);

    // License info
    const license = user?.license;
    const isLifetime = license?.plan === "lifetime";
    const isTrial = license?.plan === "trial";
    const isMonthly = license?.plan === "monthly";

    // A license is active if:
    // - It's lifetime (never expires)
    // - It's monthly/trial AND either has no expiry (admin granted, valid indefinitely) OR expiry is in the future
    const isActive = !!license && (
        isLifetime ||
        !license.expires_at ||
        new Date(license.expires_at) > new Date()
    );

    const planName = isLifetime ? "Lifetime Access" :
        isTrial ? "Trial" :
            isMonthly ? "Monthly Plan" :
                license?.plan === "custom" ? "Custom License" :
                    license ? "Active License" : "No License";

    // Days remaining calculation
    let daysRemaining = "0";
    let progressPercent = 0;
    const MONTHLY_DURATION = 30; // Monthly plan is 30 days

    if (isLifetime) {
        daysRemaining = "∞";
        progressPercent = 100;
    } else if (license?.expires_at) {
        const days = Math.ceil((new Date(license.expires_at).getTime() - Date.now()) / (1000 * 60 * 60 * 24));
        if (days > 0) {
            daysRemaining = days.toString();
            // Calculate progress based on 30-day monthly cycle
            progressPercent = Math.min(100, Math.max(0, (days / MONTHLY_DURATION) * 100));
        } else {
            daysRemaining = "Expired";
            progressPercent = 0;
        }
    } else if (license && !license.expires_at) {
        // No expiry set (admin granted unlimited) - treat as lifetime for display
        daysRemaining = "∞";
        progressPercent = 100;
    }

    // HWID info
    const hwidStatus = license?.hwid ? "Locked" : "Not Set";
    const hwidResetInfo = license?.hwid_last_reset
        ? `Reset available in ${Math.max(0, 30 - Math.ceil((Date.now() - new Date(license.hwid_last_reset).getTime()) / (1000 * 60 * 60 * 24)))} days`
        : "Reset available";

    return (
        <motion.div
            variants={staggerContainer}
            initial="initial"
            animate="animate"
            className="space-y-8"
        >
            <motion.div variants={fadeInUp}>
                <Heading className="text-4xl mb-2">Welcome back, {user?.username || "User"}</Heading>
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
                        <Badge variant={isActive ? "success" : "error"}>
                            {isActive ? "ACTIVE" : "INACTIVE"}
                        </Badge>
                    </div>

                    <h3 className="text-[var(--text-secondary)] text-sm font-medium mb-1">Current Plan</h3>
                    <div className="text-3xl font-bold text-white mb-6">{planName}</div>

                    <div className="h-1.5 w-full bg-[var(--bg-tertiary)] rounded-full overflow-hidden">
                        <div
                            className="h-full bg-[var(--accent-primary)] shadow-[0_0_10px_var(--accent-primary)] transition-all duration-500"
                            style={{ width: `${progressPercent}%` }}
                        />
                    </div>
                    {isActive && (
                        <Text className="text-xs mt-2">{daysRemaining === "∞" ? "Unlimited access" : `${daysRemaining} days remaining`}</Text>
                    )}
                </BentoItem>

                {/* HWID Status */}
                <BentoItem>
                    <div className="flex items-center gap-4 mb-4">
                        <div className="p-2.5 bg-purple-500/10 rounded-lg text-purple-500">
                            <Activity className="w-6 h-6" />
                        </div>
                        <div className="text-sm font-medium text-[var(--text-secondary)]">HWID Status</div>
                    </div>
                    <div className="text-2xl font-bold text-white mb-2">{hwidStatus}</div>
                    <Text className="text-xs">{hwidResetInfo}</Text>
                </BentoItem>

                {/* Session Time */}
                <BentoItem>
                    <div className="flex items-center gap-4 mb-4">
                        <div className="p-2.5 bg-yellow-500/10 rounded-lg text-yellow-500">
                            <Clock className="w-6 h-6" />
                        </div>
                        <div className="text-sm font-medium text-[var(--text-secondary)]">Account</div>
                    </div>
                    <div className="text-2xl font-bold text-white mb-2">
                        {user?.role === "admin" ? "Admin" : "User"}
                    </div>
                    <Text className="text-xs">
                        Member since {user?.created_at ? new Date(user.created_at).toLocaleDateString() : "N/A"}
                    </Text>
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
                            href="/dashboard/downloads"
                        />
                        <ActionCard
                            icon={ExternalLink}
                            title="Join Discord"
                            description="Get help & community updates"
                            color="purple"
                            href="https://discord.gg/frHmJ6TbMF"
                            external
                        />
                    </div>

                    <Heading className="text-xl mb-6 mt-8">Announcements</Heading>
                    <LoadingTransition loading={loading} skeleton={<Loader centered />}>
                        {announcements.length > 0 ? (
                            <div className="space-y-4">
                                {announcements.slice(0, 3).map((announcement) => (
                                    <Card key={announcement.id} className="p-6">
                                        <div className="flex items-start gap-4">
                                            <div className="p-2 bg-[var(--accent-primary)]/10 rounded-lg text-[var(--accent-primary)] mt-1">
                                                <Activity className="w-5 h-5" />
                                            </div>
                                            <div>
                                                <div className="flex items-center gap-2 mb-2">
                                                    <h3 className="text-lg font-bold text-white">{announcement.title}</h3>
                                                    {announcement.pinned && (
                                                        <Badge variant="warning">Pinned</Badge>
                                                    )}
                                                </div>
                                                <Text className="text-sm leading-relaxed mb-4">
                                                    {announcement.content}
                                                </Text>
                                                <div className="flex items-center gap-2 text-xs font-medium text-[var(--text-secondary)]">
                                                    <span className="text-[var(--accent-primary)]">Administrator</span>
                                                    <span>•</span>
                                                    <span>{new Date(announcement.created_at).toLocaleDateString()}</span>
                                                </div>
                                            </div>
                                        </div>
                                    </Card>
                                ))}
                            </div>
                        ) : (
                            <Card className="p-6">
                                <Text className="text-center">No announcements yet.</Text>
                            </Card>
                        )}
                    </LoadingTransition>
                </motion.div>

                {/* Recent News */}
                <motion.div variants={fadeInUp}>
                    <Heading className="text-xl mb-6">Latest Updates</Heading>
                    <LoadingTransition loading={loading} skeleton={<Loader centered />}>
                        <Card className="space-y-6">
                            {changelogs.length > 0 ? (
                                changelogs.slice(0, 5).map((changelog) => (
                                    <UpdateItem
                                        key={changelog.id}
                                        version={changelog.version}
                                        date={new Date(changelog.released_at).toLocaleDateString()}
                                        description={changelog.changes?.[0] || "New update"}
                                    />
                                ))
                            ) : (
                                <Text className="text-center py-4">No changelogs yet.</Text>
                            )}
                        </Card>
                    </LoadingTransition>
                </motion.div>
            </div>
        </motion.div>
    );
}
