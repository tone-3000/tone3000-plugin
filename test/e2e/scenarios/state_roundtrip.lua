-- VST3 only: Ardour never reads an LV2 property value back after a state
-- restore, so its control shows the default whatever the plugin restored.
local e2e = dofile(os.getenv("T3K_E2E_LIB"))
local PARAM = "toneBass"

e2e.create()
local track = e2e.add_mono_track()
local c = e2e.param_control(e2e.add_t3k(track), PARAM)
assert(c, "no control for " .. PARAM)
local want = c:lower() + 0.725 * (c:upper() - c:lower())
c:set_value(want, PBD.GroupControlDisposition.NoGroup)
e2e.roll(); e2e.sleep(1); e2e.stop()

e2e.reload()
local t3k = Session:route_by_name("guitar"):nth_plugin(0)
e2e.check(not t3k:isnil(), "TONE3000 back after reload")
local got = e2e.param_control(t3k, PARAM):get_value()
e2e.metric("set", want)
e2e.metric("after_reload", got)
e2e.check(math.abs(got - want) < 1e-3, PARAM .. " survives save and reload")
e2e.done()
