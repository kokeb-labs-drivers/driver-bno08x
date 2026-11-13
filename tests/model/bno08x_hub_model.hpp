/**
 * @file bno08x_hub_model.hpp
 * @brief An independent model of a BNO08x sensor hub: SHTP engine, SH-2 control channel, FRS,
 * commands, the transport fronts, a fault injector and one knob for each documented ambiguity (A1
 * to A42).
 *
 * The model is written from the datasheet, the SH-2 reference manual and the SHTP specification,
 * not from the driver: every constant below is a literal with its source, so a mistake in the
 * driver's tables cannot hide in a matching mistake here.
 */
#ifndef BNO08X_HUB_MODEL_HPP
#define BNO08X_HUB_MODEL_HPP

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hubmodel {

using Bytes = std::vector<uint8_t>;

/// One knob per ambiguity of the design note (A1 to A42). A knob without behaviour is a record
/// only.
struct Knobs {
  uint32_t product_part_number{10004095}; ///< A1: no document maps part numbers to variants.
  int32_t i2c_empty_poll{1}; ///< A2: read with no data: 0 stretch, 1 zero-length header, 2 NACK.
  bool bno086_features_on_085{false};   ///< A3, A4: the BNO086-only items work on any chip.
  bool rvc_mi_mr{true};                 ///< A5: MI and MR bytes (BNO086) instead of reserved bytes.
  bool rvc_csum_includes_mi_mr{true};   ///< A6: checksum over bytes 2..17, else 2..14 plus 17.
  bool rvc_angle_noise{false};          ///< A7: angles outside the documented range.
  int32_t rvc_input_cs{0};              ///< A8: $MIB checksum: 0 XOR, 1 literal 0x87, 2 none.
  bool rvc_banner{true};                ///< A9: ASCII banner before the frames.
  bool clock_internal{false};           ///< A10: internal clock, UART bytes corrupted at a rate.
  int32_t startup_order{0};             ///< A11: 0 reset-complete then initialise, 1 the reverse.
  bool omit_reset_complete{false};      ///< The boot's executable reset-complete is never sent.
  uint8_t cmd_reserved_fill{0};         ///< The value of the reserved bytes of command responses.
  bool omit_init_response{false};       ///< A11: never send the initialise response.
  uint16_t advert_max_cargo_write{128}; ///< A12: advertised limits.
  uint16_t advert_max_cargo_read{2048}; ///< A12.
  uint16_t advert_max_transfer_write{128}; ///< A12.
  uint16_t advert_max_transfer_read{2048}; ///< A12.
  bool advert_without_limits{false};       ///< A12: advertise no limits at all.
  bool accept_fragmented_writes{false};    ///< A13: the BNO08x does not accept them.
  int32_t cont_header_style{0};            ///< A14: 0 header as specified, 1 none (payload only).
  uint8_t seq_start{0};                    ///< A15: first sequence number sent on each channel.
  uint32_t seq_skip_rate{0};     ///< A15: skip a sequence number every n transfers (0 never).
  bool report_seq_global{false}; ///< A16: one global report sequence instead of one per report.
  bool base_overflow{false};     ///< A17: base timestamps report 0x7FFFFFFF.
  uint16_t step_counter_upper_bytes{0}; ///< A18: bytes 10 and 11 of the step counter report.
  uint8_t payload_upper_nonzero{0};     ///< A19: upper byte of the one-byte event payloads.
  uint64_t env_sensors{0};        ///< A20: environmental sensors present (bit n = sensor ID n).
  int32_t doc_only{0};            ///< A21-A25, A34, A36, A39, A40: documentation records only.
  uint8_t frs_status_ge12{12};    ///< A26: status for a failure the model cannot place.
  bool frs_final_8_then_3{false}; ///< A27: send 8 then 3 instead of 3 only.
  bool frs_reject_nonzero_reserved{
      true};                          ///< A28: refuse a read request with non-zero reserved bytes.
  bool frs_busy_interleave{true};     ///< A29: a second FRS request in flight is answered BUSY.
  uint16_t metadata_revision{4};      ///< A30: metadata record revision (3 or 4).
  uint64_t sensor_mask{~uint64_t{0}}; ///< A31: sensors served (bit n = sensor ID n).
  uint16_t rv_q3{13};                 ///< A32: metadata Q point 3 of the rotation vector.
  uint32_t product_id_entries{4};   ///< A33: Product ID responses per request (vendor library: 4).
  uint32_t hint_host_timeout_ms{0}; ///< A38: H_INTN drops when the host is this late (0 never).
  uint8_t pickup_report_len{6};     ///< A41: pickup detector length: RM Figure 107 6, vendor 8.
  uint8_t wheel_encoder_len{12};    ///< A42: wheel encoder 0x2E: no RM layout, vendor length 12.
  bool advertise_report_lengths{false}; ///< A41, A42: send a ReportLengths TLV for 0x2E, 0x1B.
  uint8_t extra_len_id{0}; ///< With it: one more ReportLengths pair, for this ID (0 none).
  uint8_t extra_len{0};    ///< The length of that pair.
};

/// What the model observed, for assertions.
struct Counters {
  uint32_t empty_reads{0};
  uint32_t bus_while_reset{0};
  uint32_t writes{0};
  uint32_t host_seq_errors{0};
  uint32_t host_seq_first_not_zero{0};
  uint32_t short_writes{0};
  uint32_t oversize_writes{0};
  uint32_t fragmented_writes{0};
  uint32_t unknown_channel{0};
  uint32_t writes_without_wake{0};
  uint32_t uart_gap_violations{0};
  uint32_t uart_no_token_writes{0};
  uint32_t uart_bad_frames{0};
  uint32_t uart_frames_closed{0}; ///< Frames with content ended by a flag byte.
  uint32_t uart_stray_bytes{0};   ///< Bytes received outside a frame.
  uint32_t bootloader_starts{0};  ///< NRST released with BOOTN low: the bootloader runs.
  uint32_t int_timeouts{0};
  uint32_t reboots{0};
  uint32_t errors_sent{0};
  uint32_t mib_ok{0};
  uint32_t mib_bad{0};
  uint32_t bsq_in{0};      ///< Buffer Status Queries received.
  uint32_t bsn_dropped{0}; ///< Buffer Status Notifications lost on purpose.
  uint32_t bad_strap{0};   ///< NRST edges or start-up windows with PS0/WAKE low (DS p18).
  uint32_t nrst_low_us{0}; ///< Length of the last NRST low pulse.
};

/// One sensor's configuration as the hub holds it.
struct Feature {
  uint8_t flags{0};
  uint16_t sensitivity{0};
  uint32_t interval_us{0};
  uint32_t batch_us{0};
  uint32_t config{0};
  uint32_t next_us{0};
  uint32_t samples{0};
};

enum class Chip : uint8_t { Bno080 = 0, Bno085 = 1, Bno086 = 2 };

class Hub { // NOLINT(clang-analyzer-optin.performance.Padding): a test model, its size does not
            // matter
public:
  static constexpr std::size_t kSensors = 0x2F;

  Knobs k;
  Counters c;
  Chip chip{Chip::Bno085};
  uint32_t now{0};

  Hub() {
    for (auto& h : host_seq_) {
      h = -1;
    }
  }

  // observable state
  std::map<uint16_t, std::vector<uint32_t>> frs;
  std::vector<uint8_t> errors_seen;
  Feature feat[kSensors]{};
  bool awake{true};
  uint32_t simple_cal_interval{0};
  bool simple_cal_started{false};
  uint8_t simple_cal_status{0};
  uint8_t me_cal[6]{1, 1, 1, 0, 0, 0};
  uint8_t periodic_dcd_p0{0xFF};
  uint8_t oscillator{0};
  uint32_t tares{0};
  uint8_t last_tare_axes{0};
  uint8_t last_tare_basis{0};
  bool last_tare_persist{false};
  Bytes last_reorient;
  uint8_t last_motion_intent{0xFF};
  Bytes last_wheel;
  uint32_t dcd_saves{0};
  bool dcd_cleared{false};
  std::vector<Bytes> sh2_errors;
  uint32_t counts[4]{1000, 900, 800, 700};
  uint8_t save_dcd_status{0};
  uint8_t init_status{0};
  uint8_t me_status{0};
  uint8_t frs_fail_status{0};
  std::vector<uint16_t> frs_unknown;
  std::vector<uint16_t> frs_readonly{0x4D4D, 0x4E4E};
  uint32_t min_interval_us{1000};
  uint32_t boot_delay_us{94000};
  uint32_t exec_reset_delay_us{40000};
  bool set_feature_reply{true};
  bool boot_silent{false};
  uint32_t uart_timeout_ms{200};
  uint16_t uart_buffer_bytes{256};
  Bytes advert_extra;
  std::size_t cargo_split{120};
  uint32_t flush_log{0};
  uint32_t bsn_drop{0};      ///< Fault: the next N Buffer Status Notifications are lost.
  uint32_t bsn_loss_rate{0}; ///< Fault: every Nth Buffer Status Notification is lost (0 never).

  // ---- control by the test
  // --------------------------------------------------------------------------
  void PowerOn(uint32_t t) {
    strap_window_ = true;
    startBoot(t, 1U, boot_delay_us);
  }
  void Tick(uint32_t t);
  void Nrst(bool level, uint32_t t) {
    now = t;
    if (wake_low_) {
      ++c.bad_strap; // PS0/WAKE must be high from before reset until H_INTN (DS p18)
    }
    if (!level) {
      in_reset_pin_ = true;
      booted_ = false;
      nrst_low_at_ = t;
      strap_window_ = true; // a software reset is not modelled as a strap read (unknown)
      ClearQueue();
    } else if (in_reset_pin_) {
      in_reset_pin_ = false;
      c.nrst_low_us = t - nrst_low_at_;
      if (bootn_low_) {
        ++c.bootloader_starts; // BOOTN low at reset starts the bootloader: no application
                               // (DS 1.2.2, p12)
        return;
      }
      startBoot(t, 4U, boot_delay_us);
    } else {
      // NRST high and no reset was pending: nothing to do
    }
  }
  /// The advertisement cargo this hub sends (for tests that inject it).
  [[nodiscard]] Bytes AdvertCargo() const {
    return advert();
  }
  void Bootn(bool level) {
    bootn_low_ = !level;
  }
  void Inject(uint8_t channel, const Bytes& cargo) {
    queue(channel, cargo);
  }
  /// Like `Inject`, but ahead of whatever is already queued for the host.
  void InjectFirst(uint8_t channel, const Bytes& cargo) {
    out_.push_front({channel, cargo});
    markInt();
  }
  void InjectRaw(const Bytes& transfer) {
    raw_.push_back(transfer);
    markInt();
  }
  void ClearQueue() {
    out_.clear();
    raw_.clear();
    cont_active_ = false;
    uart_out_.clear();
    int_active_ = false;
  }
  void SendErrorList(uint8_t code) {
    queue(0, Bytes{1, code});
    ++c.errors_sent;
  }
  [[nodiscard]] bool Booted() const {
    return booted_;
  }
  [[nodiscard]] bool WakeLow() const {
    return wake_low_;
  }
  [[nodiscard]] bool IntActive() const {
    return booted_ && (!out_.empty() || !raw_.empty() || cont_active_ || (wake_int_ && wake_low_));
  }
  [[nodiscard]] uint32_t IntEdgeUs() const {
    return int_edge_us_;
  }
  [[nodiscard]] std::size_t Pending() const {
    return out_.size() + raw_.size() + (cont_active_ ? 1U : 0U);
  }
  void SetAutoSensors(bool on) {
    auto_sensors_ = on;
  }
  [[nodiscard]] uint8_t SampleByte(uint8_t id, uint32_t n, uint32_t i) const {
    return static_cast<uint8_t>((id * 131U) + (n * 7U) + (i * 3U));
  }

  // ---- bus fronts
  // -------------------------------------------------------------------------------------
  bool HostWrite(const uint8_t* p, std::size_t n);
  bool HostRead(uint8_t* p, std::size_t n);
  bool ReadImpl(uint8_t* p, std::size_t n, bool bus_poll);
  bool SpiTransfer(const uint8_t* tx, uint8_t* rx, std::size_t n);
  void SetWake(bool level);
  void UartByteIn(uint8_t b);
  std::size_t UartTake(uint8_t* p, std::size_t cap);
  std::size_t UartAvailable() {
    return uart_out_.size() + (((!out_.empty()) || (!raw_.empty())) ? 1U : 0U);
  }

  // ---- UART-RVC
  // -----------------------------------------------------------------------------------------
  [[nodiscard]] Bytes RvcFrame(uint8_t idx, int16_t yaw, int16_t pitch, int16_t roll, int16_t ax,
                               int16_t ay, int16_t az, uint8_t mi, uint8_t mr) const;
  void RvcStart() {
    if (k.rvc_banner) {
      const std::string_view banner = "BNO086 RVC ready\r\n";
      for (const char ch : banner) {
        uart_out_.push_back(std::bit_cast<uint8_t>(ch));
      }
    }
  }
  void RvcPush(const Bytes& b) {
    uart_out_.insert(uart_out_.end(), b.begin(), b.end());
  }
  /// A8: checks a `$MIB,<body>*<cs>` host input the way the knob says (0 XOR of the text, 1 the
  /// literal 87 of the manual, 2 no checksum). Counts the outcome.
  bool MibCheck(const std::string& text) {
    const std::size_t star = text.find('*');
    bool ok = (text.starts_with("$MIB,")) && (star != std::string::npos);
    if (ok && (k.rvc_input_cs != 2)) {
      uint8_t x = 0;
      for (std::size_t i = 1; i < star; ++i) {
        x = static_cast<uint8_t>(x ^ std::bit_cast<uint8_t>(text[i]));
      }
      const uint32_t want = (k.rvc_input_cs == 0) ? x : 0x87U;
      ok = (std::stoul(text.substr(star + 1U), nullptr, 16) == want);
    }
    ++(ok ? c.mib_ok : c.mib_bad);
    return ok;
  }
  std::string mib_last;

private:
  struct Pkt {
    uint8_t ch;
    Bytes cargo;
  };
  // transport state
  std::deque<Pkt> out_;
  std::deque<Bytes> raw_;
  bool cont_active_{false};
  uint8_t cont_ch_{0};
  Bytes cont_rest_;
  uint8_t seq_out_[256]{};
  uint32_t seq_count_[256]{};
  int32_t host_seq_[256]{};
  bool booted_{false};
  bool in_reset_pin_{false};
  bool bootn_low_{false};
  uint32_t nrst_low_at_{0};
  bool strap_window_{false}; ///< From a reset until the first H_INTN: the straps are read.
  uint32_t bsn_count_{0};
  uint32_t boot_at_{0};
  uint8_t boot_cause_{1};
  uint8_t next_cause_{1};
  bool boot_pending_{false};
  bool auto_sensors_{true};
  bool int_active_{false};
  uint32_t int_edge_us_{0};
  uint32_t int_since_us_{0};
  bool wake_low_{false};
  bool wake_int_{false};
  uint32_t wake_at_{0};
  // control
  uint8_t report_seq_[256]{};
  uint8_t report_seq_global_{0};
  uint8_t cmd_resp_seq_{0};
  bool advert_pending_{false};
  std::map<uint8_t, std::vector<std::pair<uint32_t, Bytes>>> batch_;
  // FRS state machine
  int32_t frs_phase_{0}; // 0 idle, 1 writing
  uint16_t frs_type_{0};
  uint16_t frs_len_{0};
  std::vector<uint32_t> frs_buf_;
  // UART
  std::deque<uint8_t> uart_out_;
  bool uart_in_frame_{false};
  bool uart_escape_{false};
  Bytes uart_frame_;
  bool uart_have_token_{false};
  uint32_t uart_token_at_{0};
  uint16_t uart_token_bytes_{0};
  uint32_t uart_last_byte_us_{0};
  bool uart_seen_byte_{false};
  uint32_t uart_corrupt_{0};

  void markInt() {
    if (!int_active_) {
      int_active_ = true;
      int_edge_us_ = now;
      int_since_us_ = now;
    }
  }
  void queue(uint8_t ch, const Bytes& cargo) {
    out_.push_back({ch, cargo});
    markInt();
  }
  uint8_t nextSeq(uint8_t ch) {
    if (seq_count_[ch] == 0U) {
      seq_out_[ch] = k.seq_start;
    }
    uint8_t s = seq_out_[ch];
    ++seq_count_[ch];
    seq_out_[ch] = static_cast<uint8_t>(seq_out_[ch] + 1U);
    if ((k.seq_skip_rate != 0U) && ((seq_count_[ch] % k.seq_skip_rate) == 0U)) {
      seq_out_[ch] = static_cast<uint8_t>(seq_out_[ch] + 1U);
    }
    return s;
  }
  static Bytes header(std::size_t len, bool cont, uint8_t ch, uint8_t seq) {
    Bytes h(4);
    const auto v = static_cast<uint16_t>((len & 0x7FFFU) | (cont ? 0x8000U : 0U));
    h[0] = static_cast<uint8_t>(v & 0xFFU);
    h[1] = static_cast<uint8_t>(v >> 8U);
    h[2] = ch;
    h[3] = seq;
    return h;
  }
  Bytes takeTransfer();
  void startBoot(uint32_t t, uint8_t cause, uint32_t delay) {
    now = t;
    ++c.reboots;
    booted_ = false;
    ClearQueue();
    boot_pending_ = !boot_silent;
    boot_at_ = t + delay;
    next_cause_ = cause;
    for (auto& s : seq_count_) {
      s = 0;
    }
    for (auto& h : host_seq_) {
      h = -1;
    }
    for (auto& f : feat) {
      f = Feature{};
    }
    batch_.clear();
    frs_phase_ = 0;
    uart_have_token_ = false;
    uart_in_frame_ = false;
    awake = true;
    advert_pending_ = false;
  }
  void finishBoot();
  [[nodiscard]] Bytes advert() const;
  void process(uint8_t ch, const Bytes& cargo);
  void onCommandChannel(const Bytes& cargo);
  void onExecutable(const Bytes& cargo);
  void onControl(const Bytes& r);
  void onSetFeature(const Bytes& r);
  void onGetFeature(uint8_t id);
  void sendFeature(uint8_t id);
  void onFrsRead(const Bytes& r);
  void onFrsWriteReq(const Bytes& r);
  void onFrsWriteData(const Bytes& r);
  void frsWriteDone();
  void sendFrsWrite(uint8_t status, uint16_t off);
  void onCommand(const Bytes& r);
  void cmdResponse(uint8_t cmd, uint8_t cmd_seq, uint8_t resp_seq, const Bytes& rr,
                   bool autonomous = false);
  void hostError(uint8_t code) {
    errors_seen.push_back(code);
    SendErrorList(code);
  }
  void reboot(uint8_t cause, uint32_t delay) {
    startBoot(now, cause, delay);
  }
  void sensors();
  Bytes makeReport(uint8_t id, uint32_t n, uint32_t delay_ticks);
  void sendBatch(uint8_t id);
  void emitCargo(uint8_t id, const std::vector<std::pair<uint32_t, Bytes>>& reps);
  void pumpUartTx();
  void uartFrameIn();
  void frameOut(uint8_t proto, const Bytes& payload);
  [[nodiscard]] Bytes metadataWords(uint8_t sensor) const;
  [[nodiscard]] bool served(uint8_t id) const {
    return ((k.sensor_mask >> id) & 1U) != 0U;
  }
};

// =============================================================================================================
// Implementation
// =============================================================================================================

inline void Hub::finishBoot() {
  booted_ = true;
  boot_pending_ = false;
  strap_window_ = false; // the advertisement below asserts H_INTN
  boot_cause_ = next_cause_;
  queue(0, advert());
  const Bytes reset_done{1};
  Bytes init(16, 0);
  init[0] = 0xF1;
  init[2] = 0x84;
  init[5] = init_status;
  init[6] = 1;
  if (k.startup_order == 0) {
    if (!k.omit_reset_complete) {
      queue(1, reset_done);
    }
    if (!k.omit_init_response) {
      queue(2, init);
    }
  } else {
    if (!k.omit_init_response) {
      queue(2, init);
    }
    if (!k.omit_reset_complete) {
      queue(1, reset_done);
    }
  }
}

inline Bytes Hub::advert() const {
  Bytes a{0}; // response 0
  const auto tlv = [&a](uint8_t tag, const Bytes& v) {
    a.push_back(tag);
    a.push_back(static_cast<uint8_t>(v.size()));
    a.insert(a.end(), v.begin(), v.end());
  };
  const auto le16 = [](uint16_t v) {
    return Bytes{static_cast<uint8_t>(v & 0xFFU), static_cast<uint8_t>(v >> 8U)};
  };
  tlv(0x80, Bytes{'1', '.', '0', '.', '0'}); // SHTP version string (tag 0x80)
  tlv(1, Bytes{0, 0, 0, 0});                 // GUID 0: SHTP itself
  if (!k.advert_without_limits) {
    tlv(2, le16(k.advert_max_cargo_write));
    tlv(3, le16(k.advert_max_cargo_read));
    tlv(4, le16(k.advert_max_transfer_write));
    tlv(5, le16(k.advert_max_transfer_read));
  }
  tlv(0x81, Bytes{static_cast<uint8_t>(uart_timeout_ms & 0xFFU),
                  static_cast<uint8_t>((uart_timeout_ms >> 8U) & 0xFFU),
                  static_cast<uint8_t>((uart_timeout_ms >> 16U) & 0xFFU),
                  static_cast<uint8_t>((uart_timeout_ms >> 24U) & 0xFFU)}); // 32 bit (SHTP p17)
  tlv(1, Bytes{1, 0, 0, 0}); // GUID 1: the executable
  tlv(6, Bytes{1});
  tlv(9, Bytes{'d', 'e', 'v', 'i', 'c', 'e', 0});
  tlv(1, Bytes{2, 0, 0, 0});        // GUID 2: the sensor hub
  if (k.advertise_report_lengths) { // ReportLengths (SHU Figure 3): wheel encoder, then pickup
    Bytes lengths{0x2E, k.wheel_encoder_len, 0x1B, k.pickup_report_len};
    if (k.extra_len_id != 0U) {
      lengths.push_back(k.extra_len_id);
      lengths.push_back(k.extra_len);
    }
    tlv(0x81, lengths);
  }
  tlv(6, Bytes{2});
  tlv(9, Bytes{'c', 'o', 'n', 't', 'r', 'o', 'l', 0});
  tlv(6, Bytes{3});
  tlv(9, Bytes{'i', 'n', 'p', 'u', 't', 'N', 'o', 'r', 'm', 'a', 'l', 0});
  tlv(7, Bytes{4});
  tlv(9, Bytes{'i', 'n', 'p', 'u', 't', 'W', 'a', 'k', 'e', 0});
  tlv(6, Bytes{5});
  tlv(9, Bytes{'i', 'n', 'p', 'u', 't', 'G', 'y', 'r', 'o', 'R', 'v', 0});
  a.insert(a.end(), advert_extra.begin(), advert_extra.end());
  return a;
}

inline void Hub::Tick(uint32_t t) {
  now = t;
  if (boot_pending_ && !in_reset_pin_ && (static_cast<int32_t>(now - boot_at_) >= 0)) {
    finishBoot();
  }
  if (!booted_) {
    return;
  }
  if (wake_low_ && !wake_int_ && (static_cast<int32_t>(now - wake_at_) >= 0)) {
    wake_int_ = true;
    markInt();
  }
  if (auto_sensors_) {
    sensors();
  }
  if ((k.hint_host_timeout_ms != 0U) && (IntActive()) && int_active_ &&
      ((now - int_since_us_) > (k.hint_host_timeout_ms * 1000U))) {
    ++c.int_timeouts; // the hub gives up on this edge and raises it again (DS p18)
    int_since_us_ = now;
    int_edge_us_ = now;
  }
}

inline void Hub::SetWake(bool level) {
  if (!level && strap_window_) {
    ++c.bad_strap; // WAKE low before the first H_INTN after a reset selects UART (DS p18)
  }
  wake_low_ = !level;
  if (wake_low_) {
    wake_at_ = now + 50U; // the hub needs a moment to wake (SHTP S3.1)
    wake_int_ = false;
  } else {
    wake_int_ = false;
  }
}

inline Bytes Hub::takeTransfer() {
  Bytes t;
  if (cont_active_) {
    const uint8_t seq = nextSeq(cont_ch_);
    t = (k.cont_header_style == 0) ? header(cont_rest_.size() + 4U, true, cont_ch_, seq) : Bytes{};
    t.insert(t.end(), cont_rest_.begin(), cont_rest_.end());
    cont_active_ = false;
  } else if (!raw_.empty()) {
    t = raw_.front();
    raw_.pop_front();
  } else if (!out_.empty()) {
    const Pkt p = out_.front();
    out_.pop_front();
    t = header(p.cargo.size() + 4U, false, p.ch, nextSeq(p.ch));
    t.insert(t.end(), p.cargo.begin(), p.cargo.end());
  } else {
    // nothing queued: an empty transfer
  }
  return t;
}

inline bool Hub::HostRead(uint8_t* p, std::size_t n) {
  return ReadImpl(p, n, true);
}

inline bool Hub::ReadImpl(uint8_t* p, std::size_t n, bool bus_poll) {
  if (!booted_) {
    ++c.bus_while_reset;
    return false;
  }
  Bytes t = takeTransfer();
  if (t.empty()) {
    if (bus_poll) {
      ++c.empty_reads;
      if (k.i2c_empty_poll == 2) {
        return false;
      }
    }
    t.assign(4, 0); // a zero-length header (A2)
  }
  std::fill(p, p + n, uint8_t{0});
  const std::size_t m = std::min(n, t.size());
  std::copy(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(m), p);
  if ((t.size() > n) && ((t[0] != 0U) || (t[1] != 0U))) {
    // the host stopped early: the rest comes back as a continuation (SHTP S2.3.1, DS p22)
    cont_active_ = true;
    cont_ch_ = t[2];
    cont_rest_.assign(t.begin() + static_cast<std::ptrdiff_t>(n), t.end());
    if ((k.cont_header_style == 0) && (n < 4U)) {
      cont_rest_.assign(t.begin() + 4, t.end());
    }
  }
  if (out_.empty() && raw_.empty() && !cont_active_) {
    int_active_ = false;
    wake_int_ = false;
  } else {
    int_active_ = true;
    int_since_us_ = now;
    int_edge_us_ = now;
  }
  return true;
}

inline bool Hub::SpiTransfer(const uint8_t* tx, uint8_t* rx, std::size_t n) {
  if (!booted_) {
    ++c.bus_while_reset;
    return false;
  }
  const bool had_int = IntActive();
  const std::size_t host_len =
      (n >= 4U)
          ? ((static_cast<std::size_t>(tx[0]) | (static_cast<std::size_t>(tx[1] & 0x7FU) << 8U)))
          : 0U;
  if (host_len >= 4U) {
    if (!wake_low_ && !had_int) {
      ++c.writes_without_wake;
    }
    if (host_len <= n) {
      static_cast<void>(HostWrite(tx, host_len)); // the outcome is in the counters
    }
  }
  const bool ok = ReadImpl(rx, n, false);
  if (!ok) {
    std::fill(rx, rx + n, uint8_t{0});
  }
  wake_int_ = false;
  return true;
}

inline bool Hub::HostWrite(const uint8_t* p, std::size_t n) {
  if (!booted_) {
    ++c.bus_while_reset;
    return false;
  }
  if (n < 4U) {
    ++c.short_writes;
    hostError(2);
    return true;
  }
  const std::size_t len =
      static_cast<std::size_t>(p[0]) | (static_cast<std::size_t>(p[1] & 0x7FU) << 8U);
  const bool cont = (p[1] & 0x80U) != 0U;
  const uint8_t ch = p[2];
  const uint8_t seq = p[3];
  if (cont) {
    ++c.fragmented_writes;
    if (!k.accept_fragmented_writes) {
      hostError(6);
      return true;
    }
  }
  if (len > static_cast<std::size_t>(k.advert_max_cargo_write)) {
    ++c.oversize_writes;
    hostError(3);
    return true;
  }
  if (len <= 4U) {
    hostError(4);
    return true;
  }
  if (len > n) {
    ++c.short_writes;
    return true;
  }
  if (ch >= 6U) {
    ++c.unknown_channel;
    hostError(9);
    return true;
  }
  if ((host_seq_[ch] >= 0) && (seq != static_cast<uint8_t>(host_seq_[ch] + 1))) {
    ++c.host_seq_errors;
  } else if ((host_seq_[ch] < 0) && (seq != 0U)) {
    ++c.host_seq_first_not_zero; // the first write after a start must carry sequence number 0
  } else {
    // a write in sequence
  }
  host_seq_[ch] = seq;
  ++c.writes;
  process(ch, Bytes(p + 4, p + len));
  return true;
}

inline void Hub::process(uint8_t ch, const Bytes& cargo) {
  if (cargo.empty()) {
    return;
  }
  if (ch == 0) {
    onCommandChannel(cargo);
  } else if (ch == 1) {
    onExecutable(cargo);
  } else if (ch == 2) {
    onControl(cargo);
  } else {
    hostError(9);
  }
}

inline void Hub::onCommandChannel(const Bytes& cargo) {
  if (cargo[0] == 0U) {
    if (advert_pending_) {
      hostError(10);
      return;
    }
    if ((cargo.size() < 2U) || (cargo[1] > 1U)) {
      hostError(8);
      return;
    }
    queue(0, advert());
  } else if (cargo[0] == 1U) {
    SendErrorList(errors_seen.empty() ? uint8_t{0} : errors_seen.back());
  } else {
    hostError(7);
  }
}

inline void Hub::onExecutable(const Bytes& cargo) {
  if (cargo[0] == 1U) {
    reboot(2, exec_reset_delay_us);
  } else if (cargo[0] == 2U) {
    awake = true;
  } else if (cargo[0] == 3U) {
    awake = false;
  } else {
    // reserved
  }
}

inline void Hub::onControl(const Bytes& r) {
  switch (r[0]) {
  case 0xFD:
    if (r.size() >= 17U) {
      onSetFeature(r);
    }
    break;
  case 0xFE:
    if (r.size() >= 2U) {
      onGetFeature(r[1]);
    }
    break;
  case 0xF9: {
    for (uint32_t i = 0; i < k.product_id_entries; ++i) {
      Bytes p(16, 0);
      p[0] = 0xF8;
      p[1] = boot_cause_;
      p[2] = 3;
      p[3] = 2;
      // entry 0 is the application; later entries describe other components (vendor library: four
      // entries, sh2.c), so they carry other part numbers
      const uint32_t part = (i == 0U) ? k.product_part_number : (10003000U + i);
      for (uint32_t b = 0; b < 4U; ++b) {
        p[4U + b] = static_cast<uint8_t>((part >> (8 * b)) & 0xFFU);
      }
      p[8] = 0x78;
      p[9] = 0x56;
      p[12] = 7;
      queue(2, p);
    }
    break;
  }
  case 0xF0:
    if (r.size() >= 2U) {
      sendBatch(r[1]);
      queue(2, Bytes{0xEF, r[1]});
    }
    break;
  case 0xF7:
    onFrsWriteReq(r);
    break;
  case 0xF6:
    onFrsWriteData(r);
    break;
  case 0xF4:
    onFrsRead(r);
    break;
  case 0xF2:
    if (r.size() >= 12U) {
      onCommand(r);
    }
    break;
  default:
    hostError(7);
    break;
  }
}

inline void Hub::onSetFeature(const Bytes& r) {
  const uint8_t id = r[1];
  if ((id >= kSensors) || !served(id) ||
      ((id == 0x2B) && (chip != Chip::Bno086) && !k.bno086_features_on_085)) {
    return;
  }
  Feature& f = feat[id];
  f.flags = r[2];
  f.sensitivity =
      static_cast<uint16_t>(static_cast<uint32_t>(r[3]) | (static_cast<uint32_t>(r[4]) << 8U));
  uint32_t interval = 0;
  uint32_t batch = 0;
  uint32_t cfg = 0;
  for (uint32_t i = 0; i < 4U; ++i) {
    interval |= static_cast<uint32_t>(r[5U + i]) << (8 * i);
    batch |= static_cast<uint32_t>(r[9U + i]) << (8 * i);
    cfg |= static_cast<uint32_t>(r[13U + i]) << (8 * i);
  }
  f.interval_us = (interval == 0U) ? 0U : std::max(interval, min_interval_us);
  f.batch_us = batch;
  f.config = cfg;
  f.next_us = now + f.interval_us;
  if (f.interval_us == 0U) {
    sendBatch(id);
  }
  if (set_feature_reply) {
    sendFeature(id);
  }
}

inline void Hub::sendFeature(uint8_t id) {
  const Feature& f = feat[id];
  Bytes b(17, 0);
  b[0] = 0xFC;
  b[1] = id;
  b[2] = f.flags;
  b[3] = static_cast<uint8_t>(f.sensitivity & 0xFFU);
  b[4] = static_cast<uint8_t>(f.sensitivity >> 8U);
  for (uint32_t i = 0; i < 4U; ++i) {
    b[5U + i] = static_cast<uint8_t>((f.interval_us >> (8 * i)) & 0xFFU);
    b[9U + i] = static_cast<uint8_t>((f.batch_us >> (8 * i)) & 0xFFU);
    b[13U + i] = static_cast<uint8_t>((f.config >> (8 * i)) & 0xFFU);
  }
  queue(2, b);
}

inline void Hub::onGetFeature(uint8_t id) {
  if (id < kSensors) {
    sendFeature(id);
  }
}

// ---- sensors
// ---------------------------------------------------------------------------------------------

inline Bytes Hub::makeReport(uint8_t id, uint32_t n, uint32_t delay_ticks) {
  static const uint8_t kLen[0x2F] = {0, 10, 10, 10, 10, 14, 10, 16, 12, 14, 8, 8, 6,  6,  6,  16,
                                     5, 12, 6,  6,  16, 16, 16, 0,  8,  6,  6, 6, 6,  0,  16, 6,
                                     6, 6,  6,  6,  0,  0,  0,  0,  14, 12, 0, 6, 24, 60, 0};
  std::size_t len = kLen[id];
  if (id == 0x1BU) {
    len = k.pickup_report_len; // A41
  } else if (id == 0x2EU) {
    len = k.wheel_encoder_len; // A42
  } else {
    // the table above
  }
  if (len < 4U) {
    len = 10U;
  }
  Bytes b(len, 0);
  b[0] = id;
  uint8_t& seq = k.report_seq_global ? report_seq_global_ : report_seq_[id];
  b[1] = seq++;
  b[2] = static_cast<uint8_t>(((delay_ticks >> 6U) & 0xFCU) |
                              3U); // accuracy 3, delay bits 13..8 in bits 7..2
  b[3] = static_cast<uint8_t>(delay_ticks & 0xFFU);
  for (std::size_t i = 4; i < len; ++i) {
    b[i] = SampleByte(id, n, static_cast<uint32_t>(i));
  }
  if ((id == 0x11) && (len >= 12U)) {
    b[10] = static_cast<uint8_t>(k.step_counter_upper_bytes & 0xFFU);
    b[11] = static_cast<uint8_t>(k.step_counter_upper_bytes >> 8U);
  }
  if (((id == 0x10) || (id == 0x13) || (id == 0x1F)) && (len >= 5U)) {
    b[4] = static_cast<uint8_t>(1U | k.payload_upper_nonzero);
  }
  return b;
}

inline void Hub::sensors() {
  for (uint8_t id = 1; id < kSensors; ++id) {
    Feature& f = feat[id];
    if (f.interval_us == 0U) {
      continue;
    }
    if ((id >= 0x0A) && (id <= 0x0E) && (((k.env_sensors >> id) & 1U) == 0U)) {
      continue; // A20: the secondary-bus part is absent
    }
    const bool runs = awake || ((f.flags & 0x0CU) != 0U);
    while (static_cast<int32_t>(now - f.next_us) >= 0) {
      if (runs) {
        batch_[id].emplace_back(f.next_us, Bytes{});
        ++f.samples;
      }
      f.next_us += f.interval_us;
    }
    auto it = batch_.find(id);
    if ((it != batch_.end()) && !it->second.empty() &&
        ((f.batch_us == 0U) || ((now - it->second.front().first) >= f.batch_us))) {
      sendBatch(id);
    }
  }
}

inline void Hub::sendBatch(uint8_t id) {
  auto it = batch_.find(id);
  if ((it == batch_.end()) || it->second.empty()) {
    return;
  }
  auto samples = it->second;
  it->second.clear();
  emitCargo(id, samples);
}

inline void Hub::emitCargo(uint8_t id, const std::vector<std::pair<uint32_t, Bytes>>& reps) {
  const uint8_t ch = ((feat[id].flags & 0x04U) != 0U) ? 4 : 3;
  std::size_t i = 0;
  uint32_t n0 = feat[id].samples - static_cast<uint32_t>(reps.size());
  while (i < reps.size()) {
    const uint32_t ref = reps[i].first;
    Bytes cargo(5, 0);
    cargo[0] = 0xFB;
    const uint32_t base = k.base_overflow ? 0x7FFFFFFFU : ((now - ref) / 100U);
    for (uint32_t b = 0; b < 4U; ++b) {
      cargo[1U + b] = static_cast<uint8_t>((base >> (8 * b)) & 0xFFU);
    }
    while ((i < reps.size()) && ((cargo.size() + 20U) <= cargo_split)) {
      const uint32_t delay = (reps[i].first - ref) / 100U;
      const Bytes r = makeReport(id, n0 + static_cast<uint32_t>(i), delay & 0x3FFFU);
      cargo.insert(cargo.end(), r.begin(), r.end());
      ++i;
    }
    queue(ch, cargo);
  }
}

// ---- FRS
// ---------------------------------------------------------------------------------------------------

inline Bytes Hub::metadataWords(uint8_t sensor) const {
  Bytes w;
  const auto put32 = [&w](uint32_t v) {
    for (uint32_t b = 0; b < 4U; ++b) {
      w.push_back(static_cast<uint8_t>((v >> (8 * b)) & 0xFFU));
    }
  };
  put32(0x00030201U); // MotionEngine, MotionHub, SensorHub versions
  put32(0x00000800U); // range
  put32(0x00000100U); // resolution
  put32((static_cast<uint32_t>(k.metadata_revision) << 16U) | 0x0080U); // revision, power
  put32(10000U);                                                        // min period
  put32((16U << 16U) | 64U);                                            // fifo reserved, fifo max
  put32((9U << 16U) | 96U); // vendor ID length, batch buffer bytes
  put32((static_cast<uint32_t>(sensor == 0x05U ? 14U : 8U) << 16U) | 0U);     // Q2, Q1
  put32((static_cast<uint32_t>(sensor == 0x05U ? k.rv_q3 : 0U) << 16U) | 0U); // Q3, specific length
  if (k.metadata_revision >= 4U) {
    put32(1000000U); // max period
  }
  const std::string_view vid = "Vendor-ID-";
  for (uint32_t i = 0; i < 12U; ++i) {
    w.push_back(std::bit_cast<uint8_t>(vid[i % vid.size()]));
  }
  return w;
}

inline void Hub::onFrsRead(const Bytes& r) {
  const auto type =
      static_cast<uint16_t>(static_cast<uint32_t>(r[4]) | (static_cast<uint32_t>(r[5]) << 8U));
  const auto reply = [this, type](uint8_t status, uint8_t words, uint16_t off, uint32_t d0,
                                  uint32_t d1) {
    Bytes b(16, 0);
    b[0] = 0xF3;
    b[1] = static_cast<uint8_t>((static_cast<uint32_t>(words) << 4U) | (status & 0x0FU));
    b[2] = static_cast<uint8_t>(off & 0xFFU);
    b[3] = static_cast<uint8_t>(off >> 8U);
    for (uint32_t i = 0; i < 4U; ++i) {
      b[4U + i] = static_cast<uint8_t>((d0 >> (8 * i)) & 0xFFU);
      b[8U + i] = static_cast<uint8_t>((d1 >> (8 * i)) & 0xFFU);
    }
    b[12] = static_cast<uint8_t>(type & 0xFFU);
    b[13] = static_cast<uint8_t>(type >> 8U);
    queue(2, b);
  };
  if (frs_phase_ == 1) {
    if (k.frs_busy_interleave) {
      reply(2, 0, 0, 0, 0); // BUSY (A29)
    }
    return;
  }
  if ((k.frs_reject_nonzero_reserved && ((r[1] != 0U) || (r[6] != 0U) || (r[7] != 0U))) ||
      (std::find(frs_unknown.begin(), frs_unknown.end(), type) != frs_unknown.end())) {
    reply(1, 0, 0, 0, 0);
    return;
  }
  std::vector<uint32_t> words;
  const auto it = frs.find(type);
  if (it != frs.end()) {
    words = it->second;
  } else if ((type >= 0xE301U) && (type <= 0xE325U)) {
    const Bytes m = metadataWords(0);
    static_cast<void>(m);
    // a metadata record is synthesised from the sensor it belongs to (RM Figure 27)
    static const uint8_t kSensorOf[0x26] = {
        0,    0x14, 0x01, 0x04, 0x06, 0x15, 0x02, 0x07, 0x16, 0x03, 0x0F, 0x05, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x10, 0x18, 0x11, 0x12, 0x13, 0x19, 0x1A,
        0x1B, 0x1C, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x28, 0x29, 0x2A, 0x2B};
    const uint8_t sensor = kSensorOf[type - 0xE300U];
    const Bytes mb = metadataWords(sensor);
    for (std::size_t i = 0; (i + 3U) < mb.size(); i += 4U) {
      words.push_back(static_cast<uint32_t>(mb[i]) | (static_cast<uint32_t>(mb[i + 1U]) << 8U) |
                      (static_cast<uint32_t>(mb[i + 2U]) << 16U) |
                      (static_cast<uint32_t>(mb[i + 3U]) << 24U));
    }
  } else {
    // an unknown record: no words
  }
  if (words.empty()) {
    reply(5, 0, 0, 0, 0);
    return;
  }
  for (std::size_t off = 0; off < words.size(); off += 2U) {
    const std::size_t cnt = std::min<std::size_t>(2U, words.size() - off);
    const bool last = (off + cnt) >= words.size();
    reply(last ? 3 : 0, static_cast<uint8_t>(cnt), static_cast<uint16_t>(off), words[off],
          cnt > 1U ? words[off + 1U] : 0U);
  }
}

inline void Hub::sendFrsWrite(uint8_t status, uint16_t off) {
  Bytes b(4, 0);
  b[0] = 0xF5;
  b[1] = status;
  b[2] = static_cast<uint8_t>(off & 0xFFU);
  b[3] = static_cast<uint8_t>(off >> 8U);
  queue(2, b);
}

inline void Hub::onFrsWriteReq(const Bytes& r) {
  if (r.size() < 6U) {
    return;
  }
  frs_len_ =
      static_cast<uint16_t>(static_cast<uint32_t>(r[2]) | (static_cast<uint32_t>(r[3]) << 8U));
  frs_type_ =
      static_cast<uint16_t>(static_cast<uint32_t>(r[4]) | (static_cast<uint32_t>(r[5]) << 8U));
  if (std::find(frs_unknown.begin(), frs_unknown.end(), frs_type_) != frs_unknown.end()) {
    sendFrsWrite(1, 0);
    return;
  }
  if (std::find(frs_readonly.begin(), frs_readonly.end(), frs_type_) != frs_readonly.end()) {
    sendFrsWrite(11, 0);
    return;
  }
  if ((frs_phase_ == 1) && k.frs_busy_interleave) {
    sendFrsWrite(2, 0);
    return;
  }
  frs_phase_ = 1;
  frs_buf_.clear();
  sendFrsWrite(4, 0);
  if (frs_len_ == 0U) {
    frsWriteDone();
  }
}

inline void Hub::onFrsWriteData(const Bytes& r) {
  if ((frs_phase_ != 1) || (r.size() < 12U)) {
    sendFrsWrite(6, 0);
    return;
  }
  const auto off =
      static_cast<uint16_t>(static_cast<uint32_t>(r[2]) | (static_cast<uint32_t>(r[3]) << 8U));
  if (off != frs_buf_.size()) {
    sendFrsWrite(6, off);
    return;
  }
  for (uint32_t w = 0; w < 2U; ++w) {
    if (frs_buf_.size() < frs_len_) {
      uint32_t v = 0;
      for (uint32_t i = 0; i < 4U; ++i) {
        v |= static_cast<uint32_t>(r[4U + (4 * w) + i]) << (8 * i);
      }
      frs_buf_.push_back(v);
    }
  }
  sendFrsWrite(0, off);
  if (frs_buf_.size() >= frs_len_) {
    frsWriteDone();
  }
}

inline void Hub::frsWriteDone() {
  frs_phase_ = 0;
  if (frs_fail_status != 0U) {
    sendFrsWrite(frs_fail_status, 0);
    return;
  }
  if (frs_buf_.empty()) {
    frs.erase(frs_type_);
  } else {
    frs[frs_type_] = frs_buf_;
  }
  if (k.frs_final_8_then_3) {
    sendFrsWrite(8, 0);
  }
  sendFrsWrite(3, 0);
}

// ---- commands
// -----------------------------------------------------------------------------------------------

inline void Hub::cmdResponse(uint8_t cmd, uint8_t cmd_seq, uint8_t resp_seq, const Bytes& rr,
                             bool autonomous) {
  Bytes b(16, 0);
  b[0] = 0xF1;
  b[1] = cmd_resp_seq_++;
  b[2] = static_cast<uint8_t>(cmd | (autonomous ? 0x80U : 0U));
  b[3] = cmd_seq;
  b[4] = resp_seq;
  for (std::size_t i = 0; i < 11U; ++i) {
    b[5U + i] = (i < rr.size()) ? rr[i] : k.cmd_reserved_fill; // reserved bytes are undefined
  }
  queue(2, b);
}

inline void Hub::onCommand(const Bytes& r) {
  const uint8_t seq = r[1];
  const uint8_t cmd = r[2];
  const uint8_t* p = &r[3];
  switch (cmd) {
  case 0x01: { // errors
    uint8_t rs = 0;
    for (const Bytes& e : sh2_errors) {
      if ((e.size() >= 6U) && (e[0] <= p[0])) {
        cmdResponse(cmd, seq, rs++, e);
      }
    }
    cmdResponse(cmd, seq, rs, Bytes{0, 0, 255, 0, 0, 0});
    break;
  }
  case 0x02:
    if (p[0] == 0U) {
      for (uint32_t half = 0; half < 2U; ++half) {
        Bytes rr(11, 0);
        rr[0] = p[1];
        rr[1] = 1;
        for (uint32_t i = 0; i < 4U; ++i) {
          rr[3U + i] = static_cast<uint8_t>(
              (counts[static_cast<std::size_t>(half) * 2U] >> (8 * i)) & 0xFFU);
          rr[7U + i] = static_cast<uint8_t>((counts[(2 * half) + 1] >> (8 * i)) & 0xFFU);
        }
        cmdResponse(cmd, seq, static_cast<uint8_t>(half), rr);
      }
    } else {
      for (auto& v : counts) {
        v = 0;
      }
    }
    break;
  case 0x03:
    if (p[0] == 0U) {
      ++tares;
      last_tare_axes = p[1];
      last_tare_basis = p[2];
    } else if (p[0] == 1U) {
      last_tare_persist = true;
    } else if (p[0] == 2U) {
      last_reorient.assign(p + 1, p + 9);
    } else {
      // another tare sub-command: nothing recorded
    }
    break;
  case 0x04:
    cmdResponse(cmd, seq, 0, Bytes{init_status, p[0]});
    break;
  case 0x06:
    ++dcd_saves;
    cmdResponse(cmd, seq, 0, Bytes{save_dcd_status});
    break;
  case 0x07:
    if (p[3] == 0U) {
      me_cal[0] = p[0];
      me_cal[1] = p[1];
      me_cal[2] = p[2];
      me_cal[4] = p[4];
      me_cal[5] = p[5];
    }
    cmdResponse(cmd, seq, 0,
                Bytes{me_status, me_cal[0], me_cal[1], me_cal[2], me_cal[4], me_cal[5]});
    break;
  case 0x09:
    periodic_dcd_p0 = p[0];
    break;
  case 0x0A:
    cmdResponse(cmd, seq, 0, Bytes{oscillator});
    break;
  case 0x0B:
    dcd_cleared = true;
    reboot(2, exec_reset_delay_us);
    break;
  case 0x0C:
    if (p[0] == 0U) {
      simple_cal_started = true;
      simple_cal_interval = static_cast<uint32_t>(p[1]) | (static_cast<uint32_t>(p[2]) << 8U) |
                            (static_cast<uint32_t>(p[3]) << 16U) |
                            (static_cast<uint32_t>(p[4]) << 24U);
      cmdResponse(cmd, seq, 0, Bytes{0});
    } else {
      cmdResponse(cmd, seq, 0, Bytes{1, simple_cal_status});
    }
    break;
  case 0x0E:
    if ((chip == Chip::Bno086) || k.bno086_features_on_085) {
      last_motion_intent = p[0];
    }
    break;
  case 0x0F:
    last_wheel.assign(p, p + 8);
    break;
  default:
    break;
  }
}

// ---- UART
// --------------------------------------------------------------------------------------------------

inline void Hub::frameOut(uint8_t proto, const Bytes& payload) {
  const auto put = [this](uint8_t b) {
    if ((b == 0x7EU) || (b == 0x7DU)) {
      uart_out_.push_back(0x7D);
      uart_out_.push_back(static_cast<uint8_t>(b ^ 0x20U));
    } else {
      uart_out_.push_back(b);
    }
  };
  uart_out_.push_back(0x7E);
  put(proto);
  for (uint8_t b : payload) {
    put(b);
  }
  uart_out_.push_back(0x7E);
}

inline void Hub::pumpUartTx() {
  while (booted_ && (!out_.empty() || !raw_.empty())) {
    Bytes t = takeTransfer();
    if (!t.empty()) {
      frameOut(1, t);
    }
  }
  int_active_ = false;
}

inline std::size_t Hub::UartTake(uint8_t* p, std::size_t cap) {
  pumpUartTx();
  std::size_t n = 0;
  while ((n < cap) && !uart_out_.empty()) {
    uint8_t b = uart_out_.front();
    uart_out_.pop_front();
    if (k.clock_internal && ((++uart_corrupt_ % 97U) == 0U)) {
      b = static_cast<uint8_t>(b ^ 0x10U); // A10: an inaccurate clock garbles bytes
    }
    p[n++] = b;
  }
  return n;
}

inline void Hub::UartByteIn(uint8_t b) {
  if (uart_seen_byte_ && ((now - uart_last_byte_us_) < 100U)) {
    ++c.uart_gap_violations;
  }
  uart_seen_byte_ = true;
  uart_last_byte_us_ = now;
  if (b == 0x7EU) {
    if (uart_in_frame_ && !uart_frame_.empty()) {
      ++c.uart_frames_closed;
      uartFrameIn();
    }
    uart_frame_.clear();
    uart_in_frame_ = true;
    uart_escape_ = false;
    return;
  }
  if (!uart_in_frame_) {
    ++c.uart_stray_bytes;
    return;
  }
  if (b == 0x7DU) {
    uart_escape_ = true;
    return;
  }
  uart_frame_.push_back(uart_escape_ ? static_cast<uint8_t>(b ^ 0x20U) : b);
  uart_escape_ = false;
}

inline void Hub::uartFrameIn() {
  const uint8_t proto = uart_frame_[0];
  if (proto == 0U) { // Buffer Status Query (SHTP S4.3): answer with a Buffer Status Notification
    ++c.bsq_in;
    ++bsn_count_;
    const bool lost =
        (bsn_drop > 0U) || ((bsn_loss_rate != 0U) && ((bsn_count_ % bsn_loss_rate) == 0U));
    if (lost) {
      bsn_drop = (bsn_drop > 0U) ? (bsn_drop - 1U) : 0U;
      ++c.bsn_dropped; // the notification never reaches the host
      return;
    }
    frameOut(0, Bytes{static_cast<uint8_t>(uart_buffer_bytes & 0xFFU),
                      static_cast<uint8_t>(uart_buffer_bytes >> 8U)});
    uart_have_token_ = true;
    uart_token_at_ = now;
    uart_token_bytes_ = uart_buffer_bytes;
    return;
  }
  if (proto != 1U) {
    ++c.uart_bad_frames;
    return;
  }
  const bool token_ok = uart_have_token_ &&
                        (static_cast<uint64_t>(now - uart_token_at_) <=
                         (static_cast<uint64_t>(uart_timeout_ms) * 1000U)) &&
                        ((uart_frame_.size() - 1U) <= uart_token_bytes_);
  uart_have_token_ = false; // a token ends with the next host write
  if (!token_ok) {
    ++c.uart_no_token_writes;
    return;
  }
  static_cast<void>(HostWrite(uart_frame_.data() + 1, uart_frame_.size() - 1U));
}

// ---- RVC
// ----------------------------------------------------------------------------------------------------

inline Bytes Hub::RvcFrame(uint8_t idx, int16_t yaw, int16_t pitch, int16_t roll, int16_t ax,
                           int16_t ay, int16_t az, uint8_t mi, uint8_t mr) const {
  Bytes f(19, 0);
  f[0] = 0xAA;
  f[1] = 0xAA;
  f[2] = idx;
  const int16_t v[6] = {yaw, pitch, roll, ax, ay, az};
  for (uint32_t i = 0; i < 6U; ++i) {
    f[3U + (2 * i)] = static_cast<uint8_t>(static_cast<uint16_t>(v[i]) & 0xFFU);
    f[4U + (2 * i)] = static_cast<uint8_t>(static_cast<uint16_t>(v[i]) >> 8U);
  }
  if (k.rvc_mi_mr) {
    f[15] = mi;
    f[16] = mr;
  }
  uint32_t sum = 0;
  if (k.rvc_csum_includes_mi_mr) {
    for (uint32_t i = 2; i <= 17U; ++i) {
      sum += f[i];
    }
  } else {
    for (uint32_t i = 2; i <= 14U; ++i) {
      sum += f[i];
    }
    sum += f[17];
  }
  f[18] = static_cast<uint8_t>(sum & 0xFFU);
  return f;
}

} // namespace hubmodel

#endif // BNO08X_HUB_MODEL_HPP
