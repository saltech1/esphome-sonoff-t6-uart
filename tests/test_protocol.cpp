#include "../components/sonoff_t6/protocol.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace esphome::sonoff_t6;

struct Decoder {
  PacketParser parser;
  std::vector<Frame> frames;
  size_t errors{0};
  void feed(const std::vector<uint8_t> &bytes) {
    for (auto byte : bytes) {
      parser.push(byte);
      Frame frame;
      ParseResult result;
      while ((result = parser.next(frame)) != ParseResult::INCOMPLETE) {
        if (result == ParseResult::FRAME) frames.push_back(frame);
        else errors++;
      }
    }
  }
};

static std::vector<uint8_t> make_frame(uint8_t command, std::vector<uint8_t> payload) {
  std::vector<uint8_t> bytes{0xAA, 0x55, 1, command, static_cast<uint8_t>(payload.size())};
  for (auto byte : payload) bytes.push_back(byte);
  uint8_t checksum = 0;
  for (auto byte : bytes) checksum += byte;
  bytes.push_back(checksum);
  return bytes;
}

int main() {
  // All twelve frames from the hardware log: 3 left taps, then 3 right taps.
  Decoder captures;
  const uint8_t log[12][8] = {
    {0xAA,0x55,1,2,2,3,1,8}, {0xAA,0x55,1,2,2,3,0,7},
    {0xAA,0x55,1,2,2,3,1,8}, {0xAA,0x55,1,2,2,3,0,7},
    {0xAA,0x55,1,2,2,3,1,8}, {0xAA,0x55,1,2,2,3,0,7},
    {0xAA,0x55,1,2,2,1,1,6}, {0xAA,0x55,1,2,2,1,0,5},
    {0xAA,0x55,1,2,2,1,1,6}, {0xAA,0x55,1,2,2,1,0,5},
    {0xAA,0x55,1,2,2,1,1,6}, {0xAA,0x55,1,2,2,1,0,5},
  };
  for (const auto &frame : log) {
    // One byte per call verifies that fragmented transport is preserved.
    for (auto byte : frame) captures.feed({byte});
  }
  assert(captures.frames.size() == 12 && captures.errors == 0);
  for (size_t i = 0; i < captures.frames.size(); i++) {
    TouchEvent event;
    assert(decode_touch(captures.frames[i], event));
    assert(event.button_id == (i < 6 ? 3 : 1));
    assert(event.pressed == (i % 2 == 0));
  }

  // Multiple frames per call and arbitrary junk, including overlapping AA headers.
  Decoder joined;
  auto press = make_frame(2, {1, 1});
  auto release = make_frame(2, {1, 0});
  std::vector<uint8_t> bytes{0, 0x55, 0xAA, 0xAA};
  bytes.insert(bytes.end(), press.begin(), press.end());
  bytes.insert(bytes.end(), release.begin(), release.end());
  joined.feed(bytes);
  assert(joined.frames.size() == 2);

  // Bad checksum must not emit an event; recover at the next correct header.
  Decoder corrupt;
  auto bad = press;
  bad.back() ^= 0x01;
  corrupt.feed(bad);
  assert(corrupt.frames.empty() && corrupt.errors == 1);
  corrupt.feed(release);
  assert(corrupt.frames.size() == 1);

  // Reject wrong versions and oversize lengths before buffering payloads.
  Decoder invalid;
  invalid.feed({0xAA, 0x55, 2, 2, 2, 1, 1, 6});
  invalid.feed({0xAA, 0x55, 1, 2, 255});
  invalid.feed(press);
  assert(invalid.errors == 2 && invalid.frames.size() == 1);

  // Header-like payload bytes belong to the payload, not a new frame.
  Decoder payload;
  payload.feed(make_frame(0x81, {0xAA, 0x55}));
  assert(payload.frames.size() == 1 && payload.errors == 0);
  TouchEvent event;
  assert(!decode_touch(payload.frames[0], event));
  for (const auto &packet : {make_frame(2, {0, 1}), make_frame(2, {4, 1}),
                             make_frame(2, {1, 2}), make_frame(2, {1}), make_frame(0x81, {1, 0})}) {
    Decoder unknown;
    unknown.feed(packet);
    assert(unknown.frames.size() == 1 && !decode_touch(unknown.frames[0], event));
  }

  // Simulate expiring an incomplete packet, then receiving the next valid frame.
  Decoder expired;
  expired.feed({0xAA, 0x55, 1, 2, 2});
  assert(expired.parser.size() == 5 && expired.frames.empty());
  expired.parser.clear();
  expired.feed(press);
  assert(expired.frames.size() == 1);

  // Adversarial input cannot exceed the fixed buffer size.
  PacketParser bounded;
  for (int i = 0; i < 10000; i++) {
    bounded.push(static_cast<uint8_t>(i));
    assert(bounded.size() <= MAX_FRAME);
  }
  std::puts("PASS: hardware captures, fragmentation, concatenation, noise, checksums, lengths, events, timeout reset, bounds");
}
