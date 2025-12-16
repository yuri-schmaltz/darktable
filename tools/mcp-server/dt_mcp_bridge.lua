local dt = require "darktable"
local du = require "lib/dtutils"

-- Configuration
local KEEP_ALIVE_INTERVAL = 250 -- check every 250ms for responsiveness
local MCP_DIR = dt.configuration.config_dir .. "/mcp"
local CMD_FILE = MCP_DIR .. "/cmd.json"
local PROC_FILE = MCP_DIR .. "/processing.json" -- Atomic move target
local RESP_TMP_FILE = MCP_DIR .. "/response.json.tmp"
local RESP_FILE = MCP_DIR .. "/response.json"
local LOCK_FILE = MCP_DIR .. "/bridge.lock"

-- Ensure MCP directory exists
dt.configuration.check_user_config_dir("mcp")

-- Logging
local function log(msg)
    dt.print_log("[MCP Bridge] " .. tostring(msg))
end

-- ============================================================
-- JSON Parser (Embedded for standalone robustness)
-- Based on json.lua (MIT) - lightweight
-- ============================================================
local json = {}
local function kind_of(obj)
  if type(obj) ~= 'table' then return type(obj) end
  local i = 1
  for _ in pairs(obj) do
    if obj[i] ~= nil then i = i + 1 else return 'table' end
  end
  if i == 1 then return 'table' else return 'array' end
end

local function escape_str(s)
  local in_char  = {'\\', '"', '/', '\b', '\f', '\n', '\r', '\t'}
  local out_char = {'\\', '"', '/',  'b',  'f',  'n',  'r',  't'}
  for i, c in ipairs(in_char) do
    s = s:gsub(c, '\\' .. out_char[i])
  end
  return s
end

local function skip_delim(str, pos, delim, err_if_missing)
  pos = pos + #str:match('^%s*', pos)
  if str:sub(pos, pos) ~= delim then
    if err_if_missing then error('Expected ' .. delim .. ' near position ' .. pos) end
    return pos, false
  end
  return pos + 1, true
end

local function parse_str_val(str, pos, val)
  val = val or ''
  local early_end_error = 'End of input found while parsing string.'
  if pos > #str then error(early_end_error) end
  local c = str:sub(pos, pos)
  if c == '"'  then return val, pos + 1 end
  if c ~= '\\' then return parse_str_val(str, pos + 1, val .. c) end
  -- Escaped characters
  local next_c = str:sub(pos + 1, pos + 1)
  if not next_c then error(early_end_error) end
  return parse_str_val(str, pos + 2, val .. next_c)
end

local function parse_num_val(str, pos)
  local num_str = str:match('^-?%d+%.?%d*[eE]?[+-]?%d*', pos)
  local val = tonumber(num_str)
  if not val then error('Error parsing number at position ' .. pos) end
  return val, pos + #num_str
end

