// Enthalpy —— 尤里的复仇「红色警戒3 式列队」独立 Syringe 插件。
//
// 每帧的执行顺序：
//   1. Update()        读鼠标、跑手势状态机、把已到位的单位钉在格子里、驱动最终朝向；
//   2. ComputeLayout() 把光标算成「每个单位一个目标格」（障碍、占位、步兵子格、飞行高度）；
//   3. PrepareGhosts() 在帧渲染之前把剪影缓存填好；
//   4. DrawPreview()   只贴缓存，绝不在渲染过程中捕获；
//   5. CommitOrders()  松手时下单，并给 keeper（保管者）上膛。
//
// 剪影不是手画的：CaptureUnitGhostRaw 借引擎自己画一遍单位到战术表面，
// 用哨兵色做差集得到两张位掩码，之后由 DrawPreview 混合上屏——所以捕获代码看着很奇怪。
//
// 本文件曾因一次编辑事故被机械重建，注释全部丢失；以下注释是事后按原意补写的，
// 讲的是「为什么这么做」，不只是「做了什么」。

#include "Formation.h"
#include "Log.h"
#include "Settings.h"
#include "ShpGhost.h"
#include <ConvertClass.h>
#include <InfantryTypeClass.h>
#include <ObjectTypeClass.h>
#include <UnitClass.h>
#include <cmath>
#include <climits>
#include <cstring>

