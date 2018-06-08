--
-- Key defs
--

local cc = {"ctrl", "cmd"}

--
-- Reload
--

hs.hotkey.bind(cc, "r", hs.reload)

--
-- Screenshots
--


--- hs.hotkey.bind(cc, "s", function ()
---  local cb = function (exitcode, stdout, stderr)
---    if not ( exitcode == 0 ) then
  ---    hs.alert(stderr)
    ---else
      ---hs.alert("Screenshot Ready")
    ---end
  ---end

  ---local task = hs.task.new("/bin/bash", cb, {"-lic", "screenshot"})
  ---task:start()
---end)

--
-- Application hotkeys
--

function toggleApp(hint)
  local app = hs.application.get(hint)
  if not app then
    hs.application.open(hint)
    return
  end
  local mainwin = app:mainWindow()
  if mainwin then
    if mainwin == hs.window.focusedWindow() then
      mainwin:application():hide()
    else
      mainwin:application():activate()
    end
  end
end

local shortcuts = {e="Sublime Text", t="Things", c="Google Chrome", i="iTerm"}

for key, appName in pairs(shortcuts) do
  hs.hotkey.bind(cc, key, function() toggleApp(appName) end)
end

--
-- Window manipulation
--

-- Set window animation off. It's much smoother.
hs.window.animationDuration = 0


-- Grid setup
hs.grid.MARGINX = 0
hs.grid.MARGINY = 0
hs.grid.GRIDWIDTH = 4
hs.grid.GRIDHEIGHT = 2


-- a helper function that returns another function that resizes the current window
-- to a certain grid size.
local gridset = function(x, y, w, h)
    return function()
        local win = hs.window.focusedWindow()
        hs.grid.set(
            win,
            {x=x, y=y, w=w, h=h},
            win:screen()
        )
    end
end




-- Maximize window
hs.hotkey.bind(cc, "m", function()
  hs.grid.maximizeWindow(hs.window.focusedWindow())
end)


-- Left 1/4 of the screen
hs.hotkey.bind(cc, "l", gridset(0, 0, 1, 2))

-- Right 3/4 of the screen
hs.hotkey.bind(cc, "r", gridset(1, 0, 3, 2))
