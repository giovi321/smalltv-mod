#pragma once
// Compile the real pinned decoder, without unrelated EEPROM/I2C decorators.
#include <StreamUtils/Configuration.hpp>
#include <StreamUtils/Streams/ChunkDecodingStream.hpp>
using StreamUtils::ChunkDecodingStream;
