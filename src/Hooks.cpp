#include "Formation.h"
#include "Log.h"
#include "Compat.h"

#include <Syringe.h>
#include <YRPP.h>

namespace
{
	void LogLoadContext()
	{
		char exe[MAX_PATH] = { 0 };
		if (GetModuleFileNameA(nullptr, exe, MAX_PATH))
			Logf("load: game process = %s", exe);

		char module[MAX_PATH] = { 0 };
		HMODULE self = nullptr;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&LogLoadContext), &self)
			&& GetModuleFileNameA(self, module, MAX_PATH))
		{
			Logf("load: plugin module = %s", module);
		}
	}
}

// Runs once while WinMain initializes, i.e. after Syringe has installed every hook.
DEFINE_HOOK(6BD68D, Enthalpy_Init, 6)
{
	Logf("hook: WinMain registrations reached - Enthalpy is armed");

	LogLoadContext();

	const Compat::Report report = Compat::VerifyGameBuild();
	Logf("compat: %s", report.Summary);

	if (!report.BuildMatches)
	{
		// Every hook address in this plugin is absolute, so patching an unknown build means
		// jumping into unrelated code.  Standing down keeps the user's game alive and leaves
		// one clear line in the log instead of a crash with no explanation.
		FormationController::Instance().Disable(
			"gamemd.exe is not the build this plugin targets (needs Yuri's Revenge 1.001)");
	}

	return 0;
}

// TacticalClass::Render - first thing drawn every frame while the tactical view exists.
DEFINE_HOOK(6D3D10, Enthalpy_TacticalRender_Before, 6)
{
	static int frame = 0;
	++frame;

	FormationController::Instance().Update();

	if (frame % 300 == 1)
	{
		Point2D mouse = { 0, 0 };
		FormationController::CursorClientPos(mouse);

		CoordStruct world = { 0, 0, 0 };
		FormationController::ClientToWorld(mouse, world);

		CellStruct cell = { 0, 0 };
		if (MapClass::Instance)
		{
			if (CellClass* pCell = MapClass::Instance->TryGetCellAt(world))
				cell = pCell->MapCoords;
		}

		Logf("frame=%d mouse=(%d,%d) world=(%d,%d,%d) cell=(%d,%d) LMB=%d RMB=%d",
			frame, mouse.X, mouse.Y, world.X, world.Y, world.Z, cell.X, cell.Y,
			(GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
			(GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
	}

	return 0;
}

// Late in TacticalClass::Render, after the world, trees, units and their pips are on the
// surface.  Drawing here is what keeps the preview from being covered by scenery.
// The overwritten instruction is `push 0x886FA0` (5 bytes).
// A second draw point. 0x6D9134 sits in the render-layer pass, which some maps never run
// (plain terrain with no layers at all), so on those maps every silhouette vanished.
// 0x6D9076 runs on every frame, right after the terrain, and covers exactly those maps.
// Whichever of the two runs first in a frame paints; the other is deduped by frame id.
DEFINE_HOOK(6D9076, Enthalpy_TacticalRender_Layers, 5)
{
	FormationController::Instance().DrawPreview();
	return 0;
}
DEFINE_HOOK(6D9134, Enthalpy_TacticalRender_AfterWorld, 5)
{
	FormationController::Instance().DrawPreview();
	return 0;
}

// DisplayClass::ProcessClickCoords decides which order a click turns into.  While a
// formation drag is in progress the engine must not see the release at all, otherwise it
// races the drop point we commit and sometimes steals the order.  Returning the engine's
// own "nothing happened" exit (0x6925FC) keeps both buttons from producing an order.
//
// The hook sits on `add ecx, 80h` (6 bytes, ends exactly on an instruction boundary at
// 0x692429) rather than on 0x692419, which Phobos already owns.  Execution state here is
// identical to Phobos' site, so the same epilogue is safe; the overwritten `add` is
// emulated when the click is allowed through.
DEFINE_HOOK(692423, Enthalpy_SuppressClick, 6)
{
	if (FormationController::Instance().ShouldSuppressClick())
		return 0x6925FC;

	R->ECX(R->ECX() + 0x80);
	return 0;
}
