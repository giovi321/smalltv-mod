#include "ThemeEngine.h"
#include <cassert>
#include <iostream>
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
int main() {
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
