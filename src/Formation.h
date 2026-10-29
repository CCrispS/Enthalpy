#pragma once

#include <YRPP.h>

#include <TechnoClass.h>
#include <TacticalClass.h>
#include <Surface.h>
#include <GeneralStructures.h>

// 尤里的复仇「红警3 式列队」插件。
//
// 操作方式：选中单位后，在目的地按下鼠标中键，
// 拖动决定阵型朝向；半透明剪影随之跟随，松开中键后
// 单位驶向各自分配的格子，然后转到「背对落点」的朝向。
// （朝向由阵型方向决定，见 ComputeLayout。）
class FormationController
{
public:
	static FormationController& Instance();

// 每个战术帧调一次，在渲染之前——捕获必须在这里做，否则会画进玩家看到的帧。
	void Update();

// 世界绘制之后调用：只负责把缓存里的剪影贴上去。
	void DrawPreview(bool late);

// 手势过程中返回真，让引擎的点击处理让路；
// 否则松开左键会被当成一次普通移动命令。
	bool ShouldSuppressClick();

// 自检失败（游戏版本不符、hook 站点异常）时整体停用插件：
// 让不兼容的玩家只看到一行日志，而不是弹窗崩溃。
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

// 一个格位计划：分配给某单位的格子，以及格内偏移
// （多个步兵共用一格时用）。
	struct SlotPlan
	{
		int CellX;
		int CellY;
		int OffsetX;
		int OffsetY;
bool Snapped;   // 理想格不可用，这是就近吸附到的合法格（预览标红）
bool StartCell; // 该格只站着「即将离开」的单位，预览用橘黄而非红色
bool Occupied;  // 当前有单位站在这里

// 飞机与跳跳车不落地，剪影画在悬停高度，
// 并从剪影垂直向下画一条引线到目标格。
int AirHeight;  // 距地高度（lepton），0 表示剪影贴地

// 可选的单位自带图像：该单位的 SHP 帧，以半透明绘制，
// 让预览看起来像单位本身而不是一块脚印。
		SHPStruct* Image;
		int Frame;
	};

// 布局时已分配的格子，保证两个单位不会抢同一格
// （步兵例外，一格最多三人）。
	struct CellClaim
	{
		int CellX;
		int CellY;
		int InfantryCount;
		bool UsedByVehicle;
	};

// 到位后还需要转向的单位记录。
	struct PendingFacing
	{
		TechnoClass* Unit;
		int CellX;
		int CellY;
		DirStruct Facing;
		int FramesLeft;
		bool Applied;
// 确认它确实开始朝格位行驶过（Used 防止刚下单时被误判为「没动」）。
	bool SeenMoving;
// 连续「静止」帧数，用来滤掉移动模拟器的单帧抖动。
	int StillFrames;
		int TurnAttempts;
	};

// 一张剪影掩码：按（单位类型、朝向）捕获一次，
// 之后所有同类型同朝向的格位复用。逐个格位实时绘制既慢，
// 也是早期「某些单位干脆没有剪影」的原因。
	//
// 宽度要容得下最宽的侧向载具剪影：引擎画的坦克明显比一格宽，
// 捕获框太小就会只拿到半个剪影。
	static constexpr int kGhostWidth = 192;
	static constexpr int kGhostHeight = 256;
	static constexpr int kGhostHalfWidth = 96;
	static constexpr int kGhostTopAbove = 152;

	struct GhostPatch
	{
		TechnoTypeClass* Type;
// 只保留轮廓：体素在正常渲染流程之外被绘制时会变成纯黑，
// 所以颜色没有信息量，只有掩码有用。
		unsigned char Mask[kGhostWidth * kGhostHeight];
// 关掉该单位阴影后捕获的同一剪影；只在 Mask 里出现的像素
// 就是美术自带的影子，绘制时加深；GhostShadow=no 时直接不画。
// （同上：影子行的处理策略。）
		unsigned char BodyMask[kGhostWidth * kGhostHeight];
// 美术本身是否画了影子（Mask 里不在 BodyMask 的像素）；飞机与
// 体素通常没有，所以地面影子要自己补。
		bool HasShadow;
	bool WaterPatch = false;   // 该补丁是在水面格上捕获的：影子不可信，改用合成椭圆
int Top;   // 原始像素捕获时的表面偏移
		int Left;
// 单位「脚点」在补丁内的位置（相对补丁左上角）——它就是借画时
// 单位被画上去的那个点，因此块状阵型旋转时剪影原地转，
// 而不是跟着美术包围盒滑动。
		int FootX;
		int FootY;
int MaskHeight;   // 剪影实际高度
int MaskWidth;    // 剪影实际宽度
	};

// 缓存条目：每个（类型、朝向）一份。捕获要花一次引擎绘制和一次全框扫描，
// 所以只做一次，之后整个会话复用；复用也保证了拖动中
// 步兵剪影保持同一个姿势而不是每帧换动作。
	struct GhostCacheEntry
	{
		TechnoTypeClass* Type;
unsigned short Facing;   // DirStruct 值，量化到 32 个方向
// 平地为 0：平地上剪影贴在哪都一样。斜坡会按「单位所在格的坡度」倾斜，
// 所以斜坡目标格要按格坐标单独作键，朝向变了还要重捕。
// （同上：斜坡缓存键。）
		int CellKey;
int LastUse;             // 最近一次被使用的帧号，用于淘汰
		GhostPatch Patch;
	};

	static constexpr int kGhostCacheSize = 48;

// 光标在朝向档边界轻轻抖动时，不能让阵型每帧翻转：
// 每次翻转都会重捕剪影，看起来就是闪烁。
static constexpr int kFacingDeadband = 1638;   // 约 9 度（1/65536 圈为单位）

	bool CellOnlyHoldsMovers(CellClass* pCell) const;

	static bool IsWorldPointValid(const CoordStruct& world);
	static bool ScreenDirectionToWorld(double screenX, double screenY,
		double& worldX, double& worldY);

	void GatherSelectedUnits();
	void ComputeLayout();
	void CommitOrders();
	void ServicePendingFacings();


// 把某个单位的美术捕获成补丁（借引擎画一次），
// 以及把已捕获的补丁贴到表面某个屏幕位置。
// 下面的借画-差集流程会在借来的对象上跑引擎代码，因此外面包了 SEH，
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
	int captureFailures_ = 0;
	int captureBlockedUntil_ = 0;

	State state_ = State::Idle;
	int cooldownFrames_ = 0;
	int suppressFrames_ = 0;
	bool wasSuppressing_ = false;

	// Advances once per frame; keeps engine-side ghost capture to one per frame.
	int frameId_ = 0;
	int lateDrawFrame_ = -1;
	int seenDrawer_ = 0;
	int expectedDrawer_ = 0;
	int drawStallFrames_ = 0;
	bool drewThisFrame_ = false;

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
	bool released_[256] = {};
	bool stopLogged_[256] = {};
	bool releaseRequested_ = false;
	int releaseGuard_ = 0;
	int permanentCount_ = 0;
	DWORD lastPinLog_ = 0;
	int keepTries_[256] = {};
	static constexpr int kMaxKeepTries = 8;
	static constexpr int kMaxReasserts = 2;

	PendingFacing pending_[256] = {};
	int pendingCount_ = 0;
};
