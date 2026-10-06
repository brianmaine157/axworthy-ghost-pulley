# Axworthy Ghost Pulley

A DIY, Arduino-based Halloween prop that glides a ghost back and forth along a
line across your yard — on its own schedule, with no computer attached. A
stepper motor drives a pulley that sweeps a lightweight ghost between two points
you set, pausing and reversing all night long.

Everything you need to build one is here and free: schematic, firmware, bill of
materials, and a full step-by-step assembly guide. The 3D-printable parts are on
MakerWorld, and the one custom circuit board is available as a ready-to-solder
bare PCB.

- **3D models (print files):** [MakerWorld](https://makerworld.com/en/models/3396885-axworthy-ghost-pulley#profileId-3866871)
- **Controller PCB (bare board):** [Tindie](https://www.tindie.com/products/44123/)

---

## What it does

- **Two motion modes** — a steady back-and-forth sweep, or a randomized
  "wander" that drifts to different spots and pauses, so it never looks the same
  twice.
- **Built-in scheduling** — a real-time clock starts and stops the haunt on the
  hours you set, hands-free.
- **Standalone menu** — set it all up from an on-board OLED screen and a knob;
  no laptop needed after flashing.
- **Learn mode** — teach it the two endpoints of travel by hand, no measuring.
- **Adjustable speed and acceleration**, limit-switch homing, and an emergency
  stop.

---

## What's in this repo

| File / folder | What it is |
|---|---|
| [`Axworthy Assembly Guide R1.pdf`](Axworthy%20Assembly%20Guide%20R1.pdf) | Full step-by-step build guide, including the schematic and interconnect (wiring) diagrams. |
| [`Axworthy User Guide R0.pdf`](Axworthy%20User%20Guide%20R0.pdf) | How to operate the finished prop (menus, modes, scheduling). |
| [`BOM.md`](BOM.md) / [`BOM.csv`](BOM.csv) | Bill of materials — every part with purchase links. |
| [`firmware/`](firmware) | The Arduino firmware (MIT licensed). |
| [`LICENSE`](LICENSE) | Full licensing terms. |

---

## How to build one

1. **Print the parts** from [MakerWorld](https://makerworld.com/en/models/3396885-axworthy-ghost-pulley#profileId-3866871).
   Use ASA or PETG for the enclosure if it'll live outdoors.
2. **Get the controller board** — order the bare PCB from
   [Tindie](https://www.tindie.com/products/44123/) (solder-it-yourself; no parts
   included) and buy the components from the [BOM](BOM.md).
3. **Assemble** by following the [Assembly Guide](Axworthy%20Assembly%20Guide%20R1.pdf).
4. **Flash the firmware** in the Arduino IDE:
   - Install the **Seeed SAMD Boards** package and select **Seeeduino XIAO**.
   - Install the libraries listed at the top of the sketch
     ([`firmware/ghost_pulley_xiao`](firmware/ghost_pulley_xiao)).
   - Upload, then set everything up from the on-board menu (see the
     [User Guide](Axworthy%20User%20Guide%20R0.pdf)).

The electronics are all plug-in modules and through-hole soldering — no fine
surface-mount work — so the build is approachable even if you're newer to
electronics.

---

## Licensing

This project shares almost everything you need to build an Axworthy Ghost
Pulley, but the **bare PCB is a product sold by the author**. Different parts
have different terms:

| What | License | You may... |
|---|---|---|
| **Firmware** | [MIT](https://opensource.org/license/mit) | use, modify, redistribute freely |
| **Docs / BOM / build guide / wiring** | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | share & adapt (even commercially), with credit |
| **Schematic** | Reference only, all rights reserved | read, build, repair, modify your own board — **not** manufacture boards for sale |
| **PCB Gerber / fab files** | Not published | *(the bare PCB is sold by the author)* |
| **3D print files (on MakerWorld)** | Non-commercial (MakerWorld terms) | print and remix for personal use — not for resale |

> This is **not** an "open source hardware" project. It's open firmware + open
> documentation, with a commercial bare-PCB product. The full schematic is
> provided so you can build, understand, and repair the board — but the
> manufacturing files are not shared.

**Name & logo:** *"Axworthy Ghost"* and the ghost logo are reserved by the
author. Please use your own branding for any modified/derivative work.

See [`LICENSE`](LICENSE) for the full terms.
