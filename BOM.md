# Axworthy Ghost — Block 2 Bill of Materials

Parts list for building one **Block 2** ghost pulley (Seeeduino XIAO SAMD21 version).

**How this build works:** you buy the **bare PCB from the author** (a blank,
solder-it-yourself board — no parts included), then buy the components in this
list separately and solder them onto the board. The Gerber/fabrication files are
not published; the bare board is a product sold by the author.

- **Qty/Build** = how many of that part the finished unit uses.
- **Qty/Pk** = how many come in the pack you buy (buy enough packs to cover Qty/Build).
- Prices are estimates — check current vendor pricing.
- Links marked `search:` are common commodity parts — buy from any vendor.

> This BOM (documentation) is licensed CC BY 4.0. See LICENSE for full terms.

**Build docs:** [Assembly Guide](Axworthy%20Assembly%20Guide%20R1.pdf) ·
[User Guide](Axworthy%20User%20Guide%20R0.pdf) — step-by-step assembly (with
schematic and interconnect diagrams) and operating instructions.

**Firmware:** [`firmware/ghost_pulley_xiao`](firmware/ghost_pulley_xiao) — the
Arduino sketch (MIT). Open in the Arduino IDE with the "Seeed SAMD" board
package and the libraries listed in the sketch header.

---

## 3D-Printed Parts (not purchased)

The gears, drive pulley, and enclosure are **3D printed** — they are not on this
purchasing list. Download the print models from MakerWorld, and see the full 3D
assembly (how everything fits together) on Onshape:

- **Print models:** MakerWorld — *(link TBD)*
- **3D assembly (view-only):** Onshape — *(link TBD)*

Print the enclosure in a UV/weather-tolerant filament (ASA or PETG) for outdoor use.

---

## Electronics — Core

