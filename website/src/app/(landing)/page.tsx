"use client";

import { motion } from "framer-motion";
import { Shield, Zap, RefreshCw, MousePointer2, ChevronRight, Settings } from "lucide-react";
import Link from "next/link";
import { Button } from "@/components/ui/Button";
import { BentoGrid, BentoItem } from "@/components/ui/BentoGrid";
import { Heading, Text } from "@/components/ui/Typography";
import { Footer } from "@/components/ui/Footer";
import { CallToAction } from "@/components/ui/CallToAction";
import { PricingCard } from "@/components/ui/PricingCard";
import { FAQ } from "@/components/ui/FAQ";
import { Typewriter } from "@/components/ui/Typewriter";
import { fadeInUp } from "@/lib/motion";
import clsx from "clsx";

export default function Home() {
  return (
    <main className="bg-[#0A0A0A] overflow-x-hidden">
      {/* Hero Section */}
      <section className="relative min-h-screen flex items-center justify-center overflow-hidden py-20">
        {/* Background Gradients - Animated Lava Lamp Effect */}
        <motion.div
          animate={{ scale: [1, 1.2, 1], x: [0, 50, 0], y: [0, 30, 0], opacity: [0.2, 0.3, 0.2] }}
          transition={{ duration: 10, repeat: Infinity, ease: "easeInOut" }}
          className="absolute top-0 left-1/2 -translate-x-1/2 w-[1000px] h-[600px] bg-[var(--accent-primary)] opacity-20 blur-[120px] rounded-full pointer-events-none"
        />
        <motion.div
          animate={{ scale: [1, 1.3, 1], x: [0, -40, 0], y: [0, -60, 0], opacity: [0.15, 0.25, 0.15] }}
          transition={{ duration: 12, repeat: Infinity, ease: "easeInOut", delay: 1 }}
          className="absolute bottom-0 right-0 w-[800px] h-[600px] bg-red-600 opacity-15 blur-[120px] rounded-full pointer-events-none"
        />
        <motion.div
          animate={{ scale: [1, 1.1, 1], x: [0, 30, 0], y: [0, 50, 0], opacity: [0.1, 0.2, 0.1] }}
          transition={{ duration: 15, repeat: Infinity, ease: "easeInOut", delay: 2 }}
          className="absolute top-1/4 left-10 w-[600px] h-[500px] bg-orange-600 opacity-10 blur-[100px] rounded-full pointer-events-none"
        />

        <div className="max-w-7xl mx-auto px-6 relative z-10 text-center">



          <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.8, delay: 0.2 }}
            className="mb-8"
          >
            <h1 className="text-7xl md:text-8xl font-black font-heading text-white tracking-tighter mb-6 leading-tight">
              Cobble
            </h1>
            <div className="text-2xl md:text-4xl font-medium text-[var(--text-secondary)] h-12 flex items-center justify-center gap-2">
              <Typewriter
                words={["Undetectable.", "Built for Players.", "Precision.", "Zero compromises.", "Designed to dominate."]}
                typingSpeed={100}
                deletingSpeed={50}
                pauseDuration={2000}
              />
            </div>
          </motion.div>

          <motion.div
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.8, delay: 0.6 }}
            className="flex flex-col sm:flex-row items-center justify-center gap-4"
          >
            <Link href="/register">
              <Button size="lg" icon={<ChevronRight className="w-5 h-5" />}>
                Get Started Now
              </Button>
            </Link>
            <Link href="#features">
              <Button variant="ghost" size="lg" className="border border-white/10">
                View Features
              </Button>
            </Link>
          </motion.div>

          {/* Floating Stats */}
          <motion.div
            initial={{ opacity: 0, y: 30 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.8, delay: 0.8 }}
            className="mt-16 flex flex-wrap items-center justify-center gap-8 md:gap-12"
          >
            {[
              { value: "10K+", label: "Active Users" },
              { value: "99.9%", label: "Undetected" },
              { value: "24/7", label: "Support" },
            ].map((stat) => (
              <div key={stat.label} className="text-center">
                <div className="text-3xl md:text-4xl font-black text-white mb-1">{stat.value}</div>
                <div className="text-sm text-[var(--text-secondary)]">{stat.label}</div>
              </div>
            ))}
          </motion.div>

        </div>
      </section>

      {/* Modern Features Grid (Bento Style) */}
      <section id="features" className="py-32 px-6 relative">
        <div className="max-w-7xl mx-auto">
          <motion.div
            initial="initial"
            whileInView="animate"
            viewport={{ once: true }}
            variants={fadeInUp}
            className="text-center mb-20"
          >
            <Heading className="text-5xl mb-6">Unmatched Power.</Heading>
            <Text className="text-lg">Everything you need to dominate right at your fingertips.</Text>
          </motion.div>

          <BentoGrid>
            {/* Large Card */}
            <BentoItem colSpan={2} delay={0.1}>
              <div className="absolute top-0 right-0 w-64 h-64 bg-[var(--accent-primary)]/10 blur-[80px] rounded-full group-hover:bg-[var(--accent-primary)]/20 transition-all" />
              <Shield className="w-12 h-12 text-[var(--accent-primary)] mb-6" />
              <Heading className="text-3xl mb-4">Undetectable Security</Heading>
              <Text className="text-lg leading-relaxed max-w-md">
                Our kernel-level protection system ensures you stay completely hidden from screenshares and server-side anti-cheats.
                Play with confidence.
              </Text>
            </BentoItem>

            {/* Tall Card */}
            <BentoItem rowSpan={2} delay={0.2}>
              <div className="absolute top-0 left-0 w-full h-full bg-gradient-to-b from-transparent to-[var(--accent-primary)]/5 opacity-0 group-hover:opacity-100 transition-opacity" />
              <div className="h-full flex flex-col justify-end">
                <MousePointer2 className="w-12 h-12 text-[var(--accent-primary)] mb-6" />
                <Heading className="text-3xl mb-4">Aim Assist</Heading>
                <Text className="leading-relaxed">
                  Humanized smoothing algorithms that look legit even in slow motion. Customizable FOV and randomization.
                </Text>
              </div>
            </BentoItem>

            {/* Small Card */}
            <BentoItem delay={0.3}>
              <Zap className="w-10 h-10 text-[var(--warning)] mb-4 group-hover:scale-110 transition-transform" />
              <Heading className="text-xl mb-2">Zero Latency</Heading>
              <Text className="text-sm">Optimized C++ core for maximum FPS.</Text>
            </BentoItem>

            {/* Small Card */}
            <BentoItem delay={0.4}>
              <Settings className="w-10 h-10 text-purple-500 mb-4 group-hover:rotate-90 transition-transform duration-500" />
              <Heading className="text-xl mb-2">Cloud Configs</Heading>
              <Text className="text-sm">Sync your settings across all devices instantly.</Text>
            </BentoItem>

            {/* Wide Card */}
            <BentoItem colSpan={2} delay={0.5}>
              <div className="absolute -right-10 -bottom-10 w-48 h-48 bg-[var(--accent-primary)]/10 rounded-full blur-3xl group-hover:bg-[var(--accent-primary)]/20 transition-all" />
              <div className="flex flex-col md:flex-row items-start md:items-center justify-between gap-6">
                <div className="max-w-md">
                  <RefreshCw className="w-12 h-12 text-[var(--accent-primary)] mb-6" />
                  <Heading className="text-3xl mb-4">Constant Updates</Heading>
                  <Text className="text-lg">
                    We are always ahead of the curve. Updates are pushed automatically via our cloud loader.
                  </Text>
                </div>
                <Button variant="secondary">View Changelogs</Button>
              </div>
            </BentoItem>
          </BentoGrid>
        </div>
      </section>

      {/* Pricing Section (Revamped) */}
      <section id="pricing" className="py-32 px-6">
        <motion.div
          initial="initial"
          whileInView="animate"
          viewport={{ once: true }}
          variants={fadeInUp}
          className="text-center mb-20"
        >
          <span className="text-[var(--accent-primary)] font-bold tracking-widest uppercase text-sm">PRICING</span>
          <Heading className="text-5xl mt-4 mb-6">Simple, Transparent Pricing.</Heading>
          <Text>No hidden fees. Cancel anytime.</Text>
        </motion.div>

        <div className="max-w-5xl mx-auto grid grid-cols-1 md:grid-cols-2 gap-8">
          <PricingCard
            title="Monthly"
            price="10"
            description="Perfect for starters."
            features={["Full Access to All Modules", "24/7 Premium Support", "Cloud Configs", "HWID Resets"]}
            delay={0}
            buttonText="Get Started"
          />
          <PricingCard
            title="Lifetime"
            price="50"
            description="The ultimate value choice."
            features={["Lifetime Access", "Priority Support Queue", "Exclusive Discord Role", "Early Access Features"]}
            isPopular
            delay={0.2}
            buttonText="Get Lifetime"
          />
        </div>
      </section>

      <FAQ />

      <CallToAction />

      <Footer />
    </main>
  );
}
