-- ============================================================
--  BleachImport.mcr - пункты меню "Bleach Tool"
--  Открывают окна из BleachRoSImport.dlu через интерфейс BleachCpp.
-- ============================================================

macroScript BleachImport_Open
    category:"Bleach Tool"
    buttonText:"Import BLEACH RoS"
    toolTip:"BLEACH Rebirth of Souls - импорт модели (меш, кости, текстуры, оружие, LOD)"
(
    on execute do
    (
        local sExpected = "1.0.0"
        local o = undefined
        try (if (BleachCpp != undefined) do o = BleachCpp) catch (o = undefined)
        if (o == undefined) do
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*BleachCpp*" do o = i) catch ()
        if (o != undefined) then
        (
            local v = undefined
            try (v = o.version()) catch ()
            if (v != undefined and v != sExpected) do
                format "Bleach Tool: скрипт % а плагин % - переустанови (INSTALL.bat).\n" sExpected v
            o.showDialog()
        )
        else
        (
            local sLog = (systemTools.getEnvVariable "LOCALAPPDATA") + "\\BleachRoSImport\\start.log"
            messageBox ("BleachRoSImport.dlu " + sExpected + " не загружен.\n\nПроверь установку (INSTALL.bat) и Max.log.\nstart.log плагина: " + (if (doesFileExist sLog) then "есть" else "нет")) title:"Bleach Tool"
        )
    )
)

macroScript BleachImport_Anim
    category:"Bleach Tool"
    buttonText:"BLEACH Animations"
    toolTip:"BLEACH Rebirth of Souls - анимации из .tactpkg на скелет"
(
    on execute do
    (
        local o = undefined
        try (if (BleachCpp != undefined) do o = BleachCpp) catch (o = undefined)
        if (o == undefined) do
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*BleachCpp*" do o = i) catch ()
        if (o != undefined) then o.showAnimDialog()
        else format "Bleach Tool: BleachCpp недоступен - запусти \"Import BLEACH RoS\" для диагностики.\n"
    )
)
