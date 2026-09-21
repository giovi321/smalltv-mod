#include "ThemeDataClient.h"
#if WITH_THEME
#include "Platform.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

namespace {
struct Deadline {
  smalltv::DataFetch& request;
  uint32_t started=millis();
  explicit Deadline(smalltv::DataFetch& r) : request(r) {}
  bool expired() const {
    return request.cancelled() || uint32_t(millis()-started)>=smalltv::DataTimeoutMs;
  }
};

// HTTPClient's timeout is an idle timeout, including for header parsing. Stop
// its transport at the overall deadline too, so trickling headers cannot keep
// the worker alive indefinitely. No other task touches this socket.
template<class Transport>
class BoundedClient : public Transport {
 public:
  explicit BoundedClient(Deadline& deadline) : deadline_(deadline) {}
  using Transport::connect;
  int connect(const char* host,uint16_t port,int32_t timeout) override {
    if(!allowed()) return 0;
    int result=Transport::connect(host,port,timeout);
    return allowed()?result:0;
  }
  int available() override { return allowed()?Transport::available():0; }
  uint8_t connected() override { return allowed()?Transport::connected():0; }
  int read() override { return allowed()?Transport::read():-1; }
  int read(uint8_t* buffer,size_t count) override { return allowed()?Transport::read(buffer,count):-1; }
  size_t write(uint8_t byte) override { return write(&byte,1); }
  size_t write(const uint8_t* buffer,size_t count) override { return allowed()?Transport::write(buffer,count):0; }
 private:
  bool allowed() {
    if(!deadline_.expired()) return true;
    Transport::stop();
    // Also end Stream::readStringUntil's internal timedRead after cancellation.
    this->setTimeout(1);
    return false;
  }
  Deadline& deadline_;
};

struct BodyStream {
  NetClient& client;
  Deadline& deadline;
  uint32_t now() const { return millis(); }
  bool cancelled() const { return deadline.expired(); }
  int available() { return client.available(); }
  bool connected() { return client.connected(); }
  int read(uint8_t* buffer,size_t count) { return client.read(buffer,count); }
  void idle() { delay(1); }
};

std::string jsonValue(JsonVariantConst value) {
  if(value.isNull()) return "--";
  if(value.is<const char*>()) {std::string s=value.as<const char*>();if(s.size()>31)s.resize(31);return s;}
  if(!value.is<bool>()&&!value.is<int>()&&!value.is<long>()&&!value.is<float>()&&!value.is<double>()) return "--";
  char encoded[40]={};size_t n=serializeJson(value,encoded,sizeof(encoded)-1);encoded[n]=0;
  if(n>=sizeof(encoded)-1) return "--";
  return std::string(encoded);
}
JsonVariantConst jsonPath(JsonVariantConst root,const std::string& path) {
  size_t start=0;
  while(start<path.size()) {
    size_t dot=path.find('.',start);std::string part=path.substr(start,dot==std::string::npos?std::string::npos:dot-start);
    if(part.empty()||!root.is<JsonObjectConst>()) return JsonVariantConst();
    root=root[part.c_str()];if(dot==std::string::npos) break;start=dot+1;
  }
  return root;
}
bool fetchThemeSource(smalltv::DataFetch& request) {
  if(request.cancelled()) return false;
  Deadline deadline(request);
  std::unique_ptr<NetClient> client;
  if(request.source.url.rfind("https://",0)==0) {
    if(ESP.getFreeHeap()<18000) return false;
    auto secure=new(std::nothrow) BoundedClient<SecureClient>(deadline);
    if(!secure) return false;
    secure->setInsecure();
    secure->setHandshakeTimeout(smalltv::DataTimeoutMs/1000);
    client.reset(secure);
  } else client.reset(new(std::nothrow) BoundedClient<WiFiClient>(deadline));
  if(!client) return false;
  client->setConnectionTimeout(smalltv::DataTimeoutMs);
  HTTPClient http;
  http.setConnectTimeout(smalltv::DataTimeoutMs);http.setTimeout(smalltv::DataTimeoutMs);
  http.setReuse(false);http.useHTTP10(true);
  if(!http.begin(*client,request.source.url.c_str())) return false;
  http.addHeader("Accept","application/json");
  if(http.GET()!=HTTP_CODE_OK || deadline.expired()) return false;
  std::string body;
  BodyStream stream{*client,deadline};
  bool complete=smalltv::readDataBody(stream,http.getSize(),body);
  http.end();
  if(!complete || request.cancelled()) return false;
  JsonDocument doc;
  if(deserializeJson(doc,body,DeserializationOption::NestingLimit(8))) return false;
  request.values.reserve(request.source.fields.size());
  for(const auto& field:request.source.fields) {
    request.values.push_back({request.source.id+"."+field.id,jsonValue(jsonPath(doc.as<JsonVariantConst>(),field.path))});
  }
  return !request.cancelled();
}
void runFetch(void* parameter) {
  {
    std::unique_ptr<std::shared_ptr<smalltv::DataFetch>> context(
        static_cast<std::shared_ptr<smalltv::DataFetch>*>(parameter));
    auto request=std::move(*context);context.reset();
    request->finish(fetchThemeSource(*request));
  } // release every C++ object before FreeRTOS deletes the task's stack
  vTaskDelete(nullptr);
}
}

bool startThemeDataFetch(const std::shared_ptr<smalltv::DataFetch>& request) {
  constexpr uint32_t StackBytes=8192;
  if(ESP.getFreeHeap()<StackBytes+18000) return false;
  auto context=new(std::nothrow) std::shared_ptr<smalltv::DataFetch>(request);
  if(!context) return false;
  if(xTaskCreate(runFetch,"theme-json",StackBytes,context,1,nullptr)!=pdPASS) {
    delete context;return false;
  }
  return true;
}
#endif
