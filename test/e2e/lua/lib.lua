-- Shared helpers for the e2e scenarios. Results go to stdout as marker lines
-- (PASS/FAIL/METRIC) because luasession's exit code is meaningless: it aborts
-- with 134 during teardown even after a clean run.
io.stdout:setvbuf("line")

local M = {}
M.URI = "https://www.tone3000.com/plugin"
M.format = os.getenv("T3K_E2E_FORMAT") or "LV2"
M.dir = os.getenv("T3K_E2E_SESSION_DIR")
M.name = "e2e"

function M.check(ok, what) print((ok and "PASS " or "FAIL ") .. what) end
function M.metric(k, v) print(string.format("METRIC %s=%s", k, tostring(v))) end

function M.sleep(s) ARDOUR.LuaAPI.usleep(math.floor(s * 1e6)) end

local function start_engine()
	if not AudioEngine:running() then pcall(function() AudioEngine:start() end) end
end

function M.create()
	AudioEngine:set_backend("Dummy", "", "")
	assert(create_session(M.dir, M.name, 48000), "create_session failed at " .. M.dir)
	start_engine()
end

function M.reload()
	Session:save_state("", false, false, false, false, false)
	close_session()
	assert(load_session(M.dir, M.name), "load_session failed at " .. M.dir)
	start_engine()
end

-- Ardour 9 crashes outright when new_audio_track gets a nil route group.
function M.add_mono_track()
	local tl = Session:new_audio_track(1, 2, ARDOUR.RouteGroup(), 1, "guitar",
		ARDOUR.PresentationInfo.max_order, ARDOUR.TrackMode.Normal, true)
	assert(tl:size() == 1, "new_audio_track failed")
	return tl:front()
end

function M.add_player(track)
	local proc = ARDOUR.LuaAPI.new_luaproc(Session, "T3K E2E Player")
	assert(not proc:isnil(), "player luaproc not found (scripts dir)")
	track:add_processor_by_index(proc, 0, nil, true)
end

local function vst3_id()
	for pi in ARDOUR.LuaAPI.list_plugins():iter() do
		if pi.type == ARDOUR.PluginType.VST3 and pi.name:find("TONE3000", 1, true) then
			return pi.unique_id
		end
	end
end

function M.add_t3k(track)
	local p
	if M.format == "VST3" then
		local id = vst3_id()
		assert(id, "TONE3000 VST3 not found by the plugin scan")
		p = ARDOUR.LuaAPI.new_plugin(Session, id, ARDOUR.PluginType.VST3, "")
	else
		p = ARDOUR.LuaAPI.new_plugin(Session, M.URI, ARDOUR.PluginType.LV2, "")
	end
	assert(not p:isnil(), "TONE3000 " .. M.format .. " did not instantiate")
	track:add_processor_by_index(p, -1, nil, true)
	return p
end

-- Set through the control: LuaAPI.set_processor_param silently does nothing
-- on these inserts.
function M.param_control(proc, id)
	local a = proc:to_automatable()
	for prm in a:all_automatable_params():iter() do
		local c = a:automation_control(prm, false)
		if not c:isnil() and c:name() == id then return c end
	end
end

function M.peak_db(route, seconds)
	local meter, peak = route:peak_meter(), -math.huge
	for _ = 1, math.floor(seconds * 20) do
		M.sleep(0.05)
		for c = 0, meter:output_streams():n_audio() - 1 do
			peak = math.max(peak, meter:meter_level(c, ARDOUR.MeterType.MeterPeak))
		end
	end
	return peak
end

function M.roll() Session:request_roll(ARDOUR.TransportRequestSource.TRS_UI) end
function M.stop() Session:request_stop(false, false, ARDOUR.TransportRequestSource.TRS_UI) end

function M.done()
	print("DONE")
	close_session()
end

return M
