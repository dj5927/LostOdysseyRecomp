#include "gpu/depth_clear_layout.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace gpu::renderer;
bool contains(const std::vector<DepthClearRect>& rs,int x,int y) {for(auto r:rs) if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom)return true;return false;}
int LegacyMappingTests(){
 auto right=MapDepthClear(440,2,{240,0,400,216},880,896,0);
 assert(!right.empty());assert(contains(right,480,0));assert(contains(right,799,431));assert(!contains(right,200,100));assert(!contains(right,800,431));assert(!contains(right,480,432));
 // One source tile-row wraps into two destination rows when the pitch halves.
 auto wrap=MapDepthClear(160,0,{80,0,160,16},80,64,0);
 assert(contains(wrap,0,16)&&contains(wrap,79,31));assert(!contains(wrap,0,0));
 assert(MapDepthClear(80,0,{0,64,80,80},80,16,0).empty());
 // Compare coverage to independently enumerated sample addresses for all MSAA pairs.
 for(unsigned s=0;s<3;s++)for(unsigned t=0;t<3;t++) {
  unsigned sx=s==2?2:1,sy=s?2:1,dx=t==2?2:1,dy=t?2:1;
  auto r=MapDepthClear(160,s,{23,7,139,31},240,128,t);
  bool expected[128][240]={};
  for(unsigned y=7*sy;y<31*sy;y++)for(unsigned x=23*sx;x<139*sx;x++){
   unsigned address=((y/16)*((160*sx+79)/80)+x/80)*1280+(y%16)*80+x%80;
   unsigned tile=address/1280,inside=address%1280;
   unsigned tx=((tile%((240*dx+79)/80))*80+inside%80)/dx;
   unsigned ty=((tile/((240*dx+79)/80))*16+inside/80)/dy;
   if(tx<240&&ty<128)expected[ty][tx]=true;
  }
  for(int y=0;y<128;y++)for(int x=0;x<240;x++)assert(contains(r,x,y)==expected[y][x]);
 }
 puts("PASS: atlas isolation, pitch wrap, clipped empty mapping, 9 MSAA coverage pairs");
 return 0;
}

// A pixel union, independent of the coalescer's sorting/merging strategy.
static std::vector<uint8_t> Coverage(const std::vector<DepthClearRect>& rects, DepthClearRect bounds)
{
 const int width = bounds.right - bounds.left;
 std::vector<uint8_t> pixels(size_t(width) * (bounds.bottom - bounds.top));
 for (const auto& r : rects)
  for (int y = std::max(r.top, bounds.top); y < std::min(r.bottom, bounds.bottom); ++y)
   for (int x = std::max(r.left, bounds.left); x < std::min(r.right, bounds.right); ++x)
    pixels[size_t(y - bounds.top) * width + x - bounds.left] = 1;
 return pixels;
}

static size_t comparedPixels = 0;
static std::vector<DepthClearRect> CheckCoalesced(std::vector<DepthClearRect> original, DepthClearRect bounds)
{
 const auto expected = Coverage(original, bounds);
 auto merged = original;
 // Deliberately remove the original tile traversal order.
 std::reverse(merged.begin(), merged.end());
 CoalesceDepthClearRects(merged);
 assert(merged.size() <= original.size());
 assert(Coverage(merged, bounds) == expected);
 comparedPixels += expected.size();
 return merged;
}

static int CoalescedTests()
{
 const auto tiles = MapDepthClear(640, 2, {0, 0, 640, 360}, 1280, 736, 0);
 assert(tiles.size() == 720);
 const auto full = CheckCoalesced(tiles, {0, 0, 1280, 736});
 assert(full.size() == 1 && full[0].left == 0 && full[0].top == 0 &&
  full[0].right == 1280 && full[0].bottom == 720);

 // Independently enumerate EDRAM sample addresses for all nine MSAA pairs.
 for (unsigned s = 0; s < 3; ++s) for (unsigned t = 0; t < 3; ++t) {
  const unsigned sx = s == 2 ? 2 : 1, sy = s ? 2 : 1;
  const unsigned dx = t == 2 ? 2 : 1, dy = t ? 2 : 1;
  const auto mapped = MapDepthClear(160, s, {23, 7, 139, 31}, 240, 128, t);
  const auto merged = CheckCoalesced(mapped, {0, 0, 240, 128});
  std::vector<uint8_t> expected(240 * 128);
  for (unsigned y = 7 * sy; y < 31 * sy; ++y) for (unsigned x = 23 * sx; x < 139 * sx; ++x) {
   const unsigned address = ((y / 16) * ((160 * sx + 79) / 80) + x / 80) * 1280 +
    (y % 16) * 80 + x % 80;
   const unsigned tile = address / 1280, inside = address % 1280;
   const unsigned tx = ((tile % ((240 * dx + 79) / 80)) * 80 + inside % 80) / dx;
   const unsigned ty = ((tile / ((240 * dx + 79) / 80)) * 16 + inside / 80) / dy;
   if (tx < 240 && ty < 128) expected[ty * 240 + tx] = 1;
  }
  assert(Coverage(merged, {0, 0, 240, 128}) == expected);
  comparedPixels += expected.size();
 }
 const DepthClearRect small{-8, -8, 16, 16};
 assert(CheckCoalesced({{4, 1, 8, 3}, {0, 1, 4, 3}, {2, 1, 6, 3}}, small).size() == 1);
 assert(CheckCoalesced({{0, 0, 8, 2}, {0, 2, 8, 4}, {0, 4, 8, 6}}, small).size() == 1);
 const auto hole = CheckCoalesced({{0, 0, 8, 2}, {0, 6, 8, 8}, {0, 2, 2, 6}, {6, 2, 8, 6}}, small);
 assert(!contains(hole, 4, 4) && contains(hole, 0, 4) && contains(hole, 4, 0));
 CheckCoalesced({{0, 0, 6, 2}, {2, 2, 8, 4}, {0, 4, 6, 6}}, small); // Unequal spans.
 CheckCoalesced({{0, 0, 5, 4}, {3, 2, 8, 6}, {1, 1, 3, 2}}, small); // Partial overlap.
 const auto gap = CheckCoalesced({{0, 0, 4, 1}, {0, 2, 4, 3}}, small);
 assert(!contains(gap, 2, 1));
 CheckCoalesced({{0, 0, 4, 3}, {0, 2, 4, 5}, {0, 0, 4, 3}}, small); // Vertical overlap/duplicate.
 CheckCoalesced({{2, 2, 2, 4}, {4, 3, 2, 5}, {-4, -3, 0, -1}, {0, -3, 4, -1}}, small);
 assert(CheckCoalesced({}, small).empty());
 assert(CheckCoalesced(MapDepthClear(80, 0, {0, 64, 80, 80}, 80, 16, 0), {0, 0, 80, 16}).empty());
 std::printf("PASS: coalescer 720 -> 1, 9 MSAA pairs, partial/hole/overlap/empty coverage; %zu pixel comparisons\n", comparedPixels);
 return 0;
}

int main(int argc, char** argv)
{
 if (argc == 1) return LegacyMappingTests();
 if (argc == 2 && std::strcmp(argv[1], "--coalesced-only") == 0) return CoalescedTests();
 std::fprintf(stderr, "usage: depth_clear_layout_test [--coalesced-only]\n");
 return 2;
}
