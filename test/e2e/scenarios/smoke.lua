local e2e = dofile(os.getenv("T3K_E2E_LIB"))

e2e.create()
local track = e2e.add_mono_track()
e2e.add_player(track)
local t3k = e2e.add_t3k(track)

e2e.roll()
local peak = e2e.peak_db(track, 4)
e2e.stop()
e2e.metric("peak_dbfs", string.format("%.1f", peak))
e2e.metric("signal_latency", t3k:signal_latency())
e2e.check(peak > -60, "output is not silent")
e2e.done()
