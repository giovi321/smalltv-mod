#include "ThemeEngine.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
using namespace smalltv;
struct Pixels : Display {
 uint16_t data[240*240]{};int writes=0;
 uint8_t glyphColumn(uint8_t c,uint8_t col) const override {return c=='1'?(col==0?0x7f:0):0x7f;}
 void row(int x,int y,const uint16_t* p,int n) override {assert(x>=0&&y>=0&&x+n<=240&&y<240);for(int i=0;i<n;++i)data[y*240+x+i]=p[i];++writes;}
};
struct Images : Assets {
 int reads=0, resolves=0;
 uint16_t resolve(const std::string& path) override {
   ++resolves;if(path=="missing.sti") return InvalidAsset;
   return path.find("001")!=std::string::npos?1:0;
 }
 bool row(uint16_t handle,int,int,int count,uint16_t* c,uint8_t* a) override {
   ++reads;for(int i=0;i<count;++i){c[i]=handle==1?0xf800:0x07e0;a[i]=i?255:0;}return true;
 }
};
// Glyph '1' inks only column 0 of its cell; every other glyph inks columns 1-4.
// A "1" therefore marks the start of a cell, letting tests locate exactly which
// content column a viewport pixel maps to.
static void testScrollRendering() {
  Theme loopTheme;loopTheme.background=0x0000;
  Layer text;text.type=LayerType::Text;text.value="1000000000";text.size=8;
  text.x=0;text.y=0;text.anchorX=0;text.anchorY=0;
  text.scroll.enabled=true;text.scroll.width=24;text.scroll.speed=20;
  text.scroll.pauseMs=1000;text.scroll.gap=6;text.scroll.mode=ScrollMode::Loop;
  loopTheme.layers={text};
  Engine loop;Pixels d;Images assets;
  loop.setTheme(loopTheme,0);
  assert(render(loop,loop.update(0,nullptr),assets,d));
  // Text (60px) is wider than the 24px viewport: nothing draws past it.
  assert(d.data[24]==0x0000);
  // Offset 0: char 0 ('1') shows ink only at column 0 of its cell.
  assert(d.data[0]!=0x0000 && d.data[1]==0x0000);
  // Offset 6 (one full glyph cell): char 1 ('0') is now first, inking every
  // column of its cell (unlike '1', which only marks column 0).
  assert(render(loop,loop.update(1300,nullptr),assets,d));
  assert(d.data[0]!=0x0000 && d.data[1]!=0x0000);
  // Offset 60 (start of the 6px gap): the gap reveals the background.
  Engine gapEngine;gapEngine.setTheme(loopTheme,0);gapEngine.update(0,nullptr);
  assert(render(gapEngine,gapEngine.update(4000,nullptr),assets,d));
  assert(d.data[0]==0x0000);
  // Offset 65 (one pixel before a full cycle): the second copy's first column
  // is already visible one pixel into the viewport.
  Engine wrapEngine;wrapEngine.setTheme(loopTheme,0);wrapEngine.update(0,nullptr);
  assert(render(wrapEngine,wrapEngine.update(4250,nullptr),assets,d));
  assert(d.data[0]==0x0000 && d.data[1]!=0x0000);
  // Bounce: the far-edge offset packs the viewport with the trailing glyph,
  // with no blank overshoot past the end of the text.
  Theme bounceTheme=loopTheme;bounceTheme.layers[0].value="0000000001";
  bounceTheme.layers[0].scroll.mode=ScrollMode::Bounce;
  Engine bounce;bounce.setTheme(bounceTheme,0);bounce.update(0,nullptr);
  assert(render(bounce,bounce.update(2800,nullptr),assets,d));
  assert(bounce.states()[0].scrollOffset==36);
  assert(d.data[18]!=0x0000);
  // A foreground layer above scrolling text stays put and unaffected by it,
  // and text stays clipped to its viewport even under a wider dirty region.
  Theme overlaid=loopTheme;
  Layer overlay;overlay.type=LayerType::Shape;overlay.shape=Shape::Rectangle;
  overlay.x=2;overlay.y=0;overlay.width=1;overlay.height=1;overlay.hasFill=true;overlay.fill=0xffff;
  overlaid.layers.push_back(overlay);
  Engine composed;composed.setTheme(overlaid,0);
  assert(render(composed,composed.update(0,nullptr),assets,d));
  assert(d.data[2]==0xffff);
  assert(render(composed,{Rect(0,0,240,240)},assets,d));
  assert(d.data[2]==0xffff && d.data[30]==0x0000);
  composed.update(1300,nullptr);
  assert(render(composed,{Rect(0,0,240,240)},assets,d));
  assert(d.data[2]==0xffff);
}
static void testRoundedRectRendering() {
  Theme theme;theme.background=0x0000;
  Layer rect;rect.type=LayerType::Shape;rect.shape=Shape::Rectangle;
  rect.x=0;rect.y=0;rect.width=10;rect.height=8;rect.cornerRadius=3;
  rect.hasFill=true;rect.fill=0x07e0;rect.hasStroke=true;rect.stroke=0xf800;rect.strokeWidth=2;
  theme.layers={rect};
  Engine e;Pixels d;Images assets;
  e.setTheme(theme,0);
  assert(render(e,e.update(0,nullptr),assets,d));
  assert(d.data[0]==0x0000);          // clipped corner
  assert(d.data[3]==0xf800);          // rounded outer edge (top, past the corner)
  assert(d.data[3*240+3]==0x07e0);    // interior
  assert(d.data[15]==0x0000);         // outside the 10px width
  // Square-path assertions (cornerRadius=0) must still hold.
  Theme square=theme;square.layers[0].cornerRadius=0;
  e.setTheme(square,0);
  assert(render(e,e.update(0,nullptr),assets,d));
  assert(d.data[0]==0xf800 && d.data[3*240+3]==0x07e0);
  // Binding width down to 4 clamps the resolved corner radius to 2 and leaves
  // no ink outside the resolved width.
  Binding widthBind;widthBind.property=BoundProperty::Width;widthBind.source="data.width";
  widthBind.numeric.output0=4;widthBind.numeric.output1=4;
  Theme bound=theme;bound.layers[0].bindings.push_back(widthBind);
  e.setTheme(bound,0);e.setValues({{"data.width","1"}});
  assert(render(e,e.update(0,nullptr),assets,d));
  assert(e.states()[0].resolved.cornerRadius==2);
  assert(d.data[4]==0x0000);
}
// ---- Rotation shapes (#31 item 2) ----
static Layer rotationLayer(Shape shape) {
  Layer l;l.type=LayerType::Shape;l.shape=shape;l.x=120;l.y=120;l.hasStroke=true;l.stroke=0xf800;l.strokeWidth=1;
  return l;
}
static Layer arcLayer(int radius,int start,int sweep,int strokeWidth) {
  Layer l=rotationLayer(Shape::Arc);l.radius=radius;l.angle=start;l.sweep=sweep;l.strokeWidth=strokeWidth;return l;
}
static Layer handLayer(int length,int tail,int angle,int strokeWidth) {
  Layer l=rotationLayer(Shape::Hand);l.length=length;l.tail=tail;l.angle=angle;l.strokeWidth=strokeWidth;return l;
}
static Layer ticksLayer(int radius,int count,int length,int strokeWidth) {
  Layer l=rotationLayer(Shape::Ticks);l.radius=radius;l.count=count;l.length=length;l.strokeWidth=strokeWidth;return l;
}
static void renderOne(const Layer& layer,Pixels& d) {
  Theme theme;theme.spec=2;theme.background=0;theme.layers={layer};
  Engine e;Images assets;e.setTheme(theme,0);
  assert(render(e,e.update(0,nullptr),assets,d));
}
static bool ink(const Pixels& d,int x,int y) { return d.data[y*240+x]!=0; }
// True if any pixel within one of (x, y) is drawn: the shapes are within a pixel of the ideal ones.
static bool inkNear(const Pixels& d,int x,int y) {
  for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(ink(d,x+dx,y+dy)) return true;
  return false;
}
static void testRotationShapes() {
  static Pixels d;
  // Hand: 0 is 12 o'clock and angles grow clockwise; the tail reaches behind the pivot.
  renderOne(handLayer(50,10,0,3),d);
  assert(ink(d,120,120)&&ink(d,120,70)&&ink(d,120,95)&&ink(d,120,130));
  assert(!ink(d,120,66)&&!ink(d,120,135)&&!ink(d,140,100)&&!ink(d,100,100));
  renderOne(handLayer(50,10,90,3),d);
  assert(ink(d,170,120)&&ink(d,110,120)&&!ink(d,120,100)&&!ink(d,120,140)&&!ink(d,176,120));
  renderOne(handLayer(50,0,180,3),d);
  assert(ink(d,120,170)&&!ink(d,120,110));
  renderOne(handLayer(50,0,270,3),d);
  assert(ink(d,70,120)&&!ink(d,130,120));
  renderOne(handLayer(50,0,-90,3),d);       // negative angles wrap
  assert(ink(d,70,120));
  renderOne(handLayer(50,0,360,3),d);
  assert(ink(d,120,70)&&!ink(d,170,120));
  // Every degree lands within a pixel of the exact direction, and its ink stays inside its box.
  for(int angle=0;angle<360;++angle) {
    Theme theme;theme.spec=2;theme.layers={handLayer(100,20,angle,3)};
    Engine e;Images assets;e.setTheme(theme,0);auto dirty=e.update(0,nullptr);assert(render(e,dirty,assets,d));
    const double a=angle*3.14159265358979/180;
    assert(inkNear(d,int(120+100*std::sin(a)+0.5),int(120-100*std::cos(a)+0.5)));
    assert(inkNear(d,int(120-20*std::sin(a)+0.5),int(120+20*std::cos(a)+0.5)));
    const Rect box=e.states()[0].bounds;
    for(int y=0;y<240;++y) for(int x=0;x<240;++x) if(ink(d,x,y)) assert(x>=box.x&&x<box.x+box.w&&y>=box.y&&y<box.y+box.h);
  }
  // Arc: a ring of strokeWidth inside `radius`, from `start` clockwise over `sweep`.
  renderOne(arcLayer(50,0,90,4),d);
  assert(ink(d,155,85)&&ink(d,120,70)&&ink(d,170,120)&&ink(d,121,71));
  assert(!ink(d,85,85)&&!ink(d,155,155)&&!ink(d,85,155)&&!ink(d,120,120)&&!ink(d,150,90)&&!ink(d,120,50));
  renderOne(arcLayer(50,0,-90,4),d);        // negative sweep runs counter-clockwise
  assert(ink(d,85,85)&&!ink(d,155,85)&&!ink(d,155,155));
  renderOne(arcLayer(50,270,180,4),d);      // 9 o'clock over the top to 3 o'clock
  assert(ink(d,120,70)&&ink(d,70,120)&&ink(d,170,120)&&!ink(d,120,170));
  renderOne(arcLayer(50,350,20,4),d);       // across the 12 o'clock seam
  assert(ink(d,120,70)&&ink(d,115,71)&&ink(d,125,71)&&!ink(d,170,120)&&!ink(d,70,120));
  renderOne(arcLayer(50,-10,20,4),d);       // a negative start wraps the same way
  assert(ink(d,120,70)&&!ink(d,170,120));
  renderOne(arcLayer(50,0,360,4),d);
  assert(ink(d,120,70)&&ink(d,170,120)&&ink(d,120,170)&&ink(d,70,120)&&!ink(d,120,120)&&!ink(d,120,100));
  renderOne(arcLayer(50,90,-360,4),d);
  assert(ink(d,120,70)&&ink(d,70,120));
  renderOne(arcLayer(50,0,0,4),d);          // sweep 0 draws nothing
  for(int i=0;i<240*240;++i) assert(d.data[i]==0);
  renderOne(arcLayer(10,0,180,10),d);       // strokeWidth >= radius is a filled wedge
  assert(ink(d,120,120)&&ink(d,125,120)&&!ink(d,115,120));
  // Ticks: `count` marks from 12 o'clock, each `length` long, ending on `radius`.
  renderOne(ticksLayer(50,4,10,1),d);
  assert(ink(d,120,70)&&ink(d,120,75)&&ink(d,170,120)&&ink(d,165,120)&&ink(d,120,170)&&ink(d,70,120));
  assert(!ink(d,120,120)&&!ink(d,120,85)&&!ink(d,155,85)&&!ink(d,85,85)&&!ink(d,120,64));
  renderOne(ticksLayer(50,12,10,1),d);
  assert(inkNear(d,int(120+45*0.5+0.5),int(120-45*0.866+0.5))&&inkNear(d,int(120+45*0.866+0.5),int(120-45*0.5+0.5)));
  renderOne(ticksLayer(50,1,10,1),d);       // one tick, at 12 o'clock
  assert(ink(d,120,70)&&!ink(d,170,120)&&!ink(d,120,170));
  renderOne(ticksLayer(50,60,6,1),d);       // the densest dial still draws every mark
  for(int k=0;k<60;++k) {
    const double a=k*6*3.14159265358979/180;
    assert(inkNear(d,int(120+47*std::sin(a)+0.5),int(120-47*std::cos(a)+0.5)));
  }
  renderOne(ticksLayer(30,12,100,2),d);     // a length beyond the radius reaches the center
  assert(ink(d,120,92));
}
// Changing a hand's angle or an arc's sweep repaints only the old and new boxes, and
// every incremental frame equals the same frame drawn from scratch.
static void testRotationRedraw() {
  static Pixels incremental, scratch;
  Theme theme;theme.spec=2;theme.background=0x0010;
  Layer bg;bg.type=LayerType::Shape;bg.shape=Shape::Rectangle;bg.width=240;bg.height=240;bg.hasFill=true;bg.fill=0x0010;
  Layer arc=arcLayer(100,-90,0,10),hand=handLayer(90,25,0,5),minute=handLayer(60,0,0,3),ticks=ticksLayer(112,12,10,3);
  arc.stroke=0x07e0;minute.stroke=0xffff;ticks.stroke=0x8410;
  Binding sweep;sweep.property=BoundProperty::Sweep;sweep.source="d.sweep";sweep.numeric.input0=0;sweep.numeric.input1=100;sweep.numeric.output0=0;sweep.numeric.output1=360;
  Binding angle;angle.property=BoundProperty::Angle;angle.source="d.angle";angle.numeric.input0=0;angle.numeric.input1=360;angle.numeric.output0=0;angle.numeric.output1=360;
  Binding angle2=angle;angle2.source="d.minute";
  Binding radius;radius.property=BoundProperty::Radius;radius.source="d.radius";radius.numeric.input0=0;radius.numeric.input1=100;radius.numeric.output0=0;radius.numeric.output1=100;
  arc.bindings={sweep,radius};hand.bindings={angle};minute.bindings={angle2};
  Layer text;text.type=LayerType::Text;text.value="{d.angle}";text.size=16;text.x=100;text.y=110;
  theme.layers={bg,arc,ticks,hand,minute,text};
  Engine e;Images assets;
  e.setTheme(theme,0);e.setValues({{"d.sweep","0"},{"d.angle","0"},{"d.minute","0"},{"d.radius","100"}});
  assert(render(e,e.update(0,nullptr),assets,incremental));
  uint32_t seed=12345;
  auto next=[&](int n) { seed=seed*1103515245u+12345u;return int((seed>>16)%n); };
  std::vector<ThemeValue> values={{"d.sweep","0"},{"d.angle","0"},{"d.minute","0"},{"d.radius","100"}};
  for(int step=0;step<400;++step) {
    // Move one value at a time as often as several, including onto an unchanged one.
    const int mode=next(5);
    if(mode==0||mode>=4) values[0].value=std::to_string(next(101));
    if(mode==1||mode>=4) values[1].value=std::to_string(next(361));
    if(mode==2||mode>=4) values[2].value=std::to_string(next(361));
    if(mode==3||mode>=4) values[3].value=std::to_string(next(101));
    e.setValues(values);
    auto dirty=e.update(step,nullptr);
    assert(render(e,dirty,assets,incremental));
    Engine fresh;fresh.setTheme(theme,0);fresh.setValues(values);
    assert(render(fresh,fresh.update(0,nullptr),assets,scratch));
    for(int i=0;i<240*240;++i) if(incremental.data[i]!=scratch.data[i]) {
      std::cerr<<"step "<<step<<": pixel ("<<i%240<<","<<i/240<<") differs\n";assert(false);
    }
  }
  // Only the hand moves: the repaint is its old and new boxes, nothing else, and an
  // unchanged value repaints nothing.
  theme.layers={bg,arc,ticks,hand,minute};
  e.setTheme(theme,0);e.setValues({{"d.sweep","50"},{"d.angle","0"},{"d.minute","0"},{"d.radius","100"}});
  e.update(0,nullptr);
  const Rect before=e.states()[3].bounds;
  e.setValues({{"d.sweep","50"},{"d.angle","30"},{"d.minute","0"},{"d.radius","100"}});
  auto dirty=e.update(1,nullptr);
  const Rect after=e.states()[3].bounds;
  assert(before.w*before.h<240*240/4&&after.w*after.h<240*240/4);
  long area=0;
  for(const Rect& r:dirty) area+=r.w*r.h;
  const Rect both=unite(before,after);
  assert(!dirty.empty()&&area<=both.w*both.h&&area<240*240/4);
  e.setValues({{"d.sweep","50"},{"d.angle","30"},{"d.minute","0"},{"d.radius","100"}});
  assert(e.update(2,nullptr).empty());
  // Only the sweep moves: the repaint is the arc's old and new boxes, not the whole ring.
  e.setValues({{"d.sweep","55"},{"d.angle","30"},{"d.minute","0"},{"d.radius","100"}});
  dirty=e.update(3,nullptr);
  area=0;
  for(const Rect& r:dirty) area+=r.w*r.h;
  assert(!dirty.empty()&&area<240*240/2);
}
int main() {
 testScrollRendering();
 testRotationShapes();
 testRotationRedraw();
 testRoundedRectRendering();
 Theme theme;theme.background=0x001f;
 Layer bg;bg.type=LayerType::Shape;bg.shape=Shape::Rectangle;bg.width=240;bg.height=240;bg.hasFill=true;bg.fill=0x001f;
 Layer text;text.type=LayerType::Text;text.value="{SS}";text.size=8;
 Layer animation;animation.type=LayerType::Animation;animation.x=30;animation.width=2;animation.height=2;animation.frames=3;animation.fps=8;animation.source="cat";
 theme.layers={bg,text,animation};Engine e;e.setTheme(theme,0);tm t{};t.tm_sec=0;Images assets;Pixels d;
 assert(render(e,e.update(0,&t),assets,d));assert(d.data[0]==0xffff&&d.data[30]==0x001f&&d.data[31]==0x07e0);
 assert(assets.resolves==1 && assets.reads==2); // resolve once, stream both rows
 int writes=d.writes;assert(e.update(124,&t).empty());assert(d.writes==writes);
 auto dirty=e.update(125,&t);assert(dirty.size()==1&&dirty[0].w==2);assert(e.states()[2].frame==1);
 assert(render(e,dirty,assets,d));assert(d.data[31]==0xf800);
 assert(assets.resolves==2); // the next frame must resolve its own asset
 t.tm_sec=11;assert(render(e,e.update(126,&t),assets,d));assert(d.data[1]==0x001f); // old wide glyph erased
 assert(e.update(375,&t).size()==1&&e.states()[2].frame==0); // no drift, skipped frames
 theme.layers[2].loop=false;e.setTheme(theme,0);e.update(10000,&t);assert(e.states()[2].frame==2);assert(e.update(20000,&t).empty());
 e.setTheme(theme,0xffffffc0);e.update(0xffffffc0,&t);e.update(61,&t);assert(e.states()[2].frame==1); // millis wraps
 e.invalidate();assert(e.update(62,&t)[0].w==240); // notification restore
 // All anchors use the complete cell bounds, independent of glyph ink.
 for(int a=0;a<9;++a){theme.layers={text};theme.layers[0].x=120;theme.layers[0].y=120;theme.layers[0].anchorX=a%3;theme.layers[0].anchorY=a/3;e.setTheme(theme,0);e.update(0,&t);assert(e.states()[0].bounds.x==120-12*(a%3)/2);assert(e.states()[0].bounds.y==120-8*(a/3)/2);}
 // Clipping and strokes, including off-canvas geometry.
 Layer circle;circle.type=LayerType::Shape;circle.shape=Shape::Circle;circle.x=0;circle.y=0;circle.radius=2;circle.hasStroke=true;circle.stroke=0xf800;
 theme.layers={circle};e.setTheme(theme,0);assert(render(e,e.update(0,&t),assets,d));assert(d.data[2]==0xf800&&d.data[3]==0x001f);
 // A full-height asset needs one resolution, and handles remain distinct across layers.
 Layer image;image.type=LayerType::Image;image.width=240;image.height=240;image.source="background";
 Layer front=image;front.x=100;front.width=4;front.source="001";
 theme.layers={image,front};e.setTheme(theme,0);
 int lookups=assets.resolves;
 assert(render(e,e.update(0,&t),assets,d));
 assert(assets.resolves-lookups==2 && d.data[1]==0x07e0 && d.data[101]==0xf800);
 // Resolve once even when several disjoint regions touch the same layer.
 lookups=assets.resolves;
 assert(render(e,{Rect(0,0,5,2),Rect(0,10,5,2)},assets,d));assert(assets.resolves-lookups==1);
 // Data shrinkage must erase the previous glyphs and preserve overlapping layers.
 text.x=100;text.y=100;text.value="{data.value}";text.anchorX=1;
 Layer overlay;overlay.type=LayerType::Shape;overlay.width=1;overlay.height=8;overlay.x=100;overlay.y=100;overlay.hasFill=true;overlay.fill=0xf800;
 theme.layers={image,text,overlay};e.setTheme(theme,0);e.setValues({{"data.value","8888"}});
 assert(render(e,e.update(0,&t),assets,d));assert(d.data[100*240+89]==0xffff);
 e.setValues({{"data.value","1"}});dirty=e.update(1,&t);
 writes=d.writes;assert(render(e,dirty,assets,d));
 assert(d.writes-writes==8 && d.data[100*240+89]==0x07e0 && d.data[100*240+100]==0xf800);
 image.source="missing";theme.layers={image};e.setTheme(theme,0);
 assert(!render(e,e.update(0,&t),assets,d));
 std::cout<<"theme rendering tests passed\n";
}