| Part | Purpose | Qty/Build | Qty/Pk | Link | Notes |
|---|---|---|---|---|---|
| Seeeduino XIAO SAMD21 | Microcontroller | 1 | 1 | [Amazon (Seeed Studio)](https://a.co/d/06WSjVxy) | Chip: ATSAMD21G18A. Use the "Seeed SAMD" board package. |
| A4988 stepper driver module | STEP/DIR motor driver | 1 | 3 | [Amazon (WWZMDiB)](https://a.co/d/0cpniapO) | 1/8 microstepping. Set Vref ~0.4-0.5V for this motor. |
| DS3231 RTC module | Scheduling clock | 1 | 1 | [Amazon (Dorhea)](https://a.co/d/0i0HqPRa) | I2C addr 0x68. Has onboard I2C pull-ups. |
| CR2032 battery | RTC backup | 1 | 2 | [Amazon (Energizer)](https://a.co/d/0bv8MjWc) | Fits the DS3231 module holder. |
| SSD1309 2.42" OLED (I2C) | Display | 1 | 1 | [Amazon (HiLetgo)](https://a.co/d/05t35Wxv) | 128x64, I2C addr 0x3C. Has onboard I2C pull-ups. Power at 3.3V. |
| EC11 rotary encoder w/ switch | Menu input | 1 | 10 | [Amazon (DIYhz)](https://a.co/d/0eueh0Od) | CLK/DT/SW + common. |
| Micro limit switch (roller lever) | Homing / endstop | 1 | 1 | [Amazon (HiLetgo)](https://www.amazon.com/dp/B07X142VGC) | Wire NC + COM. |

## Electronics — Power

| Part | Purpose | Qty/Build | Qty/Pk | Link | Notes |
|---|---|---|---|---|---|
| TSR 1-2450 (Traco) | 12V -> 5V buck, SIP3 | 1 | 2 | [Amazon (AOTUPASION)](https://a.co/d/0bQRsoD2) | Drop-in, no support parts. Feeds XIAO 5V pad. |
| NOYITO AC-DC 120V->12V 2A | System power | 1 | 1 | [Amazon (NOYITO)](https://a.co/d/0e7NLVbR) | Isolated AC-DC module (12V 2A). OR use a 12V barrel-jack adapter. |
| DC-005 barrel jack (5.5x2.1mm) | Optional 12V input | 1 | 50 | [Amazon (Clyxgs)](https://a.co/d/0fidOFS1) | Only if using the barrel-jack power option. Center-positive. |
| 47uF 50V electrolytic cap | A4988 VMOT bulk cap | 1 | 50 | [Amazon (Innfeeltech)](https://a.co/d/05nts4Yw) | Across VMOT/GND — back-EMF protection. |

## Electronics — PCB passives & switches

| Part | Purpose | Qty/Build | Qty/Pk | Link | Notes |
|---|---|---|---|---|---|
| DIP switch, 3-position (2.54mm) | Microstep select (MS1/2/3) | 1 | 5 | [Amazon (uxcell)](https://a.co/d/0bJygiCu) | Set ON/ON/OFF = 1/8 microstepping. |
| 10k ohm resistor | Pull-downs (MS1/2/3) + ENABLE pull-up | 4 | 100 | [Amazon (EDGELEC)](https://a.co/d/09GDdvaT) | 3x MS pull-down, 1x ENABLE pull-up to 3V3. |
| Terminal block, 3-pin, 5mm (PCB mount) | Drive + Voltage connectors | 4 | 20 | [Amazon (Tegg)](https://a.co/d/0hnKyZVu) | 250V 8A. Four 3-pin blocks (motor + limit + power + AUX). |
| Female pin headers, 2.54mm | Module mounting (XIAO, OLED, DS3231) | 2 | 40 | [Amazon (Dahszhi)](https://a.co/d/02sVTZys) | Socket the modules so they're removable. |

## Bare PCB (sold by the author)

| Part | Purpose | Qty/Build | Qty/Pk | Where to get | Notes |
|---|---|---|---|---|---|
| Axworthy Block 2 bare PCB | Mainboard | 1 | 1 | [Tindie (author's shop)](https://www.tindie.com/products/44123/) | Solder-it-yourself blank board; **no components included**. Gerber/fab files are not published. |

## Motor & Mechanical

| Part | Purpose | Qty/Build | Qty/Pk | Link | Notes |
|---|---|---|---|---|---|
| Joyfy hanging ghost (47 LED, light-up) | The ghost itself | 1 | 1 | [Target (Joyfy)](https://www.target.com/p/joyfy-halloween-hanging-ghost-outdoor-decoration-47-led-light-hanging-ghost-halloween-hanging-decoration-for-indoor-outdoor-party-decor/-/A-1005691718) | The prop that rides the line. Any lightweight ghost works; this is the one used in the build. |
| NEMA 17 stepper (1.5A, ~42Ncm) | Drive motor | 1 | 3 | [Amazon (STEPPERONLINE)](https://www.amazon.com/dp/B00PNEQKC0) | 200 steps/rev, bipolar. |
| 608 2RS ball bearing (8x22x7mm) | Pulley shaft support | 5 | 20 | [Amazon](https://a.co/d/0aapJzzu) | Standard skate bearing. |
| Dowel pin, 8x60mm, stainless | Pulley/shaft | 2 | 5 | [Amazon (uxcell)](https://a.co/d/0h0HNcBo) | Match your gear/pulley bore. |

## Fasteners & Heat-Set Inserts

Hardware for assembling the 3D-printed parts. Heat-set inserts are pressed into
the printed plastic; the machine screws thread into them. Insert part numbers are
McMaster-Carr.

| Part | Qty/Build | McMaster PN | Notes |
|---|---|---|---|
| Hex socket head cap screw, M3x0.50, 6mm | 27 | — | into M3 inserts (incl. 1 self-tapped for limit lever) |
| Hex socket head cap screw, M2x0.40, 6mm | 4 | — | into M2 inserts (pinion + clock) |
| Hex socket head cap screw, M2x0.40, 10mm | 6 | — | limit switch (2) + OLED (4) |
| Hex socket head cap screw, M2x0.40, 4mm | 4 | — | into M2 inserts |
| Hex socket head cap screw, M4x0.70, 30mm | 4 | — | into M4 inserts (lid) |
| M2 washer | 8 | — | back the OLED screws so they don't protrude (use 4-8 as needed) |
| Hex head bolt, M12x1.75, 55mm | 2 | — | pulley/output shaft |
| Hex nut, M12x1.75 (grade A&B) | 2 | — | pulley/output shaft |
| Heat-set insert, M3, tapered brass | 28 | 94180A331 | for M3 screws |
| Heat-set insert, M4, tapered brass | 4 | 94180A351 | for M4 screws |
| Heat-set insert, M2, tapered brass | 10 | 94180A307 | for M2 screws |
| Wood screw, #8 x 2" | 6 | — | mounting the unit to wood/structure |

## Wiring & Connectors (motor <-> enclosure)

| Part | Purpose | Qty/Build | Qty/Pk | Link | Notes |
|---|---|---|---|---|---|
| 22/6 stranded cable (tinned) | Motor + limit run | as needed | 1 | [Amazon (RESHAKE)](https://www.amazon.com/RESHAKE-Electrical-Thermostat-Irrigation-Automotive/dp/B0C7MLSV8F) | 4 coil + 2 limit conductors. Keep exposed length out of sun or sleeve it. |
| WEIPU SP13 6-pin kit (SP1312 rear-nut) | Enclosure/drive disconnect + cable entry | 2 | 1 | [Amazon (ZBLZGP WEIPU)](https://www.amazon.com/dp/B0CB8K5WQJ) | IP68. The panel-mount SP13 IS the sealed cable entry at both the enclosure and drive unit — no separate gland needed. |

## Enclosure / Misc

| Part | Purpose | Qty/Build | Qty/Pk | Link | Notes |
|---|---|---|---|---|---|
| USB-C panel-mount cable (flush/dash mount) | External flashing port | 1 | 1 | [Amazon (CERRXIAN USB-C)](https://a.co/d/0aMCXJ4x) | For reflashing without opening the box. 30cm, USB-C 2.0. Size the 3D-printed enclosure hole to this part's opening. Not IP-rated — shelter or cap it. |
| USB-C 90° adapter (male to female) | Right-angle USB-C for XIAO | 1 | 1 | [Amazon (AGVEE)](https://a.co/d/00o2YN2m) | USB-C 3.2 Gen 2, 10G. Angles the flashing cable so it clears inside the enclosure. |
| AC power cord (18/3 SJT, NEMA 5-15) | Wall power (if AC-DC module) | 1 | 1 | [Amazon (Pinfox)](https://a.co/d/0bUXQiGp) | Switch must break LINE (hot). |
| Mini rocker switch, SPST on/off (KCD11, 10x15mm) | Power switch | 1 | 10 | [Amazon (VEXUNGA)](https://a.co/d/01NQFCxt) | 3A 250V / 6A 125V. Wired to break the AC LINE (hot). |
| Kevlar line (high tensile) | Ghost travel line | as needed | 1 | [Amazon (9KM DWLIFE)](https://a.co/d/0h1WJ8JD) | High-strength braided Kevlar (50-1500lb). Shared consumable. |
| Turnbuckle | Tension the ghost line | 1 | 1 | [McMaster 3003T14](https://www.mcmaster.com/3003T14/) | Takes up slack / tensions the travel line. |
| Acrylic sheet, ~3x3" | Screen guard window | 1 | 1 | — | Glued into the printed screen guard over the OLED. Remove protective film first. |
| Super glue (cyanoacrylate) | Screen guard bonding | as needed | 1 | — | Bonds the acrylic + screen guard; seal the groove water-tight. |
| Cable clamp | AC cord strain relief | 1 | 1 | — | Clamps the AC cord inside the box (AC power option). Fastened with M3x6mm. |

---

## Removed from Block 1 (do NOT need for Block 2)
- Fan, 2N7000 MOSFET, 1N4001 flyback diode, fan pull-down resistor — **fan circuit deleted**.
- MS1/MS2/MS3 as GPIO — now set by the DIP switch instead.
- Arduino MKR Zero — replaced by the XIAO SAMD21.

## Notes for builders
- **Microstepping:** set the DIP switch to ON/ON/OFF (1/8). The firmware's
  339.4 steps/inch assumes 1/8. Other modes need a firmware change.
- **I2C pull-ups:** the OLED and DS3231 modules provide them — do NOT add more
  on the PCB.
- **A4988 Vref:** set low (~0.4-0.5V) for this motor to avoid overheating.
- **Power the XIAO from its 5V pad** (the TSR 1-2450 output). Its onboard
  regulator makes 3.3V for the rest of the logic. Never feed 5V to the 3V3 pad.
- Confirm the OLED header pin order matches your specific module before power-up.
