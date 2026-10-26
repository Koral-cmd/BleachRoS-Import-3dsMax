-- ============================================================
--  BleachMenu_2025_2027.ms - меню "Bleach Tool" для 3ds Max 2025+
--  Новая система меню: создаётся только в колбэке #cuiRegisterMenus.
--  Пакет запускает это как PRE-start-up скрипт (до построения меню).
--  647394 - таблица действий MacroScript; id действия = "макрос`категория".
-- ============================================================
(
  local sSubMenuGuid = "9DF61374-760A-473B-B035-2DA41F5F807B"
  local sOpenGuid    = "929BD377-5B9D-4D55-B4B1-966C56A2B1A0"
  local sAnimGuid    = "4B72435E-B7BE-44EB-A2BF-9CFEDAF5293D"

  fn BleachRegisterMenu =
  (
    local mgr = callbacks.notificationParam()
    if (mgr != undefined) do
    (
      try
      (
        local mnu = mgr.mainMenuBar.CreateSubMenu sSubMenuGuid "Bleach Tool"
        if (mnu != undefined) then
        (
          mnu.CreateAction sOpenGuid 647394 "BleachImport_Open`Bleach Tool"
          mnu.CreateAction sAnimGuid 647394 "BleachImport_Anim`Bleach Tool"
        )
        else format "[Bleach Tool] CreateSubMenu returned undefined (GUID taken?)\n"
      )
      catch (format "[Bleach Tool] menu not created: %\n" (getCurrentException()))
    )
  )
  callbacks.removeScripts id:#bleachImportMenu
  callbacks.addScript #cuiRegisterMenus BleachRegisterMenu id:#bleachImportMenu
)
