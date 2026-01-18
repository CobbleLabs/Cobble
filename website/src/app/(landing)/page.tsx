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

import { useAuth } from "@/context/AuthContext";
import { useRouter } from "next/navigation";

export default function Home() {
  const { isAuthenticated } = useAuth();
  const router = useRouter();

  const handleChangelogsClick = () => {
    if (isAuthenticated) {
      router.push("/dashboard");
    } else {
      router.push("/login");
    }
  };

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
                words={["Built to win.", "Must be skill.", "Just better.", "Too late.", "Just a good gaming chair."]}
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

          {/* Decorative gradient line */}
          <motion.div
            initial={{ opacity: 0, scaleX: 0 }}
            animate={{ opacity: 1, scaleX: 1 }}
            transition={{ duration: 1, delay: 1 }}
            className="mt-16 mx-auto w-full max-w-md h-px bg-gradient-to-r from-transparent via-[var(--accent-primary)]/50 to-transparent"
          />

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
            <Heading className="text-5xl mb-6">Unfair by design.</Heading>
            <Text className="text-lg">Weird how easy it is.</Text>
          </motion.div>

          <BentoGrid>
            {/* Row 1: Invisible, Precision, Instant */}
            <BentoItem delay={0.1}>
              <div className="absolute top-0 right-0 w-48 h-48 bg-[var(--accent-primary)]/10 blur-[60px] rounded-full group-hover:bg-[var(--accent-primary)]/20 transition-all duration-500" />
              <Shield className="w-10 h-10 text-[var(--accent-primary)] mb-4 transition-transform duration-300 group-hover:scale-110" />
              <Heading className="text-2xl mb-2">Invisible.</Heading>
              <Text className="text-sm leading-relaxed">
                Screenshare all you want. We&apos;re not in the picture.
              </Text>
            </BentoItem>

            <BentoItem delay={0.2}>
              <div className="absolute bottom-0 left-0 w-full h-32 bg-gradient-to-t from-[var(--accent-primary)]/5 to-transparent opacity-0 group-hover:opacity-100 transition-opacity duration-500" />
              <div className="relative">
                <MousePointer2 className="w-10 h-10 text-[var(--accent-primary)] mb-4 transition-transform duration-300 group-hover:translate-x-1 group-hover:-translate-y-1" />
                <div className="absolute top-0 left-0 w-10 h-10 bg-[var(--accent-primary)]/50 blur-lg rounded-full opacity-0 group-hover:opacity-50 transition-opacity duration-300" />
              </div>
              <Heading className="text-2xl mb-2">Precision.</Heading>
              <Text className="text-sm leading-relaxed">
                Looks legit even in slow motion. Feels like you just got better.
              </Text>
            </BentoItem>

            <BentoItem delay={0.3}>
              <div className="absolute inset-0 bg-[var(--accent-primary)]/5 opacity-0 group-hover:opacity-100 transition-opacity duration-300" />
              <Zap className="w-10 h-10 text-[var(--accent-primary)] mb-4 transition-all duration-300 group-hover:scale-125 group-hover:fill-[var(--accent-primary)]/20" />
              <Heading className="text-2xl mb-2">Instant.</Heading>
              <Text className="text-sm leading-relaxed">
                If you miss, it&apos;s not us.
              </Text>
            </BentoItem>

            {/* Row 2: Always synced, Always ahead */}
            <BentoItem delay={0.4}>
              <Settings className="w-10 h-10 text-[var(--accent-primary)] mb-4 transition-transform duration-300 group-hover:scale-110" />
              <Heading className="text-2xl mb-2">Always synced.</Heading>
              <Text className="text-sm leading-relaxed">
                Log in. Everything&apos;s already there. Cloud saves.
              </Text>
            </BentoItem>

            <BentoItem colSpan={2} delay={0.5}>
              <div className="absolute -right-10 -bottom-10 w-64 h-64 bg-[var(--accent-primary)]/10 rounded-full blur-[80px] group-hover:bg-[var(--accent-primary)]/20 transition-all duration-500" />
              <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-6 relative z-10">
                <div>
                  <RefreshCw className="w-10 h-10 text-[var(--accent-primary)] mb-4 transition-transform duration-300 group-hover:scale-110" />
                  <Heading className="text-2xl mb-2">Always ahead.</Heading>
                  <Text className="text-sm leading-relaxed max-w-sm">
                    They patch. We&apos;re already there. Constant automatic updates via cloud.
                  </Text>
                </div>
                <Button
                  size="sm"
                  variant="secondary"
                  className="mt-2 sm:mt-0"
                  onClick={handleChangelogsClick}
                >
                  View Changelogs
                </Button>
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
          <Text>Yeah, it’s that simple.</Text>
        </motion.div>

        <div className="max-w-5xl mx-auto grid grid-cols-1 md:grid-cols-2 gap-8">
          <PricingCard
            title="Monthly"
            price="10"
            description="Start here."
            features={["Full Access to All Modules", "24/7 Premium Support", "Cloud Configs", "HWID Resets"]}
            delay={0}
            buttonText="Get Started"
          />
          <PricingCard
            title="Lifetime"
            price="35"
            description="The right choice."
            features={["Lifetime Access", "Priority Support", "Exclusive Discord Role", "Early Access Features"]}
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
