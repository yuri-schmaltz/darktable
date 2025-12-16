local dt = require "darktable"
local du = require "lib/dtutils"

-- Configuration
local KEEP_ALIVE_INTERVAL = 500 -- check every 500ms
local MCP_DIR = dt.configuration.config_dir .. "/mcp"
local CMD_FILE = MCP_DIR .. "/cmd.json"
local RESP_FILE = MCP_DIR .. "/response.json"

-- Ensure MCP directory exists
dt.configuration.check_user_config_dir("mcp")

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

function commands.attach_tag(args)
    local img_id = args.img_id
    local tag_name = args.tag_name
    local img = dt.database.get_image(img_id)
    
    if not img then error("Image not found: " .. img_id) end
    
    local tag = dt.tags.create(tag_name)
    dt.tags.attach(tag, img)
    return { success = true, id = img_id, tag_attached = tag_name }
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

-- Helper to simple json parse/encode (avoiding external deps if possible, but here we assume rudimentary structure)
-- For robustness effectively we'd want a JSON lib, but for this bridge we'll do simple string matching or assume users have a json lib
-- To make this standalone without deps, we'll try to rely on python handling the complex data and us just reading simple keys
-- ACTUALLY: We will use `dkjson` if available, or a simple regex parser for our known commands. 
-- Since we are in Darktable Lua, let's assume valid JSON input from our Python script.
-- For simplicity in this Proof-of-Concept, we'll use a hacky parser since we control the input format strictly from Python.

local function parse_json(str)
   -- Very basic parser, assumes flat object: {"cmd": "set_rating", "args": {"img_id": 123, "rating": 5}}
   -- In production, bundle dkjson.lua
   -- Here we rely on the implementation plan's Python side to send strict formatted JSON
   
   -- Mock implementation: In reality, we need to load a json library. 
   -- Darktable often includes libs. Let's try to verify if we can use one.
   -- For now, let's write a dummy return that we'd replace with real JSON parsing
   return nil 
end

-- Since we can't easily rely on a JSON lib being present without installing it, 
-- we will make the Python side write LUA CODE that we execute. This is safer for dependencies but riskier for security.
-- given this is a local tool, we'll assume `dofile`.
-- ALTERNATIVE: Use a simple text protocol.
-- COMMAND|ARG1|ARG2...

local function check_command()
    local content = read_file(CMD_FILE)
    if not content or content == "" then return end
    
    -- Clear file immediately to prevent double execution
    write_file(CMD_FILE, "")
    
    -- Protocol: CMD_NAME|JSON_ARGS
    local cmd_name, json_args = content:match("^(.-)|(.*)$")
    
    if not cmd_name then return end
    
    local status, result
    
    -- Very basic JSON-like parser for the arguments we support
    -- We'll assume the Python side formats standard args
    local args = {}
    for k,v in json_args:gmatch([["(%w+)":%s*"?([^",}]+)"?]]) do
        if tonumber(v) then args[k] = tonumber(v) else args[k] = v end
    end
    
    if commands[cmd_name] then
        status, result = pcall(commands[cmd_name], args)
    else
        status = false
        result = "Unknown command: " .. cmd_name
    end
    
    local resp_str
    if status then
        resp_str = '{"status": "ok", "data": "' .. tostring(result) .. '"}'
    else
        resp_str = '{"status": "error", "error": "' .. tostring(result) .. '"}'
    end
    
    write_file(RESP_FILE, resp_str)
end

-- Start polling
dt.register_event("mcp_poller", KEEP_ALIVE_INTERVAL, check_command)
