"use client";

import { motion } from "framer-motion";
import { Shield, Zap, MessageSquare, Lock, Settings, RefreshCw, Check, MousePointer2, ChevronRight } from "lucide-react";
import Link from "next/link";
import clsx from "clsx";
import { Button } from "@/components/ui/Button";
import { BentoGrid, BentoItem } from "@/components/ui/BentoGrid";
import { Heading, Text } from "@/components/ui/Typography";
import { fadeInUp, staggerContainer } from "@/lib/motion";
import { Card } from "@/components/ui/Card";

export default function Home() {
  return (
    <main className="bg-[#0A0A0A] overflow-x-hidden">
      {/* Hero Section */}
      <section className="relative min-h-screen flex items-center justify-center overflow-hidden pt-20">
        {/* Background Gradients */}
        <div className="absolute top-0 left-1/2 -translate-x-1/2 w-[1000px] h-[600px] bg-[#4A90E2] opacity-20 blur-[120px] rounded-full pointer-events-none" />
        <div className="absolute bottom-0 right-0 w-[800px] h-[600px] bg-purple-600 opacity-10 blur-[120px] rounded-full pointer-events-none" />

        <div className="max-w-7xl mx-auto px-6 relative z-10 text-center">

          <motion.div
            initial={{ opacity: 0, y: 30 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.8, ease: "easeOut" }}
            className="mb-8 inline-flex items-center gap-2 px-4 py-2 rounded-full bg-white/5 border border-white/10 backdrop-blur-md"
          >
            <span className="w-2 h-2 rounded-full bg-[#4A90E2] animate-pulse" />
            <span className="text-sm font-medium text-gray-300">Cobble Client v2.0 is live</span>
          </motion.div>

          <motion.h1
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.8, delay: 0.2 }}
            className="text-6xl md:text-8xl font-black font-heading text-white tracking-tight mb-8 leading-tight"
          >
            Dominance <br />
            <span className="text-transparent bg-clip-text bg-gradient-to-r from-[#4A90E2] to-purple-500">
              Reimagined.
            </span>
          </motion.h1>

          <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            transition={{ duration: 0.8, delay: 0.4 }}
          >
            <Text className="text-xl max-w-2xl mx-auto mb-10 leading-relaxed">
              Experience the next generation of Minecraft ghost clients.
              Undetectable, powerful, and beautifully designed for the modern player.
            </Text>
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

          {/* Float UI Mockup */}
          <motion.div
            initial={{ opacity: 0, y: 100, rotateX: 20 }}
            animate={{ opacity: 1, y: 0, rotateX: 0 }}
            transition={{ duration: 1, delay: 0.8, type: "spring" }}
            className="mt-20 mx-auto max-w-5xl rounded-xl overflow-hidden shadow-2xl border border-white/10 perspective-1000 group"
          >
            <div className="bg-[#121212] aspect-video w-full relative flex items-center justify-center overflow-hidden">
              <div className="absolute inset-0 bg-gradient-to-tr from-[#121212] to-[#1E1E1E] opacity-50" />
              {/* Abstract Representation of Client UI */}
              <div className="w-[80%] h-[80%] bg-[#1E1E1E] rounded-lg border border-[#333] shadow-2xl flex flex-col relative z-20 group-hover:scale-[1.02] transition-transform duration-500">
                <div className="h-12 border-b border-[#333] flex items-center px-6 justify-between bg-[#1A1A1A]">
                  <div className="flex gap-2">
                    <div className="w-3 h-3 rounded-full bg-red-500/80" />
                    <div className="w-3 h-3 rounded-full bg-yellow-500/80" />
                    <div className="w-3 h-3 rounded-full bg-green-500/80" />
                  </div>
                  <span className="text-gray-500 font-mono text-xs">COBBLE.EXE</span>
                </div>
                <div className="flex-1 flex p-6 gap-8">
                  <div className="w-48 space-y-2">
                    {['Combat', 'Visuals', 'Movement', 'Player', 'Exploits'].map((item, i) => (
                      <div key={item} className={clsx("p-3 rounded-lg text-sm font-medium cursor-pointer transition-colors", i === 0 ? "bg-[#4A90E2]/10 text-[#4A90E2]" : "text-gray-500 hover:text-gray-300")}>
                        {item}
                      </div>
                    ))}
                  </div>
                  <div className="flex-1 grid grid-cols-2 gap-4">
                    {[1, 2, 3, 4].map(i => (
                      <div key={i} className="h-24 bg-[#121212] border border-[#333] rounded-lg p-4 flex items-center justify-between group/card hover:border-[#4A90E2]/50 transition-colors">
                        <div className="space-y-2">
                          <div className="w-24 h-2 bg-[#333] rounded group-hover/card:bg-[#4A90E2]/50 transition-colors" />
                          <div className="w-16 h-2 bg-[#252525] rounded" />
                        </div>
                        <div className="w-10 h-6 bg-[#333] rounded-full relative">
                          <div className={clsx("w-4 h-4 rounded-full absolute top-1 transition-all", i % 2 === 0 ? "right-1 bg-[#4A90E2]" : "left-1 bg-gray-500")} />
                        </div>
                      </div>
                    ))}
                  </div>
                </div>
              </div>
            </div>
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
              <div className="absolute top-0 right-0 w-64 h-64 bg-[#4A90E2]/10 blur-[80px] rounded-full group-hover:bg-[#4A90E2]/20 transition-all" />
              <Shield className="w-12 h-12 text-[#4A90E2] mb-6" />
              <Heading className="text-3xl mb-4">Undetectable Security</Heading>
              <Text className="text-lg leading-relaxed max-w-md">
                Our kernel-level protection system ensures you stay completely hidden from screenshares and server-side anti-cheats.
                Play with confidence.
              </Text>
            </BentoItem>

            {/* Tall Card */}
            <BentoItem rowSpan={2} delay={0.2}>
              <div className="absolute top-0 left-0 w-full h-full bg-gradient-to-b from-transparent to-[#4A90E2]/5 opacity-0 group-hover:opacity-100 transition-opacity" />
              <div className="h-full flex flex-col justify-end">
                <MousePointer2 className="w-12 h-12 text-[#4A90E2] mb-6" />
                <Heading className="text-3xl mb-4">Aim Assist</Heading>
                <Text className="leading-relaxed">
                  Humanized smoothing algorithms that look legit even in slow motion. Customizable FOV and randomization.
                </Text>
              </div>
            </BentoItem>

            {/* Small Card */}
            <BentoItem delay={0.3}>
              <Zap className="w-10 h-10 text-yellow-500 mb-4 group-hover:scale-110 transition-transform" />
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
              <div className="absolute -right-10 -bottom-10 w-48 h-48 bg-[#4A90E2]/10 rounded-full blur-3xl group-hover:bg-[#4A90E2]/20 transition-all" />
              <div className="flex flex-col md:flex-row items-start md:items-center justify-between gap-6">
                <div className="max-w-md">
                  <RefreshCw className="w-12 h-12 text-[#4A90E2] mb-6" />
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
      <section id="pricing" className="py-32 px-6 border-t border-[#1E1E1E]">
        <motion.div
          initial="initial"
          whileInView="animate"
          viewport={{ once: true }}
          variants={fadeInUp}
          className="text-center mb-20"
        >
          <span className="text-[#4A90E2] font-bold tracking-widest uppercase text-sm">PRICING</span>
          <Heading className="text-5xl mt-4 mb-6">Simple, Transparent Pricing.</Heading>
          <Text>No hidden fees. Cancel anytime.</Text>
        </motion.div>

        <div className="max-w-5xl mx-auto grid grid-cols-1 md:grid-cols-2 gap-8">
          <PricingCard
            title="Monthly"
            price="10"
            desc="Perfect for starters."
            features={["Full Access to All Modules", "24/7 Premium Support", "Cloud Configs", "HWID Resets"]}
            delay={0}
          />
          <PricingCard
            title="Lifetime"
            price="50"
            desc="The ultimate value choice."
            features={["Lifetime Access", "Priority Support Queue", "Exclusive Discord Role", "Early Access Features", "Private Beta Access"]}
            isPopular
            delay={0.2}
          />
        </div>
      </section>

      <footer className="bg-[#050505] py-12 border-t border-[#1E1E1E]">
        <div className="max-w-7xl mx-auto px-6 flex flex-col md:flex-row justify-between items-center gap-6">
          <div className="text-2xl font-bold text-white tracking-widest">COBBLE</div>
          <div className="text-gray-500 text-sm">© 2026 Cobble Client. All rights reserved.</div>
          <div className="flex gap-6">
            <Link href="#" className="text-gray-400 hover:text-white transition-colors">Terms</Link>
            <Link href="#" className="text-gray-400 hover:text-white transition-colors">Privacy</Link>
            <Link href="#" className="text-gray-400 hover:text-white transition-colors">Discord</Link>
          </div>
        </div>
      </footer>
    </main>
  );
}

