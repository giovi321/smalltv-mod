#include "ThemeData.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
using namespace smalltv;

// A socket that can stop halfway through a response, or drip one byte at a time.
struct Response {
  std::string bytes;
  size_t position=0;
  uint32_t start=0, elapsed=0, spacing=0;
  bool closeAtEnd=false, cancel=false;
  uint32_t now() const { return start+elapsed; }
  bool cancelled() const { return cancel; }
  int available() const {
    return position<bytes.size() && elapsed>=position*spacing ? 1 : 0;
  }
  int read(uint8_t* dest,size_t count) {
    if(!count || !available()) return 0;
    *dest=bytes[position++];return 1;
  }
  bool connected() const { return !closeAtEnd || position<bytes.size(); }
  void idle() { ++elapsed;assert(elapsed<=3500); }
};

int main() {
  std::string body;
  Response complete;complete.bytes="{\"value\":42}";
  assert(readDataBody(complete,12,body) && body==complete.bytes && complete.elapsed==0);
  Response stalled;stalled.bytes="{";
  assert(!readDataBody(stalled,12,body) && stalled.elapsed==3000);
  Response disconnected;disconnected.bytes="{";disconnected.closeAtEnd=true;
  assert(!readDataBody(disconnected,12,body));
  Response unknown;unknown.bytes=std::string(2048,' ');unknown.closeAtEnd=true;
  assert(readDataBody(unknown,-1,body) && body.size()==2048);
  Response tooLarge;tooLarge.bytes=std::string(2049,' ');tooLarge.closeAtEnd=true;
  assert(!readDataBody(tooLarge,-1,body) && body.size()<=2048);
  Response oversizedHeader;assert(!readDataBody(oversizedHeader,2049,body));
  Response empty;empty.closeAtEnd=true;assert(!readDataBody(empty,0,body));
  assert(!readDataBody(empty,-1,body));
  Response idleJson;idleJson.bytes="{}"; // silence is not EOF for an unknown length
  assert(!readDataBody(idleJson,-1,body) && idleJson.elapsed==3000);
  Response slow;slow.bytes="{\"value\":42}";slow.spacing=900;
  assert(!readDataBody(slow,12,body) && slow.elapsed==3000);
  Response wrapped;wrapped.start=0xfffffff0;wrapped.bytes="{";
  assert(!readDataBody(wrapped,12,body) && wrapped.elapsed==3000);
  Response cancelled;cancelled.cancel=true;
  assert(!readDataBody(cancelled,12,body) && cancelled.elapsed==0);

  // A slow old request must neither block animation ticks nor update a new theme.
  DataRequests requests;ThemeDataSource source;source.id="old";
  auto job=requests.start(source,0);assert(job);
  std::atomic<bool> release(false);
  std::thread worker([&] {
    while(!release.load()) std::this_thread::yield();
    job->values.push_back({"old.value","42"});job->finish(true);
  });
  Theme theme;Layer sprite;sprite.type=LayerType::Animation;sprite.frames=12;sprite.fps=8;
  sprite.width=sprite.height=4;theme.layers.push_back(sprite);Engine engine;engine.setTheme(theme,0);
  engine.update(0,nullptr);
  for(unsigned frame=1;frame<8;++frame) {
    assert(!requests.take());assert(!engine.update(frame*125,nullptr).empty());
    assert(engine.states()[0].frame==frame);
  }
  requests.cancel();assert(job->cancelled());assert(!requests.start(source,1));
  release.store(true);worker.join();assert(!requests.take() && !requests.busy());
  source.id="new";job=requests.start(source,1);assert(job);
  job->values.push_back({"new.value","7"});job->finish(true);
  auto result=requests.take();assert(result && result->success && result->index==1);
  assert(result->values[0].key=="new.value" && !requests.busy());
  job=requests.start(source,0);job->finish(false);result=requests.take();
  assert(result && !result->success && !requests.busy());
  std::cout<<"theme data deadline and asynchronous result tests passed\n";
}
