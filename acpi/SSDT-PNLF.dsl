/*
 * Huawei MCLG-XX: PNLF backlight device for macOS (AppleBacklightDisplay -> brightness slider).
 *
 * The DSDT has no PNLF device; "PNLF" there is a 16-bit NVS field at root scope (\PNLF, DSDT.dsl:7071, read at
 * :19994). So no PNLF -> XNLF rename (a byte-level replace that would corrupt that field), and the device lives under
 * the iGPU, \_SB.PC00.GFX0.PNLF, not at a path that collides with \PNLF - same as the X1C G11 port (X1C-G11-PORT.md c12).
 * _UID 19 = Coffee Lake and newer. Brightness itself goes through Apple's framebuffer; NootedGreen's XE-BLR rescales
 * the duty cycle to this panel's own PWM period.
 *
 * Compile: iasl SSDT-PNLF.dsl  ->  SSDT-PNLF.aml
 */
DefinitionBlock ("", "SSDT", 2, "HACK", "PNLF", 0x00000000)
{
    External (_SB_.PC00.GFX0, DeviceObj)

    Scope (_SB.PC00.GFX0)
    {
        Device (PNLF)
        {
            Name (_HID, EisaId ("APP0002"))
            Name (_CID, "backlight")
            Name (_UID, 0x13)

            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin"))
                {
                    Return (0x0B)
                }

                Return (Zero)
            }
        }
    }
}
