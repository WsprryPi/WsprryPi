---
name: Wsprry Pi Console
description: A precise bench-instrument interface for WSPR-family radio workflows.
colors:
  transmitter-slate: "#405e7a"
  transmitter-slate-dark: "#344a60"
  slate-muted: "#6b8096"
  signal-text-light: "#ecf1f6"
  signal-text-dark: "#e5ecf2"
  zephyr-primary: "#3459e6"
  body-text-light: "#495057"
  body-bg-light: "#ffffff"
  secondary-bg-light: "#e9ecef"
  border-light: "#dee2e6"
  body-text-dark: "#dee2e6"
  body-bg-dark: "#212529"
  secondary-bg-dark: "#343a40"
  border-dark: "#495057"
  state-success: "#5f8f74"
  state-warning: "#b79256"
  state-danger: "#b66a76"
  state-active: "#c8a85d"
typography:
  display:
    fontFamily: "Barlow Semi Condensed, Segoe UI, sans-serif"
    fontSize: "clamp(1.5rem, 2vw, 2rem)"
    fontWeight: 600
    lineHeight: 1.05
    letterSpacing: "0.012em"
  headline:
    fontFamily: "Barlow Semi Condensed, Segoe UI, sans-serif"
    fontSize: "1.5625rem"
    fontWeight: 600
    lineHeight: 1.12
    letterSpacing: "0.012em"
  title:
    fontFamily: "Barlow Semi Condensed, Segoe UI, sans-serif"
    fontSize: "1.25rem"
    fontWeight: 600
    lineHeight: 1.12
    letterSpacing: "0.012em"
  body:
    fontFamily: "Source Sans 3, Segoe UI, sans-serif"
    fontSize: "1rem"
    fontWeight: 400
    lineHeight: 1.55
  label:
    fontFamily: "Barlow Semi Condensed, Segoe UI, sans-serif"
    fontSize: "0.75rem"
    fontWeight: 700
    lineHeight: 1.3
    letterSpacing: "0.08em"
rounded:
  sm: "0.25rem"
  md: "0.375rem"
  lg: "0.5rem"
  tab: "0.9rem 0.9rem 0 0"
  pill: "999px"
spacing:
  xs: "0.5rem"
  sm: "0.75rem"
  md: "1rem"
  lg: "1.5rem"
  xl: "2rem"
  xxl: "clamp(2.5rem, 4vw, 3.5rem)"
components:
  button-primary:
    backgroundColor: "{colors.zephyr-primary}"
    textColor: "{colors.body-bg-light}"
    rounded: "{rounded.md}"
    padding: "0.5rem 1rem"
  nav-primary:
    backgroundColor: "{colors.transmitter-slate}"
    textColor: "{colors.signal-text-light}"
    rounded: "{rounded.lg}"
    padding: "0.5rem 0.8rem"
  panel:
    backgroundColor: "{colors.body-bg-light}"
    textColor: "{colors.body-text-light}"
    rounded: "{rounded.md}"
    padding: "1.05rem 1.15rem"
  chip:
    backgroundColor: "{colors.body-bg-light}"
    textColor: "{colors.body-text-light}"
    rounded: "{rounded.pill}"
    padding: "0.45rem 0.75rem"
  footer-state-badge:
    backgroundColor: "{colors.transmitter-slate}"
    textColor: "{colors.signal-text-light}"
    rounded: "{rounded.sm}"
    padding: "0.05rem 0.35rem"
---

# Design System: Wsprry Pi Console

## Overview

**Creative North Star: "The Bench Instrument"**

Wsprry Pi Console is an operational control surface for technically capable amateur-radio operators. It should feel like a clean bench instrument: compact, readable, and specific, with clear state changes and enough hierarchy to scan transmitter status, configuration, logs, maintenance, and spots without theatrical styling.

Bootstrap 5 and Zephyr provide familiar control behavior. The product gains its character from a transmitter-slate shell, local Source Sans 3 and Barlow Semi Condensed fonts, restrained tonal panels, and explicit operational state colors. Trust comes from truthful state, predictable placement, readable technical values, and consistent light, dark, desktop, and mobile behavior.

