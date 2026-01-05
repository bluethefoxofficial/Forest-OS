# CPU Feature Detection

Forest OS now keeps track of a richer set of CPU attributes during boot.
This document explains what the kernel looks for, how the new frequency
calibration works, and how we detect 64‑bit capability so that future
long‑mode builds have the metadata they need.

## 1. Measuring CPU Speed

There are two complementary sources of clock information:

1. **CPUID leaf `0x16` (if present)** – modern CPUs expose their base and
   maximum clock in MHz. Forest OS records these numbers directly.
2. **TSC + PIT calibration** – on every boot we program PIT channel 2
   (`mode 0`, divisor `0xFFFF`) and measure how many Time Stamp Counter
   ticks elapsed while the PIT counts down. Because the PIT frequency is
   1.193182 MHz, the kernel can compute the TSC frequency with the form
   `hz = (delta_tsc * PIT_FREQ) / divisor`. We take two measurements and
   keep the larger value to reduce jitter from SMI/NMI activity.

If the processor lacks a TSC the kernel falls back to the CPUID‑reported
MHz value. The result is cached in `cpuid_info_t.measured_frequency_hz`
and exposed via the Direct Kernel Shell (`cpuid` command) so it can be
displayed alongside the vendor/brand strings.

The implementation follows the general approach described in the OSDev
“Detecting CPU Speed” article: we wait for a deterministic hardware
interval, count cycles with `RDTSC`, then derive MHz/GHz from that delta.

## 2. Detecting x86‑64 Support

Forest OS still ships as a 32‑bit kernel today, but the hardware layer
now records whether the processor can enter long mode so that we can
gate future AMD64 builds appropriately. Detection follows the canonical
CPUID flow:

1. Confirm that CPUID is supported by toggling the `EFLAGS.ID` bit.
2. Query the maximum extended CPUID leaf (`0x80000000`).
3. If `0x80000001` is available, inspect `EDX[29]` (“LM”) – when this bit
   is set the CPU implements AMD64/EM64T long mode.

The result is cached in `cpuid_info_t.long_mode_supported` and shown by
the DKS `cpuid` command. This mirrors the procedure outlined in the
“Setting Up Long Mode” guide: once CPUID says long mode exists the OS can
set up PAE paging (PML4/PDPT/PDT/PT), enable `CR4.PAE`, set `MSR_EFER.LME`
and switch into 64‑bit mode.

For a more detailed walkthrough of the paging structures, SIPI sequence,
and UEFI considerations see `docs/MULTICORE_UEFI.md`.
