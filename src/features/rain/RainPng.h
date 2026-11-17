// RainPng.h — on-device PNG decoder for the RainViewer radar tiles.
#pragma once
#include <Arduino.h>
#include <FS.h>

// The decoder's LZ window: 8192 pixels × 1 byte (the colour's index in
// kRainSrc, the RGBA table RainViewer publishes) = 8 KB of heap. 8192 pixels
// cover the native deflate window (32 KB of raw bytes ≈ 8184 pixels): NEVER
// shrink it (at 4096 pixels 89 % of the pixels come out wrong).
//
// It is allocated by the caller for the decode of one tile only, after the
// download (RainClient.cpp, fetchFrame): never held across the network.
#define RAIN_WINDOW_PIXELS 8192

// Small-memory mode, for heaps that cannot give the 8 KB window in one
// piece (the ESP8266 standard image): the decoder keeps only the last
// RAIN_RING_PIXELS pixels in RAM (8 rows of a 256 px tile) and writes the
// image's pixel indices to a RainHistory in segments of that size; a copy
// reaching further back reads them from there. On real tiles ~15 % of the
// copies (~3.5 % of the copied bytes) reach past 8 rows. The output is the
// same as with the full window, byte for byte (tests/rain, tests/fuzz).
#define RAIN_RING_PIXELS 2048

// Where the small-memory mode keeps the pixels that left the ring.
// Segment k holds pixels k*RAIN_RING_PIXELS .. (k+1)*RAIN_RING_PIXELS-1,
// stored once, whole, when its last pixel is decoded. The decoder drops a
// segment once no copy can reach it any more (at most 4 are kept), and
// never reads a segment it has not stored.
class RainHistory {
 public:
  virtual ~RainHistory() {}
  virtual bool put(uint8_t seg, const uint8_t* px) = 0;                     // RAIN_RING_PIXELS bytes
  virtual bool get(uint8_t seg, uint16_t off, uint8_t* px, uint16_t n) = 0;
  virtual void drop(uint8_t seg) = 0;
};

// Decodes `src` (a RainViewer radar tile: RGBA 8-bit, filter "None" on every
// row, as verified on real tiles) into `dst` (already open for writing, at
// offset 0): centre crop cropK x cropK, bilinear upscale to 240x240,
// quantized to 8-bit indices into kRainColors (240 bytes per row, 57,600
// bytes in all), written sequentially. Pixels are premultiplied on black
// before quantization, so the result looks as RainViewer draws it (identical
// to the pixel to a reference zlib decode of 16 live tiles, tests/rain).
//
// `win` is not cleared between the frames of one fetch: a deflate
// back-reference only reads bytes written during the current decode, never
// stale entries (tools/rain_fwtest/test2.cpp: two decodes over the same
// uncleared window give identical outputs).
//
// The tile is untrusted (plain HTTP from a third party). The decoder refuses
// anything outside what it supports instead of guessing: a PNG that is not
// one IHDR (256x256 RGBA 8-bit, deflate, no interlace) then IDAT chunks, a
// bad zlib header, invalid Huffman code lengths (over-subscribed sets, a
// repeat with nothing to repeat, more lengths than declared), the reserved
// length/distance symbols, a copy reaching before the start of the image's
// data, a filter byte other than "None", or more pixel data than the image
// holds. Because of the copy check, the window's old contents can never
// reach the output. tests/fuzz/fuzz_rain.cpp checks all of this.
//
// `hist` null: `win` holds RAIN_WINDOW_PIXELS bytes. `hist` set: the
// small-memory mode, `win` holds RAIN_RING_PIXELS bytes.
//
// Returns false on any error (or a null `win`); `dst` then holds whatever
// was written before the decoder stopped (the caller decodes into a
// temporary file and drops it).
bool rainDecodeToFs(File& src, File& dst, int tileSize, int cropK, uint8_t* win,
                    RainHistory* hist = nullptr);

// The last rainDecodeToFs() failed for lack of heap for its work area
// (~4.5 KB in five pieces of at most ~1.7 KB).
bool rainDecodeNoMem();
// True when the last rainDecodeToFs() failed because a write to `dst` came up
// short (filesystem full): the output is incomplete and must be dropped.
bool rainDecodeWriteFail();
// True when the last rainDecodeToFs() stopped because free heap fell under
// the decode's floor (rainDecodeHeapLow()): the tile is skipped, not broken.
bool rainDecodeLowHeap();
// True when the last rainDecodeToFs() stopped because its time budget was
// spent (rainDecodeOverTime()): a tile too slow to decode, skipped.
bool rainDecodeTimedOut();

// Called once per output row while decoding, to sample the heap low-water
// mark. RainPng.cpp has an empty weak default (host builds); RainClient.cpp
// provides the real one.
void rainHeapSample();
// Called regularly during a decode to let the system run. Weak default in
// RainPng.cpp: yield(). On the ESP8266, RainClient.cpp yields only when the
// heap leaves the network stack room (RAIN_YIELD_MIN_HEAP) and feeds the
// watchdog otherwise.
void rainDecodeYield();
// The decode's heap floor, checked once its buffers are allocated and at
// every output row: true stops the decode (rainDecodeLowHeap()). Weak default
// in RainPng.cpp: false. RainClient.cpp: free heap < RAIN_DECODE_MIN_HEAP.
bool rainDecodeHeapLow();
// The decode's time budget, checked before each history read of the
// small-memory mode, at every output row and every 1024 blocks or symbols:
// true stops the decode (rainDecodeTimedOut()). Weak default in RainPng.cpp:
// false. RainClient.cpp: the pass has run for over RAIN_DECODE_MAX_MS.
bool rainDecodeOverTime();
