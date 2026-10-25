-- ============================================================
--  BleachMenu_2016_2024.ms - меню "Bleach Tool" для 3ds Max 2016-2024 (menuMan)
-- ============================================================
(
  local sMenu = "Bleach Tool"
  local aItems = #(#("Import BLEACH RoS", "BleachImport_Open"), #("BLEACH Animations", "BleachImport_Anim"))
  local mnu = menuMan.findMenu sMenu
  if (mnu == undefined) do
  (
    local bar = menuMan.getMainMenuBar()
    mnu = menuMan.createMenu sMenu
    local sub = menuMan.createSubMenuItem sMenu mnu
    bar.addItem sub (bar.numItems())
  )
  for a in aItems do
  (
    local found = false
    for i = 1 to mnu.numItems() while not found do if ((mnu.getItem i).getTitle()) == a[1] do found = true
    if not found do
    (
      local it = menuMan.createActionItem a[2] "Bleach Tool"
      it.setTitle a[1]
      it.setUseCustomTitle true
      mnu.addItem it -1
    )
  )
  menuMan.updateMenuBar()
)
