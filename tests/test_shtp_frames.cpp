/**
 * @file test_shtp_frames.cpp
 * @brief The SHTP engine on its own: headers, rules V1 to V12, UART framing and the advertisement.
 */
#include <array>
#include <bit>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "bno08x.hpp"
#include "test_harness.hpp"

using namespace bno08x;
using namespace bno08x::detail;

namespace {

using V = std::vector<uint8_t>;

/// The bytes of a text (a character is never used as a number: it is converted bit for bit).
V Str(std::string_view s) {
  V out;
  for (const char c : s) {
    out.push_back(std::bit_cast<uint8_t>(c));
  }
  return out;
}

V Join(std::initializer_list<V> parts) {
  V out;
  for (const V& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

V Hdr(uint32_t len, bool cont, uint8_t ch, uint8_t seq, const V& cargo = {}) {
  V v(4);
  EncodeHeader(v, len, ch, seq);
  if (cont) {
    v[1] = static_cast<uint8_t>(v[1] | 0x80U);
  }
  v.insert(v.end(), cargo.begin(), cargo.end());
  return v;
}

struct Eng {
  Stats stats{};
  Config cfg{};
  RxEngine<64> e{stats, cfg};
  RxResult Feed(const V& t) {
    return e.Accept(std::span<const uint8_t>{t});
  }
};

} // namespace

TEST_CASE("shtp: header encode and decode, length bits and the continuation bit") {
  const V h = Hdr(0x1234, true, 3, 9);
  const ShtpHeader d = DecodeHeader(h);
  CHECK_EQ(d.length, 0x1234U);
  CHECK(d.cont);
  CHECK_EQ(d.channel, 3U);
  CHECK_EQ(d.seq, 9U);
  const ShtpHeader n = DecodeHeader(Hdr(5, false, 0, 0));
  CHECK(!n.cont);
  CHECK(n == DecodeHeader(Hdr(5, false, 0, 0)));
}

TEST_CASE("shtp: little-endian helpers and signed conversions") {
  const std::array<uint8_t, 4> b{0x78, 0x56, 0x34, 0x12};
  CHECK_EQ(Le16(b, 0), 0x5678U);
  CHECK_EQ(Le32(b, 0), 0x12345678U);
  CHECK_EQ(S16(0xFFFF), -1);
  CHECK_EQ(S16(0x8000), -32768);
  CHECK_EQ(S16(0x7FFF), 32767);
  CHECK_EQ(S32(0xFFFFFFFFU), -1);
  CHECK_EQ(S32(0x80000000U), static_cast<int32_t>(-2147483647 - 1));
  std::array<uint8_t, 4> o{};
  PutLe32(o, 0, 0xA1B2C3D4U);
  CHECK_EQ(o[0], 0xD4U);
  CHECK_EQ(o[3], 0xA1U);
}

TEST_CASE("shtp: V1 a transfer shorter than a header") {
  Eng g;
  CHECK(g.Feed(V{1, 2, 3}) == RxResult::Violation);
  CHECK_EQ(g.stats.short_transfer, 1U);
}

TEST_CASE("shtp: V2 length 0xFFFF, V3 length 0, V4 lengths 1 to 3") {
  Eng g;
  CHECK(g.Feed(V{0xFF, 0xFF, 0, 0}) == RxResult::Violation);
  CHECK_EQ(g.stats.length_ffff, 1U);
  CHECK(g.Feed(V{0, 0, 0, 0}) == RxResult::Null);
  CHECK_EQ(g.stats.null_header, 1U);
  CHECK(g.Feed(V{3, 0, 0, 0}) == RxResult::Violation);
  CHECK_EQ(g.stats.length_short, 1U);
  CHECK(g.Feed(V{4, 0, 0, 0}) == RxResult::Null); // a header with no cargo
}

TEST_CASE("shtp: a cargo in one transfer, and in two (header then continuation)") {
  Eng g;
  CHECK(g.Feed(Hdr(7, false, 3, 0, V{1, 2, 3})) == RxResult::CargoComplete);
  CHECK_EQ(g.e.Cargo().size(), 3U);
  CHECK_EQ(g.e.Channel(), 3U);
  CHECK(g.Feed(Hdr(9, false, 2, 0)) == RxResult::Partial);
  CHECK(g.e.Open());
  CHECK_EQ(g.e.NextReadLen(100), 9U); // remaining 5 + 4
  CHECK(g.Feed(Hdr(9, true, 2, 1, V{1, 2, 3, 4, 5})) == RxResult::CargoComplete);
  CHECK_EQ(g.e.Cargo().size(), 5U);
  CHECK(!g.e.Open());
}

TEST_CASE("shtp: V5 an oversize cargo is read and discarded") {
  Eng g;
  CHECK(g.Feed(Hdr(104, false, 3, 0)) == RxResult::Partial);
  CHECK_EQ(g.stats.oversize_cargo, 1U);
  CHECK(g.Feed(Hdr(104, true, 3, 1, V(100, 0))) == RxResult::Dropped);
  CHECK(!g.e.Open());
}

TEST_CASE("shtp: V6 a bad channel is counted once and its cargo is consumed") {
  Eng g;
  CHECK(g.Feed(Hdr(8, false, 9, 0)) == RxResult::Partial);
  CHECK_EQ(g.stats.bad_channel, 1U);
  CHECK(g.e.Open());
  CHECK(g.Feed(Hdr(8, true, 9, 1, V{1, 2, 3, 4})) == RxResult::Dropped);
  CHECK_EQ(g.stats.bad_channel, 1U);
  CHECK(!g.e.Open());
  CHECK(g.Feed(Hdr(4, false, 9, 0)) == RxResult::Dropped);
  CHECK(g.Feed(Hdr(8, true, 9, 0, V{1, 2, 3, 4})) ==
        RxResult::Dropped); // an orphan on a bad channel
  CHECK_EQ(g.stats.bad_channel, 3U);
}

TEST_CASE("shtp: V7 a new header while a cargo is open loses the old cargo") {
  Eng g;
  CHECK(g.Feed(Hdr(10, false, 3, 0)) == RxResult::Partial);
  CHECK(g.Feed(Hdr(6, false, 3, 1, V{1, 2})) == RxResult::CargoComplete);
  CHECK_EQ(g.stats.lost_cargo, 1U);
}

TEST_CASE("shtp: V8 an orphan continuation, V9 another channel, V10 the wrong length") {
  Eng g;
  CHECK(g.Feed(Hdr(8, true, 3, 0, V{1, 2, 3, 4})) == RxResult::Dropped);
  CHECK_EQ(g.stats.orphan_cont, 1U);
  CHECK(g.Feed(Hdr(10, false, 3, 1)) == RxResult::Partial);
  CHECK(g.Feed(Hdr(10, true, 4, 0, V{1, 2, 3, 4, 5, 6})) == RxResult::Violation);
  CHECK_EQ(g.stats.lost_cargo, 1U);
  CHECK(g.Feed(Hdr(10, false, 3, 2)) == RxResult::Partial);
  CHECK(g.Feed(Hdr(12, true, 3, 3, V{1, 2, 3, 4, 5, 6})) == RxResult::Violation);
  CHECK_EQ(g.stats.bad_cont_len, 1U);
}

TEST_CASE("shtp: V10 relaxed by strict_cont_len = false") {
  Eng g;
  g.cfg.strict_cont_len = false;
  CHECK(g.Feed(Hdr(10, false, 3, 0)) == RxResult::Partial);
  CHECK(g.Feed(Hdr(12, true, 3, 1, V{1, 2, 3, 4, 5, 6})) == RxResult::CargoComplete);
}

TEST_CASE("shtp: V11 sequence numbers: duplicates are dropped, gaps counted, wrap is fine") {
  Eng g;
  CHECK(g.Feed(Hdr(5, false, 3, 254, V{1})) == RxResult::CargoComplete);
  CHECK(g.Feed(Hdr(5, false, 3, 255, V{1})) == RxResult::CargoComplete);
  CHECK(g.Feed(Hdr(5, false, 3, 0, V{1})) == RxResult::CargoComplete);
  CHECK_EQ(g.stats.seq_gap, 0U);
  CHECK(g.Feed(Hdr(5, false, 3, 0, V{1})) == RxResult::Dropped);
  CHECK_EQ(g.stats.dup_seq, 1U);
  CHECK(g.Feed(Hdr(5, false, 3, 5, V{1})) == RxResult::CargoComplete);
  CHECK_EQ(g.stats.seq_gap, 1U);
  CHECK(g.Feed(Hdr(5, false, 4, 77, V{1})) ==
        RxResult::CargoComplete); // another channel: own baseline
  g.e.Reset();
  CHECK(g.Feed(Hdr(5, false, 3, 5, V{1})) ==
        RxResult::CargoComplete); // after a reset the baseline is gone
}

TEST_CASE("shtp: V12 padding after the cargo is counted only in strict mode") {
  Eng g;
  g.cfg.strict_padding = true;
  CHECK(g.Feed(Hdr(5, false, 3, 0, V{1, 0, 9, 0})) == RxResult::CargoComplete);
  CHECK_EQ(g.stats.nonzero_pad, 1U);
  CHECK(g.Feed(Hdr(5, false, 3, 1, V{1, 0, 0, 0})) == RxResult::CargoComplete);
  CHECK_EQ(g.stats.nonzero_pad, 1U);
}

TEST_CASE("shtp: NextReadLen for the policies and the transfer limit") {
  Eng g;
  CHECK_EQ(g.e.NextReadLen(100), 4U);
  g.cfg.read_policy = ReadPolicy::Fixed;
  g.cfg.fixed_read_bytes = 64;
  CHECK_EQ(g.e.NextReadLen(100), 64U);
  CHECK_EQ(g.e.NextReadLen(32), 32U);
  g.e.Abort();
  CHECK(!g.e.Open());
}

TEST_CASE("uart framing: flags, escapes and protocol IDs") {
  Stats st{};
  UartFramer<32> f(st);
  const auto feed = [&f](const V& v) {
    FrameEvent last = FrameEvent::None;
    for (const uint8_t b : v) {
      const FrameEvent r = f.Feed(b);
      if (r == FrameEvent::Frame) {
        last = r;
      }
    }
    return last;
  };
  CHECK(feed(V{0x00, 0x11, 0x7E, 0x01, 0x05, 0x7D, 0x5E, 0x7D, 0x5D, 0x7E}) == FrameEvent::Frame);
  REQUIRE(f.Frame().size() == 4U);
  CHECK_EQ(f.Frame()[0], 1U);
  CHECK_EQ(f.Frame()[2], 0x7EU);
  CHECK_EQ(f.Frame()[3], 0x7DU);
  CHECK(feed(V{0x7E, 0x7E, 0x00, 0x7E}) == FrameEvent::Frame); // two flags are one start flag
  CHECK_EQ(f.Frame().size(), 1U);
  CHECK(feed(V{0x7E, 0x07, 1, 0x7E}) == FrameEvent::None);
  CHECK_EQ(st.bad_protocol_id, 1U);
  CHECK(feed(V{0x7E, 0x01, 0x7D, 0x7E, 0x01, 0x02, 0x7E}) ==
        FrameEvent::Frame); // escape then flag restarts
  CHECK_EQ(st.bad_escape, 1U);
  f.Reset();
  CHECK(feed(V{0x01, 0x02, 0x7E}) == FrameEvent::None); // no start flag after a reset
}

TEST_CASE("uart framing: an overlong frame is dropped at its end flag") {
  Stats st{};
  UartFramer<8> f(st);
  FrameEvent r = FrameEvent::None;
  r = f.Feed(0x7E);
  for (int32_t i = 0; i < 20; ++i) {
    r = f.Feed(0x01);
  }
  CHECK(r == FrameEvent::None);
  r = f.Feed(0x7E);
  CHECK(r == FrameEvent::None);
  CHECK_EQ(st.oversize_frame, 1U);
  CHECK(f.Frame().size() <= 8U);
  // exactly N bytes is still a frame, N + 1 is not
  f.Reset();
  static_cast<void>(f.Feed(0x7E));
  for (int32_t i = 0; i < 8; ++i) {
    static_cast<void>(f.Feed(i == 0 ? 0x01 : 0x55));
  }
  CHECK(f.Feed(0x7E) == FrameEvent::Frame);
  CHECK_EQ(f.Frame().size(), 8U);
  static_cast<void>(f.Feed(0x7E));
  for (int32_t i = 0; i < 9; ++i) {
    static_cast<void>(f.Feed(i == 0 ? 0x01 : 0x55));
  }
  CHECK(f.Feed(0x7E) == FrameEvent::None);
  CHECK_EQ(st.oversize_frame, 2U);
  // after the drop the framer still works
  CHECK(f.Feed(0x01) == FrameEvent::None);
  CHECK(f.Feed(0x7E) == FrameEvent::None);
}

TEST_CASE("uart framing: encode with stuffing, and refuse a frame that does not fit") {
  std::array<uint8_t, 16> out{};
  const std::array<uint8_t, 3> payload{0x7E, 0x01, 0x7D};
  const std::size_t n = EncodeUartFrame(out, 1, payload);
  REQUIRE(n == 8U);
  CHECK_EQ(out[0], 0x7EU);
  CHECK_EQ(out[1], 0x01U);
  CHECK_EQ(out[2], 0x7DU);
  CHECK_EQ(out[3], 0x5EU);
  CHECK_EQ(out[n - 1U], 0x7EU);
  std::array<uint8_t, 5> small{};
  CHECK_EQ(EncodeUartFrame(small, 1, payload), 0U);
  std::array<uint8_t, 2> tiny{};
  CHECK_EQ(EncodeUartFrame(tiny, 0, std::span<const uint8_t>{}), 0U);
  std::array<uint8_t, 3> bsq{};
  CHECK_EQ(EncodeUartFrame(bsq, 0, std::span<const uint8_t>{}), 3U);
}

TEST_CASE("advertisement: limits, channels, UART timeout and report lengths") {
  const V tlv = Join({
      V{0x01, 4, 0, 0, 0, 0},        // GUID 0
      V{0x02, 2, 0x80, 0},           // max cargo write 128
      V{0x03, 2, 0x00, 0x08},        // max cargo read 2048
      V{0x04, 2, 0x40, 0},           // max transfer write
      V{0x05, 2, 0x00, 0x04},        // max transfer read
      V{0x81, 4, 0xC8, 0, 0, 0},     // UART timeout 200 ms
      V{0x06, 1, 4, 0x09, 12},       // channel 4 is named ...
      Str("inputNormal"),            // ... "inputNormal" (11 characters)
      V{0, 0x01, 4, 2, 0, 0, 0},     // terminator, then GUID 2
      V{0x81, 4, 0x01, 12, 0x2B, 6}, // report lengths: 0x01 -> 12, 0x2B -> 6
      V{0x55, 2, 1, 2},              // an unknown tag is ignored
  });
  Advert a = DefaultAdvert();
  bool bad = true;
  ParseAdvertisement(tlv, a, bad);
  CHECK(!bad);
  CHECK(a.valid);
  CHECK_EQ(a.max_cargo_write, 128U);
  CHECK_EQ(a.max_cargo_read, 2048U);
  CHECK_EQ(a.max_transfer_write, 64U);
  CHECK_EQ(a.max_transfer_read, 1024U);
  CHECK_EQ(a.uart_timeout_ms, 200U);
  CHECK_EQ(a.channel[reg::kChanInputNormal], 4U);
  CHECK_EQ(a.report_len[0x01], 12U);
  CHECK_EQ(a.report_len[0x02], 10U); // the built-in length stays
  CHECK_EQ(a.report_len[0x2B], 6U);  // the last pair of the TLV is used too
}

TEST_CASE("advertisement: a truncated TLV is reported and the defaults survive") {
  const V tlv{0x02, 2, 0x80};
  Advert a = DefaultAdvert();
  bool bad = false;
  ParseAdvertisement(tlv, a, bad);
  CHECK(bad);
  CHECK_EQ(a.max_cargo_write, 0U);
  const V names =
      Join({V{0x06, 1, 3, 0x09, 6}, Str("device"), V{0x06, 1, 1, 0x09, 8}, Str("executa")});
  bool bad2 = false;
  Advert b = DefaultAdvert();
  ParseAdvertisement(names, b, bad2);
  CHECK(b.valid);
}

/// A channel name with its terminating zero, as the advertisement carries it.
template <std::size_t N>
std::array<uint8_t, N> Name(const char (&s)[N]) {
  std::array<uint8_t, N> a{};
  for (std::size_t i = 0; i < N; ++i) {
    a[i] = std::bit_cast<uint8_t>(s[i]);
  }
  return a;
}

TEST_CASE("advertisement: channel names map to roles") {
  CHECK_EQ(ChannelRole(Name("control")), reg::kChanControl);
  CHECK_EQ(ChannelRole(Name("inputWake")), reg::kChanInputWake);
  CHECK_EQ(ChannelRole(Name("inputGyroRv")), reg::kChanGyroRv);
  CHECK_EQ(ChannelRole(Name("executable")), reg::kChanExecutable);
  CHECK_EQ(ChannelRole(Name("other")), reg::kChannelCount);
}

TEST_CASE("shtp: V10 a continuation shorter than the rest is refused too") {
  Eng g;
  CHECK(g.Feed(Hdr(10, false, 3, 0)) == RxResult::Partial);
  CHECK(g.Feed(Hdr(8, true, 3, 1, V{1, 2, 3, 4})) == RxResult::Violation);
  CHECK_EQ(g.stats.bad_cont_len, 1U);
}

TEST_CASE("shtp: a sequence gap inside a cargo loses it, whatever strict_cont_len says") {
  for (const bool strict : {true, false}) {
    Eng g;
    g.cfg.strict_cont_len = strict;
    CHECK(g.Feed(Hdr(10, false, 3, 0, V{1, 2})) == RxResult::Partial);
    CHECK(g.Feed(Hdr(8, true, 3, 2, V{3, 4, 5, 6})) == RxResult::Dropped); // seq 1 is missing
    CHECK_EQ(g.stats.lost_cargo, 1U);
    CHECK(!g.e.Open());
    CHECK(g.Feed(Hdr(5, false, 3, 3, V{9})) == RxResult::CargoComplete); // the next one is fine
  }
}

TEST_CASE("shtp: V6 a continuation on another unknown channel is counted again") {
  Eng g;
  CHECK(g.Feed(Hdr(12, false, 9, 0)) == RxResult::Partial);
  CHECK(g.Feed(Hdr(12, true, 8, 0, V{1, 2, 3, 4})) == RxResult::Dropped);
  CHECK_EQ(g.stats.bad_channel, 2U);
}

TEST_CASE("shtp: V5 a cargo of exactly the buffer size is kept") {
  Eng g;
  CHECK(g.Feed(Hdr(68, false, 3, 0, V(64, 7))) == RxResult::CargoComplete);
  CHECK_EQ(g.stats.oversize_cargo, 0U);
  CHECK_EQ(g.e.Cargo().size(), 64U);
}

TEST_CASE("shtp: a channel name must match whole, not as a prefix") {
  const std::array<uint8_t, 8> longer{'c', 'o', 'n', 't', 'r', 'o', 'l', 'X'};
  CHECK_EQ(bno08x::detail::ChannelRole(longer), reg::kChannelCount);
  const std::array<uint8_t, 8> exact{'c', 'o', 'n', 't', 'r', 'o', 'l', 0};
  CHECK_EQ(bno08x::detail::ChannelRole(exact), reg::kChanControl);
}

TEST_CASE("shtp: V12 the first padding byte after the cargo is checked too") {
  Eng g;
  g.cfg.strict_padding = true;
  CHECK(g.Feed(Hdr(5, false, 3, 0, V{1, 9})) == RxResult::CargoComplete);
  CHECK_EQ(g.stats.nonzero_pad, 1U);
}
