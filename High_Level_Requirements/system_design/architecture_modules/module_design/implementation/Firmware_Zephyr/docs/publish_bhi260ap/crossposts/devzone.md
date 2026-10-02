# Nordic DevZone post (paste as new topic)

**Board/category:** nRF Connect SDK / Zephyr · **Labels:** Zephyr, SPI, devicetree

**Title:** BHI260AP (Nicla Sense ME) on Zephyr: SPI reads all-zeros — silent CS skip for nodes without a binding

---

Spent a full day on this and the failure mode produces zero diagnostics, so leaving it here for the next person.

**Symptom:** every BHY2 register read returns `0x00`, `bhy2_soft_reset` / `bhy2_boot_from_flash` time out. The MX25R flash on the *same* SPI bus works perfectly. An Arduino (mbed) image on the same board boots the hub instantly — same peripheral (SPIM2), same pins (P0.03/04/05), same CS (P0.31).

**Root cause:** `compatible = "bosch,bhi260ap"` has no binding in Zephyr (no in-tree driver/yaml). An unbound node gets no spi-bus resolution in `devicetree_generated.h` → `SPI_CS_GPIOS_DT_SPEC_GET(node)` expands to an **empty spec** → the SPI driver **silently skips chip-select**. The device is never selected. No build warning, no runtime log.

Second trap if you then switch to a manual GPIO CS: with `.dt_flags = GPIO_ACTIVE_LOW`, `gpio_pin_set_dt(&cs, 0)` is *logical inactive* = physical HIGH — your "assert" is a release. Use plain polarity and drive 0/1 directly.

Also: BHY2 register `0x90` reads `0x00` even on a healthy running hub (verified through the working mbed stack), and `bhy2_soft_reset` timing out is routine — the vendor's reference ignores its return code. Real aliveness metric: `boot_status` `0x11` → `0x13`.

**Working recipe:** manual plain-polarity GPIO CS around each `spi_transceive`, single scratch buffer covering address+data with manual byte-0 discard, ≤8 MHz mode 0, reset pin (P0.18) held high. Hub now boots from flash on Zephyr 4.4:

```
bhy2: product_id -> 0, id 137        (0x89 = BHI260AP)
bhy2: soft_reset -> 0
bhy2: boot_status -> 0x11 → 0x13     (hub running)
```

Full case study + code: https://github.com/skixer2/bhi260ap-zephyr-bringup
Upstream issue: https://github.com/zephyrproject-rtos/zephyr/issues/121051

— Jean Paul Voyat (investigation executed with OpenClaw running GLM 5.3, Z.ai)
