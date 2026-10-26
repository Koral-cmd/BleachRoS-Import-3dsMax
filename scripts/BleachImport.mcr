-- ============================================================
--  BleachImport.mcr - пункты меню "Bleach Tool"
--  Открывают окна из BleachRoSImport.dlu через интерфейс BleachCpp.
-- ============================================================

macroScript BleachImport_Open
    category:"Bleach Tool"
    buttonText:"Import BLEACH RoS"
    toolTip:"BLEACH Rebirth of Souls - model import (mesh, bones, textures, weapon, LOD)"
(
    on execute do
    (
        local sExpected = "1.4.2"
        local o = undefined
        try (if (BleachCpp != undefined) do o = BleachCpp) catch (o = undefined)
        if (o == undefined) do
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*BleachCpp*" do o = i) catch ()
        if (o != undefined) then
        (
            local v = undefined
            try (v = o.version()) catch ()
            if (v != undefined and v != sExpected) do
                format "Bleach Tool: script % but plugin % - reinstall (INSTALL.bat).\n" sExpected v
            o.showDialog()
        )
        else
        (
            local sLog = (systemTools.getEnvVariable "LOCALAPPDATA") + "\\BleachRoSImport\\start.log"
            messageBox ("BleachRoSImport.dlu " + sExpected + " is not loaded.\n\nCheck the installation (INSTALL.bat) and Max.log.\nPlugin start.log: " + (if (doesFileExist sLog) then "present" else "missing")) title:"Bleach Tool"
        )
    )
)

macroScript BleachImport_Anim
    category:"Bleach Tool"
    buttonText:"BLEACH Animations"
    toolTip:"BLEACH Rebirth of Souls - animations from .tactpkg onto the skeleton"
(
    on execute do
    (
        local o = undefined
        try (if (BleachCpp != undefined) do o = BleachCpp) catch (o = undefined)
        if (o == undefined) do
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*BleachCpp*" do o = i) catch ()
        if (o != undefined) then o.showAnimDialog()
        else format "Bleach Tool: BleachCpp is not available - run \"Import BLEACH RoS\" for diagnostics.\n"
    )
)
