// Stand-in for the PubSubClient library, for the host tests of MqttClient.cpp.
// It copies the one behaviour the memory work depends on: the constructor
// takes the receive buffer from the heap (MQTT_MAX_PACKET_SIZE bytes) and,
// when that fails, leaves getBufferSize() at 0 without any other sign. The
// test sets g_pubsubBufferFails to make it fail, and reads the counters.
#pragma once
#include <stdint.h>
#include <stdlib.h>

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 768
#endif

extern bool g_pubsubBufferFails;   // the next constructor finds no heap for its buffer
extern int  g_pubsubLive;          // objects built and not yet destroyed
extern int  g_pubsubConnects;      // connect() calls
extern int  g_pubsubLoops;         // loop() calls
extern void (*g_pubsubCallback)(char*, uint8_t*, unsigned int);   // as set by setCallback()

class PubSubClient {
 public:
  template <class C> explicit PubSubClient(C&) {
    g_pubsubLive++;
    buffer_ = g_pubsubBufferFails ? nullptr : (uint8_t*)malloc(MQTT_MAX_PACKET_SIZE);
    size_ = buffer_ ? MQTT_MAX_PACKET_SIZE : 0;
  }
  ~PubSubClient() { free(buffer_); g_pubsubLive--; }
  PubSubClient(const PubSubClient&) = delete;
  PubSubClient& operator=(const PubSubClient&) = delete;

  uint16_t getBufferSize() { return size_; }
  PubSubClient& setCallback(void (*cb)(char*, uint8_t*, unsigned int)) { g_pubsubCallback = cb; return *this; }
  PubSubClient& setServer(const char*, uint16_t) { return *this; }
  bool connect(const char*, const char*, const char*, const char*, uint8_t, bool, const char*) {
    g_pubsubConnects++;
    return false;   // the broker is never there
  }
  bool connected() { return up_; }
  void disconnect() { up_ = false; }
  bool loop() { g_pubsubLoops++; return up_; }
  bool publish(const char*, const char*, bool = false) { return up_; }
  bool subscribe(const char*) { return up_; }

 private:
  uint8_t* buffer_ = nullptr;
  uint16_t size_ = 0;
  bool up_ = false;
};
