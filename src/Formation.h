#pragma once

#include <YRPP.h>

#include <TechnoClass.h>
#include <TacticalClass.h>
#include <Surface.h>
#include <GeneralStructures.h>

// RA3-style formation ordering for Yuri's Revenge.
//
// Gesture: with units selected, press the left and right mouse buttons together at the
// destination, then drag.  A translucent ghost formation follows the drag; releasing the
// right button hands the formation to the units, which drive to their assigned cells and
// then turn to face away from the drop point.
class FormationController
{
public:
	static FormationController& Instance();

	// Called once per tactical frame, before the frame is drawn.
	void Update();

	// Called late in the frame, after the world has been drawn.
	void DrawPreview();

	// True while the gesture owns the mouse, so the engine's own click handling can be
	// told to stand down.
	bool ShouldSuppressClick();

	// Turns the whole plugin off after a failed self check (wrong game build, missing hook),
	// so a user with an unsupported setup gets a log line instead of a crash.
	void Disable(const char* reason);

	static bool BothMouseButtonsDown();	static bool RightButtonDown();
	static bool MiddleButtonDown();
	static bool CursorWorldPos(CoordStruct& out);
	static bool CursorClientPos(Point2D& out);
	static void ClientToWorld(const Point2D& client, CoordStruct& world);
	static HWND GameWindow();
	static bool GameWindowRect(RECT& out);

private:
	FormationController() = default;

	enum class State
	{
		Idle,
		Dragging,
		Cooldown
	};

	// One planned destination: the cell a unit is assigned to, plus the per-unit offset
	// used when several infantry share a cell.
	struct SlotPlan
	{
		int CellX;
		int CellY;
		int OffsetX;
		int OffsetY;
		bool Snapped;   // the ideal cell was unusable, so this is the nearest legal one
		bool StartCell; // the cell is only occupied by units that are about to leave
		bool Occupied;  // some unit stands there right now

		// Aircraft and jumpjet units never park on the ground, so their ghost is drawn at the
		// altitude they hover at, with a vertical guide line down to the target cell.
		int AirHeight;  // leptons above the cell; 0 means the ghost sits on the ground

		// Optional unit artwork for the ghost: the unit's own SHP frame, drawn translucent
		// so the preview reads as the unit itself rather than a bare footprint.
		SHPStruct* Image;
		int Frame;
	};

	// A cell already handed out during layout, so two units never claim the same square
	// (infantry excepted, up to three per cell).
	struct CellClaim
	{
		int CellX;
		int CellY;
		int InfantryCount;
		bool UsedByVehicle;
	};

	// A unit that still has to be turned towards the formation facing after it arrives.
	struct PendingFacing
	{
		TechnoClass* Unit;
		int CellX;
		int CellY;
		DirStruct Facing;
		int FramesLeft;
		bool Applied;
	// True once the unit was actually seen driving towards the slot.
	bool SeenMoving;
	// Consecutive frames the locomotor reported no movement; filters its one-tick gaps.
	int StillFrames;
		int TurnAttempts;
	};

	// The pixels one unit's own artwork paints, captured once per (unit type, facing) and
	// reused for every slot of that pair: drawing each ghost through the engine individually
	// is both slow and the reason a unit could end up without a ghost at all.
	//
	// Wide enough for the broadest side-on vehicle silhouette; the engine draws a tank
	// noticeably wider than one cell, and a box that clips it produced half a ghost.
	static constexpr int kGhostWidth = 192;
	static constexpr int kGhostHeight = 192;
	static constexpr int kGhostHalfWidth = 96;
	static constexpr int kGhostTopAbove = 128;

	struct GhostPatch
	{
		TechnoTypeClass* Type;
		// Only the silhouette is kept: a voxel drawn outside the normal render pass comes out
		// of the engine as solid black, so the painted colours carry no information.
		unsigned char Mask[kGhostWidth * kGhostHeight];
		// The same silhouette captured with the unit type's shadow switched off.  Pixels that
		// only exist in Mask are the shadow the artwork paints; they are drawn darker than the
		// body, or left out entirely when GhostShadow is off.
		unsigned char BodyMask[kGhostWidth * kGhostHeight];
		// True when the artwork itself paints a shadow (mask pixels outside BodyMask).  Aircraft
		// and voxel art usually have none, so the ground shadow has to be synthesised.
		bool HasShadow;
		int Top;   // surface offset the raw pixels were captured at
		int Left;
		// Where the unit's feet sit inside the patch, measured from its top-left corner.  This
		// is the fixed point the borrowed unit was painted at, so the ghost stays put while the
		// block rotates instead of sliding with the artwork's bounding box.
		int FootX;
		int FootY;
		int MaskHeight;   // height of the painted silhouette
		int MaskWidth;    // width of the painted silhouette
	};

	// One captured ghost per (unit type, facing) pair.  Capturing costs an engine draw plus a
	// full scan of the patch, so it happens once and is then reused for the rest of the
	// session; reuse is also what keeps an infantry ghost frozen in one pose mid-drag.
	struct GhostCacheEntry
	{
		TechnoTypeClass* Type;
		unsigned short Facing;   // DirStruct value, quantised to 32 directions
		// Zero for flat ground, where a silhouette is the same wherever it is pasted.  A ramp
		// tilts the unit by the slope of its own cell, so ramp destinations are keyed by cell
		// and re-captured when the facing changes.
		int CellKey;
		int LastUse;             // frame id of the last blit, for eviction
		GhostPatch Patch;
	};

	static constexpr int kGhostCacheSize = 48;