function PricingCard({ title, price, desc, features, isPopular, delay }: any) {
  return (
    <motion.div
      initial={{ opacity: 0, y: 30 }}
      whileInView={{ opacity: 1, y: 0 }}
      viewport={{ once: true }}
      transition={{ duration: 0.5, delay }}
    >
      <Card variant={isPopular ? "premium" : "default"} className={clsx("h-full hover:scale-[1.02] transition-transform", isPopular && "border-[#4A90E2]")}>
        {isPopular && (
          <div className="absolute top-0 left-1/2 -translate-x-1/2 -translate-y-1/2 bg-[#4A90E2] text-white px-4 py-1 rounded-full text-xs font-bold tracking-widest shadow-lg">
            MOST POPULAR
          </div>
        )}

        <Heading className="text-2xl mb-2">{title}</Heading>
        <Text className="text-sm mb-8">{desc}</Text>

        <div className="flex items-baseline gap-1 mb-8">
          <span className="text-white text-6xl font-black font-heading">${price}</span>
          <span className="text-gray-500">/USD</span>
        </div>

        <Link href="/register">
          <Button variant={isPopular ? "primary" : "secondary"} className="w-full mb-10">
            Let&apos;s Go
          </Button>
        </Link>

        <div className="space-y-4">
          {features.map((f: string, i: number) => (
            <div key={i} className="flex items-center gap-4 text-gray-300">
              <div className={clsx("rounded-full p-1", isPopular ? "bg-[#4A90E2]/20 text-[#4A90E2]" : "bg-gray-800 text-gray-400")}>
                <Check className="w-4 h-4" />
              </div>
              {f}
            </div>
          ))}
        </div>
      </Card>
    </motion.div>
  );
}
