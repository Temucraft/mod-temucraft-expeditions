-- Temucraft Expeditions: a small window for mod-temucraft-expeditions (prefix TEX).
-- Shows the current run (tier, enemies left, deaths), this run's expedition powers, the equipped relics and the
-- Wayfinder's Ledger. Talks to the server by whispering itself addon messages; the server answers the same way.
-- /tex (or /wayfinder) opens and closes it; it opens by itself when you enter an expedition.
-- The same file is built into the interface by mod-temucraft-client-version; then this addon copy does nothing.

if TemucraftExpeditionsFrame then return end

local TEX = "TEX"
local MAX_LINES = 24

local function Send(msg)
    SendAddonMessage(TEX, msg, "WHISPER", UnitName("player"))
end

-- the data from the server (filled between BEGIN and END)
local data, pending = nil, nil
local hadRun = false

local W = CreateFrame("Frame", "TemucraftExpeditionsFrame", UIParent)
W:SetWidth(300)
W:SetHeight(120)
W:SetPoint("RIGHT", UIParent, "RIGHT", -60, 60)
W:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 },
})
W:SetBackdropColor(0, 0, 0, 0.85)
W:SetMovable(true)
W:EnableMouse(true)
W:SetClampedToScreen(true)
W:RegisterForDrag("LeftButton")
W:SetScript("OnDragStart", function(self) self:StartMoving() end)
W:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
W:SetFrameStrata("MEDIUM")
W:Hide()
tinsert(UISpecialFrames, "TemucraftExpeditionsFrame")

local title = W:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOPLEFT", W, "TOPLEFT", 12, -10)
title:SetText("Expedition")

local close = CreateFrame("Button", nil, W, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", W, "TOPRIGHT", 0, 0)

local lines = {}
for i = 1, MAX_LINES do
    local fs = W:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    fs:SetJustifyH("LEFT")
    fs:SetWidth(276)
    if i == 1 then
        fs:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8)
    else
        fs:SetPoint("TOPLEFT", lines[i - 1], "BOTTOMLEFT", 0, -3)
    end
    lines[i] = fs
end

local function Refresh()
    local out = {}
    local function add(text) if #out < MAX_LINES then out[#out + 1] = text end end
    if not data then
        add("|cff999999Waiting for the server...|r")
    else
        local r = data.run
        if r then
            title:SetText("Expedition: " .. r.name)
            add("|cffffcc00Tier " .. r.tier .. "|r" .. (r.affixes ~= "" and ("  |cffff8040" .. r.affixes .. "|r") or ""))
            if r.state == 1 then
                add("|cff40ff40Complete!|r Open the Expedition Cache.")
            elseif r.state == 2 then
                add("|cffff4040Failed.|r Use .temucraft expedition leave to go back.")
            else
                add("Enemies left: |cffffffff" .. r.left .. "|r of " .. r.total)
            end
            if r.lives > 0 then
                add("Deaths: " .. r.deaths .. " (of " .. r.lives .. " lives)")
            else
                add("Deaths: " .. r.deaths)
            end
            add(" ")
            add("|cff66ccffExpedition powers|r")
            if #data.powers == 0 then
                add("  |cff999999none yet|r")
            else
                for _, p in ipairs(data.powers) do
                    add("  " .. p.name .. " x" .. p.stacks .. " |cffaaaaaa- " .. p.text .. "|r")
                end
            end
        else
            title:SetText("Expedition")
            add("|cff999999You are not in an expedition.|r")
        end
        add(" ")
        add("|cffa335eeRelics|r")
        local any = false
        for slot = 0, 1 do
            local rel = data.relics[slot]
            if rel then
                any = true
                add("  " .. (slot == 1 and "Utility" or "Combat") .. ": " .. rel.name .. " (rank " .. rel.rank .. ")")
            end
        end
        if not any then add("  |cff999999none equipped|r") end
        local l = data.ledger
        if l then
            add(" ")
            add("|cffffcc00Wayfinder's Ledger|r (season " .. l.season .. ")")
            add("  Rank " .. l.rank .. ", " .. l.points .. " points" .. (l.next > 0 and (" (next rank at " .. l.next .. ")") or ""))
            add("  " .. l.coinName .. ": |cffffffff" .. l.coins .. "|r")
        end
    end
    for i = 1, MAX_LINES do
        if out[i] then lines[i]:SetText(out[i]); lines[i]:Show() else lines[i]:SetText(""); lines[i]:Hide() end
    end
    local height = 38
    for i = 1, #out do height = height + lines[i]:GetHeight() + 3 end
    W:SetHeight(height)
end

local function OnMessage(msg)
    local kind, a, b, c, d, e, f, g, h = strsplit("~", msg)
    if kind == "BEGIN" then
        pending = { run = nil, powers = {}, relics = {}, ledger = nil }
    elseif not pending then
        return
    elseif kind == "RUN" then
        pending.run = { name = a or "?", tier = b or "?", left = tonumber(c) or 0, total = tonumber(d) or 0,
                        deaths = tonumber(e) or 0, lives = tonumber(f) or 0, state = tonumber(g) or 0, affixes = h or "" }
    elseif kind == "P" then
        tinsert(pending.powers, { name = a or "?", stacks = tonumber(b) or 1, text = c or "" })
    elseif kind == "R" then
        pending.relics[tonumber(a) or 0] = { name = b or "?", rank = tonumber(c) or 1 }
    elseif kind == "L" then
        pending.ledger = { season = tonumber(a) or 1, rank = tonumber(b) or 0, points = tonumber(c) or 0,
                           next = tonumber(d) or 0, coinName = e or "Wayfinder Coin", coins = tonumber(f) or 0 }
    elseif kind == "END" then
        data, pending = pending, nil
        -- open by itself when a run starts
        if data.run and not hadRun then W:Show() end
        hadRun = data.run ~= nil
        if W:IsShown() then Refresh() end
    end
end

W:SetScript("OnShow", function() Refresh(); Send("GET") end)

local ev = CreateFrame("Frame")
ev:RegisterEvent("CHAT_MSG_ADDON")
ev:RegisterEvent("PLAYER_ENTERING_WORLD")
ev:SetScript("OnEvent", function(_, event, prefix, msg, channel, sender)
    if event == "PLAYER_ENTERING_WORLD" then
        Send("HELLO")
        return
    end
    if prefix ~= TEX or channel ~= "WHISPER" or sender ~= UnitName("player") then return end
    OnMessage(msg)
end)

SLASH_TEMUCRAFTEXPEDITIONS1 = "/tex"
SLASH_TEMUCRAFTEXPEDITIONS2 = "/wayfinder"
SlashCmdList["TEMUCRAFTEXPEDITIONS"] = function()
    if W:IsShown() then W:Hide() else W:Show() end
end
