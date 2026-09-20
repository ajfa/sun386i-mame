-- Photograph the emulated screen every so often. video:snapshot() writes the
-- PNG even with -video none, so no window is needed to capture the screen.
local shot = 0
local cap = tonumber(os.getenv("SUN386I_SHOTS") or "60")
local every = tonumber(os.getenv("SUN386I_SNAP") or "25")

subscription = emu.add_machine_frame_notifier(function()
	local t = manager.machine.time.seconds
	if shot < cap and t >= (shot + 1) * every then
		shot = shot + 1
		manager.machine.video:snapshot()
	end
end)
