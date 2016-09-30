--
-- Key defs
--

local cc = {"ctrl", "cmd"}

--
-- Reload
--

hs.hotkey.bind(cc, "R", hs.reload)

--
-- Application hotkeys
--

-- Toggle an application between being the frontmost app, and being hidden
function toggle_app(hint)
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
            mainwin:application():activate(true)
            mainwin:application():unhide()
            mainwin:focus()
        end
    end
end

hs.hotkey.bind(cc, "S", function() toggle_app("Sublime Text") end)
hs.hotkey.bind(cc, "T", function() toggle_app("Things") end)
hs.hotkey.bind(cc, "C", function() toggle_app("Google Chrome") end)
hs.hotkey.bind(cc, "I", function() toggle_app("iTerm") end)

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
hs.hotkey.bind(cc, "M", function()
  hs.grid.maximizeWindow(hs.window.focusedWindow())
end)


-- Left 1/4 of the screen
hs.hotkey.bind(cc, "Left", gridset(0, 0, 1, 2))

-- Right 3/4 of the screen
hs.hotkey.bind(cc, "Right", gridset(1, 0, 3, 2))
