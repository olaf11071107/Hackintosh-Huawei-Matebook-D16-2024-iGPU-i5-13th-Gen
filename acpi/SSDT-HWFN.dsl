/*
 * Huawei MCLG-XX: Fn keys for macOS.
 *
 * The EC (\_SB.PC00.LPCB.HWEC) reports Fn keys as EC queries. Brightness (Fn+F1/F2, _Q0A/_Q0B) goes to the
 * display only once Windows enabled ACPI brightness (DSEN bit 2), otherwise through the IGD OpRegion; mute and
 * volume (Fn+F4/F5/F6, _Q0D/_Q0E/_Q0F) are WMI events (WMEN = 0x284/0x285/0x286 + Notify WMI1 0xA0) that only
 * Huawei's Windows software reads. Neither reaches macOS.
 *
 * On Darwin these queries are turned into PS/2 key presses for VoodooPS2Keyboard instead
 * (Notify value 0x04xx = e0 xx): e005/e006 brightness down/up, e020 mute, e02e/e030 volume down/up.
 * Other systems run the original methods, renamed by OpenCore ACPI patches (DSDT only): _Q0A->XQ0A, _Q0B->XQ0B,
 * _Q0D->XQ0D, _Q0E->XQ0E, _Q0F->XQ0F.
 *
 * Compile: iasl SSDT-HWFN.dsl  ->  SSDT-HWFN.aml
 */
DefinitionBlock ("", "SSDT", 2, "HACK", "HWFN", 0x00000001)
{
    External (_SB_.PC00.LPCB.HWEC, DeviceObj)
    External (_SB_.PC00.LPCB.PS2K, DeviceObj)
    External (_SB_.PC00.LPCB.HWEC.XQ0A, MethodObj)
    External (_SB_.PC00.LPCB.HWEC.XQ0B, MethodObj)
    External (_SB_.PC00.LPCB.HWEC.XQ0D, MethodObj)
    External (_SB_.PC00.LPCB.HWEC.XQ0E, MethodObj)
    External (_SB_.PC00.LPCB.HWEC.XQ0F, MethodObj)

    Scope (_SB.PC00.LPCB.HWEC)
    {
        // Send a key to VoodooPS2Keyboard
        Method (FNKY, 1, NotSerialized)
        {
            If (CondRefOf (\_SB.PC00.LPCB.PS2K))
            {
                Notify (\_SB.PC00.LPCB.PS2K, Arg0)
            }
        }

        Method (_Q0A, 0, NotSerialized)  // Fn+F1: brightness down
        {
            If (_OSI ("Darwin"))
            {
                FNKY (0x0405)
            }
            Else
            {
                XQ0A ()
            }
        }

        Method (_Q0B, 0, NotSerialized)  // Fn+F2: brightness up
        {
            If (_OSI ("Darwin"))
            {
                FNKY (0x0406)
            }
            Else
            {
                XQ0B ()
            }
        }

        Method (_Q0D, 0, NotSerialized)  // Fn+F4: mute
        {
            If (_OSI ("Darwin"))
            {
                FNKY (0x0420)
            }
            Else
            {
                XQ0D ()
            }
        }

        Method (_Q0E, 0, NotSerialized)  // Fn+F5: volume down
        {
            If (_OSI ("Darwin"))
            {
                FNKY (0x042E)
            }
            Else
            {
                XQ0E ()
            }
        }

        Method (_Q0F, 0, NotSerialized)  // Fn+F6: volume up
        {
            If (_OSI ("Darwin"))
            {
                FNKY (0x0430)
            }
            Else
            {
                XQ0F ()
            }
        }
    }
}
