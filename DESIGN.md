---
name: Silicon Patient
description: Computational Neuropharmacology Research Console Design System
colors:
  primary: "#6366f1"
  primary-hover: "#818cf8"
  secondary: "#06b6d4"
  secondary-hover: "#22d3ee"
  accent-emerald: "#10b981"
  neutral-bg: "#020209"
  neutral-panel: "rgba(3, 4, 18, 0.45)"
  neutral-border: "rgba(99, 102, 241, 0.20)"
  text-main: "#f8fafc"
  text-muted: "#94a3b8"
typography:
  display:
    fontFamily: "Inter, system-ui, sans-serif"
    fontSize: "1.25rem"
    fontWeight: 700
    lineHeight: "1.2"
    letterSpacing: "-0.025em"
  title:
    fontFamily: "Inter, system-ui, sans-serif"
    fontSize: "0.875rem"
    fontWeight: 600
    lineHeight: "1.4"
    letterSpacing: "normal"
  body:
    fontFamily: "Inter, system-ui, sans-serif"
    fontSize: "0.75rem"
    fontWeight: 400
    lineHeight: "1.5"
    letterSpacing: "normal"
  label:
    fontFamily: "JetBrains Mono, monospace"
    fontSize: "0.53125rem"
    fontWeight: 700
    lineHeight: "1"
    letterSpacing: "0.2em"
rounded:
  sm: "8px"
  md: "12px"
  lg: "16px"
spacing:
  xs: "4px"
  sm: "8px"
  md: "12px"
  lg: "16px"
components:
  button-primary:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.text-main}"
    rounded: "{rounded.md}"
    padding: "8px 14px"
  button-primary-hover:
    backgroundColor: "{colors.primary-hover}"
  button-secondary:
    backgroundColor: "rgba(15, 23, 42, 0.40)"
    textColor: "{colors.text-main}"
    rounded: "{rounded.md}"
    padding: "8px 14px"
---

# Design System: Silicon Patient

## Overview

**Creative North Star: "The Biophysical Research Instrument"**

Silicon Patient is designed as a high-precision, dark-mode computational neuropharmacology research console. Rather than presenting content inside generic SaaS cards or grid boxes, the 2D biological microcircuit simulation serves as the primary stage (filling 100% of the viewport). Scientific telemetry, execution controls, and experiment ledgers float as translucent frosted glass panels (`backdrop-blur-md bg-slate-950/40 border border-indigo-500/35`) that reveal progressively without compressing or reducing stage dimensions.

**Key Characteristics:**
- Unrestricted full-viewport 2D Canvas stage.
- High-translucency frosted glass overlays (`backdrop-blur-md bg-slate-950/40`).
- Cursor-sensitive edge corridors that reveal floating tabs with 100% transparent default styling (`bg-transparent`).
- Strict single-active-panel mutual exclusion to maintain visual focus.
- Quantitative data integrity: `JetBrains Mono` uppercase labels for biophysical metrics and explicit model view badges.

## Colors

The palette uses a deep observatory navy-black base paired with precise scientific signal accents for ion channel dynamics and post-synaptic kinetics.