namespace
{
	// Lightweight frame profiler. The reported lag is view dependent, so we want numbers for the
	// capture pass and for the silhouette blit instead of guesses.
	double QpcMs()
	{
		static LARGE_INTEGER freq = { 0 };
		if (freq.QuadPart == 0)
			QueryPerformanceFrequency(&freq);
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return static_cast<double>(now.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
	}

	double gCaptureMs = 0.0;
	double gDrawMs = 0.0;
	int gDrawCalls = 0;
	int gUpdateCalls = 0;
	int gDrawPasses = 0;

	struct ScopedMs
	{
		double* pTarget;
		double started;
		explicit ScopedMs(double* target) : pTarget(target), started(QpcMs()) {}
		~ScopedMs() { *pTarget += QpcMs() - started; }
	};
}
namespace{
constexpr DWORD kColorSlotFill = 0x03E0;
   
constexpr DWORD kColorSlotLine = 0x07E0;
   
constexpr DWORD kColorBadFill = 0xA000;
    
constexpr DWORD kColorBadLine = 0xF800;
    
constexpr DWORD kColorStartFill = 0xC300;
  
constexpr DWORD kColorStartLine = 0xFD20;
  
constexpr DWORD kColorArrow = 0xFFFF;
      
constexpr DWORD kColorFacing = 0x03E0;
     
constexpr int kMaxUnits = 256;
constexpr int kMaxPerRank = 8;
constexpr int kMaxFilledSlots = 48;
constexpr int kMaxVoxelGhosts = 6;
        
constexpr int kCooldownFrames = 45;
// Byte written into the Z buffer area a silhouette is captured over: nothing in the world
// may occlude the borrowed draw, or the ghost inherits the hole.
constexpr unsigned char kZBufferFarByte = 0xFF;
constexpr int kCapturesPerFrame = 3;
constexpr DWORD kColorGroundShadow = 0x0000;
constexpr DWORD kColorGhostShadow = 0x0320;

// The capture box is flooded with this colour before the borrowed draw, so the mask means
// 'pixels the unit painted' instead of 'pixels that differ from the last frame'.  Diffing
// against the last frame let trees, buildings and units sitting on the capture spot punch
// holes into the silhouette, and since the shape is cached per type those holes then showed
// up on every ghost of that type.
constexpr DWORD kCaptureSentinel = 0xF81F;
// dark shade of the ghost green
   
constexpr int kLeptonPerCell = 256;
constexpr int kHalfCell = 128;
constexpr int kSnapSearchRadius = 5;
constexpr int kArrivalRadius = 260;
constexpr int kArrivalFrames = 900;
constexpr int kFacingTurnFrames = 600;
constexpr int kFacingNearRadius = 520; // two cells: a unit parked a cell off still turns
constexpr int kFacingStillFrames = 45;  // settle ticks before claiming the pose
// Aircraft keep re-deriving their facing from the flight model, so a pinned facing has to be
// re-applied for a while instead of being set once.
constexpr int kAirFacingFrames = 90;
        
constexpr double kPi = 3.14159265358979323846;
int IRound(double value)
{
return static_cast<int>(value < 0.0 ? value - 0.5 : value + 0.5);
}
double Clamp(double value, double low, double high)
{
return value < low ? low : (value > high ? high : value);
}
inline DWORD GreenGhost(DWORD colour, DWORD tint){
const int r = static_cast<int>((colour >> 11) & 0x1F);
const int g = static_cast<int>((colour >> 5) & 0x3F);
const int b = static_cast<int>(colour & 0x1F);
const int luma = (r * 3 + g * 7 + b * 2) / 12;
      
const int tr = static_cast<int>((tint >> 11) & 0x1F);
const int tg = static_cast<int>((tint >> 5) & 0x3F);
const int tb = static_cast<int>(tint & 0x1F);
const int lifted = (luma * 5) / 4 + 12;
int outR = (lifted * tr) / 200;
int outG = (lifted > 63) ? 63 : lifted;
int outB = (lifted * tb) / 200;
if (outR > 31) outR = 31;
if (outG > 63) outG = 63;
if (outB > 31) outB = 31;
return static_cast<DWORD>((outR << 11) | (outG << 5) | outB);
}
DWORD Blend565(DWORD dst, DWORD src, int srcEighths)
{
const int dstR = (dst >> 11) & 0x1F, dstG = (dst >> 5) & 0x3F, dstB = dst & 0x1F;
const int srcR = (src >> 11) & 0x1F, srcG = (src >> 5) & 0x3F, srcB = src & 0x1F;
const int inv = 8 - srcEighths;
const int r = (dstR * inv + srcR * srcEighths) >> 3;
const int g = (dstG * inv + srcG * srcEighths) >> 3;
const int b = (dstB * inv + srcB * srcEighths) >> 3;
return static_cast<DWORD>((r << 11) | (g << 5) | b);
}
void FillQuadTinted(Surface* pSurface, const Point2D* points, DWORD tint, int srcEighths,
int minX, int minY, int maxX, int maxY)
{
for (int triangle = 0; triangle < 2; ++triangle)
{
const Point2D& a = points[0];
const Point2D& b = points[triangle + 1];
const Point2D& c = points[triangle + 2];
const int triangleMinY = std::min(a.Y, std::min(b.Y, c.Y));
const int triangleMaxY = std::max(a.Y, std::max(b.Y, c.Y));
for (int y = std::max(triangleMinY, minY); y <= std::min(triangleMaxY, maxY); ++y)
{
int leftX = INT_MAX;
int rightX = INT_MIN;
const Point2D* edge[3][2] = {
 {
 &a, &b }
, {
 &b, &c }
, {
 &c, &a }
 }
;
for (int e = 0; e < 3; ++e)
{
const Point2D* p0 = edge[e][0];
const Point2D* p1 = edge[e][1];
if (p0->Y == p1->Y)
continue;
const int lowY = std::min(p0->Y, p1->Y);
const int highY = std::max(p0->Y, p1->Y);
if (y < lowY || y > highY)
continue;
const double t = static_cast<double>(y - p0->Y) / static_cast<double>(p1->Y - p0->Y);
const int x = IRound(p0->X + (p1->X - p0->X) * t);
leftX = std::min(leftX, x);
rightX = std::max(rightX, x);
}
if (leftX > rightX)
continue;
for (int x = std::max(leftX, minX); x <= std::min(rightX, maxX); ++x)
{
Point2D pixel = {
 x, y }
;
const DWORD existing = pSurface->GetPixel(&pixel);
pSurface->SetPixel(&pixel, Blend565(existing, tint, srcEighths));
}
}
}
}
CoordStruct CellCentre(int cellX, int cellY, int offsetX, int offsetY)
{
CellStruct cell = {
 static_cast<short>(cellX), static_cast<short>(cellY) }
;
CoordStruct result = CellClass::Cell2Coord(cell, 0);
result.X += offsetX;
result.Y += offsetY;
return result;
}
// 取格子中心的世界坐标，含地形层高与桥面高度（GetCoordsWithBridge），
// 这样斜坡上的剪影贴着坡面，而不是贴在坡下的平地。
bool CellWorldPos(int cellX, int cellY, int offsetX, int offsetY, CoordStruct& out)
{
if (!MapClass::Instance)
return false;
CellStruct cell = {
 static_cast<short>(cellX), static_cast<short>(cellY) }
;
CellClass* pCell = MapClass::Instance->TryGetCellAt(cell);
if (!pCell)
return false;
out = pCell->GetCoordsWithBridge();
out.X += offsetX;
out.Y += offsetY;
return true;
}
// 判断这个格子能不能站这个单位，两件独立的事：
//   * 单位本身能不能在那——水/陆按单位的 SpeedType 判，不按地形图例，所以船不会上草地、
//     坦克不会下水；
//   * 格子是不是被挡住——只有建筑和地形对象（树、石）算障碍，站在那儿的单位不算，
//     因为列队本身会把它们调走。
bool IsUsableCell(CellClass* pCell, TechnoClass* pUnit)
{
	if (!pCell)
		return false;

	const LandType land = pCell->LandType;
	if (land == LandType::Rock || land == LandType::Wall)
		return false;

	// Movement class decides water, not the map legend: a ship only floats, an amphibious or
	// hover unit goes anywhere, a winged unit ignores the ground and everything else must stay
	// on dry land. Without this a formation dropped near the shore painted land cells green for
	// ships and water cells green for tanks. SpeedType::FloatBeach is the beaching variant and
	// is allowed both on water and on beach, which is what LandType::Beach already means.
	const SpeedType speed = (pUnit && pUnit->GetTechnoType())
		? pUnit->GetTechnoType()->SpeedType : SpeedType::Track;
	bool allowWater = false;
	bool allowLand = true;
	switch (speed)
	{
	case SpeedType::Float:
		allowWater = true;
		allowLand = false;
		break;
	case SpeedType::FloatBeach:
		allowWater = true;
		allowLand = (land == LandType::Beach);
		break;
	case SpeedType::Amphibious:
	case SpeedType::Hover:
	case SpeedType::Winged:
		allowWater = true;
		break;
	default:
		break;
	}
	if ((land == LandType::Water) && !allowWater)
		return false;
	if ((land != LandType::Water) && !allowLand)
		return false;

	// Only real obstacles make a cell illegal: a building, or a terrain object such as a tree
	// or a rock. Units and infantry that merely stand on the cell are not obstacles - the
	// formation orders them away - so rejecting those cells painted dense rows red even though
	// the units could walk straight onto them.
	if (pCell->GetBuilding())
		return false;
	for (ObjectClass* pObject = pCell->FirstObject; pObject; pObject = pObject->NextObject)
	{
		const AbstractType what = pObject->WhatAmI();
		if (what == AbstractType::Building || what == AbstractType::Terrain)
			return false;
	}
	return true;
}

}
FormationController& FormationController::Instance(){
static FormationController instance;
return instance;
}
bool FormationController::BothMouseButtonsDown(){
const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
return left && right;
}

bool FormationController::RightButtonDown(){
	return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

bool FormationController::MiddleButtonDown(){
	return (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
}
// 用 Win32 找游戏窗口，而不是 WWMouseClass：这个 build 里引擎的鼠标对象与头文件布局
// 不一致，按头文件写会踩坏栈（早期就是这么崩的）。
HWND FormationController::GameWindow(){
static HWND hwnd = nullptr;
if (hwnd && IsWindow(hwnd))
return hwnd;
hwnd = nullptr;
EnumWindows([](HWND window, LPARAM context) -> BOOL
{
DWORD pid = 0;
GetWindowThreadProcessId(window, &pid);
if (pid != GetCurrentProcessId() || !IsWindowVisible(window))
return TRUE;
char title[128] = {
};
GetWindowTextA(window, title, static_cast<int>(sizeof(title)) - 1);
if (std::strstr(title, "Yuri") || std::strstr(title, "Command"))
{
*reinterpret_cast<HWND*>(context) = window;
return FALSE;
}
return TRUE;
}, reinterpret_cast<LPARAM>(&hwnd));
return hwnd;
}
bool FormationController::GameWindowRect(RECT& out){
HWND hwnd = GameWindow();
if (!hwnd)
return false;
return GetClientRect(hwnd, &out) != FALSE;
}
bool FormationController::CursorClientPos(Point2D& out){
HWND hwnd = GameWindow();
if (!hwnd)
return false;
POINT point = {
 0, 0 }
;
if (!GetCursorPos(&point) || !ScreenToClient(hwnd, &point))
return false;
out.X = point.x;
out.Y = point.y;
return true;
}
// 光标转世界坐标。引擎的 ClientToCoords 只解算平面，所以先把结果抬到所在格的高度，
// 再反投影回屏幕用残差修正，迭代几轮取残差最小的那次。另外做了一格的迟滞，
// 避免光标压在格子边界上时布局来回抖。
bool FormationController::CursorWorldPos(CoordStruct& out){
if (!TacticalClass::Instance)
return false;
Point2D point = {
 0, 0 }
;
if (!CursorClientPos(point))
return false;
Point2D probe = point;
CoordStruct world = {
 0, 0, 0 }
;
CoordStruct best = {
 0, 0, 0 }
;
int bestResidual = INT_MAX;
bool haveBest = false;
for (int attempt = 0; attempt < 3; ++attempt)
{
ClientToWorld(probe, world);
const CellStruct cell = CellClass::Coord2Cell(world);
CoordStruct ground = {
 0, 0, 0 }
;
if (!CellWorldPos(cell.X, cell.Y, 0, 0, ground))
return false;
world.Z = ground.Z;
Point2D drawn = {
 0, 0 }
;
TacticalClass::Instance->CoordsToClient(&world, &drawn);
const int dx = point.X - drawn.X;
const int dy = point.Y - drawn.Y;
const int residual = std::abs(dx) + std::abs(dy);
if (!haveBest || residual < bestResidual)
{
best = world;
bestResidual = residual;
haveBest = true;
}
if (dx == 0 && dy == 0)
break;
probe.X = static_cast<short>(probe.X + dx);
probe.Y = static_cast<short>(probe.Y + dy);
}
if (!haveBest)
return false;
out = best;
return true;
}
// 引擎 ABI：这个 build 的 ClientToCoords 是「目标 CoordStruct 在前、屏幕点在后」，
// 与 YRpp 的声明正好相反。这里故意用相反的指针类型互转，请不要「顺手修正」。
void FormationController::ClientToWorld(const Point2D& client, CoordStruct& world){
if (!TacticalClass::Instance)
return;
TacticalClass::Instance->ClientToCoords(
reinterpret_cast<Point2D*>(&world),
reinterpret_cast<CoordStruct*>(const_cast<Point2D*>(&client)));
}
int FormationController::CollectSelected(TechnoClass** buffer, int capacity){
int count = 0;
if (!TechnoClass::Array)
return 0;
for (int index = 0; index < TechnoClass::Array->Count; ++index)
{
if (count >= capacity)
break;
TechnoClass* pTechno = TechnoClass::Array->Items[index];
if (!pTechno || !pTechno->IsSelected)
continue;
if (pTechno->WhatAmI() == AbstractType::Building)
continue;
buffer[count++] = pTechno;
}
return count;
}
bool FormationController::WithinCell(TechnoClass* pTechno, int cellX, int cellY)
{
	if (!pTechno)
		return false;
	return pTechno->GetCell() != nullptr && pTechno->GetCell()->MapCoords.X == static_cast<short>(cellX) && pTechno->GetCell()->MapCoords.Y == static_cast<short>(cellY);
}

bool FormationController::IsAlive(TechnoClass* pTechno){
if (!pTechno || !TechnoClass::Array)
return false;
for (int index = 0; index < TechnoClass::Array->Count; ++index)
{
if (TechnoClass::Array->Items[index] == pTechno)
return true;
}
return false;
}
// 选择快照：一次手势从头到尾都作用在「按下时选中的那批单位」上，
// 即使中途引擎把选择清掉了也照样工作。
void FormationController::GatherSelectedUnits(){
unitCount_ = CollectSelected(units_, kMaxUnits);
for (int i = 1; i < unitCount_; ++i)
{
TechnoClass* key = units_[i];
int j = i - 1;
while (j >= 0 && units_[j] > key)
{
units_[j + 1] = units_[j];
--j;
}
units_[j + 1] = key;
}
if (unitCount_ == 0 && Settings::Current().FormationSelectSaved)
{
for (int index = 0; index < snapshotCount_ && unitCount_ < kMaxUnits; ++index)
{
if (IsAlive(snapshot_[index]))
units_[unitCount_++] = snapshot_[index];
}
}}

bool FormationController::CellOnlyHoldsMovers(CellClass* pCell) const{
if (!pCell)
return false;
bool any = false;
for (ObjectClass* pObject = pCell->FirstObject; pObject; pObject = pObject->NextObject)
{
any = true;
bool isMover = false;
for (int index = 0; index < unitCount_; ++index)
{
if (units_[index] == pObject)
{
isMover = true;
break;
}
}
if (!isMover)
return false;
}
return any;
}
bool FormationController::IsWorldPointValid(const CoordStruct& world){
return world.X != 0 || world.Y != 0 || world.Z != 0;
}
bool FormationController::ScreenDirectionToWorld(double screenX, double screenY,
double& worldX, double& worldY){
if (!DSurface::Composite || !TacticalClass::Instance)
return false;
int tacticalWidth = DSurface::Composite->GetWidth();
if (tacticalWidth <= 0)
return false;
RECT clientRect = {
 0, 0, 0, 0 }
;
if (!GameWindowRect(clientRect))
return false;
const int clientHeight = clientRect.bottom - clientRect.top;
if (clientHeight <= 0)
return false;
if (tacticalWidth > clientRect.right - clientRect.left)
tacticalWidth = clientRect.right - clientRect.left;
Point2D base = {
 static_cast<short>(tacticalWidth / 2),
static_cast<short>(clientHeight / 2) }
;
Point2D alongX = {
 static_cast<short>(base.X + 32), base.Y }
;
Point2D alongY = {
 base.X, static_cast<short>(base.Y + 32) }
;
CoordStruct baseWorld = {
 0, 0, 0 }
;
CoordStruct xWorld = {
 0, 0, 0 }
;
CoordStruct yWorld = {
 0, 0, 0 }
;
ClientToWorld(base, baseWorld);
ClientToWorld(alongX, xWorld);
ClientToWorld(alongY, yWorld);
if (!IsWorldPointValid(baseWorld) || !IsWorldPointValid(xWorld)
|| !IsWorldPointValid(yWorld))
{
return false;
}
const double axisXX = (xWorld.X - baseWorld.X) / 32.0;
const double axisXY = (xWorld.Y - baseWorld.Y) / 32.0;
const double axisYX = (yWorld.X - baseWorld.X) / 32.0;
const double axisYY = (yWorld.Y - baseWorld.Y) / 32.0;
worldX = axisXX * screenX + axisYX * screenY;
worldY = axisXY * screenX + axisYY * screenY;
return true;
}
// Aircraft fly, jumpjet infantry and hover vehicles never really touch the ground: for those
// the ghost belongs in the air, at the altitude the unit holds (or will hold) above its cell.
static constexpr int kAirThreshold = 96;


// 飞行单位的显示高度：飞机取 Rules 的 FlightLevel，跳跳车按它悬停格上方的高度差判断。
static int AirHeightFor(TechnoClass* pUnit)
{
	if (!pUnit)
		return 0;

	const CoordStruct location = pUnit->Location;

	CellClass* pCell = MapClass::Instance ? MapClass::Instance->TryGetCellAt(location) : nullptr;
	const int measured = pCell ? (location.Z - pCell->GetCoordsWithBridge().Z) : 0;

	if (pUnit->WhatAmI() == AbstractType::Aircraft)
	{
		RulesClass* pRules = RulesClass::Instance;
		if (pRules && pRules->FlightLevel > kAirThreshold)
			return pRules->FlightLevel;
	}

	return (measured > kAirThreshold) ? measured : 0;
}
// Aircraft and jumpjets hang in the air, so their ghost shadow belongs on the ground of the
// target cell.  Voxel art carries no shadow pixels of its own, in which case a soft ellipse
// is stamped under the cell instead.
static void DrawGroundShadow(Surface* pSurface, const Point2D& centre, int width, int height,
// 把缓存里的剪影混合到表面。opacity 以「八分之一」为单位（GhostOpacity 0..4 映射到 0..8），
// 描边用同样权重，这样每一档都看得出差别。layers 是位掩码：bit0 画本体、bit1 画影子——
// 飞行单位本体画在飞行高度、影子画在地面，所以要分两次贴。
	int opacity, int clipWidth, int clipHeight)
{
	if (!pSurface || opacity <= 0)
		return;

	// width and height are the full size of the ellipse in pixels.
	const int radiusX = (width / 2 > 0) ? (width / 2) : 1;
	const int radiusY = (height / 2 > 0) ? (height / 2) : 1;
	const int shadowOpacity = (opacity * 3) / 4;
	Point2D pixel = {
 0, 0 }
;
	for (int dy = -radiusY; dy <= radiusY; ++dy)
	{
		const int y = centre.Y + dy;
		if (y < 0 || y >= clipHeight)
			continue;
		const double ratio = static_cast<double>(dy * dy) / static_cast<double>(radiusY * radiusY);
		const int span = IRound(radiusX * std::sqrt(1.0 - ratio));
		for (int dx = -span; dx <= span; ++dx)
		{
			const int x = centre.X + dx;
			if (x < 0 || x >= clipWidth)
				continue;
			pixel.X = static_cast<short>(x);
			pixel.Y = static_cast<short>(y);
			const DWORD under = pSurface->GetPixel(&pixel);
			pSurface->SetPixel(&pixel, Blend565(under, kColorGhostShadow, shadowOpacity));
		}
	}
}
// 按当前光标位置，为每个选中单位算一个目标格。顺序很重要：
// 锚点所在的格是第一个单位的理想格；有障碍时环形搜索最近的合法格；被同队其它格位先占的
// 格子只是把当前格位顺移（不会标红——红表示地面本身不可用）；步兵一格三人，格内位置直接
// 问引擎的 FindInfantrySubposition；飞行单位额外得到飞行高度和地面影子格。
void FormationController::ComputeLayout(){
CoordStruct candidate = {
 0, 0, 0 }
;
if (CursorWorldPos(candidate) && IsWorldPointValid(candidate))
{
const CellStruct candidateCell = CellClass::Coord2Cell(candidate);
const bool sameCell = hasCursorCell_
&& candidateCell.X == cursorCellX_ && candidateCell.Y == cursorCellY_;
if (!sameCell)
{
cursorWorld_ = candidate;
cursorCellX_ = candidateCell.X;
cursorCellY_ = candidateCell.Y;
hasCursorCell_ = true;
Point2D client = {
 0, 0 }
;
if (CursorClientPos(client))
cursorClient_ = client;
}
else
{
Point2D client = {
 0, 0 }
;
if (CursorClientPos(client))
cursorClient_ = client;
}
}
const double deltaX = static_cast<double>(cursorWorld_.X - anchor_.X);
const double deltaY = static_cast<double>(cursorWorld_.Y - anchor_.Y);
const double length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
if (length > 32.0)
{
dragX_ = deltaX / length;
dragY_ = deltaY / length;
const double screenDx = static_cast<double>(cursorClient_.X - anchorClient_.X);
const double screenDy = static_cast<double>(cursorClient_.Y - anchorClient_.Y);
double worldX = 0.0;
double worldY = 0.0;
const bool mapped = ScreenDirectionToWorld(-screenDy, screenDx, worldX, worldY);
const double mappedLength = std::sqrt(worldX * worldX + worldY * worldY);
if (mapped && mappedLength > 0.0)
{
facingX_ = worldX / mappedLength;
facingY_ = worldY / mappedLength;
}
else
{
facingX_ = dragX_;
facingY_ = dragY_;
}
const double angle = std::atan2(facingY_, facingX_);
const int wanted = IRound(angle / (2.0 * kPi) * 65536.0) & 0xFFFF;
const int current = static_cast<int>(facingDir_.value()) & 0xFFFF;
int difference = (wanted - current) & 0xFFFF;
if (difference >= 0x8000)
difference -= 0x10000;
if (std::abs(difference) >= kFacingDeadband)
facingDir_ = DirStruct(wanted);
}
spacingCells_ = 1;
if (length > 4.0 * kLeptonPerCell)
spacingCells_ = 2;
if (length > 8.0 * kLeptonPerCell)
spacingCells_ = 3;
int cellOrdinal[kMaxUnits] = {
 0 }
;
int cellCount = 0;
int infantryInCell = 0;
for (int index = 0; index < unitCount_; ++index)
{
const bool infantry = units_[index] && units_[index]->WhatAmI() == AbstractType::Infantry;
if (infantry)
{
if (infantryInCell == 0)
++cellCount;
cellOrdinal[index] = cellCount - 1;
infantryInCell = (infantryInCell + 1) % 3;
}
else
{
cellOrdinal[index] = cellCount;
++cellCount;
infantryInCell = 0;
}
}
if (cellCount < 1)
cellCount = 1;
columns_ = cellCount < kMaxPerRank ? cellCount : kMaxPerRank;
if (columns_ < 1)
columns_ = 1;
ranks_ = (cellCount + columns_ - 1) / columns_;
claimCount_ = 0;
slotCount_ = 0;
const double rowX = facingX_;
const double rowY = facingY_;
const double backX = -dragX_;
const double backY = -dragY_;
for (int index = 0; index < unitCount_; ++index)
{
const int cell = cellOrdinal[index];
const int rank = cell / columns_;
const int column = cell % columns_;
const int remaining = cellCount - rank * columns_;
const int inRank = remaining < columns_ ? remaining : columns_;
const double sideways = static_cast<double>(column) - (inRank - 1) * 0.5;
const double step = static_cast<double>(spacingCells_ * kLeptonPerCell);
const double worldX = anchor_.X + rowX * sideways * step + backX * rank * step;
const double worldY = anchor_.Y + rowY * sideways * step + backY * rank * step;
// Let the engine do the conversion: the manual version rounded, which shifts every
// slot by half a cell and can land the cell index on the neighbouring (sometimes
// illegal) cell depending on the direction of the row.
const CellStruct ideal = CellClass::Coord2Cell(CoordStruct{
static_cast<int>(worldX), static_cast<int>(worldY), anchor_.Z });
const bool isInfantry = units_[index] && units_[index]->WhatAmI() == AbstractType::Infantry;
int chosenX = ideal.X;
int chosenY = ideal.Y;
bool snapped = false;
int claimIndex = -1;
for (int c = 0; c < claimCount_; ++c)
{
if (claims_[c].CellX == chosenX && claims_[c].CellY == chosenY)
{
claimIndex = c;
break;
}
}
CellClass* pIdealCell = MapClass::Instance->TryGetCellAt(CellStruct{
static_cast<short>(chosenX), static_cast<short>(chosenY) }
);
	bool idealOk = IsUsableCell(pIdealCell, units_[index]);
// Two very different reasons land here: the ground itself refuses the cell (building,
// tree, water, rock) or another slot of this same formation already claimed it. Only
// the first one is worth a red ghost - a claimed cell is empty ground that one of the
// selected units simply occupies for now, and painting it red was the "empty cell shown
// as illegal" complaint.
const bool blockedByObstacle = !idealOk && !CellOnlyHoldsMovers(pIdealCell);
bool startCell = false;
if (!idealOk && CellOnlyHoldsMovers(pIdealCell))
{
idealOk = true;
startCell = true;
}
bool occupied = pIdealCell && pIdealCell->FirstObject != nullptr;
if (claimIndex >= 0)
{
idealOk = isInfantry && !claims_[claimIndex].UsedByVehicle
&& claims_[claimIndex].InfantryCount < 3;
}
if (!idealOk)
{
// Keep the ghost green unless the ground itself is the problem; a neighbour cell is
// picked either way so two units never target the same square.
snapped = blockedByObstacle;
bool found = false;
for (int radius = 1; radius <= kSnapSearchRadius && !found; ++radius)
{
int bestDistance = INT_MAX;
int bestX = chosenX;
int bestY = chosenY;
for (int offsetY = -radius; offsetY <= radius; ++offsetY)
{
for (int offsetX = -radius; offsetX <= radius; ++offsetX)
{
if (std::abs(offsetX) != radius && std::abs(offsetY) != radius)
continue;
const int candidateX = ideal.X + offsetX;
const int candidateY = ideal.Y + offsetY;
CellClass* pCandidate = MapClass::Instance->TryGetCellAt(CellStruct{
static_cast<short>(candidateX), static_cast<short>(candidateY) }
);
				if (!IsUsableCell(pCandidate, units_[index]) && !CellOnlyHoldsMovers(pCandidate))
continue;
bool taken = false;
for (int c = 0; c < claimCount_; ++c)
{
if (claims_[c].CellX == candidateX && claims_[c].CellY == candidateY)
{
taken = !(isInfantry && !claims_[c].UsedByVehicle
&& claims_[c].InfantryCount < 3);
break;
}
}
if (taken)
continue;
const int distance = offsetX * offsetX + offsetY * offsetY;
if (distance < bestDistance)
{
bestDistance = distance;
bestX = candidateX;
bestY = candidateY;
found = true;
}
}
}
if (found)
{
chosenX = bestX;
chosenY = bestY;
}
}
if (!found)
snapped = false;
   
}
int freeClaim = -1;
for (int c = 0; c < claimCount_; ++c)
{
if (claims_[c].CellX == chosenX && claims_[c].CellY == chosenY)
{
freeClaim = c;
break;
}
}
if (freeClaim < 0 && claimCount_ < kMaxUnits)
{
freeClaim = claimCount_++;
claims_[freeClaim].CellX = chosenX;
claims_[freeClaim].CellY = chosenY;
claims_[freeClaim].InfantryCount = 0;
claims_[freeClaim].UsedByVehicle = false;
}
if (freeClaim >= 0)
{
if (isInfantry)
++claims_[freeClaim].InfantryCount;
else
claims_[freeClaim].UsedByVehicle = true;
}
int offsetX = 0;
int offsetY = 0;
if (isInfantry && freeClaim >= 0)
{
const int slot = claims_[freeClaim].InfantryCount - 1;
const CellStruct claimCell = {
 static_cast<short>(claims_[freeClaim].CellX),
static_cast<short>(claims_[freeClaim].CellY) }
;
const CoordStruct base = CellClass::Cell2Coord(claimCell, 0);
if (CellClass* pClaimCell = MapClass::Instance
? MapClass::Instance->TryGetCellAt(claimCell) : nullptr)
{
			// Positions measured from the engine own sub-cell probe: the three soldiers of a
			// cell do not stand in a row.  CellClass::FindInfantrySubposition answers from the
			// cell live contents, so it cannot be asked for a plan ahead of time; these are
			// the offsets it reports for the first three sub-cells of an empty cell.
			static const int kSubCell[3][2] = {{ 64, -64 }, { 64, 64 }, { -64, 64 } };// centre, then the two back corners;
			offsetX = kSubCell[(slot < 3) ? slot : 0][0];
			offsetY = kSubCell[(slot < 3) ? slot : 0][1];
Logf("subslot: cell=(%d,%d) slot=%d offset=(%d,%d)",
claimCell.X, claimCell.Y, slot, offsetX, offsetY);
}
}
slots_[slotCount_].CellX = chosenX;
slots_[slotCount_].CellY = chosenY;
slots_[slotCount_].OffsetX = offsetX;
slots_[slotCount_].OffsetY = offsetY;
slots_[slotCount_].Snapped = snapped;
slots_[slotCount_].StartCell = startCell;
slots_[slotCount_].Occupied = occupied;
slots_[slotCount_].AirHeight = AirHeightFor(units_[index]);
slots_[slotCount_].Image = nullptr;
slots_[slotCount_].Frame = 0;
++slotCount_;
}
{
static int lastCount = -1;
static int lastColumns = -1;
static int lastRanks = -1;
static int lastSpacing = -1;
static unsigned short lastFacing = 0;
static int lastCellX = 0;
static int lastCellY = 0;
const int firstCellX = slotCount_ > 0 ? slots_[0].CellX : -1;
const int firstCellY = slotCount_ > 0 ? slots_[0].CellY : -1;
const unsigned short facingNow = facingDir_.value<16>();
if (slotCount_ != lastCount || columns_ != lastColumns || ranks_ != lastRanks
|| spacingCells_ != lastSpacing || facingNow != lastFacing
|| firstCellX != lastCellX || firstCellY != lastCellY)
{
Logf("layout change: slots=%d columns=%d ranks=%d spacing=%d facing=%u cell0=(%d,%d)",
slotCount_, columns_, ranks_, spacingCells_, facingNow, firstCellX, firstCellY);
lastCount = slotCount_;
lastColumns = columns_;
lastRanks = ranks_;
lastSpacing = spacingCells_;
lastFacing = facingNow;
lastCellX = firstCellX;
lastCellY = firstCellY;
}
}}

// 真正下单。只 SetDestination 不够：引擎会把上一条 mission 留在队列里，上一次手势残留的
// Move 可能在这次任务结束后才执行，看起来就像「单位自己又跑回去了」。所以必须 ForceMission
// 让新命令立刻成为当前任务。这里同时给 keeper 上膛（trackFrames_、wasHome_、permanent_），
// 并准备 ServicePendingFacings 要用的朝向记录。
void FormationController::CommitOrders(){
	permanentCount_ = 0;
	releaseGuard_ = 90;
	for (int i = 0; i < kMaxUnits; ++i)
	{
		reassertTries_[i] = 0;
		wasHome_[i] = false;
		keepTries_[i] = 0;
		permanent_[i] = false;
		released_[i] = false;
	}
int issued = 0;
pendingCount_ = 0;
for (int index = 0; index < unitCount_ && index < slotCount_; ++index)
{
TechnoClass* pTechno = units_[index];
if (!pTechno)
continue;
			{
				TechnoTypeClass* pCommitType = pTechno->GetTechnoType();
				const bool wantsAreaGuard = pCommitType && pCommitType->DefaultToGuardArea;
				const bool isAircraft = pTechno->WhatAmI() == AbstractType::Aircraft;
				permanent_[index] = wantsAreaGuard || isAircraft;
				if (permanent_[index])
				{
					++permanentCount_;
					Logf("keep: %s %p stays pinned (guardArea=%d aircraft=%d)",
						pCommitType ? pCommitType->ID : "?", pTechno,
						wantsAreaGuard ? 1 : 0, isAircraft ? 1 : 0);
				}
			}
const CellStruct cell = {
 static_cast<short>(slots_[index].CellX),
static_cast<short>(slots_[index].CellY) }
;
CellClass* pCell = MapClass::Instance->TryGetCellAt(cell);
if (!pCell)
continue;
pTechno->SetDestination(pCell, true);
pTechno->ForceMission(Mission::Move);
++issued;
if (pendingCount_ < kMaxUnits)
{
pending_[pendingCount_].Unit = pTechno;
pending_[pendingCount_].CellX = slots_[index].CellX;
pending_[pendingCount_].CellY = slots_[index].CellY;
pending_[pendingCount_].Facing = facingDir_;
pending_[pendingCount_].FramesLeft = kArrivalFrames;
pending_[pendingCount_].Applied = false;
	pending_[pendingCount_].SeenMoving = false;
	pending_[pendingCount_].StillFrames = 0;
++pendingCount_;
}
}
reassertFrames_ = 300;
trackFrames_ = 1500;
suppressFrames_ = 8;
Logf("formation: commit %d/%d orders (columns=%d ranks=%d spacingCells=%d anchor=(%d,%d) facing=%d)",
issued, unitCount_, columns_, ranks_, spacingCells_, anchor_.X, anchor_.Y,
static_cast<int>(facingDir_.value()));
}
// 让每个已到位的单位转到剪影显示的朝向，然后把它从所有「带着目的地」的任务里摘出来（Stop）。
// Guard / Area Guard / Move 都带着一个目的地——也就是它的出发点——引擎于是会把它走回去；
// Stop 不带目的地，所以不会再动。
// 转向按帧请求，引擎才会按单位自身的转向速度平滑转；但一旦到位就停止请求，
// 否则每帧重新请求会让单位原地打转。
void FormationController::ServicePendingFacings(){
	int alive = 0;
	for (int index = 0; index < pendingCount_; ++index)
	{
		PendingFacing& item = pending_[index];
		if (item.FramesLeft <= 0 || !item.Unit)
			continue;
		if (!IsAlive(item.Unit))
		{
			item.FramesLeft = 0;
			continue;
		}
		if (item.Applied)
			continue;

		// Aircraft and jumpjets keep hovering over their cell: the destination never clears
		// and the flight model rewrites the facing every tick, so they are pinned instead.
		const bool airUnit = AirHeightFor(item.Unit) > 0;
		const CoordStruct target = CellCentre(item.CellX, item.CellY, 0, 0);
		const long long dx = static_cast<long long>(item.Unit->Location.X) - target.X;
		const long long dy = static_cast<long long>(item.Unit->Location.Y) - target.Y;
		const long long dist2 = dx * dx + dy * dy;
		// A unit often parks slightly off its cell centre (claimed cell, pathing granularity,
		// infantry sub-slots), so the slot test is two cells wide or those units would
		// silently never turn at all.
		const bool nearSlot = dist2 <= static_cast<long long>(kFacingNearRadius) * kFacingNearRadius;

		bool moving = false;
		if (ILocomotion* pLoco = static_cast<FootClass*>(item.Unit)->Locomotor.operator->())
			moving = pLoco->Is_Moving_Now();

		if (!airUnit)
		{
			if (moving)
			{
				// On the way: wait without spending the budget, so a long march cannot
				// expire the record before the unit gets there.
				item.SeenMoving = true;
				item.StillFrames = 0;
				pending_[alive++] = item;
				continue;
			}
			++item.StillFrames;
			if (!item.SeenMoving)
			{
				// Right after the order the locomotor has not started yet: grace period.
				--item.FramesLeft;
				if (item.FramesLeft > 0)
					pending_[alive++] = item;
				continue;
			}
			if (!nearSlot)
			{
				if (item.StillFrames > kFacingStillFrames)
				{
					Logf("formation: facing dropped for %p (cell %d,%d) dist2=%lld",
						item.Unit, item.CellX, item.CellY, dist2);
					item.FramesLeft = 0;
				}
				else
					pending_[alive++] = item;
				continue;
			}
			if (item.StillFrames <= kFacingStillFrames)
			{
				// It only just stopped: let it settle before claiming the pose.
				pending_[alive++] = item;
				continue;
			}
		}

		if (airUnit)
		{
			item.Unit->Facing.set(item.Facing);
			item.Unit->TurretFacing.set(item.Facing);
			++item.TurnAttempts;
			if (item.TurnAttempts >= kAirFacingFrames)
			{
				item.Applied = true;
				item.FramesLeft = 0;
				Logf("formation: air facing pinned on %p (cell %d,%d)", item.Unit, item.CellX, item.CellY);
			}
		}
		else if (item.Unit->Facing.current() != item.Facing)
		{
			// turn() only sets the target, so the hull rotates at its own ROT and every unit
			// type keeps its own turning speed. Asking again each frame keeps the engine's
			// final-path turn from winning; stopping the moment it matches is what keeps the
			// old endless circling away.
			item.Unit->Facing.turn(item.Facing);
			++item.TurnAttempts;
			if (item.TurnAttempts >= kFacingTurnFrames)
			{
				item.Applied = true;
				item.FramesLeft = 0;
				Logf("formation: facing gave up on %p (cell %d,%d) after %d frames",
					item.Unit, item.CellX, item.CellY, item.TurnAttempts);
			}
		}
		else
		{
			item.Applied = true;
			item.FramesLeft = 0;
			// Position and facing are both final now: hand the unit a Stop so nothing can walk it away.
			item.Unit->ForceMission(Mission::Stop);
			Logf("formation: facing applied to %p (cell %d,%d) facing=%u, stopped",
				item.Unit, item.CellX, item.CellY, static_cast<unsigned>(item.Facing.value<16>()));
		}

		--item.FramesLeft;
		if (item.FramesLeft > 0)
			pending_[alive++] = item;
	}
	pendingCount_ = alive;
}
// 兜底：出现无法恢复的异常就把插件整体停掉，而不是留一个半残的手势干扰玩家。
void FormationController::Disable(const char* reason)
{
if (disabled_)
return;

disabled_ = true;
Logf("formation: disabled - %s", reason ? reason : "unspecified");
}

// 手势状态机 + keeper（保管者）。三个状态：
//   Idle      中键松开，等一次新的按下（且要有选中单位）；
//   Dragging  中键按住，持续重算布局，DrawPreview 负责画；
//   Cooldown  松手之后，用来吞掉这次松开本身产生的那一下点击。
// 下面的 keeper 绝不重新发起行军：它只钉住「已经站在自己格子里」的单位。
void FormationController::Update(){
if (disabled_)
return;
++frameId_;
	++gUpdateCalls;

// If the frame hook keeps firing but the draw hook never does, another plugin owns the
// draw address (Syringe allows one hook per address) and ghosts cannot be painted.  Saying
// so in the log is the difference between "it does nothing" and a bug report.
if (!warnedAboutMissingDraw_ && frameId_ > 900 && drawFrames_ == 0)
{
warnedAboutMissingDraw_ = true;
Logf("formation: WARNING the ghost draw hook never ran; another plugin probably owns 0x6D9134. Ghosts cannot be drawn until that conflict is resolved.");
}
if (cooldownFrames_ > 0)
--cooldownFrames_;
if (suppressFrames_ > 0)

// The engine starts its own drag-select box the moment the left button goes down, which
// leaves a white rectangle racing the formation preview.  Clearing the flag every frame
// while a gesture is in progress keeps the box from ever appearing.
if (state_ == State::Dragging && DisplayClass::Global())
		DisplayClass::Global()->DraggingRectangle = false;
	if (state_ == State::Dragging && DisplayClass::Global())
		DisplayClass::Global()->DraggingRectangle = false;
--suppressFrames_;
ServicePendingFacings();
	{
		ScopedMs perfCapture(&gCaptureMs);
		PrepareGhosts();
	}
PrepareGhosts();
{
static int typeDumpCountdown = 600;
if (typeDumpCountdown > 0 && --typeDumpCountdown == 0 && UnitTypeClass::Array)
{
for (int i = 0; i < UnitTypeClass::Array->Count; ++i)
{
UnitTypeClass* pUnitType = UnitTypeClass::Array->Items[i];
if (!pUnitType)
continue;
Logf("unit type: %s shadowIndex=%d noShadow=%d shp=%d vxl=%d",
pUnitType->get_ID(), pUnitType->ShadowIndex, pUnitType->NoShadow ? 1 : 0,
pUnitType->GetImage() ? 1 : 0, pUnitType->MainVoxel.VXL ? 1 : 0);
}
}
}
const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
const bool rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
// Formation mode is a middle-button gesture: hold it, move the cursor to aim, release to order.
const bool middleDown = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
const bool bothDown = leftDown && rightDown;
if (bothDown)
	gestureLatch_ = true;
if (!rightDown)
	gestureLatch_ = false;
const bool leftPressed = leftDown && !leftWasDown_;
const bool rightPressed = rightDown && !rightWasDown_;
const bool leftReleased = !leftDown && leftWasDown_;
const bool rightReleased = !rightDown && rightWasDown_;
const bool middlePressed = middleDown && !middleWasDown_;
const bool middleReleased = !middleDown && middleWasDown_;
if (reassertFrames_ > 0 && (state_ == State::Cooldown || state_ == State::Idle))
{
--reassertFrames_;
bool allHome = true;
for (int index = 0; index < unitCount_ && index < slotCount_; ++index)
{
TechnoClass* pTechno = units_[index];
if (!pTechno || !IsAlive(pTechno))
continue;
CellClass* pCell = MapClass::Instance->TryGetCellAt(CellStruct{
static_cast<short>(slots_[index].CellX), static_cast<short>(slots_[index].CellY) }
);
if (!pCell)
continue;
const CoordStruct target = CellCentre(slots_[index].CellX, slots_[index].CellY, 0, 0);
const long long dx = static_cast<long long>(pTechno->Location.X) - target.X;
const long long dy = static_cast<long long>(pTechno->Location.Y) - target.Y;
	auto pFoot = static_cast<FootClass*>(pTechno);
	const bool home = (dx * dx + dy * dy <= static_cast<long long>(kArrivalRadius) * kArrivalRadius);
	if (home)
	{
		// Arrived. Do NOT clear the destination: a Move mission whose destination turns null makes
		// the unit revert to Guard, and Guard walks it back to where it originally stood - that is the
		// "unit returns to the start point" symptom. Leaving the finished move alone is harmless.
		if (index < kMaxUnits)
			reassertTries_[index] = kMaxReasserts;
		// Pin the unit with a fresh Guard mission started on this very cell: the engine's guard
		// target then points here instead of the cell the unit stood on when it got the order,
		// which is what walks an arrived unit back to its old position.
		if (pTechno->CurrentMission != Mission::Guard)
			pTechno->ForceMission(Mission::Guard);
		continue;
	}
	allHome = false;
	// Only nudge a unit that has already stopped short of its cell, and only a couple of times:
	// re-issuing while it is still driving just restarts its path over and over.
	if (index < kMaxUnits && pFoot->Destination == nullptr && reassertTries_[index] < kMaxReasserts && !WithinCell(pTechno, slots_[index].CellX, slots_[index].CellY) && (reassertFrames_ % 10) == 0)
	{
		pTechno->SetDestination(pCell, true);
		pTechno->ForceMission(Mission::Move);
		++reassertTries_[index];
	}
}
reassertFrames_ = 0;
}
if (!leftDown && !rightDown)
{
selectedCount_ = CollectSelected(snapshot_, kMaxUnits);
if (selectedCount_ > 0)
snapshotCount_ = selectedCount_;
const int live = selectedCount_;
static int lastLogged = -1;
if (live != lastLogged)
{
lastLogged = live;
Logf("selection: %d unit(s) selected, snapshot=%d", live, snapshotCount_);
}
}
	if (releaseGuard_ > 0)
		--releaseGuard_;
	if (releaseRequested_)
	{
		releaseRequested_ = false;
		permanentCount_ = 0;
		trackFrames_ = 0;
		for (int i = 0; i < kMaxUnits; ++i)
		{
			permanent_[i] = false;
			released_[i] = true;
		}
		Logf("keep: released the whole keeper so the player's click can command them");
	}
	if (trackFrames_ > 0)
		--trackFrames_;
	if (trackFrames_ > 0 || permanentCount_ > 0)
	{
		for (int index = 0; index < unitCount_ && index < slotCount_; ++index)
		{
			if (trackFrames_ == 0 && !permanent_[index])
				continue;
			if (released_[index])
				continue;
			TechnoClass* pTechno = units_[index];
			if (!pTechno || !IsAlive(pTechno))
				continue;
			TechnoTypeClass* pType = pTechno->GetTechnoType();
			auto pFoot = static_cast<FootClass*>(pTechno);
			CellClass* pKeepCell = MapClass::Instance->TryGetCellAt(CellStruct{
				static_cast<short>(slots_[index].CellX), static_cast<short>(slots_[index].CellY) });
			const CoordStruct target = CellCentre(slots_[index].CellX, slots_[index].CellY, 0, 0);
			const long long dx = static_cast<long long>(pTechno->Location.X) - target.X;
			const long long dy = static_cast<long long>(pTechno->Location.Y) - target.Y;
			const long long dist = static_cast<long long>(sqrt(static_cast<double>(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy)));
			const bool inCell = WithinCell(pTechno, slots_[index].CellX, slots_[index].CellY);
			if (index < kMaxUnits && (dist <= kArrivalRadius || inCell))
				wasHome_[index] = true;
			// A Move whose destination is not the keeper cell is a fresh order from the player, not
			// the engine's Guard/Area Guard (both of which keep their own mission id). Hand the
			// unit back so the order stands instead of being dragged home by the keeper.
			if (pTechno->CurrentMission == Mission::Move && pFoot->Destination && pKeepCell &&
				pFoot->Destination != pKeepCell)
			{
				released_[index] = true;
				Logf("keep: released %s %p after a new move order", pType ? pType->ID : "?", pTechno);
				continue;
			}
			// Once a unit stands on its cell and has taken its final facing, hand it a Stop. Stop is
			// the one mission that carries no destination: Guard keeps a guard cell and Area Guard
			// patrols the area it was handed (for a unit that just marched somewhere, that area is
			// where it came from), and Move keeps a destination that the keeper would have to
			// rewrite every frame. Re-issued only when the engine has replaced Stop with something
			// else, so the unit simply stays where it was put while it is inside its cell.
			// Once a unit stands on its cell and has taken its final facing, hand it a Stop. Stop is
			// the one mission that carries no destination: Guard keeps a guard cell and Area Guard
			// patrols the area it was handed (for a unit that just marched somewhere, that area is
			// where it came from), and Move keeps a destination the keeper would have to rewrite.
			// Throttled: a unit on a ramp slides and its mission flips every frame, so re-forcing
			// Stop each frame re-initialises the locomotor over and over, which is what makes a
			// slope stutter. Ten frames apart is plenty to keep it parked.
			if (index < kMaxUnits && wasHome_[index] && inCell && pTechno->CurrentMission != Mission::Stop
				&& (trackFrames_ % 10) == 0)
			{
				pTechno->ForceMission(Mission::Stop);
				if (!stopLogged_[index])
				{
					stopLogged_[index] = true;
					Logf("stop: %s %p stopped on cell (%d,%d)", pType ? pType->ID : "?", pTechno,
						slots_[index].CellX, slots_[index].CellY);
				}
			}
			if (index < kMaxUnits && wasHome_[index] && dist > 600 && (trackFrames_ % 30) == 0)
				Logf("ALERT: %s %p off its cell, dist=%lld mission=%d dest=%p cell=(%d,%d)",
					pType ? pType->ID : "?", pTechno, dist,
					static_cast<int>(pTechno->CurrentMission), pFoot->Destination,
					slots_[index].CellX, slots_[index].CellY);
			if ((trackFrames_ % 60) == 0)
				Logf("track: %s %p dist=%lld mission=%d dest=%p cell=(%d,%d)",
					pType ? pType->ID : "?", pTechno, dist,
					static_cast<int>(pTechno->CurrentMission), pFoot->Destination,
					slots_[index].CellX, slots_[index].CellY);
		}
	}
switch (state_)
{
case State::Idle:
{
if (!middleDown || cooldownFrames_ > 0)
break;
Settings::Load();
GatherSelectedUnits();
if (unitCount_ == 0)
break;
if (!CursorWorldPos(anchor_))
break;
state_ = State::Dragging;
cursorWorld_ = anchor_;
hasCursorCell_ = false;
Point2D pressClient = {
 0, 0 }
;
if (CursorClientPos(pressClient))
{
anchorClient_ = pressClient;
cursorClient_ = pressClient;
}
leftReleasedFirst_ = false;
ComputeLayout();
if (CellStruct probeCell = {
 static_cast<short>(anchor_.X / kLeptonPerCell),
static_cast<short>(anchor_.Y / kLeptonPerCell) }
; true)
{
const CoordStruct probeBase = CellClass::Cell2Coord(probeCell, 0);
if (CellClass* pProbeCell = MapClass::Instance
? MapClass::Instance->TryGetCellAt(probeCell) : nullptr)
{
const DWORD saved = pProbeCell->OccupationFlags;
for (DWORD flags = 0; flags < 8; ++flags)
{
pProbeCell->OccupationFlags = flags;
const CoordStruct spot = pProbeCell->FindInfantrySubposition(probeBase, false, false, false);
Logf("subtable: flags=%u offset=(%d,%d)", flags, spot.X - probeBase.X,
spot.Y - probeBase.Y);
}
pProbeCell->OccupationFlags = saved;
}
}
Logf("formation: gesture start, %d units, anchor=(%d,%d,%d)",
unitCount_, anchor_.X, anchor_.Y, anchor_.Z);
for (int i = 0; i < unitCount_ && i < 8; ++i)
{
TechnoTypeClass* pType = units_[i] ? units_[i]->GetTechnoType() : nullptr;
SHPStruct* pImage = pType ? pType->GetImage() : nullptr;
SHPStruct* pCameo = pType ? pType->GetCameo() : nullptr;
const auto pHeader = reinterpret_cast<const WORD*>(pImage);
Logf("unit[%d] what=%d type=%p image=%p(type=%04X frames=%d) cameo=%p",
i, units_[i] ? static_cast<int>(units_[i]->WhatAmI()) : -1,
pType, pImage,
pImage ? pHeader[0] : 0,
pImage ? reinterpret_cast<const short*>(pImage)[5] : 0,
pCameo);
}
break;
}
case State::Dragging:
{
if (middleDown)
{
ComputeLayout();
break;
}
CommitOrders();
state_ = State::Cooldown;
cooldownFrames_ = kCooldownFrames;
break;
}
case State::Cooldown:
{
if (!middleDown && !leftPressed && !rightPressed && !middlePressed)
state_ = State::Idle;
break;
}
}
leftWasDown_ = leftDown;
rightWasDown_ = rightDown;
middleWasDown_ = middleDown;
}
// 返回真时，引擎不许把这次点击变成命令。没有它，手势过程中松开左键就会让选中单位
// 朝光标位置跑过去，整个手势就废了。这里故意写得很窄：单纯的右键仍然要能取消选择。
bool FormationController::ShouldSuppressClick(){
	// Suppress only while a formation gesture is (or may be) under way: the right button
	// must be held AND the left button must have been down at some point during that hold.
	// Suppressing every right-button hold broke the plain right click that clears a selection.
	const bool rightDown = RightButtonDown();
	const bool suppress = state_ == State::Dragging || suppressFrames_ > 0 || (rightDown && gestureLatch_) || MiddleButtonDown();
	if (suppress != wasSuppressing_)
	{
		wasSuppressing_ = suppress;
		Logf("suppress: engine click handling %s", suppress ? "disabled" : "restored");
	}
	if (!suppress)
	{
		// The engine is about to handle this click, and any left click with units selected is a
		// player order. Ask the keeper to let go before it re-pins, otherwise the order is
		// overwritten in the same frame and the unit can never be commanded again.
		const bool leftFresh = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) && !leftWasDown_;
		if (releaseGuard_ == 0 && leftFresh && (permanentCount_ > 0 || trackFrames_ > 0))
		{
			for (int index = 0; index < unitCount_ && index < slotCount_; ++index)
			{
				if (units_[index] && IsAlive(units_[index]) && units_[index]->IsSelected)
				{
					releaseRequested_ = true;
					break;
				}
			}
		}
	}
	return suppress;
}

// 只用缓存画预览。不能在这里捕获——那会把「借来的单位」画进正在渲染的这一帧；
// 捕获放在更早的 Update() 里，那一帧的世界绘制会把痕迹覆盖掉。
// 每帧只允许一遍：混合两遍等于把透明度翻倍，ini 调了也看不出效果。
void FormationController::DrawPreview(){
if (disabled_)
return;
++drawFrames_;
	if ((drawFrames_ % 60) == 0)
	{
		Logf("perf: capture=%.3fms draw=%.3fms updates=%d passes=%d slots=%d", gCaptureMs, gDrawMs, gUpdateCalls, gDrawPasses, slotCount_); gUpdateCalls = 0; gDrawPasses = 0;
		gCaptureMs = 0.0;
		gDrawMs = 0.0;
	}
	if (state_ != State::Dragging || slotCount_ <= 0)
		return;

	// One pass per frame keeps the blend opacity honest.
	static int lastDrawFrame = -1;
	if (lastDrawFrame == frameId_)
		return;
	++gDrawCalls;

	TacticalClass* pTactical = TacticalClass::Instance;
	if (!pTactical)
		return;
DSurface* pSurface = DSurface::Composite;
if (!pSurface)
pSurface = DSurface::Hidden;
if (!pSurface)
return;
const int surfaceWidth = pSurface->GetWidth();
const int surfaceHeight = pSurface->GetHeight();
const Settings::Values settings = Settings::Current();
const int opacity = settings.GhostOpacity * 2;
static int loggedOpacity = -1;
if (loggedOpacity != opacity)
{
loggedOpacity = opacity;
Logf("ghost draw: setting=%d opacity=%d", settings.GhostOpacity, opacity);
}
	// Aircraft and jumpjets take their displayed heading from the locomotor, and the AI runs
	// after Update(), so a facing written there is gone by the time the frame is drawn.  Pinning
	// it here - inside the render pass - is what actually reaches the screen.
	for (int index = 0; index < pendingCount_; ++index)
	{
		PendingFacing& item = pending_[index];
		if (!item.Unit || item.Applied || item.FramesLeft <= 0)
			continue;
		if (AirHeightFor(item.Unit) <= 0)
			continue;
		ILocomotion* pLoco = static_cast<FootClass*>(item.Unit)->Locomotor.operator->();
		if (pLoco)
			pLoco->Do_Turn(item.Facing);
		item.Unit->Facing.set(item.Facing);
		item.Unit->TurretFacing.set(item.Facing);
	}
for (int index = 0; index < slotCount_; ++index)
{
const SlotPlan& slot = slots_[index];
CoordStruct centre = {
 0, 0, 0 }
;
if (!CellWorldPos(slot.CellX, slot.CellY, slot.OffsetX, slot.OffsetY, centre))
continue;
Point2D screen = {
 0, 0 }
;
CoordStruct target = centre;
if (slot.AirHeight > 0)
target.Z += slot.AirHeight;
pTactical->CoordsToClient(&target, &screen);
Point2D groundScreen = {
 0, 0 }
;
if (slot.AirHeight > 0)
pTactical->CoordsToClient(&centre, &groundScreen);
if (settings.TargetBlock)
{
Point2D corners[4];
int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
for (int corner = 0; corner < 4; ++corner)
{
const int offsetX = (corner == 0 || corner == 3) ? -kHalfCell : kHalfCell;
const int offsetY = (corner < 2) ? -kHalfCell : kHalfCell;
CoordStruct world = centre + CoordStruct{
 offsetX, offsetY, 0 }
;
pTactical->CoordsToClient(&world, &corners[corner]);
const Point2D& point = corners[corner];
minX = std::min(minX, point.X);
minY = std::min(minY, point.Y);
maxX = std::max(maxX, point.X);
maxY = std::max(maxY, point.Y);
}
if (!(maxX < 0 || maxY < 0 || minX >= surfaceWidth || minY >= surfaceHeight))
{
const bool orange = !slot.Snapped && slot.StartCell;
const DWORD fill = slot.Snapped ? kColorBadFill
: (orange ? kColorStartFill : kColorSlotFill);
const DWORD line = slot.Snapped ? kColorBadLine
: (orange ? kColorStartLine : kColorSlotLine);
if (index < kMaxFilledSlots)
{
FillQuadTinted(pSurface, corners, fill, 3,
0, 0, surfaceWidth - 1, surfaceHeight - 1);
}
for (int corner = 0; corner < 4; ++corner)
pSurface->DrawLine(&corners[corner], &corners[(corner + 1) & 3], line);
}
}
if (slot.AirHeight > 0)
{
pSurface->DrawLine(&groundScreen, &screen, kColorFacing);
}
const DWORD ghostTint = slot.Snapped ? settings.ColorInvalid : settings.ColorValid;
const DWORD ghostEdge = slot.Snapped ? settings.ColorInvalidEdge : settings.ColorValidEdge;
// Rocketeer-style flying infantry are small, so their ground shadow is a small ellipse.
TechnoClass* pUnit = units_[index];
TechnoTypeClass* pType = pUnit ? pUnit->GetTechnoType() : nullptr;
const bool airInfantry = pUnit && pUnit->WhatAmI() == AbstractType::Infantry;
	if (!IsAlive(pUnit))
		continue;
if (pUnit && pType)
{
const GhostCacheEntry* pCached = FindGhost(pUnit, centre, facingDir_);
if (!pCached)
{
static int missFrames = 0;
static int misses = 0;
if (missFrames != frameId_)
{
missFrames = frameId_;
++misses;
}
if ((misses % 60) == 0)
{
TechnoTypeClass* pMissType = pUnit->GetTechnoType();
Logf("ghost miss: slot=%d type=%p facing=%u key=%d cache=%d",
index, pMissType, static_cast<unsigned>(facingDir_.value<16>() >> 11),
GhostSlopeKey(centre), GhostCacheCount());
}
}
if (pCached)
{
{
const bool shadowOn = Settings::Current().GhostShadow;
if (slot.AirHeight > 0)
{
++gDrawCalls; ScopedMs perfDraw(&gDrawMs); BlitGhostPatch(pSurface, pCached->Patch, screen.X - pCached->Patch.FootX,
screen.Y - pCached->Patch.FootY, ghostTint, ghostEdge, opacity, 1, surfaceWidth, surfaceHeight);
if (shadowOn)
{
if (pCached->Patch.HasShadow)
{
++gDrawCalls; ScopedMs perfDraw(&gDrawMs); BlitGhostPatch(pSurface, pCached->Patch, groundScreen.X - pCached->Patch.FootX,
groundScreen.Y - pCached->Patch.FootY, ghostTint, ghostEdge, opacity, 2, surfaceWidth, surfaceHeight);
}
else
{
DrawGroundShadow(pSurface, groundScreen, airInfantry ? 10 : 36, airInfantry ? 5 : 18,
opacity, surfaceWidth, surfaceHeight);
}
}
}
else
{
++gDrawCalls; ScopedMs perfDraw(&gDrawMs); BlitGhostPatch(pSurface, pCached->Patch, screen.X - pCached->Patch.FootX,
screen.Y - pCached->Patch.FootY, ghostTint, ghostEdge, opacity, shadowOn ? 3 : 1,
surfaceWidth, surfaceHeight);
}
}
continue;
}
}
if (slot.Image)
{
ShpGhost::Draw(pSurface, slot.Image, slot.Frame, screen.X, screen.Y, ghostTint,
opacity, 0, 0, surfaceWidth - 1, surfaceHeight - 1);
}
}
 if (Settings::Current().TargetLine)
 {
 CoordStruct cursorTip = {
  cursorWorld_.X, cursorWorld_.Y, cursorWorld_.Z }
 ;
 Point2D anchorPoint = {
  0, 0 }
 ;
 Point2D cursorPoint = {
  0, 0 }
 ;
 pTactical->CoordsToClient(&anchor_, &anchorPoint);
 pTactical->CoordsToClient(&cursorTip, &cursorPoint);
 pSurface->DrawLine(&anchorPoint, &cursorPoint, kColorFacing);
 }
}
// Drawing a unit that is mid-move borrows its animated pose: infantry come out as a narrow
// walking profile and vehicles use whatever track pose the engine last computed, which is
// how slivers ended up in the cache and then on every ghost of that type.  Prefer a
// stationary unit of the same type as the donor for the capture.
// An occluder on the capture spot eats pixels out of the middle of a silhouette, leaving an
// enclosed hole.  Anything the mask does not cover that cannot reach the border is such a
// hole; filling it keeps the ghost complete no matter what was drawn there first.
// 把遮挡在剪影内部留下的洞补上：从边界做泛洪，能到达的是外部，
// 剩下没被访问到的掩码像素就是被包住的洞，直接并入本体。
static void FillEnclosedHoles(unsigned char* mask)
{
	const int w = 192;
	const int h = 192;
	static unsigned char outside[w * h];
	static int pending[w * h];
	memset(outside, 0, sizeof(outside));
	int pendingCount = 0;
	for (int x = 0; x < w; ++x)
	{
		if (!mask[x] && !outside[x]) { outside[x] = 1; pending[pendingCount++] = x; }
		const int bottom = (h - 1) * w + x;
		if (!mask[bottom] && !outside[bottom]) { outside[bottom] = 1; pending[pendingCount++] = bottom; }
	}
	for (int y = 0; y < h; ++y)
	{
		const int left = y * w;
		const int right = left + w - 1;
		if (!mask[left] && !outside[left]) { outside[left] = 1; pending[pendingCount++] = left; }
		if (!mask[right] && !outside[right]) { outside[right] = 1; pending[pendingCount++] = right; }
	}
	while (pendingCount > 0)
	{
		const int cell = pending[--pendingCount];
		const int x = cell % w;
		const int y = cell / w;
		if (x > 0) { const int n = cell - 1; if (!mask[n] && !outside[n]) { outside[n] = 1; pending[pendingCount++] = n; } }
		if (x + 1 < w) { const int n = cell + 1; if (!mask[n] && !outside[n]) { outside[n] = 1; pending[pendingCount++] = n; } }
		if (y > 0) { const int n = cell - w; if (!mask[n] && !outside[n]) { outside[n] = 1; pending[pendingCount++] = n; } }
		if (y + 1 < h) { const int n = cell + w; if (!mask[n] && !outside[n]) { outside[n] = 1; pending[pendingCount++] = n; } }
	}
	for (int cell = 0; cell < w * h; ++cell)
		if (!mask[cell] && !outside[cell])
			mask[cell] = 1;
}

// 优先挑一个「站着不动的同类型单位」来借画：正在行驶或正在展开的单位容易画出残片，
// 而一个坏形状会被该类型的所有剪影复用，必须避免。
static TechnoClass* FindCaptureDonor(TechnoClass* pUnit, TechnoTypeClass* pType)
{
	if (!pUnit)
		return pUnit;
	auto pFoot = static_cast<FootClass*>(pUnit);
	if (!pFoot->Destination)
		return pUnit;
	if (!TechnoClass::Array)
		return pUnit;
	for (int i = 0; i < TechnoClass::Array->Count; ++i)
	{
		TechnoClass* pOther = TechnoClass::Array->Items[i];
		if (!pOther || pOther == pUnit || pOther->GetTechnoType() != pType)
			continue;
		auto pOtherFoot = static_cast<FootClass*>(pOther);
		if (pOtherFoot->Destination)
			continue;
		if (pOther->InLimbo || !pOther->IsAlive)
			continue;
		return pOther;
	}
	return pUnit;
}
// The cache is keyed by type + facing bucket + slope, so the borrowed draw does not
// have to happen on the destination cell at all.  Capturing on a bare cell instead of
// one that carries a tree or a building removes the only remaining way an occluder can
// bite a silhouette: the capture box is flooded with a sentinel and the depth buffer is
// pushed to the far plane, but an object standing in the box still paints its own pixels
// back over the box after the flood, and those pixels then read as "not the sentinel"
// and survive as a hole in the mask forever (the mask is cached per type).
// 捕获剪影的方式是「把单位画在世界坐标上」，所以那个坐标必须没有物体：如果那里有树或
// 建筑，会被哨兵填充抹掉，于是该类型的剪影就永久缺一块。这里向四周一圈圈找最近的、
// 没有建筑/没有物体、且坡度类型相同的格子。
static CoordStruct PickCapturePos(const CoordStruct& destination)
{
	CellStruct cell = CellClass::Coord2Cell(destination);
	CellClass* pDest = MapClass::Instance ? MapClass::Instance->TryGetCellAt(cell) : nullptr;
	const int slope = pDest ? static_cast<int>(pDest->SlopeIndex) : 0;
	const bool isRamp = pDest ? pDest->Tile_Is_Ramp() : false;

	auto usable = [slope, isRamp](CellClass* pCell)
	{
		if (!pCell)
			return false;
		if (pCell->GetBuilding())
			return false;
		if (static_cast<int>(pCell->SlopeIndex) != slope)
			return false;
		// The tender spot is the tilt: it comes from whether the cell is a ramp at all, so a
		// flat replacement cell must never stand in for a ramp cell.
		if (pCell->Tile_Is_Ramp() != isRamp)
			return false;
		return pCell->FirstObject == nullptr;
	};

	if (usable(pDest))
		return pDest->GetCoordsWithBridge();

	for (int radius = 1; radius <= 6; ++radius)
	{
		for (int dy = -radius; dy <= radius; ++dy)
		{
			for (int dx = -radius; dx <= radius; ++dx)
			{
				if ((dx > -radius && dx < radius) && (dy > -radius && dy < radius))
					continue;
				CellClass* pCell = MapClass::Instance ? MapClass::Instance->TryGetCellAt(CellStruct{ static_cast<short>(cell.X + dx), static_cast<short>(cell.Y + dy) }) : nullptr;
				if (usable(pCell))
					return pCell->GetCoordsWithBridge();
			}
		}
	}

	return destination;
}


// 借引擎自己的绘制代码捕获一个剪影：
//   * 先用哨兵色填满捕获框，并记下像素与 Z 缓冲；
//   * 临时搬动单位、锁住目的地坡度、锁住朝向；
//   * 画它（体素走 UnitClass::DrawAsVXL，SHP 单位走它自己的 Draw）；
//   * 与哨兵色不同的像素就是剪影；第二趟开阴影再跑一次，多出来的就是影子行；
//   * 还原像素、Z 缓冲和单位状态，再让引擎把这块区域标脏重画。
// 两条血泪规则：借画不能裁剪成捕获框（那样引擎一个像素都不画），还原边界必须是半开区间。
bool FormationController::CaptureUnitGhostRaw(TechnoClass* pUnit, const CoordStruct& worldPos,
DirStruct facing, int clipWidth, int clipHeight, GhostPatch& patch){
if (!pUnit || !TacticalClass::Instance)
return false;
Surface* pSurface = DSurface::Composite;
if (!pSurface)
pSurface = DSurface::Hidden;
if (!pSurface)
return false;
const AbstractType what = pUnit->WhatAmI();
if (what != AbstractType::Unit && what != AbstractType::Infantry && what != AbstractType::Aircraft)
return false;
	// A unit being deployed (or undeployed) has its locomotor and drawing state torn down between
	// frames; borrowing a draw from it crashes inside the engine. Same for a unit already gone.
	if (pUnit->InLimbo)
		return false;
	if (what == AbstractType::Unit)
	{
		UnitClass* pUnitClass = static_cast<UnitClass*>(pUnit);
		if (pUnitClass->Deploying || pUnitClass->Undeploying || pUnitClass->Deployed)
			return false;
	}
TechnoTypeClass* pType = pUnit->GetTechnoType();
if (!pType)
return false;
if (clipWidth < kGhostWidth || clipHeight < kGhostHeight)
return false;
CoordStruct target = worldPos;
Point2D unitScreen = {
 0, 0 }
;
TacticalClass::Instance->CoordsToClient(&target, &unitScreen);
patch.Left = unitScreen.X - kGhostHalfWidth;
patch.Top = unitScreen.Y - kGhostTopAbove;
bool slid = false;
if (patch.Left < 0)
{
patch.Left = 0;
slid = true;
}
else if (patch.Left + kGhostWidth > clipWidth)
{
patch.Left = clipWidth - kGhostWidth;
slid = true;
}
if (patch.Top < 0)
{
patch.Top = 0;
slid = true;
}
else if (patch.Top + kGhostHeight > clipHeight)
{
patch.Top = clipHeight - kGhostHeight;
slid = true;
}
patch.FootX = kGhostHalfWidth;
patch.FootY = kGhostTopAbove;
Point2D drawPoint = {
 static_cast<short>(patch.Left + kGhostHalfWidth),
static_cast<short>(patch.Top + kGhostTopAbove) }
;
if (slid)
{
ClientToWorld(drawPoint, target);
const CellStruct cell = CellClass::Coord2Cell(target);
CoordStruct cellPos = {
 0, 0, 0 }
;
if (CellWorldPos(cell.X, cell.Y, 0, 0, cellPos))
target.Z = cellPos.Z;
}
	static const int kGhostMargin = 96;   // room for everything the borrowed draw can touch
	const int kBeforeStride = kGhostWidth + 2 * kGhostMargin;
	const int kBeforeRows = kGhostHeight + 2 * kGhostMargin;
	static DWORD before[kBeforeStride * kBeforeRows];
	static unsigned char shadowFlag[kGhostWidth * kGhostHeight];
	static DWORD paintedBuf[kGhostWidth * kGhostHeight];
	::memset(shadowFlag, 0, sizeof(shadowFlag));
	const int capLeft = (patch.Left > kGhostMargin) ? patch.Left - kGhostMargin : 0;
	const int capTop = (patch.Top > kGhostMargin) ? patch.Top - kGhostMargin : 0;
	int capRight = patch.Left + kGhostWidth + kGhostMargin;    // exclusive
	int capBottom = patch.Top + kGhostHeight + kGhostMargin;   // exclusive
	const int surfWidth = pSurface->GetWidth();
	const int surfHeight = pSurface->GetHeight();
	if (capRight > surfWidth) capRight = surfWidth;
	if (capBottom > surfHeight) capBottom = surfHeight;
	Point2D pixel = { 0, 0 };
	for (int y = capTop; y < capBottom; ++y)
	{
		for (int x = capLeft; x < capRight; ++x)
		{
			pixel.X = static_cast<short>(x);
			pixel.Y = static_cast<short>(y);
			before[(y - capTop) * kBeforeStride + (x - capLeft)] = pSurface->GetPixel(&pixel);
			pSurface->SetPixel(&pixel, kCaptureSentinel);
		}
	}
// Units are drawn with a depth test against the world's Z buffer, so whatever the capture
// spot happens to sit behind - a tree, a building, a ridge - tears the borrowed silhouette
// apart. Push that part of the Z buffer far away for the duration of the passes and restore
// it afterwards, so a ghost never inherits an occluder.
ZBufferClass* pZb = ZBufferClass::ZBuffer;
unsigned char* zbSaved = nullptr;
int zbSavedBytes = 0;
int zbElem = 1;
int zbRegionWidth = 1;
int zbRegionLeft = 0;
int zbRegionTop = 0;
if (pZb && pZb->BufferStart && pZb->W > 0 && pZb->H > 0)
{
const int elems = pZb->W * pZb->H;
zbElem = (pZb->BufferSize > 0 && elems > 0) ? (pZb->BufferSize / elems) : 1;
if (zbElem < 1)
zbElem = 1;
int left = capLeft - pZb->rect.X;
int top = capTop - pZb->rect.Y;
int right = capRight - pZb->rect.X;
int bottom = capBottom - pZb->rect.Y;
if (left < 0) left = 0;
if (top < 0) top = 0;
if (right > pZb->W - 1) right = pZb->W - 1;
if (bottom > pZb->H - 1) bottom = pZb->H - 1;
if (right >= left && bottom >= top)
{
zbRegionWidth = right - left + 1;
zbRegionLeft = left;
zbRegionTop = top;
const int rows = bottom - top + 1;
const int rowBytes = zbRegionWidth * zbElem;
zbSavedBytes = rowBytes * rows;
zbSaved = new unsigned char[zbSavedBytes];
for (int row = 0; row < rows; ++row)
{
unsigned char* src = pZb->BufferStart + ((top + row) * pZb->W + left) * zbElem;
memcpy(zbSaved + row * rowBytes, src, rowBytes);
memset(src, kZBufferFarByte, rowBytes);
}
}
}const CoordStruct oldLocation = pUnit->GetCoords();
const DirStruct oldFacing = pUnit->Facing.target();
const DirStruct oldTurretFacing = pUnit->TurretFacing.target();
const bool oldNoShadow = pType->NoShadow;
const int oldShadowIndex = pType->ShadowIndex;
	// The clip stays the whole surface: a narrow clip rect sent the engine down a slow path and
	// made it paint nothing at all. Stray pixels of the borrowed draw are cleaned up below by
	// marking the restored rectangle dirty, so the engine repaints it on the next frame.
	RectangleStruct clip = { 0, 0, clipWidth, clipHeight };
	ILocomotion* pLoco = nullptr;
int ownSlope = -1;
{
CellStruct destCell = CellClass::Coord2Cell(worldPos);
CellClass* pDest = MapClass::Instance ? MapClass::Instance->TryGetCellAt(destCell) : nullptr;
if (what == AbstractType::Unit && pDest)
{
pLoco = static_cast<FootClass*>(pUnit)->Locomotor.operator->();
if (pLoco)
{
const CellStruct ownCell = CellClass::Coord2Cell(oldLocation);
if (CellClass* pOwn = MapClass::Instance->TryGetCellAt(ownCell))
ownSlope = pOwn->SlopeIndex;
	// Only a real ramp has a meaningful slope index; building footprints and flat
	// ground report values that make the locomotor draw a degenerate sliver.
	pLoco->Force_New_Slope(pDest->Tile_Is_Ramp() ? static_cast<int>(pDest->SlopeIndex) : 0);
}
}
}
	const bool captureIsShpVehicle = (what == AbstractType::Unit)
		&& (pType->GetImage() != nullptr) && (pType->MainVoxel.VXL == nullptr);
	static unsigned char behindMask[kGhostWidth * kGhostHeight];
	memset(behindMask, 0, sizeof(behindMask));
auto renderPass = [&](bool noShadow, unsigned char* outMask, bool useBehind) -> bool
{
pType->NoShadow = noShadow;
pType->ShadowIndex = noShadow ? -1 : oldShadowIndex;
		// The silhouette's shape depends on the borrowed unit's facing and slope, not on
		// where it stands: keeping its own Location avoids the degenerate draws that
		// happened when the destination fell on a building footprint or an odd cell.
		// (The destination's slope is still applied through Force_New_Slope above.)
pUnit->Facing.set(facing);
pUnit->TurretFacing.set(facing);
// Aircraft and jumpjets draw from their locomotor heading, not from Facing alone, so the
// borrowed draw has to carry the heading too or every ghost comes out in the same pose.
ILocomotion* pLoco = static_cast<FootClass*>(pUnit)->Locomotor.operator->();
const bool airCapture = AirHeightFor(pUnit) > 0;
if (airCapture && pLoco)
pLoco->Do_Turn(facing);
		if (useBehind)
		{
			pUnit->DrawBehind(&drawPoint, &clip);
		}
		else if (what == AbstractType::Unit)
		{
			auto pUnitClass = static_cast<UnitClass*>(pUnit);
			const bool hasShpArt = pType->GetImage() != nullptr;
			const bool hasVoxelArt = pType->MainVoxel.VXL != nullptr;
			if (hasVoxelArt && !hasShpArt)
				pUnitClass->DrawAsVXL(drawPoint, clip, 0, 0);
			else if (hasShpArt)
				pUnit->Draw(&drawPoint, &clip);
			else
				return false;
		}
		else
		{
			pUnit->Draw(&drawPoint, &clip);
		}
pUnit->Facing.set(oldFacing);
pUnit->TurretFacing.set(oldTurretFacing);
if (airCapture && pLoco)
pLoco->Do_Turn(oldFacing);
	for (int y = capTop; y < capBottom; ++y)
	{
		const int insideY = y - patch.Top;
		for (int x = capLeft; x < capRight; ++x)
		{
			const int insideX = x - patch.Left;
			pixel.X = static_cast<short>(x);
			pixel.Y = static_cast<short>(y);
			const DWORD origin = before[(y - capTop) * kBeforeStride + (x - capLeft)];
			if (insideX >= 0 && insideX < kGhostWidth && insideY >= 0 && insideY < kGhostHeight)
			{
				const DWORD painted = pSurface->GetPixel(&pixel);
				const bool changed = (painted != kCaptureSentinel);
				outMask[insideY * kGhostWidth + insideX] = changed ? 1 : 0;

				// Whatever the mask picked up that is markedly darker than the ground it covers is
				// the unit shadow, whether the engine draws it as a separate sprite pass (voxels)
				// or bakes it into the frame (2D SHP vehicles).  Everything else is body, which
				if (!noShadow)
					paintedBuf[insideY * kGhostWidth + insideX] = painted;
				(void)changed;
			}
	}

	}
	return true;
};

	const bool bodyOk = renderPass(true, patch.BodyMask, false);
	const bool fullOk = bodyOk && renderPass(false, patch.Mask, false);
	// A third DrawBehind pass used to run here; it painted no pixels for any unit type on this
	// engine, so it only cost a full box walk per capture.
	const bool behindOk = fullOk;
	// Put the flooded ground back before anything else can look at the surface.
	for (int y = capTop; y < capBottom; ++y)
	{
		for (int x = capLeft; x < capRight; ++x)
		{
			pixel.X = static_cast<short>(x);
			pixel.Y = static_cast<short>(y);
			pSurface->SetPixel(&pixel, before[(y - capTop) * kBeforeStride + (x - capLeft)]);
		}
	}
	// The borrowed draw can paint past the saved rectangle on tall or tilted models, and those
	// stray pixels are opaque copies of the real unit - they read as a silhouette that ignores
	// GhostOpacity. Marking the restored rectangle dirty asks the engine to repaint it on the
	// next frame, which is the sanctioned way to clear anything the restore missed.
	if (TacticalClass::Instance)
	{
		RectangleStruct dirty = { capLeft, capTop, capRight - capLeft, capBottom - capTop };
		TacticalClass::Instance->RegisterDirtyArea(dirty, true);
	}
if (zbSaved)
{
const int rows = zbSavedBytes / (zbRegionWidth * zbElem);
const int rowBytes = zbRegionWidth * zbElem;
for (int row = 0; row < rows; ++row)
{
unsigned char* dst = pZb->BufferStart + ((zbRegionTop + row) * pZb->W + zbRegionLeft) * zbElem;
memcpy(dst, zbSaved + row * rowBytes, rowBytes);
}
delete[] zbSaved;
zbSaved = nullptr;
}	// Seal enclosed holes so an occluder can never leave a bitten-out silhouette behind.
	FillEnclosedHoles(patch.BodyMask);
	FillEnclosedHoles(patch.Mask);
	(void)behindOk;
	if (!bodyOk || !fullOk)
		return false;

	// The body pass misses exactly the pixels the engine treats as shadow, so the two masks
	// already separate voxel and infantry shadows.  A 2D SHP vehicle, however, has its shadow
	// cast by a separate engine pass that a single object draw never runs: both passes come
	// out identical and nothing is separated.  Peel those by darkness instead, and if even
	// that finds nothing, project the silhouette the way the engine would.
	// The engine only separates a shadow when it renders one as its own pass: voxels and
	// infantry do, 2D SHP vehicles do not, and for those both capture passes come out
	// identical.  Baked shadows also refuse to be told apart by brightness (tracks and
	// tyres are as dark as the shadow itself), so SHP art simply gets no shadow at all.
	// A missing shadow is far better than a silhouette with holes punched through it.
	int engineShadow = 0;
	for (int cell = 0; cell < kGhostWidth * kGhostHeight; ++cell)
	{
		if (patch.Mask[cell] && !patch.BodyMask[cell])
		{
			shadowFlag[cell] = 1;
			++engineShadow;
		}
	}

	if (engineShadow == 0)
	{
		for (int cell = 0; cell < kGhostWidth * kGhostHeight; ++cell)
		{
			if (!patch.Mask[cell])
				continue;
			const DWORD painted = paintedBuf[cell];
			const DWORD origin = before[(cell / kGhostWidth) * kBeforeStride + (cell % kGhostWidth)];
			const int paintedLuma = ((painted >> 11) & 0x1F) * 3 + ((painted >> 5) & 0x3F) * 7
				+ (painted & 0x1F) * 2;
			const int originLuma = ((origin >> 11) & 0x1F) * 3 + ((origin >> 5) & 0x3F) * 7
				+ (origin & 0x1F) * 2;
			if (paintedLuma < 60 && paintedLuma * 10 < originLuma * 6)
				shadowFlag[cell] = 1;
		}
	}

	// A 2D SHP shadow sprite is skewed far past its body (the Terror Drone's worst of all), so
	// SHP art skips it entirely and gets a compact projected shadow further down instead.
	if (captureIsShpVehicle)
	{
		for (int cell = 0; cell < kGhostWidth * kGhostHeight; ++cell)
			shadowFlag[cell] = 0;
	}
	int shadowPixels = 0;
	for (int cell = 0; cell < kGhostWidth * kGhostHeight; ++cell)
		shadowPixels += shadowFlag[cell] ? 1 : 0;

	if (shadowPixels == 0)
	{
		// Project from a snapshot of the body: writing patch.Mask while reading it let every newly
		// projected pixel become the source for the next one, which walked the shadow off along a
		// diagonal chain instead of laying down a single copy.
		const int shadowShiftX = 6;
		const int shadowShiftY = 4;
		static unsigned char bodySnapshot[kGhostWidth * kGhostHeight];
		memcpy(bodySnapshot, patch.Mask, sizeof(bodySnapshot));
		for (int y = 0; y < kGhostHeight; ++y)
		{
			const int sourceY = y - shadowShiftY;
			if (sourceY < 0 || sourceY >= kGhostHeight)
				continue;
			for (int x = 0; x < kGhostWidth; ++x)
			{
				const int sourceX = x - shadowShiftX;
				if (sourceX < 0 || sourceX >= kGhostWidth)
					continue;
				const int cell = y * kGhostWidth + x;
				if (!bodySnapshot[sourceY * kGhostWidth + sourceX] || bodySnapshot[cell])
					continue;
				shadowFlag[cell] = 1;
			}
		}
		for (int cell = 0; cell < kGhostWidth * kGhostHeight; ++cell)
			if (shadowFlag[cell])
				patch.Mask[cell] = 1;
	}
	int bodyLeft = kGhostWidth, bodyRight = -1, bodyTop = kGhostHeight, bodyBottom = -1;
	for (int y = 0; y < kGhostHeight; ++y)
	{
		for (int x = 0; x < kGhostWidth; ++x)
		{
			if (!patch.Mask[y * kGhostWidth + x])
				continue;
			if (x < bodyLeft) bodyLeft = x;
			if (x > bodyRight) bodyRight = x;
			if (y < bodyTop) bodyTop = y;
			if (y > bodyBottom) bodyBottom = y;
		}
	}
	if (bodyRight >= bodyLeft)
	{
		for (int y = 0; y < kGhostHeight; ++y)
		{
			for (int x = 0; x < kGhostWidth; ++x)
			{
				const int cell = y * kGhostWidth + x;
				if (!shadowFlag[cell])
					continue;
				if (x < bodyLeft - 8 || x > bodyRight + 10 || y < bodyTop - 2 || y > bodyBottom + 8)
					shadowFlag[cell] = 0;
			}
		}
	}
	// Shadow pixels are never body; everything else the mask painted is.  Without the first
	// half a shadow baked into the sprite stayed in the body mask, drawn green with the
	// outline attached, which is what kept the two from ever looking separate.
	for (int cell = 0; cell < kGhostWidth * kGhostHeight; ++cell)
	{
		if (patch.Mask[cell] && !shadowFlag[cell])
			patch.BodyMask[cell] = 1;
	}
int minX = kGhostWidth, maxX = -1, minY = kGhostHeight, maxY = -1;
for (int y = 0; y < kGhostHeight; ++y)
{
for (int x = 0; x < kGhostWidth; ++x)
{
const int cell = y * kGhostWidth + x;
if (patch.BodyMask[cell])
patch.Mask[cell] = 1;
if (!patch.Mask[cell])
continue;
if (x < minX) minX = x;
if (x > maxX) maxX = x;
if (y < minY) minY = y;
if (y > maxY) maxY = y;
}
}
if (maxX < minX || maxY < minY)
return false;
int bodyMinX = kGhostWidth, bodyMinY = kGhostHeight, bodyMaxX = -1, bodyMaxY = -1;
{
int bodyPixels = 0;
int allPixels = 0;
for (int y = 0; y < kGhostHeight; ++y)
{
for (int x = 0; x < kGhostWidth; ++x)
{
const int cell = y * kGhostWidth + x;
if (!patch.BodyMask[cell])
{
allPixels += patch.Mask[cell] ? 1 : 0;
continue;
}
++bodyPixels;
allPixels += patch.Mask[cell] ? 1 : 0;
if (x < bodyMinX) bodyMinX = x;
if (x > bodyMaxX) bodyMaxX = x;
if (y < bodyMinY) bodyMinY = y;
if (y > bodyMaxY) bodyMaxY = y;
}
}
if (bodyMaxX < bodyMinX || bodyMaxY < bodyMinY)
{
bodyMinX = minX;
bodyMaxX = maxX;
bodyMinY = minY;
bodyMaxY = maxY;
}
	int behindPixels = 0;
	for (int cell = 0; cell < kGhostWidth * kGhostHeight; ++cell) behindPixels += behindMask[cell] ? 1 : 0;
	Logf("ghost behind: pct=%d shp=%d", behindPixels, captureIsShpVehicle ? 1 : 0);
Logf("ghost capture: type=%p (%s) facing=%u body=%d full=%d shadow=%d box=(%d,%d)-(%d,%d) z=%d",
static_cast<void*>(pType), pType->get_ID(), facing.value<16>() >> 8, bodyPixels,
allPixels, allPixels - bodyPixels, minX, minY, maxX, maxY, target.Z);
}
{
bool hasShadow = false;
for (int cell = 0; cell < kGhostWidth * kGhostHeight && !hasShadow; ++cell)
{
if (patch.Mask[cell] && !patch.BodyMask[cell])
hasShadow = true;
}
// Aircraft and jumpjets are captured while hovering, so whatever shadow pixels the artwork
// yields are projections of the flight altitude, not of the ground cell.  They are dropped
// and the ground ellipse is stamped instead, which is complete by construction.
if (AirHeightFor(pUnit) > 0)
{
for (int cell = 0; cell < kGhostWidth * kGhostHeight; ++cell)
{
if (patch.Mask[cell])
patch.BodyMask[cell] = 1;
}
hasShadow = false;
}
patch.HasShadow = hasShadow;
}
patch.MaskWidth = maxX - minX + 1;
patch.MaskHeight = maxY - minY + 1;
patch.FootX = kGhostHalfWidth;
patch.FootY = kGhostTopAbove;
return true;
}
FormationController::GhostCacheEntry* FormationController::GhostCache(){
static GhostCacheEntry cache[kGhostCacheSize];
return cache;
}
int& FormationController::GhostCacheCount(){
static int cacheCount = 0;
return cacheCount;
}
bool FormationController::CaptureUnitGhost(TechnoClass* pUnit, const CoordStruct& worldPos,
	DirStruct facing, int clipWidth, int clipHeight, GhostPatch& patch){
	// Borrowing a draw runs a lot of engine code on an object that may be mid-transformation.
	// A fault in there is not worth a crash: drop this ghost for now and let the next frame retry.
	__try
	{
		return CaptureUnitGhostRaw(pUnit, worldPos, facing, clipWidth, clipHeight, patch);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		static int faults = 0;
		if (faults < 20)
		{
			++faults;
			Logf("ghost capture: fault while borrowing a draw, skipped (total %d)", faults);
		}
		return false;
	}
}

bool FormationController::GhostKey(TechnoClass* pUnit, const CoordStruct& worldPos,
DirStruct facing, TechnoTypeClass*& pType, unsigned short& quantised){
pType = nullptr;
quantised = 0;
if (!pUnit)
return false;
pType = pUnit->GetTechnoType();
if (!pType)
return false;
quantised = static_cast<unsigned short>(facing.value<16>() >> 11);
(void)worldPos;
return true;
}
int FormationController::GhostSlopeKey(const CoordStruct& worldPos){
const CellStruct cell = CellClass::Coord2Cell(worldPos);
CellClass* pCell = MapClass::Instance ? MapClass::Instance->TryGetCellAt(cell) : nullptr;
return pCell ? static_cast<int>(pCell->SlopeIndex) : 0;
}
const FormationController::GhostCacheEntry* FormationController::FindGhost(TechnoClass* pUnit,
const CoordStruct& worldPos, DirStruct facing){
TechnoTypeClass* pType = nullptr;
unsigned short quantised = 0;
if (!GhostKey(pUnit, worldPos, facing, pType, quantised))
return nullptr;
const int cellKey = GhostSlopeKey(worldPos);
GhostCacheEntry* cache = GhostCache();
const int cacheCount = GhostCacheCount();
for (int index = 0; index < cacheCount; ++index)
{
if (cache[index].Type == pType && cache[index].Facing == quantised
&& cache[index].CellKey == cellKey)
{
cache[index].LastUse = Instance().frameId_;
return &cache[index];
}
}
return nullptr;
}
// 在渲染之前补剪影缓存：每帧最多 kCapturesPerFrame 个新形状，按（类型、朝向档、坡度）
// 作键，所以只是转队列时只有真正不同的形状才重捕。本帧和上一帧用过的条目永不被淘汰；
// 连续失败会暂停一段时间——捕获不出来还每帧重试，只会卡顿且什么都看不到。
void FormationController::PrepareGhosts(){
if (state_ != State::Dragging && suppressFrames_ == 0)
return;
Surface* pSurface = DSurface::Composite;
if (!pSurface)
pSurface = DSurface::Hidden;
if (!pSurface)
return;
const int clipWidth = pSurface->GetWidth();
const int clipHeight = pSurface->GetHeight();
if (clipWidth < kGhostWidth || clipHeight < kGhostHeight)
return;
GhostCacheEntry* cache = GhostCache();
int& cacheCount = GhostCacheCount();
static int captureFrame = -1;
static int captureCount = 0;
if (captureFrame != frameId_)
{
captureFrame = frameId_;
captureCount = 0;
}
for (int index = 0; index < slotCount_; ++index)
{
	if (captureCount >= kCapturesPerFrame || frameId_ < captureBlockedUntil_)
return;
const SlotPlan& slot = slots_[index];
TechnoClass* pUnit = units_[index];
if (!pUnit)
continue;
	if (!IsAlive(pUnit))
		continue;
TechnoTypeClass* pType = nullptr;
unsigned short quantised = 0;
if (!GhostKey(pUnit, CoordStruct{
 0, 0, 0 }
, facingDir_, pType, quantised))
continue;
CoordStruct centre = {
 0, 0, 0 }
;
if (!CellWorldPos(slot.CellX, slot.CellY, slot.OffsetX, slot.OffsetY, centre))
continue;
const int cellKey = GhostSlopeKey(centre);
bool found = false;
for (int entry = 0; entry < cacheCount; ++entry)
{
if (cache[entry].Type == pType && cache[entry].Facing == quantised
&& cache[entry].CellKey == cellKey)
{
cache[entry].LastUse = frameId_;
found = true;
break;
}
}
if (found)
continue;
int slotIndex = cacheCount;
if (slotIndex >= kGhostCacheSize)
{
slotIndex = -1;
for (int entry = 0; entry < kGhostCacheSize; ++entry)
{
if (cache[entry].LastUse + 1 >= frameId_)
continue;
if (slotIndex < 0 || cache[entry].LastUse < cache[slotIndex].LastUse)
slotIndex = entry;
}
}
if (slotIndex < 0)
continue;
++captureCount;
GhostCacheEntry& entry = cache[slotIndex];
			// Capture on bare ground when the destination cell carries a tree or a building,
			// so an object can never paint a hole into the cached silhouette.
			const CoordStruct capturePos = PickCapturePos(centre);
			{ const CellStruct d = CellClass::Coord2Cell(centre); const CellStruct c = CellClass::Coord2Cell(capturePos); if (d.X != c.X || d.Y != c.Y) Logf("ghost capture moved off the obstructed cell: dest=(%d,%d) capture=(%d,%d)", d.X, d.Y, c.X, c.Y); }
			if (!CaptureUnitGhost(FindCaptureDonor(pUnit, pType), capturePos, facingDir_, clipWidth, clipHeight, entry.Patch))
continue;
		// A borrowed draw can come out as a sliver when the unit is mid-move or the
		// locomotor matrix has not caught up with the requested facing.  Such a patch
		// must never enter the cache: it would be reused by every ghost of that type.
		int bodyPixels = 0;
		for (int cell = 0; cell < kGhostWidth * kGhostHeight; ++cell)
			bodyPixels += entry.Patch.BodyMask[cell] ? 1 : 0;
		const int bodyFloor = (pUnit->WhatAmI() == AbstractType::Infantry) ? 60 : 300;
		if (bodyPixels < bodyFloor)
		{
			Logf("ghost capture rejected: type=%p facing=%u body=%d", pType, quantised, bodyPixels);
			// A capture that keeps failing must not be retried every frame: each attempt costs a
			// borrowed engine draw plus a full box walk, which is exactly what made the game
			// stutter when the borrowed draw could not paint anything.
			if (++captureFailures_ > 8)
			{
				captureBlockedUntil_ = frameId_ + 120;
				Logf("ghost capture paused for 120 frames after %d failures", captureFailures_);
			}
			continue;
		}
entry.Type = pType;
entry.Facing = quantised;
entry.CellKey = cellKey;
entry.LastUse = frameId_;
if (slotIndex == cacheCount)
++cacheCount;
Logf("ghost cached: type=%p facing=%u foot=(%d,%d) h=%d", pType, quantised,
entry.Patch.FootX, entry.Patch.FootY, entry.Patch.MaskHeight);
}}

static DWORD BlendGhost565(DWORD under, DWORD colour, int eighths)
{
	int in = eighths < 0 ? 0 : (eighths > 8 ? 8 : eighths);
	if (in == 0)
		return under;
	if (in >= 8)
		return colour;
	const int ur = (under >> 11) & 0x1F;
	const int ug = (under >> 5) & 0x3F;
	const int ub = under & 0x1F;
	const int luma = (ur * 8 + ug * 4 + ub) / 13;
	const int scale = 128 + luma * 2;
	const DWORD target = ((((colour >> 11) & 0x1F) * scale / 256) << 11)
		| ((((colour >> 5) & 0x3F) * scale / 256) << 5)
		| (((colour & 0x1F) * scale / 256));
	return Blend565(under, target, in);
}
void FormationController::BlitGhostPatch(Surface* pSurface, const GhostPatch& patch, int left,
int top, DWORD tint, DWORD edgeTint, int opacity, int layers, int clipWidth, int clipHeight){
if (!pSurface || opacity <= 0)
return;
Point2D pixel = {
 0, 0 }
;
const DWORD ghostPixel = tint;
const DWORD edgePixel = edgeTint;
for (int y = 0; y < kGhostHeight; ++y)
{
const int screenY = top + y;
if (screenY < 0 || screenY >= clipHeight)
continue;
for (int x = 0; x < kGhostWidth; ++x)
{
const int cell = y * kGhostWidth + x;
if (!patch.Mask[cell])
continue;
const int screenX = left + x;
if (screenX < 0 || screenX >= clipWidth)
continue;
pixel.X = static_cast<short>(screenX);
pixel.Y = static_cast<short>(screenY);
if (!patch.BodyMask[cell])
{
if ((layers & 2) != 0)
{
const DWORD underShadow = pSurface->GetPixel(&pixel);
const int shadowOpacity = (opacity * 3) / 4;
pSurface->SetPixel(&pixel, BlendGhost565(underShadow, kColorGhostShadow, shadowOpacity));
}
continue;
}
if ((layers & 1) == 0)
continue;
bool edge = (x == 0 || y == 0 || x == kGhostWidth - 1 || y == kGhostHeight - 1);
if (!edge)
{
edge = !patch.BodyMask[cell - 1] || !patch.BodyMask[cell + 1]
|| !patch.BodyMask[cell - kGhostWidth] || !patch.BodyMask[cell + kGhostWidth];
}
			// Same weight as the body: boosting the outline made every setting look solid, which
			// is exactly the "GhostOpacity does nothing" report.
			const DWORD under = pSurface->GetPixel(&pixel);
			const int edgeOpacity = opacity;
			// The outline keeps the configured colour at full strength; only the body follows
			// GhostOpacity. Dimming the outline by terrain brightness made every edge colour
			// look washed out, which is exactly the "edge colour is gone" report.
			if (edge)
				pSurface->SetPixel(&pixel, Blend565(under, edgePixel, 8));
			else
				pSurface->SetPixel(&pixel, BlendGhost565(under, ghostPixel, opacity));
}
}}