	// A cursor wobbling on a bucket boundary must not flip the block's facing every frame:
	// every flip would re-capture the silhouette and read as flicker.
	static constexpr int kFacingDeadband = 1638;   // ~9 degrees, in 1/65536 turn units

	bool CellOnlyHoldsMovers(CellClass* pCell) const;

	static bool IsWorldPointValid(const CoordStruct& world);
	static bool ScreenDirectionToWorld(double screenX, double screenY,
		double& worldX, double& worldY);

	void GatherSelectedUnits();
	void ComputeLayout();
	void CommitOrders();
	void ServicePendingFacings();


	// Captures one unit's artwork into a patch (drawing it through the engine once), and
	// blits an already captured patch onto the surface at a screen position.
	// The raw draw-and-diff below runs engine code on a borrowed object, so it is wrapped in a
	// SEH guard: a fault there must cost one ghost frame, never the whole game.
	bool CaptureUnitGhostRaw(TechnoClass* pUnit, const CoordStruct& worldPos, DirStruct facing,
		int clipWidth, int clipHeight, GhostPatch& patch);
	bool CaptureUnitGhost(TechnoClass* pUnit, const CoordStruct& worldPos, DirStruct facing,
		int clipWidth, int clipHeight, GhostPatch& patch);
	// layers: bit 0 draws the body, bit 1 draws the shadow.  Aircraft draw the body at the
	// hovering altitude and the shadow separately on the ground of the target cell.
	void BlitGhostPatch(Surface* pSurface, const GhostPatch& patch, int left, int top,
		DWORD tint, DWORD edgeTint, int opacity, int layers, int clipWidth, int clipHeight);

	// Captures are done in Update(), before the engine renders, so the temporary draw of the
	// borrowed unit can never show up in the frame the player sees (that stray draw is what
	// made the infantry ghosts flicker).  DrawPreview only ever looks up the cache.
	void PrepareGhosts();

	static GhostCacheEntry* GhostCache();
	static int& GhostCacheCount();
	static bool GhostKey(TechnoClass* pUnit, const CoordStruct& worldPos, DirStruct facing,
		TechnoTypeClass*& pType, unsigned short& quantised);
	static int GhostSlopeKey(const CoordStruct& worldPos);

	// Lookup only: never draws.  Returns nullptr when this (type, facing, slope) combination
	// has not been captured yet, in which case the slot simply has no ghost for this frame.
	static const GhostCacheEntry* FindGhost(TechnoClass* pUnit, const CoordStruct& worldPos,
		DirStruct facing);

	static int CollectSelected(TechnoClass** buffer, int capacity);
	static bool IsAlive(TechnoClass* pTechno);	static bool WithinCell(TechnoClass* pTechno, int cellX, int cellY);

	bool disabled_ = false;
	bool warnedAboutMissingDraw_ = false;
	// Frames in which the draw hook actually ran.  If Update keeps running but this stays
	// zero, another plugin owns the draw address and the ghosts can never appear.
	int drawFrames_ = 0;

	State state_ = State::Idle;
	int cooldownFrames_ = 0;
	int suppressFrames_ = 0;
	bool wasSuppressing_ = false;

	// Advances once per frame; keeps engine-side ghost capture to one per frame.
	int frameId_ = 0;

	TechnoClass* units_[256] = {};
	int unitCount_ = 0;

	// The selection is dropped by the engine as soon as the first button goes down, so the
	// most recent selection is remembered and used if the live scan comes up empty.
	TechnoClass* snapshot_[256] = {};
	int snapshotCount_ = 0;
	int selectedCount_ = 0;

	CoordStruct anchor_ = { 0, 0, 0 };
	CoordStruct cursorWorld_ = { 0, 0, 0 };

	// The world point only moves when the cursor enters another cell (see ComputeLayout).
	int cursorCellX_ = 0;
	int cursorCellY_ = 0;
	bool hasCursorCell_ = false;

	// The same two points in client pixels, which is the space the facing is measured in.
	Point2D anchorClient_ = { 0, 0 };
	Point2D cursorClient_ = { 0, 0 };

	// Formation facing as a unit vector, and the same direction as a 16 bit engine facing.
	double facingX_ = 1.0;
	double facingY_ = 0.0;
	DirStruct facingDir_;

	// Drag direction (unit vector) and the grid step in cells, both derived from the drag.
	double dragX_ = 1.0;
	double dragY_ = 0.0;
	int spacingCells_ = 1;

	int columns_ = 1;
	int ranks_ = 1;

	SlotPlan slots_[256] = {};
	int slotCount_ = 0;
	CellClaim claims_[256] = {};
	int claimCount_ = 0;

	bool leftWasDown_ = false;
	bool rightWasDown_ = false;
	bool middleWasDown_ = false;
	// True from the moment both buttons were seen held until the right button comes up.
	bool gestureLatch_ = false;
	bool leftReleasedFirst_ = false;
	int reassertFrames_ = 0;
	int trackFrames_ = 0;
	// How often a unit that stopped short of its cell may be nudged again; stops the
	// supervisor from re-issuing a move every few frames for the same unit.
	int reassertTries_[256] = {};
	bool wasHome_[256] = {};
	bool permanent_[256] = {};
	int permanentCount_ = 0;
	DWORD lastPinLog_ = 0;
	int keepTries_[256] = {};
	static constexpr int kMaxKeepTries = 8;
	static constexpr int kMaxReasserts = 2;

	PendingFacing pending_[256] = {};
	int pendingCount_ = 0;
};