**Key Characteristics:**

- Precise, clean, technical, and operational.
- Dense enough for repeated appliance use without feeling cramped.
- Light and dark themes share one layout and interaction model.
- State, mode, and transmission context outrank decoration.
- Recovery actions remain close to the state requiring attention.

## Colors

The palette combines a restrained slate appliance shell, neutral Bootstrap surfaces, Zephyr blue for ordinary primary actions, and muted state colors reserved for confirmed operational meaning.

### Primary

- **Transmitter Slate:** Fixed navigation, footer, and status chrome; avoid large interior fills.
- **Low-Light Transmitter Slate:** Dark-theme shell counterpart without excess luminosity.
- **Zephyr Action Blue:** Primary actions and links where Bootstrap establishes that role; it does not replace the slate shell.

### Secondary

- **Slate Muted:** Subtle active treatments and the shared focus-ring source.

### Tertiary

- **Signal Active Gold:** Connected or ready state only.
- **Signal Success, Warning, and Danger:** Confirmed operational success, caution, and failure.

### Neutral

- **Light and Dark Canvas:** Principal page and card surfaces.
- **Light and Dark Body Text:** Default readable foregrounds.
- **Secondary Surfaces:** Tonal ingredients for grouped panels.
- **Light and Dark Borders:** One-pixel structural dividers and field boundaries.
- **Signal Text:** High-contrast foregrounds inside transmitter-slate chrome.

### Named Rules

**The State Color Rule.** State colors are operational vocabulary, never decoration.

**The Slate Shell Rule.** The shell owns transmitter slate. Interior panels stay neutral and use restrained tonal mixing only for grouping or state.

**The Accessible State Badge Rule.** Warning and danger on slate use signal text over a state-tinted slate badge, not raw semantic foreground text.

## Typography

**Display Font:** Barlow Semi Condensed, with Segoe UI and sans-serif fallbacks  
**Body Font:** Source Sans 3, with Segoe UI and sans-serif fallbacks  
**Label/Mono Font:** Barlow Semi Condensed for scan labels; Bootstrap monospace for logs and code

**Character:** Barlow Semi Condensed gives headings, navigation, tabs, and state values an instrument-panel cadence. Source Sans 3 keeps help, tables, forms, and summaries readable over repeated use.

### Hierarchy

- **Display:** Major state values and page-level titles.
- **Headline:** Navbar identity and primary view headings.
- **Title:** Card titles and dense section headings.
- **Body:** Forms, summaries, alerts, and copy, kept near 68 characters wide where practical.
- **Label:** Uppercase eyebrows, compact metadata, state labels, and scan anchors.

### Named Rules

**The Label Discipline Rule.** Uppercase Barlow labels are scan anchors, not body copy, help text, or button prose.

## Layout

Fixed navigation and footer frame a single responsive page shell. A centered Bootstrap container carries one primary card, with the spacing scale providing rhythm. Dense operational information uses grids, tables, and flat panels inside that card instead of repeated nested cards.

At narrow widths, navigation stacks, toolbars wrap, and controls expand when touch use benefits. Wide technical tables retain readable columns inside explicit horizontal scrollers instead of wrapping by character or forcing page overflow. Long identifiers and messages wrap safely where scrolling is not intended.

Observation lists pair labels and wrap-safe values in columns on wide screens, then stack each label above its value on narrow screens. Keep related status and recovery controls in the same flat section.

**The One-Shell Rule.** Keep one dominant page card; use spacing, borders, and tonal panels for interior hierarchy.

**The Responsive Truth Rule.** Mobile may stack or scroll, but it must preserve every operational distinction and behavior.

## Elevation & Depth

The system uses light structural lift plus tonal layering. The outer page card and temporary overlays may lift; interior panels remain flat and gain hierarchy through one-pixel borders, neutral mixes, and spacing.

### Shadow Vocabulary

- **Base Surface Shadow:** Small two-layer lift for the page card, tables, list groups, and modals.
- **Navbar Shadow:** Wider low-contrast separation for fixed navigation.
- **Alert Shadow:** Compact lift for the temporary connection banner.
- **Focus Ring:** Slate-derived keyboard focus on controls and navigation.