function json.encode(obj, as_key)
  local s = {}
  local kind = kind_of(obj)
  if kind == 'array' then
    if as_key then error('Can\'t encode array as key.') end
    s[#s + 1] = '['
    for i, val in ipairs(obj) do
      if i > 1 then s[#s + 1] = ', ' end
      s[#s + 1] = json.encode(val)
    end
    s[#s + 1] = ']'
  elseif kind == 'table' then
    if as_key then error('Can\'t encode table as key.') end
    s[#s + 1] = '{'
    local first = true
    for k, v in pairs(obj) do
      if not first then s[#s + 1] = ', ' end
      first = false
      s[#s + 1] = json.encode(k, true)
      s[#s + 1] = ':'
      s[#s + 1] = json.encode(v)
    end
    s[#s + 1] = '}'
  elseif kind == 'string' then
    return '"' .. escape_str(obj) .. '"'
  elseif kind == 'number' then
    return as_key and '"' .. tostring(obj) .. '"' or tostring(obj)
  elseif kind == 'boolean' then
    return tostring(obj)
  elseif kind == 'nil' then
    return 'null'
  else
    error('Unjsonifiable type: ' .. kind .. '.')
  end
  return table.concat(s)
end

function json.decode(str, pos, end_delim)
  pos = pos or 1
  if pos > #str then error('Reached unexpected end of input.') end
  local pos = pos + #str:match('^%s*', pos)
  local first = str:sub(pos, pos)
  if first == '{' then
    local obj, key, delim_found = {}, true, true
    pos = pos + 1
    while true do
      key, pos = json.decode(str, pos, '}')
      if key == nil then return obj, pos end
      if not delim_found then error('Comma missing between object items.') end
      pos = skip_delim(str, pos, ':', true)
      obj[key], pos = json.decode(str, pos)
      pos, delim_found = skip_delim(str, pos, ',')
    end
  elseif first == '[' then
    local arr, val, delim_found = {}, true, true
    pos = pos + 1
    while true do
      val, pos = json.decode(str, pos, ']')
      if val == nil then return arr, pos end
      if not delim_found then error('Comma missing between array items.') end
      arr[#arr + 1] = val
      pos, delim_found = skip_delim(str, pos, ',')
    end
  elseif first == '"' then
    return parse_str_val(str, pos + 1)
  elseif first == '-' or first:match('%d') then
    return parse_num_val(str, pos)
  elseif first == end_delim then
    return nil, pos + 1
  else
    local literals = {['true'] = true, ['false'] = false, ['null'] = nil}
    for lit_str, lit_val in pairs(literals) do
      local lit_len = #lit_str
      if str:sub(pos, pos + lit_len - 1) == lit_str then return lit_val, pos + lit_len end
    end
    error('Invalid JSON syntax starting at ' .. pos)
  end
end
-- ============================================================


-- Define available API commands
local commands = {}

function commands.set_rating(args)
    local img_id = args.img_id
    local rating = args.rating
    local img = dt.database.get_image(img_id)
    
    if not img then error("Image not found: " .. img_id) end
    
    img.rating = rating
    return { success = true, id = img_id, new_rating = img.rating }
end

function commands.set_color_label(args)
    local img_id = args.img_id
    local color = args.color -- "red", "yellow", "green", "blue", "purple"
    local img = dt.database.get_image(img_id)
    
    if not img then error("Image not found: " .. img_id) end
    
    dt.colorlabels.add_label(img, color)
    return { success = true, id = img_id, label_added = color }
end

function commands.attach_tag(args)
    local img_id = args.img_id
    local tag_name = args.tag_name
    local img = dt.database.get_image(img_id)
    
    if not img then error("Image not found: " .. img_id) end
    
    local tag = dt.tags.create(tag_name)
    dt.tags.attach(tag, img)
    return { success = true, id = img_id, tag_attached = tag_name }
end


function commands.get_selection(args)
    local selection = dt.gui.selection()
    local result = {}
    for _, img in ipairs(selection) do
        table.insert(result, { id = img.id, path = img.path .. "/" .. img.filename, filename = img.filename })
    end
    return result
end

function commands.apply_style(args)

    local img_id = args.img_id
    local style_name = args.style_name
    local img = dt.database.get_image(img_id)
    
    if not img then error("Image not found: " .. img_id) end
    
    local style = nil
    for _, s in ipairs(dt.styles.get_list()) do
        if s.name == style_name then
            style = s
            break
        end
    end
    
    if not style then error("Style not found: " .. style_name) end
    
    dt.styles.apply(style, img)
    return { success = true, id = img_id, style_applied = style_name }
end


-- Helper to read file content
local function read_file(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local content = f:read("*all")
    f:close()
    return content
end

-- Helper to write file content
local function write_file(path, content)
    local f = io.open(path, "w")
    if not f then return end
    f:write(content)
    f:close()
end


local function process_commands()
    -- Check if command file exists
    local f = io.open(CMD_FILE, "r")
    if not f then return end -- No command
    f:close()

    -- Atomic processing: Try to rename cmd.json -> processing.json
    -- os.rename handles atomic move on POSIX
    local success, err = os.rename(CMD_FILE, PROC_FILE)
    if not success then
        -- Could be race condition, or permission. Log and retry next tick
        log("Failed to rename command file: " .. tostring(err))
        return
    end

    -- Now we exclusively own PROC_FILE
    local content = read_file(PROC_FILE)
    if not content or content == "" then 
        os.remove(PROC_FILE)
        return 
    end
    
    log("Received command payload: " .. content)

    local request = nil
    local status_parse, res_parse = pcall(json.decode, content)
    
    local result_payload = {}
    
    if status_parse and res_parse then
        request = res_parse
        local cmd_name = request.cmd
        local args = request.args or {}
        
        if commands[cmd_name] then
            log("Executing command: " .. cmd_name)
            local status_exec, res_exec = pcall(commands[cmd_name], args)
            if status_exec then
                result_payload = { status = "ok", data = res_exec }
            else
                result_payload = { status = "error", error = tostring(res_exec) }
                log("Command error: " .. tostring(res_exec))
            end
        else
            result_payload = { status = "error", error = "Unknown command: " .. tostring(cmd_name) }
            log("Unknown command: " .. tostring(cmd_name))
        end
    else
        result_payload = { status = "error", error = "JSON parse error: " .. tostring(res_parse) }
        log("JSON parse error")
    end
    
    -- Serialize Response
    local resp_str = json.encode(result_payload)
    
    -- Write to temp file first
    write_file(RESP_TMP_FILE, resp_str)
    
    -- Atomic rename to response.json
    os.rename(RESP_TMP_FILE, RESP_FILE)
    
    -- Clean up processing file
    os.remove(PROC_FILE)
end

-- Start polling
dt.register_event("mcp_poller", KEEP_ALIVE_INTERVAL, process_commands)

-- Storage Module for Cloud Sync
dt.register_storage("mcp_cloud", "MCP Cloud Sync",
    function(storage_params, image, format_params, filename)
        -- Send command to Python server to upload the file
        -- We construct a special command payload that the python server will recognize
        -- Note: The bridge is for PULLING commands, but we need to PUSH an event/command.
        -- Since our architecture is polling-based (Python writes to CMD, Lua writes to RESP),
        -- Lua cannot easily "push" a command to Python unless Python is polling too.
        
        -- HACK: We will write to a separate "events.json" or simply log it for now?
        -- ACTUALLY: The easiest way for now is to write to a specific 'upload_queue.json' 
        -- and have the Python server watch it, OR just print to stdout and expect the user to see it?
        
        -- BETTER ARCHITECTURE:
        -- The Python server should expose an endpoint or watch a file.
        -- Let's write to "~/.config/darktable/mcp/upload_queue.json"
        
        local remote = dt.preferences.read("mcp", "cloud_remote", "string") or "remote:photos"

        local upload_req = {
            source_file = filename,
            target = "cloud",
            target_remote = remote,
            img_id = image.id
        }
        
        local queue_file = DT_MCP_DIR .. "/upload_queue.json.tmp"
        local final_queue_file = DT_MCP_DIR .. "/upload_queue.json"
        
        -- Simple append is hard with JSON. We will just write one file per request for simplicity using UUID?
        -- Or just overwrite for this POC (Risk of race condition if multiple exports at once).
        -- Let's use a timestamped filename.
        
        local timestamp = os.time()
        local unique_file = DT_MCP_DIR .. "/upload_" .. timestamp .. "_" .. image.id .. ".json"
        
        local f = io.open(unique_file, "w")
        if f then
            f:write(json.encode(upload_req))
            f:close()
            log("Queued upload: " .. unique_file)
        else
            log("Failed to write upload request")
        end
    end,
    nil, -- finalize
    nil, -- supported

-- UI: MCP AI Assistant Panel
local function create_ai_panel()
    local widget = dt.new_widget("box")
    widget.orientation = "vertical"

    -- Section 1: AI Tools
    local label_tools = dt.new_widget("label")
    label_tools.label = "AI Tools"
    
    local btn_tag = dt.new_widget("button")
    btn_tag.label = "Auto Tag Selection"
    btn_tag.tooltip = "Analyze & Tag selected images using AI"
    
    local btn_cull = dt.new_widget("button")
    btn_cull.label = "Smart Cull"
    btn_cull.tooltip = "Score and cull selected images by sharpness"
    
    -- Section 2: Cloud Sync
    local label_cloud = dt.new_widget("label")
    label_cloud.label = "Cloud Sync Status"
    
    local status_cloud = dt.new_widget("label")
    status_cloud.label = "Idle"
    
    -- Section 3: Settings
    local label_settings = dt.new_widget("label")
    label_settings.label = "-- Settings --"

    local entry_remote = dt.new_widget("entry")
    entry_remote.text = dt.preferences.read("mcp", "cloud_remote", "string") or "remote:photos"
    entry_remote.tooltip = "Rclone remote name (e.g., remote:photos)"
    
    local entry_sensitivity = dt.new_widget("entry")
    entry_sensitivity.text = dt.preferences.read("mcp", "ai_sensitivity", "string") or "Normal"
    entry_sensitivity.tooltip = "AI Sensitivity (Strict/Normal/Loose)"

    -- Layout
    widget[1] = label_tools
    widget[2] = btn_tag
    widget[3] = btn_cull
    widget[4] = dt.new_widget("label") -- Spacer
    widget[4].label = " "
    widget[5] = label_cloud
    widget[6] = status_cloud
    widget[7] = dt.new_widget("label") -- Spacer
    widget[7].label = " "
    widget[8] = label_settings
    widget[9] = entry_remote
    widget[10] = entry_sensitivity

    -- Save Preferences Trigger (on leaving widget or explicit save?)
    -- Simple approach: Save when executing commands.

    -- Callbacks
    dt.register_event("mcp_btn_tag", "clicked", function(w)
        local selection = dt.gui.selection()
        
        -- Save stats
        dt.preferences.write("mcp", "cloud_remote", "string", entry_remote.text)
        dt.preferences.write("mcp", "ai_sensitivity", "string", entry_sensitivity.text)

        if #selection > 0 then
            log("UI: Triggering Auto-Tag for " .. #selection .. " images")
             local req = {
                tool = "auto_tag_selection",
                args = {
                    ai_sensitivity = entry_sensitivity.text
                } 
            }
            local q = DT_MCP_DIR .. "/gui_request_" .. os.time() .. ".json"
            local f = io.open(q, "w")
            if f then
                f:write(json.encode(req))
                f:close()
                status_cloud.label = "Auto-Tag requested..."
            end
        else
            dt.print("No images selected")
        end
    end, btn_tag)

    dt.register_event("mcp_btn_cull", "clicked", function(w)
         local selection = dt.gui.selection()

         -- Save stats
        dt.preferences.write("mcp", "cloud_remote", "string", entry_remote.text)
        dt.preferences.write("mcp", "ai_sensitivity", "string", entry_sensitivity.text)

        if #selection > 0 then
             local req = {
                tool = "cull_selection",
                args = {
                    ai_sensitivity = entry_sensitivity.text
                } 
            }
            local q = DT_MCP_DIR .. "/gui_request_" .. os.time() .. ".json"
            local f = io.open(q, "w")
            if f then
                f:write(json.encode(req))
                f:close()
                status_cloud.label = "Culling requested..."
            end
        else
            dt.print("No images selected")
        end
    end, btn_cull)

    -- Status Checker
    local function check_status()
        local sfile = DT_MCP_DIR .. "/gui_status.json"
        local f = io.open(sfile, "r")
        if f then
            local content = f:read("*all")
            f:close()
            local status = json.decode(content)
            if status and status.message then
                 status_cloud.label = status.message
            end
        else
            status_cloud.label = "Server Offline (No Status)"
        end
    end

    -- Refresh Button
    local btn_refresh = dt.new_widget("button")
    btn_refresh.label = "Refresh Status"
    dt.register_event("mcp_btn_refresh", "clicked", function(w)
        check_status()
    end, btn_refresh)
    
    widget[11] = btn_refresh
    
    -- Initial Check
    check_status()


    return widget
end

dt.register_lib("mcp_ai_assistant", "MCP AI Assistant", true, false, {[dt.gui.views.lighttable] = {"DT_UI_CONTAINER_PANEL_RIGHT_CENTER", 100}}, create_ai_panel())