### Primary
- **Obsidian Deep Base** (#020209): The main observatory backdrop and viewport canvas background.
- **Voltage Indigo** (#6366f1 / `rgba(99, 102, 241, 0.4)`): Primary research control accent, Na⁺ voltage-gated ion channels, pyramidal soma nodes.

### Secondary
- **Kinetic Cyan** (#06b6d4 / `rgba(6, 182, 212, 0.4)`): Latest assay indicators, NMDA receptor targets, active evaluation tags.

### Tertiary
- **Synaptic Emerald** (#10b981 / `rgba(16, 185, 129, 0.4)`): Live activity ledger, GABA-A receptor targets, network stability indicators.

### Neutral
- **Frosted Glass Panel** (`rgba(3, 4, 18, 0.45)`): Translucent overlay background for floating cards.
- **Subtle Glass Border** (`rgba(99, 102, 241, 0.20)`): Edge boundaries for panels and sub-cards.
- **High-Contrast Text** (#f8fafc): Primary headers and numerical metrics.
- **Muted Telemetry Text** (#94a3b8 / #64748b): Sub-labels and technical metadata.

### Named Rules
**The Single-Voice Rarity Rule.** Glowing neon accents are reserved for active biological pathways and state indicators. Secondary elements use low-opacity glass borders.

## Typography

**Display & Body Font:** Inter (with `system-ui, sans-serif` fallback)
**Data & Monospace Font:** JetBrains Mono (with `monospace` fallback)

**Character:** Clean, highly legible sans-serif for interface controls paired with dense, uppercase monospace for scientific telemetry and quantitative values.

### Hierarchy
- **Display** (Bold, 20px / 1.25rem, line-height 1.2): Main compound titles and hero headings.
- **Headline** (Semi-bold, 14px / 0.875rem, line-height 1.4): Panel headers and section titles.
- **Body** (Regular/Medium, 12px / 0.75rem, line-height 1.5): Descriptive text, table items, and status pills.
- **Label** (Bold, 8.5px / 0.53125rem, uppercase, letter-spacing 0.2em, JetBrains Mono): Section headers, data metric descriptors, model view badges.

### Named Rules
**The Telemetry Monospace Rule.** All numerical values, timestamps, run IDs, and model labels must use `JetBrains Mono` to emphasize scientific data authority.

## Layout

- **Full Viewport Canvas Stage**: The biological canvas occupies 100% width and `calc(100vh - 4.25rem)` height without margin offsets.
- **Left Edge Stacked Corridors**: Trigger handles (`RESEARCH CONTROL` at top-4, `LATEST ASSAY` at top-14, `ACTIVITY LEDGER` at top-24) are stacked vertically on the top-left edge with 100% transparent default styling (`bg-transparent`).
- **Single Active Overlay Panel**: Selecting a feature opens a floating glass overlay card (width 320px) on the left side while automatically closing the previous open feature card.

## Elevation & Depth

Surfaces rely on frosted glass translucency (`backdrop-blur-md bg-slate-950/40`) and low-opacity glowing borders (`border border-indigo-500/35`) rather than heavy drop shadows.

### Shadow Vocabulary
- **Neural Glow** (`box-shadow: 0 0 0 1px rgba(99,102,241,0.15), 0 8px 32px rgba(0,0,20,0.40)`): Used on floating overlay cards.
- **Synapse Glow** (`box-shadow: 0 0 0 1px rgba(52,211,153,0.20), 0 4px 24px rgba(0,0,20,0.35)`): Used on emerald active ledger components.

### Named Rules
**The Translucent Glass Rule.** Overlay cards must remain high-translucency frosted glass so that underlying neurons, signal paths, and canvas grid lines are visibly flowing behind the UI.

## Shapes

- **Floating Panels**: Rounded-2xl corners (`border-radius: 16px`) with 1px glass borders (`border-white/[0.08]` or `border-indigo-500/35`).
- **Sub-Cards & Input Fields**: Rounded-xl corners (`border-radius: 12px`).
- **Pills & Status Badges**: Rounded-full pill shapes (`border-radius: 9999px`).

## Components

### Buttons
- **Shape:** Rounded-xl (12px radius)
- **Primary:** `bg-gradient-to-r from-indigo-600/90 to-cyan-600/90 text-white font-bold text-xs uppercase`
- **Secondary / Ghost:** `bg-slate-900/40 border border-white/[0.1] hover:bg-slate-800/60 text-slate-200 backdrop-blur-sm`

### Status Pills
- **Shape:** Rounded-full
- **Completed:** `bg-emerald-500/20 text-emerald-300 border border-emerald-500/40`
- **Running / Queued:** `bg-amber-500/20 text-amber-300 border border-amber-500/40`
- **Failed:** `bg-rose-500/20 text-rose-300 border border-rose-500/40`

### Overlay Cards
- **Shape:** Rounded-2xl (16px radius), `w-80`
- **Background:** `backdrop-blur-md bg-slate-950/40 border border-indigo-500/35 shadow-2xl`
- **Internal Padding:** `p-3.5`

## Do's and Don'ts

### Do:
- **Do** maintain 100% viewport width for the canvas stage.
- **Do** keep trigger tab button backgrounds 100% transparent by default (`bg-transparent`).
- **Do** enforce single-active panel mutual exclusion when opening feature overlays.
- **Do** explicitly label conceptual model metrics with "MODEL" or "MODEL VIEW" badges.

### Don't:
- **Don't** use opaque dark solid backgrounds (`bg-slate-950/95`) on floating panels that block the canvas.
- **Don't** add generic SaaS card grids or static sidebars that shrink canvas dimensions.
- **Don't** display fake backend telemetry, CUDA/SQLite server connectivity badges, or server ping stats.
- **Don't** use non-functional decorative purple gradients.
