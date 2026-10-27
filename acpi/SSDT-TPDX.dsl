/*
 * Huawei MCLG-XX (MateBook, RPL-P): expose the I2C touchpad to VoodooI2C.
 *
 * The DSDT declares the touchpad as \_SB.PC00.TPD0, directly under PC00, not under the I2C controller it is wired
 * to. VoodooI2C only enumerates ACPI children of the controller it attaches to, so it never finds the touchpad.
 * This adds the same device under \_SB.PC00.I2C0, for Darwin only. The original TPD0 stays as it is (its _INI still
 * runs; Windows keeps using it).
 *
 * Values from DSDT TPD0 (TPDT == 0x0C branch): _HID "BLTP7840", _CID "PNP0C50", I2CZ = 0 (I2C0), BADR = 0x2C,
 * SPED = 400 kHz, HID2 (HID descriptor register) = 1.
 *
 * v4: bus speed 100 kHz (standard mode) instead of the firmware's 400 kHz: at 400 kHz VoodooI2C's polling reads
 * kept aborting with "lost arbitration" (TX_ABRT_SOURCE bit 12) and the touchpad stopped responding.
 *
 * Interrupt (v3, APIC): on GPIO pin 173 (pad 0x0908000D = GPP_D13) no interrupt reached VoodooGPIO. Use the firmware's
 * other mode instead (TPDM == 1): route the pad to the IOxAPIC exactly as TPD0._INI does for TPDM == 1 (SGRA 1,
 * SGII 0, GRXE 0) and return TPD0.SBFI, whose IRQ TPD0._INI already set from INUM(pad). The routing is done in _CRS,
 * not _INI: TPDX._INI would run before TPD0._INI, which configures the pad for GPIO-driver mode when TPDM == 0.
 * With -vi2c-force-polling, VoodooI2C ignores the interrupt and polls instead.
 *
 * Compile: iasl SSDT-TPDX.dsl  ->  SSDT-TPDX.aml
 */
DefinitionBlock ("", "SSDT", 2, "HACK", "TPDX", 0x00000004)
{
    External (_SB_.PC00.I2C0, DeviceObj)
    External (_SB_.PC00.TPD0.SBFI, BuffObj)
    External (SGRA, MethodObj)    // 2 Arguments: GPIO pad, route to IOxAPIC
    External (SGII, MethodObj)    // 2 Arguments: GPIO pad, RX invert
    External (GRXE, MethodObj)    // 2 Arguments: GPIO pad, RX event config

    Scope (_SB.PC00.I2C0)
    {
        Device (TPDX)
        {
            Name (_HID, "BLTP7840")
            Name (_CID, "PNP0C50" /* HID Protocol Device (I2C bus) */)
            Name (_UID, One)
            Name (_S0W, 0x03)

            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0F)
                }

                Return (Zero)
            }

            Method (_CRS, 0, Serialized)
            {
                SGRA (0x0908000D, One)
                SGII (0x0908000D, Zero)
                GRXE (0x0908000D, Zero)
                Name (SBFB, ResourceTemplate ()
                {
                    I2cSerialBusV2 (0x002C, ControllerInitiated, 0x000186A0,
                        AddressingMode7Bit, "\\_SB.PC00.I2C0",
                        0x00, ResourceConsumer, , Exclusive,
                        )
                })
                Return (ConcatenateResTemplate (SBFB, \_SB.PC00.TPD0.SBFI))
            }

            // HID-over-I2C _DSM: function 0 = supported functions, function 1 = HID descriptor register (HID2 = 1)
            Method (_DSM, 4, Serialized)
            {
                If ((Arg0 == ToUUID ("3cdff6f7-4267-4555-ad05-b30a3d8938de")))
                {
                    If ((Arg2 == Zero))
                    {
                        If ((Arg1 == One))
                        {
                            Return (Buffer (One) { 0x03 })
                        }

                        Return (Buffer (One) { 0x00 })
                    }

                    If ((Arg2 == One))
                    {
                        Return (One)
                    }
                }

                Return (Buffer (One) { 0x00 })
            }
        }
    }
}
