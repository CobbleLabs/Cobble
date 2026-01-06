# Cobble Client Website

A modern, production-ready Next.js website built with reusable components and consistent design patterns.

---

## 🛠 Tech Stack

| Technology | Purpose |
|------------|---------|
| **Next.js 16** | App Router, static generation |
| **TypeScript** | Type safety |
| **Tailwind CSS** | Utility-first styling |
| **Framer Motion** | Animations |
| **Lucide React** | UI icons |
| **React Icons** | Brand icons (Discord, etc.) |

---

## 📁 Project Structure

```
src/
├── app/
│   ├── (landing)/          # Marketing pages (Home)
│   │   ├── layout.tsx      # Navbar + scroll-to-top
│   │   └── page.tsx        # Landing page
│   ├── dashboard/          # Protected user area
│   │   ├── layout.tsx      # Sidebar navigation
│   │   ├── profile/        # My Profile page
│   │   ├── downloads/      # Downloads page
│   │   └── ...
│   ├── login/              # Auth pages
│   ├── register/
│   ├── globals.css         # CSS variables + global styles
│   └── layout.tsx          # Root layout (fonts, metadata)
│
├── components/ui/          # Reusable components
│   ├── Button.tsx
│   ├── Card.tsx
│   ├── Input.tsx
│   ├── Badge.tsx
│   ├── Typography.tsx      # Heading, Text
│   └── ...
│
└── lib/
    ├── motion.ts           # Framer Motion variants
    └── utils.ts            # cn() helper
```

---

## 🎨 Design System

### Typography
**Single font: Inter** — Used for all text (headings, body, labels).

```tsx
// ✅ Do this
<Heading className="text-4xl">Title</Heading>
<Text>Description text</Text>

// ❌ Don't do this
<h1 className="text-4xl">Title</h1>
<p>Description</p>
```

### Colors (CSS Variables)
All colors are defined in `globals.css`:

```css
--bg-primary: #121212;
--bg-secondary: #1E1E1E;
--text-primary: #FFFFFF;
--text-secondary: #A0A0A0;
--accent-primary: #4A90E2;
--success: #27AE60;
--warning: #F2994A;
--error: #EB5757;
--border-color: #333333;
```

```tsx
// ✅ Use CSS variables
className="bg-[var(--bg-secondary)] text-[var(--text-secondary)]"

// ❌ Don't hardcode colors
className="bg-[#1E1E1E] text-[#A0A0A0]"
```

### Border Radius
- **Buttons/Inputs**: `rounded-xl` (12px)
- **Cards**: `rounded-2xl` or `rounded-3xl` (16-24px)

---

## 📦 Reusable Components

### Button
```tsx
import { Button } from "@/components/ui/Button";

<Button>Default</Button>
<Button variant="secondary">Secondary</Button>
<Button variant="ghost">Ghost</Button>
<Button size="lg" icon={<Icon />}>With Icon</Button>
```

### Card
```tsx
import { Card } from "@/components/ui/Card";

<Card className="p-6">
  Content here
</Card>
```

### Input
```tsx
import { Input } from "@/components/ui/Input";

<Input label="Email" type="email" placeholder="you@example.com" />
<Input label="Password" error="Required field" />
```

### Badge
```tsx
import { Badge } from "@/components/ui/Badge";

<Badge variant="success">Active</Badge>
<Badge variant="warning">Pending</Badge>
<Badge variant="error">Expired</Badge>
```

### Typography
```tsx
import { Heading, Text } from "@/components/ui/Typography";

<Heading className="text-4xl mb-2">Page Title</Heading>
<Text>Description or body text</Text>
```

---

## ➕ Adding New Pages

### Dashboard Page
1. Create folder: `src/app/dashboard/[page-name]/`
2. Create `page.tsx`:

```tsx
"use client";

import { motion } from "framer-motion";
import { Heading, Text } from "@/components/ui/Typography";
import { Card } from "@/components/ui/Card";
import { staggerContainer, fadeInUp } from "@/lib/motion";

export default function NewPage() {
    return (
        <motion.div
            variants={staggerContainer}
            initial="initial"
            animate="animate"
            className="space-y-8"
        >
            <motion.div variants={fadeInUp}>
                <Heading className="text-4xl mb-2">Page Title</Heading>
                <Text>Page description.</Text>
            </motion.div>

            <Card>
                {/* Content */}
            </Card>
        </motion.div>
    );
}
```

3. Add to sidebar in `dashboard/layout.tsx`:
```tsx
const navItems = [
    // ... existing items
    { name: "New Page", href: "/dashboard/new-page", icon: IconName },
];
```

### Landing Section
Add new section in `(landing)/page.tsx`:

```tsx
<section id="section-id" className="py-32 px-6">
  <div className="max-w-7xl mx-auto">
    {/* Content */}
  </div>
</section>
```

---

## ✅ Best Practices

### DO
- Use `Heading` and `Text` for all typography
- Use CSS variables for colors
- Use existing components (`Button`, `Card`, `Input`, `Badge`)
- Use `motion.div` with `fadeInUp` for page animations
- Keep components small and focused
- Use `space-y-8` for consistent vertical spacing

### DON'T
- Hardcode hex colors
- Use raw `<h1>`, `<p>`, `<button>` tags
- Create inline styles
- Duplicate component logic
- Mix different border-radius values

---

## 🚀 Commands

```bash
# Development
npm run dev

# Production build
npm run build

# Start production server
npm start

# Lint
npm run lint
```

---

## 📄 File Templates

### New Component
```tsx
"use client";

import clsx from "clsx";

interface ComponentProps {
    children: React.ReactNode;
    className?: string;
}

export function Component({ children, className }: ComponentProps) {
    return (
        <div className={clsx("base-styles", className)}>
            {children}
        </div>
    );
}
```

### Motion Animation
```tsx
import { motion } from "framer-motion";
import { fadeInUp } from "@/lib/motion";

<motion.div variants={fadeInUp}>
  Animated content
</motion.div>
```

---

## 🎯 Quick Reference

| Need | Use |
|------|-----|
| Page title | `<Heading className="text-4xl">` |
| Description | `<Text>` |
| Container | `<Card>` |
| Form input | `<Input label="..." />` |
| Action | `<Button>` |
| Status | `<Badge variant="...">` |
| Animation | `motion.div` + `fadeInUp` |
| Spacing | `space-y-8`, `gap-6` |
| Layout | `grid`, `flex` |

---

*Built with Next.js 16 + Tailwind CSS*
