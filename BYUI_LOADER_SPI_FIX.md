# Agent task: lower the BYUI loader's display SPI clock to 10 MHz

> Self-contained brief for an agent working in
> `/Users/lynn/Documents/Repositories/BYUI-Namebadge4-OTA`. No other
> context needed.

## Task

Lower the ILI9341 SPI clock from 40 MHz to 10 MHz in the BYUI eBadge V4
loader's display driver, and add an in-source comment explaining why.

## Repo

`/Users/lynn/Documents/Repositories/BYUI-Namebadge4-OTA` — don't touch
any sibling repos; this is a self-contained one-line change + doc + rebuild +
flash.

## Symptom that motivates this change

The panel's MADCTL register sometimes fails to latch the loader's value
(`0x60` — landscape, MX=1, MV=1) when the chip's prior state was anything
other than fresh power-on. When that happens the panel runs with its
default `MADCTL=0x00`, which renders everything rotated 90° CW and
horizontally mirrored relative to the intended landscape orientation.

Signature: looks fine on a clean power-on; broken if the loader re-enters
after a student OTA app has been running.

## Root cause (verified empirically in the sister app `namebadge-friends-around-me`)

With `DISP_SPI_FREQ = SPI_MASTER_FREQ_40M`, the very first MADCTL byte
sent after the panel has already been driven at speed has marginal
setup-time on this board's flex cable. The SPI controller reports no
error (`idf monitor` logs "Display ready"), the RST pad measurably goes
LOW then HIGH (verified with `INPUT_OUTPUT` mode + `gpio_get_level`
readback), and `ili9341_init_regs()` runs to completion — but the chip
silently drops the `0x36 / 0x60` write and stays at `MADCTL=0x00`.

Lowering the clock to 10 MHz adds enough setup margin that both
cold-boot and post-OTA hand-off paths latch the command reliably.
Confirmed by flashing the friends-around-me app at both clocks across
both install paths (USB Single-Program-Flash and OTA via this loader).

## The change

**File:** `display/display.c`

Find:

```c
#define DISP_SPI_FREQ  SPI_MASTER_FREQ_40M
```

Replace with:

```c
#define DISP_SPI_FREQ  SPI_MASTER_FREQ_10M  /* see comment block above */
```

**Also** add this block comment to the file's top header docstring (after
the existing "Pin assignments" paragraph):

```c
/*
 * SPI CLOCK — must stay ≤ 10 MHz.
 * At 40 MHz the panel works fine on a clean power-on, but when the
 * loader is re-entered after a student OTA app has been running, the
 * very first MADCTL write (cmd 0x36 / value 0x60) silently fails to
 * latch and the chip stays at default MADCTL=0x00 — which renders
 * everything rotated 90° CW + horizontally mirrored relative to the
 * intended landscape orientation. ili9341_init_regs() completes
 * without SPI error, RST is observed to drive low/high correctly, but
 * the panel doesn't apply the byte. Dropping to 10 MHz gives enough
 * setup margin that both paths latch reliably. Do NOT raise this
 * without testing both:
 *   1. Cold boot (power-on or hardware reset) into the loader.
 *   2. BOOT-pressed re-entry into the loader after a student OTA
 *      app has been running for a while (the failure mode).
 */
```

## Verification steps

1. Build:

   ```bash
   . ~/esp/esp-idf/export.sh
   idf.py -C /Users/lynn/Documents/Repositories/BYUI-Namebadge4-OTA build
   ```

2. Flash to the badge:

   ```bash
   idf.py -C /Users/lynn/Documents/Repositories/BYUI-Namebadge4-OTA \
          -p /dev/cu.usbserial-110 flash
   ```

3. Power-cycle and confirm the loader's splash + menu render in the
   correct landscape orientation (FPC connector on the left, text reads
   upright).

4. From the loader menu, install a student OTA app, let it run for a few
   seconds, then reset with BOOT held to re-enter the loader. Confirm the
   loader's display still renders correctly.

5. *(Optional)* If a publish workflow exists for this loader (look for
   `tools/publish_bootloader.sh`), bump `LOADER_SW_VERSION` in
   `loader_menu/include/loader_menu.h` and run the publish script so the
   updated bootloader rolls out to other badges via the loader's own
   self-update.

## Constraints

- Do not modify any other repository.
- Do not touch any source file other than `display/display.c` (and,
  optionally for the publish step, `loader_menu/include/loader_menu.h`).
- Do not change pin assignments, the init-register sequence, or the
  MADCTL value — only the SPI clock define and its comment.
- The 10 MHz setting is intentionally conservative; do not "optimise" it
  to 20 MHz without re-running both verification scenarios above.
