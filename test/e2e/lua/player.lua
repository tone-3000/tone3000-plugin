ardour {
	["type"] = "dsp",
	name = "T3K E2E Player",
	category = "Utility",
	license = "GPL-3.0",
	author = "TONE3000",
	description = "Plays a 110 Hz sine at -12 dBFS: luasession cannot import audio files (Editor:do_import is GUI-only)."
}

local phase, step = 0, 0

function dsp_ioconfig() return { { audio_in = -1, audio_out = -1 } } end

function dsp_init(rate) step = 2 * math.pi * 110 / rate end

function dsp_run(ins, outs, n_samples)
	local p = phase
	for c = 1, #outs do
		local out = outs[c]:array()
		p = phase
		for i = 1, n_samples do
			out[i] = 0.25 * math.sin(p)
			p = p + step
		end
	end
	phase = p % (2 * math.pi)
end