### Named Rules

**The Interior Flatness Rule.** Interior panels use a one-pixel border, restrained tint, and spacing before another shadow.

## Shapes

Controls and containers use Bootstrap's radius family: compact corners for small controls, medium corners for ordinary fields and cards, and slightly larger corners for alerts. Pills are reserved for compact metadata and live status. Tabs use stronger top corners to show their segmented relationship.

Borders are structural and complete. Avoid decorative side stripes, arbitrary clipping, and novelty geometry.

**The Familiar Geometry Rule.** Shape communicates control type and grouping; it does not compete with operational content.

## Components

### Buttons

- **Shape:** Medium curved corners with standard action padding.
- **Primary:** Zephyr Action Blue with a high-contrast light foreground.
- **Hover / Focus:** Established Bootstrap state change plus the shared slate focus ring.
- **Secondary / Destructive:** Outline or link utilities; danger only for genuine stop, shutdown, delete, or failure actions.
- **Busy State:** Disable concurrent activation and use direct progressive language such as “Connecting…”.

### Chips

- **Style:** Pill-shaped, compact, medium-weight, and neutral or slate-tinted.
- **State:** Hostname, metadata, and live status—not filters or form controls.

### Cards / Containers

- **Corner Style:** Medium Bootstrap corners.
- **Background:** Theme canvas outside; restrained neutral mixes inside.
- **Shadow Strategy:** The outer card may lift; nested operational panels stay flat.
- **Border:** One-pixel theme border or subtle tonal mix.

### Inputs / Fields

- **Style:** Familiar Bootstrap controls with room for validation and technical values.
- **Focus:** Shared slate-derived ring.
- **Error / Disabled:** Validation beneath its field, preserved drafts, wrap-safe values, and unmistakable disabled state.
- **Progressive Disclosure:** Keep visibility controls separate from output selection. When settings are hidden, retain their selection and show a nearby explanation when it remains active.

### Navigation

- **Style:** Fixed transmitter-slate shell with Barlow identity, Source Sans supporting copy, wrapped icon labels, and concise connection state.
- **Active / Focus:** Subtle translucent fill and border plus the shared focus ring.
- **Mobile Treatment:** Full-width stacked menu retaining labels, theme control, and state visibility.

### Tabs, Panels, and Tables

- **Tabs:** Condensed display type, tall targets, strong top corners, and restrained active fill.
- **Panels:** Grid structure, uppercase scan labels, semibold values, neutral tint, borders, and wrap-safe content.
- **Tables:** Compact readable type, sticky headings where useful, and horizontal scrolling when technical columns must remain legible.
- **Stateful Content:** Expose loading with `aria-busy`, keep recovery nearby, and distinguish empty results from failed requests.
- **Observations:** Label historical output with its observation age. Unavailable evidence remains explicitly unknown; place recovery progress and results beside the recovery action with polite status announcements.

## Do's and Don'ts

### Do:

- **Do** preserve one interaction model across light and dark themes.
- **Do** use transmitter slate for shell identity and status chrome.
- **Do** use Source Sans 3 for reading and Barlow Semi Condensed for scan hierarchy.
- **Do** keep terminology direct and system state easy to scan.
- **Do** keep keyboard focus visible and loading state available to assistive technology.
- **Do** wrap long values safely or put wide tables in an explicit scroller.
- **Do** use borders and neutral mixes before adding interior shadows.

### Don't:

- **Don't** make the interface flashy, ornamental, or marketing-like.
- **Don't** replace precise workflows with vague abstractions.
- **Don't** use semantic colors for decoration.
- **Don't** replace the slate shell with saturated action blue.
- **Don't** create nested-card grids for dense operational screens.
- **Don't** use gradient text, decorative glassmorphism, colored side stripes, or novelty geometry.
- **Don't** compress technical tables until content wraps character by character.
- **Don't** use modal dialogs as the first answer for normal configuration; prefer inline progressive controls.
- **Don't** modify `data/view_diag_logs.php` through automated design tooling; that page is explicitly excluded.
