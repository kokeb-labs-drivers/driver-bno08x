/**
 * @file bno08x_shtp.hpp
 * @brief The SHTP frame engine: header codec, strict receive validation (rules V1 to V12),
 * the UART framer and the advertisement parser.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is independent of the bus and of the port. The link policies in `bno08x.hpp`
 * move bytes; this file decides what they mean (SHTP rev 1.10: S2 framing, S4 UART, S5 command
 * channel and advertisement).
 */
#ifndef BNO08X_SHTP_HPP
#define BNO08X_SHTP_HPP

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bno08x_detail.hpp"
#include "bno08x_registers.hpp"
#include "bno08x_types.hpp"

namespace bno08x::detail {

/// A decoded SHTP header (SHTP S2.2.1 p4, DS Figure 1-26 p22).
struct ShtpHeader {
  uint16_t length{0}; ///< Bits 14:0: header plus cargo bytes; 0xFFFF never appears here.
  bool cont{false};   ///< Bit 15: continuation of an earlier transfer.
  uint8_t channel{0}; ///< Channel.
  uint8_t seq{0};     ///< Sequence number of this channel and direction.
  /// Hidden-friend equality of all fields.
  friend constexpr bool operator==(const ShtpHeader& lhs, const ShtpHeader& rhs) noexcept = default;
};

/// The raw 16-bit length field that marks a failed peripheral (SHTP S2.2.1).
inline constexpr uint32_t kLengthError = 0xFFFF;

/// Little-endian 16-bit value at `p[off]`; the caller has checked the span.
[[nodiscard]] constexpr uint16_t Le16(std::span<const uint8_t> p, std::size_t off) noexcept {
  return static_cast<uint16_t>(static_cast<uint32_t>(p[off]) |
                               (static_cast<uint32_t>(p[off + 1U]) << 8U));
}

/// Little-endian 32-bit value at `p[off]`; the caller has checked the span.
[[nodiscard]] constexpr uint32_t Le32(std::span<const uint8_t> p, std::size_t off) noexcept {
  return static_cast<uint32_t>(Le16(p, off)) | (static_cast<uint32_t>(Le16(p, off + 2U)) << 16U);
}

/// Signed 16-bit value from a little-endian pair (a named reinterpretation, KB-CPP20-024).
[[nodiscard]] constexpr int32_t S16(uint16_t raw) noexcept {
  return static_cast<int32_t>((static_cast<uint32_t>(raw) ^ 0x8000U)) - 0x8000;
}

/// Signed 32-bit value of a raw word.
[[nodiscard]] constexpr int32_t S32(uint32_t raw) noexcept {
  return static_cast<int32_t>(static_cast<int64_t>(raw ^ 0x8000'0000U) - 0x8000'0000LL);
}

/// Stores `v` little-endian at `p[off]`.
constexpr void PutLe16(std::span<uint8_t> p, std::size_t off, uint32_t v) noexcept {
  p[off] = static_cast<uint8_t>(v & 0xFFU);
  p[off + 1U] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
}

/// Stores `v` little-endian at `p[off]`.
constexpr void PutLe32(std::span<uint8_t> p, std::size_t off, uint32_t v) noexcept {
  PutLe16(p, off, v & 0xFFFFU);
  PutLe16(p, off + 2U, (v >> 16U) & 0xFFFFU);
}

/// Decodes the first four bytes of `bytes` (at least four, checked by the caller).
[[nodiscard]] constexpr ShtpHeader DecodeHeader(std::span<const uint8_t> bytes) noexcept {
  const uint32_t raw = Le16(bytes, 0U);
  return ShtpHeader{.length = static_cast<uint16_t>(raw & 0x7FFFU),
                    .cont = (raw & 0x8000U) != 0U,
                    .channel = bytes[2],
                    .seq = bytes[3]};
}

/// Writes a header (never with the continuation bit: the hub does not accept fragmented writes).
constexpr void EncodeHeader(std::span<uint8_t> out, uint32_t total_len, uint8_t channel,
                            uint8_t seq) noexcept {
  PutLe16(out, 0U, total_len & 0x7FFFU);
  out[2] = channel;
  out[3] = seq;
}

/// What `RxEngine::Accept` did with a transfer.
enum class RxResult : uint8_t {
  CargoComplete, ///< A whole cargo is ready: `Cargo()` and `Channel()`.
  Partial,       ///< The cargo is open and waits for a continuation.
  Null,          ///< A null header or an empty cargo: nothing to do.
  Dropped,       ///< The transfer was discarded (counted in `Stats`).
  Violation,     ///< A rule was broken; the engine has dropped its cargo and wants a resync.
};

/**
 * @brief The receive side of SHTP: validates every transfer and assembles cargoes.
 *
 * Rules V1 to V12 of the design: a transfer shorter than a header, length 0xFFFF, null header,
 * length 1 to 3, an oversize cargo (read and discarded), a bad channel, a new header while a cargo
 * is open, an orphan continuation, a continuation on another channel, a continuation of the wrong
 * length, duplicate or skipped sequence numbers, and padding. Every deviation is counted in the
 * `Stats` the engine is given.
 *
 * @tparam N Receive buffer size in bytes (the largest cargo).
 */
template <std::size_t N>
class RxEngine {
public:
  /// The engine counts into `stats` and uses `cfg` for its strict knobs.
  RxEngine(Stats& stats, const Config& cfg) noexcept : stats_(stats), cfg_(cfg) {}

  /// Forget the open cargo and the sequence baselines (after a reset of the hub or the session).
  void Reset() noexcept {
    open_ = false;
    discard_ = false;
    have_ = 0;
    want_ = 0;
    seq_valid_.fill(false);
  }

  /// Drop the open cargo only (a resynchronisation).
  void Abort() noexcept {
    open_ = false;
    discard_ = false;
    have_ = 0;
    want_ = 0;
  }

  /// A cargo is open and its remaining bytes will arrive as a continuation.
  [[nodiscard]] bool Open() const noexcept {
    return open_;
  }

  /// Bytes the next read should ask for, header included: the rest of an open cargo (at most
  /// `max_transfer`), else 4 for HeaderThenRest or `fixed` for Fixed.
  [[nodiscard]] std::size_t NextReadLen(std::size_t max_transfer) const noexcept {
    std::size_t n = 0;
    if (open_) {
      n = (want_ - have_) + reg::kShtpHeaderLen;
    } else if (cfg_.read_policy == ReadPolicy::Fixed) {
      n = cfg_.fixed_read_bytes;
    } else {
      n = reg::kShtpHeaderLen;
    }
    return std::min(n, max_transfer);
  }

  /// The completed cargo (valid after `CargoComplete`, until the next `Accept`).
  [[nodiscard]] std::span<const uint8_t> Cargo() const noexcept {
    return std::span<const uint8_t>{buf_}.first(done_len_);
  }

  /// The channel of the completed cargo.
  [[nodiscard]] uint8_t Channel() const noexcept {
    return chan_;
  }

  /// Process one received transfer (header first). `max_channel` is the highest valid channel.
  RxResult Accept(std::span<const uint8_t> transfer) noexcept {
    if (transfer.size() < reg::kShtpHeaderLen) {
      ++stats_.short_transfer;
      Abort();
      return RxResult::Violation;
    }
    const uint32_t raw = Le16(transfer, 0U);
    if (raw == kLengthError) {
      ++stats_.length_ffff;
      Abort();
      return RxResult::Violation;
    }
    const ShtpHeader h = DecodeHeader(transfer);
    if (h.length == 0U) {
      ++stats_.null_header;
      return RxResult::Null;
    }
    if (h.length < reg::kShtpHeaderLen) {
      ++stats_.length_short;
      Abort();
      return RxResult::Violation;
    }
    if (h.channel >= reg::kChannelCount) {
      return badChannel(h, transfer);
    }
    if (!checkSequence(h)) {
      return RxResult::Dropped;
    }
    const RxResult head = h.cont ? continuation(h) : startCargo(h);
    if (head != RxResult::Partial) {
      return head;
    }
    return take(transfer.subspan(reg::kShtpHeaderLen));
  }

private:
  /// V6: a header on a channel the engine does not know. The cargo is still read to its end and
  /// discarded, so the hub is not left with a half-read transfer; it is counted once.
  RxResult badChannel(const ShtpHeader& h, std::span<const uint8_t> transfer) noexcept {
    const bool again = h.cont && open_ && discard_ && (chan_ == h.channel);
    if (!again) {
      ++stats_.bad_channel;
      Abort();
      if (h.cont || (h.length <= reg::kShtpHeaderLen)) {
        return RxResult::Dropped;
      }
      chan_ = h.channel;
      want_ = static_cast<std::size_t>(h.length) - reg::kShtpHeaderLen;
      have_ = 0;
      open_ = true;
      discard_ = true;
    }
    return take(transfer.subspan(reg::kShtpHeaderLen));
  }

  /// V11: per-channel sequence numbers; a duplicate drops the transfer, a gap is counted.
  /// After a restart every channel numbers from 0 again, so the advertisement (channel 0) and the
  /// reset-complete (channel 1) that announce it may repeat the last number. A new cargo on those
  /// two channels is never dropped as a duplicate; the header alone decides, since I2C and SPI read
  /// it before the payload.
  [[nodiscard]] static bool restartSignal(const ShtpHeader& h) noexcept {
    const bool command = h.channel == reg::kChanCommand;
    const bool executable = h.channel == reg::kChanExecutable;
    return !h.cont && (command || executable);
  }

  bool checkSequence(const ShtpHeader& h) noexcept {
    const std::size_t c = h.channel;
    bool keep = true;
    gap_ = false;
    if (seq_valid_[c]) {
      const auto expected = static_cast<uint8_t>(static_cast<uint32_t>(last_seq_[c] + 1U) & 0xFFU);
      if ((h.seq == last_seq_[c]) && !restartSignal(h)) {
        ++stats_.dup_seq;
        keep = false;
      } else if (h.seq != expected) {
        ++stats_.seq_gap;
        gap_ = true;
      } else {
        // the expected number
      }
    }
    if (keep) {
      seq_valid_[c] = true;
      last_seq_[c] = h.seq;
    }
    return keep;
  }

  /// V8, V9, V10 for a header with the continuation bit.
  RxResult continuation(const ShtpHeader& h) noexcept {
    if (!open_) {
      ++stats_.orphan_cont;
      return RxResult::Dropped;
    }
    if (h.channel != chan_) {
      ++stats_.lost_cargo;
      Abort();
      return RxResult::Violation;
    }
    if (gap_) { // a fragment between is missing: the cargo cannot be put together (CEVA rxAssemble)
      ++stats_.lost_cargo;
      Abort();
      return RxResult::Dropped;
    }
    const std::size_t remaining = want_ - have_;
    if (cfg_.strict_cont_len && (h.length != (remaining + reg::kShtpHeaderLen))) {
      ++stats_.bad_cont_len;
      Abort();
      return RxResult::Violation;
    }
    return RxResult::Partial;
  }

  /// V5, V7 for a header without the continuation bit: open a new cargo.
  RxResult startCargo(const ShtpHeader& h) noexcept {
    if (open_) {
      ++stats_.lost_cargo; // V7: the old cargo is lost, this header starts a new one
      Abort();
    }
    const std::size_t cargo = static_cast<std::size_t>(h.length) - reg::kShtpHeaderLen;
    if (cargo == 0U) {
      ++stats_.null_header;
      return RxResult::Null;
    }
    chan_ = h.channel;
    want_ = cargo;
    have_ = 0;
    open_ = true;
    discard_ = cargo > N;
    if (discard_) {
      ++stats_.oversize_cargo;
    }
    return RxResult::Partial;
  }

  /// Takes the cargo bytes of a transfer (V12 for what follows the cargo).
  RxResult take(std::span<const uint8_t> data) noexcept {
    const std::size_t room = want_ - have_;
    const std::size_t n = std::min(room, data.size());
    if (!discard_) {
      std::copy(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(n),
                buf_.begin() + static_cast<std::ptrdiff_t>(have_));
    }
    have_ += n;
    if (cfg_.strict_padding) {
      for (std::size_t i = n; i < data.size(); ++i) {
        if (data[i] != 0U) {
          ++stats_.nonzero_pad;
          break;
        }
      }
    }
    if (have_ < want_) {
      return RxResult::Partial;
    }
    open_ = false;
    if (discard_) {
      discard_ = false;
      return RxResult::Dropped;
    }
    done_len_ = want_;
    return RxResult::CargoComplete;
  }

  Stats& stats_;
  const Config& cfg_;
  std::array<uint8_t, N> buf_{};
  std::size_t have_{0};
  std::size_t want_{0};
  std::size_t done_len_{0};
  uint8_t chan_{0};
  bool open_{false};
  bool discard_{false};
  bool gap_{false}; ///< The last header broke its channel's sequence.
  std::array<uint8_t, reg::kChannelCount> last_seq_{};
  std::array<bool, reg::kChannelCount> seq_valid_{};
};

/// UART frame flag and escape (SHTP S4.1).
inline constexpr uint8_t kUartFlag = 0x7E;
/// UART control escape.
inline constexpr uint8_t kUartEscape = 0x7D;
/// Protocol ID of the SHTP-over-UART control messages (BSQ, BSN).
inline constexpr uint8_t kUartProtoControl = 0;
/// Protocol ID of an SHTP cargo.
inline constexpr uint8_t kUartProtoShtp = 1;

/// What `UartFramer::Feed` produced.
enum class FrameEvent : uint8_t {
  None,  ///< Nothing complete yet.
  Frame, ///< A frame is ready: `Frame()` (protocol ID, then payload).
};

/// Where the UART receiver is: between frames, inside one, or after an escape byte.
enum class UartRxMode : uint8_t { Idle, InFrame, Escape };

/**
 * @brief The receive state machine of SHTP over UART: flags, escapes and frames (SHTP S4.1, S4.2).
 * @tparam N Largest frame body in bytes (protocol ID included).
 */
template <std::size_t N>
class UartFramer {
public:
  /// Counts bad escapes and protocol IDs in `stats`.
  explicit UartFramer(Stats& stats) noexcept : stats_(stats) {}

  /// Back to hunting for a start flag.
  void Reset() noexcept {
    state_ = UartRxMode::Idle;
    len_ = 0;
  }

  /// Feed one byte from the hub.
  FrameEvent Feed(uint8_t b) noexcept {
    if (b == kUartFlag) {
      return flag();
    }
    if (state_ == UartRxMode::Idle) {
      return FrameEvent::None; // bytes outside a frame are ignored until a flag
    }
    if (state_ == UartRxMode::Escape) {
      state_ = UartRxMode::InFrame;
      store(static_cast<uint8_t>(static_cast<uint32_t>(b) ^ 0x20U));
      return FrameEvent::None;
    }
    if (b == kUartEscape) {
      state_ = UartRxMode::Escape;
      return FrameEvent::None;
    }
    store(b);
    return FrameEvent::None;
  }

  /// The frame body: protocol ID, then the payload.
  [[nodiscard]] std::span<const uint8_t> Frame() const noexcept {
    return std::span<const uint8_t>{buf_}.first(done_len_);
  }

private:
  FrameEvent flag() noexcept {
    if (state_ == UartRxMode::Escape) {
      ++stats_.bad_escape; // an escape followed by a flag: the frame is garbage, this flag starts a
                           // new one
      state_ = UartRxMode::InFrame;
      len_ = 0;
      return FrameEvent::None;
    }
    if ((state_ == UartRxMode::InFrame) && (len_ > 0U)) {
      state_ = UartRxMode::Idle;
      const std::size_t got = len_;
      len_ = 0;
      if (got > N) {
        ++stats_.oversize_frame; // longer than the buffer: dropped, never handed out
        return FrameEvent::None;
      }
      done_len_ = got;
      if (buf_[0] > kUartProtoShtp) {
        ++stats_.bad_protocol_id;
        return FrameEvent::None;
      }
      return FrameEvent::Frame;
    }
    state_ = UartRxMode::InFrame; // start flag, or two flags in a row (S4.2)
    len_ = 0;
    return FrameEvent::None;
  }

  void store(uint8_t b) noexcept {
    if (len_ < N) {
      buf_[len_] = b;
      ++len_;
    } else {
      len_ = N + 1U; // overflow marker: the frame is dropped at its end flag
    }
  }

  Stats& stats_;
  std::array<uint8_t, N> buf_{};
  std::size_t len_{0};
  std::size_t done_len_{0};
  UartRxMode state_{UartRxMode::Idle};
};

/// Appends `b` to `out` at `pos`, escaping a flag or escape byte; false when `out` is full.
[[nodiscard]] constexpr bool PutEscaped(std::span<uint8_t> out, std::size_t& pos,
                                        uint8_t b) noexcept {
  const bool special = (b == kUartFlag) || (b == kUartEscape);
  const std::size_t need = special ? 2U : 1U;
  if ((pos + need) > out.size()) {
    return false;
  }
  if (special) {
    out[pos] = kUartEscape;
    ++pos;
    out[pos] = static_cast<uint8_t>(static_cast<uint32_t>(b) ^ 0x20U);
  } else {
    out[pos] = b;
  }
  ++pos;
  return true;
}

/**
 * @brief Builds a UART frame: flag, protocol ID, escaped payload, flag.
 * @param out Destination.
 * @param proto Protocol ID.
 * @param payload The unescaped payload.
 * @return The frame length in bytes, or 0 when `out` is too small.
 */
[[nodiscard]] constexpr std::size_t EncodeUartFrame(std::span<uint8_t> out, uint8_t proto,
                                                    std::span<const uint8_t> payload) noexcept {
  std::size_t pos = 0;
  if (out.size() < 3U) {
    return 0;
  }
  out[pos] = kUartFlag;
  ++pos;
  bool ok = PutEscaped(out, pos, proto);
  for (const uint8_t b : payload) {
    ok = ok && PutEscaped(out, pos, b);
  }
  if (!ok || (pos >= out.size())) {
    return 0;
  }
  out[pos] = kUartFlag;
  ++pos;
  return pos;
}

/// What the SHTP advertisement told the host (SHTP S5.2, S5.3, SHU Figure 3).
struct Advert {
  bool valid{false};              ///< An advertisement was parsed.
  uint16_t max_cargo_write{0};    ///< MaxCargoPlusHeaderWrite (0 = not advertised).
  uint16_t max_cargo_read{0};     ///< MaxCargoPlusHeaderRead.
  uint16_t max_transfer_write{0}; ///< MaxTransferWrite.
  uint16_t max_transfer_read{0};  ///< MaxTransferRead.
  uint32_t uart_timeout_ms{0};    ///< SHTP-over-UART timeout (tag 0x81 under GUID 0); 0 = none.
  std::array<uint8_t, reg::kChannelCount> channel{
      {0, 1, 2, 3, 4, 5}}; ///< Command, executable, control,
                           ///< input normal, input wake, gyro rotation vector.
  std::array<uint8_t, kSensorSlots>
      report_len{}; ///< Report lengths: built in, overridden by tag 0x81.
};

/// Tag numbers of the advertisement (SHTP Figures 16 and 17, SHU Figure 3).
inline constexpr uint8_t kTagGuid = 1;             ///< Tag: GUID (32 bit).
inline constexpr uint8_t kTagMaxCargoWrite = 2;    ///< Tag: MaxCargoPlusHeaderWrite.
inline constexpr uint8_t kTagMaxCargoRead = 3;     ///< Tag: MaxCargoPlusHeaderRead.
inline constexpr uint8_t kTagMaxTransferWrite = 4; ///< Tag: MaxTransferWrite.
inline constexpr uint8_t kTagMaxTransferRead = 5;  ///< Tag: MaxTransferRead.
inline constexpr uint8_t kTagNormalChannel = 6;    ///< Tag: NormalChannel.
inline constexpr uint8_t kTagWakeChannel = 7;      ///< Tag: WakeChannel.
inline constexpr uint8_t kTagChannelName = 9;      ///< Tag: ChannelName.
inline constexpr uint8_t kTagVersion = 0x80;       ///< Tag: version string.
inline constexpr uint8_t kTagExtra =
    0x81; ///< Tag: UART timeout (GUID 0) or ReportLengths (GUID 2).

/// The advertisement with the built-in report lengths filled in.
[[nodiscard]] constexpr Advert DefaultAdvert() noexcept {
  Advert a{};
  for (std::size_t i = 0; i < reg::detail::kSensorTableSize; ++i) {
    a.report_len[i] = reg::detail::kReportLengths[i];
  }
  return a;
}

/// Maps a ChannelName string onto the channel role index it names; `kChannelCount` when unknown.
[[nodiscard]] constexpr std::size_t ChannelRole(std::span<const uint8_t> name) noexcept {
  const auto is = [&name](const char* s) noexcept {
    const auto at = [s](std::size_t k) noexcept { return std::bit_cast<uint8_t>(s[k]); };
    std::size_t i = 0;
    while ((i < name.size()) && (at(i) != 0U) && (name[i] == at(i))) {
      ++i;
    }
    return (at(i) == 0U) && ((i == name.size()) || (name[i] == 0U));
  };
  std::size_t role = reg::kChannelCount;
  if (is("device") || is("executable")) {
    role = reg::kChanExecutable;
  } else if (is("control")) {
    role = reg::kChanControl;
  } else if (is("inputNormal")) {
    role = reg::kChanInputNormal;
  } else if (is("inputWake")) {
    role = reg::kChanInputWake;
  } else if (is("inputGyroRv")) {
    role = reg::kChanGyroRv;
  } else {
    // a channel this driver does not use
  }
  return role;
}

/// State carried from one TLV of an advertisement to the next.
struct AdvertState {
  uint32_t guid{0};        ///< The GUID of the TLV group being read.
  uint8_t pending_chan{0}; ///< The channel number the next ChannelName belongs to.
  bool bad_length{false};  ///< A report length shorter than the report header was refused.
};

/// A limit tag (2 to 5) of GUID 0: stores the 16-bit value in the field the tag names.
inline void ApplyLimit(uint8_t tag, std::span<const uint8_t> val, Advert& adv) noexcept {
  const uint16_t v = Le16(val, 0U);
  if (tag == kTagMaxCargoWrite) {
    adv.max_cargo_write = v;
  } else if (tag == kTagMaxCargoRead) {
    adv.max_cargo_read = v;
  } else if (tag == kTagMaxTransferWrite) {
    adv.max_transfer_write = v;
  } else {
    adv.max_transfer_read = v;
  }
}

/// A ReportLengths TLV (tag 0x81, GUID 2): pairs of report ID and length. A length shorter than
/// the 4-byte report header (RM S6.5.2) is refused and the built-in length kept.
inline void ApplyReportLengths(std::span<const uint8_t> val, Advert& adv,
                               AdvertState& st) noexcept {
  for (std::size_t i = 0; (i + 1U) < val.size(); i += 2U) {
    if (val[i] >= kSensorSlots) {
      // not a sensor report: its length is fixed by the RM
    } else if (val[i + 1U] < reg::kLenSensorHeader) {
      st.bad_length = true;
    } else {
      adv.report_len[val[i]] = val[i + 1U];
    }
  }
}

/// The channel TLVs: a channel number (tags 6 and 7) and the name that says which role it is (tag
/// 9).
inline void ApplyChannelTlv(uint8_t tag, std::span<const uint8_t> val, Advert& adv,
                            AdvertState& st) noexcept {
  if (((tag == kTagNormalChannel) || (tag == kTagWakeChannel)) && (val.size() == 1U)) {
    st.pending_chan = val[0];
  } else if (tag == kTagChannelName) {
    const std::size_t role = ChannelRole(val);
    if ((role < reg::kChannelCount) && (st.pending_chan < reg::kChannelCount)) {
      adv.channel[role] = st.pending_chan;
    }
  } else {
    // other tags are ignored (SHTP S5.3)
  }
}

/// Tag 0x81: the UART timeout under GUID 0, the report lengths under any other GUID.
inline void ApplyExtraTlv(std::span<const uint8_t> val, Advert& adv, AdvertState& st) noexcept {
  if ((st.guid == 0U) && (val.size() == 4U)) {
    adv.uart_timeout_ms = Le32(val, 0U);
  } else if ((st.guid != 0U) && ((val.size() % 2U) == 0U)) {
    ApplyReportLengths(val, adv, st);
  } else {
    // a form this driver does not know
  }
}

/// One TLV: stores what the driver uses and ignores the rest.
inline void ApplyTlv(uint8_t tag, std::span<const uint8_t> val, Advert& adv,
                     AdvertState& st) noexcept {
  if (tag == kTagGuid) {
    st.guid = (val.size() == 4U) ? Le32(val, 0U) : st.guid;
  } else if ((tag >= kTagMaxCargoWrite) && (tag <= kTagMaxTransferRead)) {
    if ((val.size() == 2U) && (st.guid == 0U)) {
      ApplyLimit(tag, val, adv);
    }
  } else if (tag == kTagExtra) {
    ApplyExtraTlv(val, adv, st);
  } else {
    ApplyChannelTlv(tag, val, adv, st);
  }
}

/**
 * @brief Parses the TLVs of an advertisement cargo (the bytes after the response ID).
 * @param tlv The cargo after byte 0.
 * @param[in,out] adv Filled in place; a truncated TLV stops the parse and sets `bad` true.
 * @param[out] bad Set when a TLV ran past the cargo end or a report length was refused.
 */
inline void ParseAdvertisement(std::span<const uint8_t> tlv, Advert& adv, bool& bad) noexcept {
  std::size_t pos = 0;
  AdvertState st{};
  bad = false;
  while ((pos + 2U) <= tlv.size()) {
    const std::size_t len = tlv[pos + 1U];
    if ((pos + 2U + len) > tlv.size()) {
      bad = true;
      break;
    }
    ApplyTlv(tlv[pos], tlv.subspan(pos + 2U, len), adv, st);
    pos += 2U + len;
  }
  bad = bad || st.bad_length;
  adv.valid = true;
}

} // namespace bno08x::detail

#endif // BNO08X_SHTP_HPP
