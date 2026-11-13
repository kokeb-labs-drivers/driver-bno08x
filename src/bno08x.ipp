/**
 * @file bno08x.ipp
 * @brief Implementation of `bno08x::Bno08x`; included at the end of `inc/bno08x.hpp`.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 */
#ifndef BNO08X_IPP
#define BNO08X_IPP

namespace bno08x {

namespace detail {

/// Reports of the control channel and their lengths (RM Figure 32).
inline constexpr std::array<std::array<uint8_t, 2>, 8> kControlLens{{
    {{reg::kRepGetFeatureRsp, 17}},
    {{reg::kRepBaseTimestamp, 5}},
    {{reg::kRepTimestampRebase, 5}},
    {{reg::kRepProductIdRsp, 16}},
    {{reg::kRepFrsWriteRsp, 4}},
    {{reg::kRepFrsReadRsp, 16}},
    {{reg::kRepCommandRsp, 16}},
    {{reg::kRepFlushDone, 2}},
}};

/// Length of a control-channel report, 0 for an ID the driver does not know.
[[nodiscard]] constexpr std::size_t ControlLen(uint8_t id) noexcept {
  const auto it = std::ranges::find_if(
      kControlLens, [id](const std::array<uint8_t, 2>& row) { return row[0] == id; });
  return (it == kControlLens.end()) ? 0U : (*it)[1];
}

/// The common feature report bytes (without the leading report ID) of `fc`.
constexpr void EncodeFeature(std::span<uint8_t> out, uint8_t id, const FeatureConfig& fc) noexcept {
  out[0] = reg::kRepSetFeature;
  out[1] = id;
  uint32_t flags = 0;
  flags |= fc.relative ? reg::kFlagRelative : 0U;
  flags |= fc.change_enable ? reg::kFlagChangeEnable : 0U;
  flags |= fc.wake ? reg::kFlagWake : 0U;
  flags |= fc.always_on ? reg::kFlagAlwaysOn : 0U;
  out[2] = static_cast<uint8_t>(flags);
  PutLe16(out, 3U, static_cast<uint32_t>(static_cast<uint16_t>(fc.change_sensitivity)));
  PutLe32(out, 5U, fc.interval_us);
  PutLe32(out, 9U, fc.batch_us);
  PutLe32(out, 13U, fc.sensor_config);
}

/// The feature configuration of a 17-byte feature report (0xFD or 0xFC).
[[nodiscard]] constexpr FeatureConfig DecodeFeature(std::span<const uint8_t> rep) noexcept {
  FeatureConfig fc{};
  const uint32_t flags = rep[2];
  fc.relative = (flags & reg::kFlagRelative) != 0U;
  fc.change_enable = (flags & reg::kFlagChangeEnable) != 0U;
  fc.wake = (flags & reg::kFlagWake) != 0U;
  fc.always_on = (flags & reg::kFlagAlwaysOn) != 0U;
  fc.change_sensitivity = static_cast<int16_t>(S16(Le16(rep, 3U)));
  fc.interval_us = Le32(rep, 5U);
  fc.batch_us = Le32(rep, 9U);
  fc.sensor_config = Le32(rep, 13U);
  return fc;
}

/// The error of a failed FRS read status (RM Figure 35): busy, a device error, or a refusal (1
/// unrecognised and the deprecated 4, 6 and 7).
[[nodiscard]] constexpr Error FrsReadStatusError(uint8_t status) noexcept {
  if (status == reg::kFrsRBusy) {
    return Error::Busy;
  }
  return (status == reg::kFrsRDeviceError) ? Error::DeviceFault : Error::Refused;
}

/// Whether `id` is one of the sensor IDs of `SensorId` (a caller can cast any byte to the enum).
[[nodiscard]] constexpr bool KnownSensor(SensorId id) noexcept {
  SensorId unused{};
  return ToSensorId(static_cast<uint8_t>(ToUnderlying(id)), unused);
}

/// Bit set of the sensors that have no change sensitivity (RM S5.4.2 and the report notes).
[[nodiscard]] constexpr bool NoSensitivity(SensorId id) noexcept {
  constexpr uint64_t kMask = (1ULL << 0x10) | (1ULL << 0x12) | (1ULL << 0x13) | (1ULL << 0x18) |
                             (1ULL << 0x19) | (1ULL << 0x1A) | (1ULL << 0x1B) | (1ULL << 0x1C) |
                             (1ULL << 0x1F) | (1ULL << 0x20) | (1ULL << 0x21) | (1ULL << 0x22) |
                             (1ULL << 0x2B) | (1ULL << 0x2C) | (1ULL << 0x2D);
  const uint32_t bit = static_cast<uint32_t>(ToUnderlying(id));
  return (bit < 64U) && (((kMask >> bit) & 1ULL) != 0ULL);
}

} // namespace detail

// --------------------------------------------------------------------------------------------
// Session
// --------------------------------------------------------------------------------------------

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::clearSession() noexcept {
  rx_.Reset();
  uart_.Reset();
  rvc_.Reset();
  adv_ = detail::DefaultAdvert();
  state_ = State::Idle;
  reset_low_ = false;
  pin_reset_ = false;
  advert_seen_ = false;
  reset_seen_ = false;
  init_seen_ = false;
  restarting_ = false;
  advert_asked_ = false;
  restart_event_pending_ = false;
  tx_seq_.fill(0U);
  resetTxPath();
  op_head_ = 0;
  op_count_ = 0;
  features_.fill(FeatureConfig{});
  desired_.fill(FeatureConfig{});
  have_latest_.fill(false);
  last_report_seq_.fill(0U);
  metadata_.fill(Metadata{});
  restore_next_ = kSensorSlots;
  viol_count_ = 0;
  product_ = ProductId{};
  product_count_ = 0;
  frs_dst_ = nullptr;
}

template <HubPort Port, LimitsLike Limits>
Result<void> Bno08x<Port, Limits>::Begin(const Config& cfg) noexcept {
  if (!cfg.Valid()) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (!port_.EnsureInitialized()) {
    return Result<void>::Failure(Error::Bus);
  }
  failAll(Error::NotInitialized); // a new session: every waiting operation of the old one ends
  bool close_frame = false;
  if constexpr ((kTransport == Transport::UartShtp) && !detail::HasSetReset<Port>) {
    // the hub keeps running: a frame it has begun is closed, not left open
    close_frame = uart_frame_open_;
  }
  cfg_ = cfg;
  now_us_ = port_.NowUs();
  since_us_ = now_us_;
  last_poll_us_ = now_us_;
  clearSession(); // the events already queued (those OpDone included) stay for PopEvent()
  if (close_frame) {
    uart_tx_[0] = detail::kUartFlag;
    uart_tx_len_ = 1U;
    uart_frame_open_ = true;
  }
  if constexpr (kTransport == Transport::UartRvc) {
    state_ = State::Ready;
    return Result<void>::Success();
  }
  return startReset();
}

template <HubPort Port, LimitsLike Limits>
Result<void> Bno08x<Port, Limits>::startReset() noexcept {
  pin_reset_ = false;
  if constexpr (kTransport == Transport::Spi) {
    // PS0/WAKE is high from before the reset until H_INTN, or the hub starts on UART (DS p18)
    wake_active_ = false;
    if (!port_.SetWake(true)) {
      return Result<void>::Failure(Error::Bus);
    }
  }
  if constexpr (detail::HasSetReset<Port>) {
    if constexpr (detail::HasSetBootn<Port>) {
      if (!port_.SetBootn(true)) { // a stray low level at reset would start the bootloader
        return Result<void>::Failure(Error::Bus);
      }
    }
    if (!port_.SetReset(false)) {
      return Result<void>::Failure(Error::Bus);
    }
    pin_reset_ = true;
    reset_low_ = true;
    state_ = State::Resetting;
    since_us_ = now_us_;
  } else {
    beginWaitAdvert();
  }
  return Result<void>::Success();
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::beginWaitAdvert() noexcept {
  state_ = State::WaitAdvert;
  since_us_ = now_us_;
  advert_at_us_ = now_us_;
  reset_heard_ = false;
  init_heard_ = false;
  advert_seen_ = false;
  reset_seen_ = false;
  init_seen_ = false;
  advert_asked_ = false;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::stepReset() noexcept {
  const uint32_t elapsed = now_us_ - since_us_;
  if constexpr (detail::HasSetReset<Port>) {
    if (reset_low_) {
      if (elapsed >= cfg_.reset_pulse_us) {
        if (!port_.SetReset(true)) {
          enterFaulted(Error::Bus);
          log(LogLevel::Error, "bno08x: NRST release failed");
          return;
        }
        reset_low_ = false;
        since_us_ = now_us_;
      }
      return;
    }
  }
  // t1 (at least 90 ms) plus t2 (about 4 ms), rounded up (DS p47)
  if (elapsed >= ((reg::kStartupT1Ms + reg::kStartupT2Ms + 6U) * 1000U)) {
    beginWaitAdvert();
  }
}

template <HubPort Port, LimitsLike Limits>
Result<void> Bno08x<Port, Limits>::HardReset() noexcept {
  if constexpr (detail::HasSetReset<Port>) {
    if (state_ == State::Idle) {
      return Result<void>::Failure(Error::NotInitialized);
    }
    failAll(Error::HubReset);
    rx_.Reset();
    uart_.Reset();
    resetTxPath();
    tx_seq_.fill(0U);
    features_.fill(FeatureConfig{});
    restarting_ = true;
    restart_event_pending_ = true;
    const Result<void> started = startReset();
    if (!started.ok()) {
      restartFailed();
    }
    return started;
  } else {
    return Result<void>::Failure(Error::Unsupported);
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::restartFailed() noexcept {
  restarting_ = false; // no restart is coming: no Reset event, no restore
  restart_event_pending_ = false;
  enterFaulted(Error::Bus);
  log(LogLevel::Error, "bno08x: the reset pins could not be driven");
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::Poll(uint32_t now_us) noexcept {
  now_us_ = now_us;
  if constexpr (kTransport == Transport::UartRvc) {
    if (state_ == State::Ready) {
      pumpRxRvc();
    }
    return;
  }
  if (state_ == State::Idle) {
    return;
  }
  if (state_ == State::Faulted) {
    serviceFaulted();
    return;
  }
  if (state_ == State::Resetting) {
    stepReset();
    return;
  }
  expireWindows();
  pumpRx();
  checkStartup();
  checkOpTimeout();
  serviceRestore();
  pumpTx();
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::expireWindows() noexcept {
  // checked on every Poll(), so a 32-bit elapsed time never wraps into a fresh one unless Poll()
  // pauses for 2^32 us (71.6 minutes)
  const uint32_t window = static_cast<uint32_t>(cfg_.resync_window_ms) * 1000U;
  if ((viol_count_ > 0U) && ((now_us_ - viol_start_us_) > window)) {
    viol_count_ = 0;
  }
  if ((state_ == State::Ready) && !inStartupWindow()) {
    reset_heard_ = true; // the window is closed for good: it cannot reopen when the clock wraps
    init_heard_ = true;
  }
  if constexpr (kTransport == Transport::UartShtp) {
    static_cast<void>(uartTokenOk(0U)); // an expired notification is dropped now
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::serviceFaulted() noexcept {
  expireWindows(); // a notification received before the fault expires here too
  if (op_count_ == 0U) {
    if constexpr (kTransport == Transport::UartShtp) {
      uartPump(); // a closing flag byte still goes out
    }
    return; // only stop requests are queued in Faulted
  }
  if constexpr (kTransport == Transport::UartShtp) {
    pumpRxUart(); // for the Buffer Status Notification a write needs; cargo is not taken
  }
  checkOpTimeout();
  pumpTx();
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::releaseWake() noexcept {
  if constexpr (kTransport == Transport::Spi) {
    if (wake_active_) {
      static_cast<void>(port_.SetWake(true));
    }
  }
  wake_active_ = false;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::resetTxPath() noexcept {
  tx_pending_ = false;
  releaseWake();
  uart_tx_len_ = 0;
  uart_tx_pos_ = 0;
  uart_tx_cargo_ = false;
  uart_frame_open_ = false;
  token_valid_ = false;
  bsq_out_ = false;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::abortUartTx() noexcept {
  if constexpr (kTransport == Transport::UartShtp) {
    // a frame the hub has begun, or a closing flag that has not gone out yet
    const bool partial = uart_frame_open_;
    if ((uart_tx_pos_ < uart_tx_len_) && !uart_tx_cargo_) {
      bsq_out_ = false; // the query never went out whole: no notification is coming
    }
    uart_tx_len_ = 0;
    if (partial) {
      uart_tx_[0] = detail::kUartFlag; // closes the frame the hub has begun (SHTP S4.1)
      uart_tx_len_ = 1;
    }
    uart_tx_pos_ = 0;
    uart_tx_cargo_ = false;
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::checkStartup() noexcept {
  if ((state_ != State::WaitAdvert) && (state_ != State::WaitReady)) {
    return;
  }
  const uint32_t elapsed = now_us_ - since_us_;
  if ((state_ == State::WaitAdvert) && !pin_reset_ && !advert_asked_ &&
      ((now_us_ - advert_at_us_) >= (static_cast<uint32_t>(cfg_.advert_retry_ms) * 1000U))) {
    detail::Op op{};
    op.kind = detail::OpKind::Advertise;
    op.silent = true;
    op.channel = reg::kChanCommand;
    op.req[0] = reg::kShtpAdvertise;
    op.req[1] = 1U; // the whole hub (SHTP S5.1.1)
    op.req_len = 2U;
    advert_asked_ = enqueue(op).ok();
    advert_at_us_ = now_us_;
    if (advert_asked_) {
      toFront(); // stops queued before the session wait for it, they cannot go out before Ready
    }
  }
  if (elapsed >= (static_cast<uint32_t>(cfg_.startup_timeout_ms) * 1000U)) {
    enterFaulted(Error::Timeout);
    log(LogLevel::Error, "bno08x: start-up timed out");
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::toFront() noexcept {
  // only called in WaitAdvert, where no request can have started: stops wait for Ready, and the
  // queue was emptied on the way in (Begin(), a reset)
  if (op_count_ < 2U) {
    return;
  }
  const detail::Op last = ops_[(op_head_ + op_count_ - 1U) % Limits::kOpQueue];
  op_head_ = (op_head_ + Limits::kOpQueue - 1U) % Limits::kOpQueue;
  ops_[op_head_] = last;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::enterFaulted(Error err) noexcept {
  state_ = State::Faulted;
  releaseWake(); // WAKE high, and a half-sent UART frame is closed, before the stops go out
  abortUartTx();
  failAll(err, true); // a queued stop request still goes out
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::maybeReady() noexcept {
  if ((state_ == State::WaitReady) && advert_seen_ && reset_seen_ &&
      (init_seen_ || !cfg_.require_init_response)) {
    becomeReady();
  }
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::inStartupWindow() const noexcept {
  return (now_us_ - ready_us_) < (static_cast<uint32_t>(cfg_.startup_timeout_ms) * 1000U);
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::becomeReady() noexcept {
  state_ = State::Ready;
  ready_us_ = now_us_;
  log(LogLevel::Info, "bno08x: session ready");
  detail::Op op{};
  op.kind = detail::OpKind::ProductId;
  op.silent = true;
  op.req[0] = reg::kRepProductIdReq;
  op.req_len = 2U;
  static_cast<void>(enqueue(op));
  if (restarting_ && cfg_.auto_restore) {
    restore_next_ = 0;
    restore_tries_ = 0;
  }
  restarting_ = false;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::hubRestarted(uint8_t trigger) noexcept {
  ++stats_.hub_resets;
  failAll(Error::HubReset);
  rx_.Abort();
  rx_.Reset();
  tx_seq_.fill(0U);
  resetTxPath();
  features_.fill(FeatureConfig{});
  have_latest_.fill(false); // samples start again; a sensor's own numbers are checked after one
  metadata_.fill(Metadata{});
  restarting_ = true;
  restart_event_pending_ = true;
  state_ = State::WaitReady;
  since_us_ = now_us_;
  advert_seen_ = true;
  reset_seen_ = (trigger != 0U);
  init_seen_ = (trigger == 2U);
  reset_heard_ = (trigger == 1U);
  init_heard_ = (trigger == 2U);
  log(LogLevel::Warning, "bno08x: the hub restarted");
  maybeReady();
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::noteViolation() noexcept {
  const uint32_t window = static_cast<uint32_t>(cfg_.resync_window_ms) * 1000U;
  if ((viol_count_ == 0U) || ((now_us_ - viol_start_us_) > window)) {
    viol_start_us_ = now_us_;
    viol_count_ = 0;
  }
  ++viol_count_;
  if (viol_count_ >= cfg_.resync_limit) {
    recover();
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::recover() noexcept {
  ++stats_.resyncs;
  viol_count_ = 0;
  log(LogLevel::Warning, "bno08x: too many framing faults, resetting the hub");
  failAll(Error::Framing);
  rx_.Reset();
  uart_.Reset();
  if constexpr (detail::HasSetReset<Port>) {
    resetTxPath(); // NRST restarts the hub: nothing of the old transmission matters
    restarting_ = true;
    restart_event_pending_ = true;
    if (!startReset().ok()) {
      restartFailed();
    }
  } else {
    // the hub keeps running: a half-sent UART frame is closed so the reset command reaches it
    releaseWake();
    abortUartTx(); // a Buffer Status Notification already received is the hub's and still holds
    bsq_out_ = false;
    // the driver's own request: no OpDone for an id the caller never had
    static_cast<void>(enqueueExec(reg::kExecReset, true));
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::restoreFailed(uint8_t sensor) noexcept {
  if ((state_ != State::Ready) || (restore_tries_ >= kRestoreTries)) {
    ++stats_.restore_failed; // a restart or a new session restores again; else it is given up
    return;
  }
  ++restore_tries_;
  restore_next_ = std::min(restore_next_, static_cast<std::size_t>(sensor));
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::serviceRestore() noexcept {
  if ((state_ != State::Ready) || (restore_next_ >= kSensorSlots)) {
    return;
  }
  // a sensor the caller has a request queued for is left to that request
  while ((restore_next_ < kSensorSlots) &&
         ((desired_[restore_next_].interval_us == 0U) || userFeatureQueued(restore_next_))) {
    ++restore_next_;
  }
  if ((restore_next_ >= kSensorSlots) || (op_count_ >= (Limits::kOpQueue - 1U))) {
    return;
  }
  detail::Op op{};
  op.kind = detail::OpKind::SetFeature;
  op.silent = true;
  op.no_response = !cfg_.set_feature_confirm;
  op.sensor = static_cast<uint8_t>(restore_next_);
  detail::EncodeFeature(op.req, op.sensor, desired_[restore_next_]);
  op.req_len = static_cast<uint8_t>(reg::kLenGetFeature);
  if (enqueue(op).ok()) {
    ++restore_next_;
  }
}

// --------------------------------------------------------------------------------------------
// Receive
// --------------------------------------------------------------------------------------------

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::pumpRx() noexcept {
  if constexpr ((kTransport == Transport::I2c) || (kTransport == Transport::Spi)) {
    pumpRxBus();
  } else if constexpr (kTransport == Transport::UartShtp) {
    pumpRxUart();
  } else {
    pumpRxRvc();
  }
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::intActive() noexcept {
  bool active = false;
  if constexpr (detail::HasIntAsserted<Port>) {
    active = port_.IntAsserted();
  }
  if constexpr (kTransport == Transport::I2c) {
    if (!active &&
        cfg_.poll_without_int) { // guarded: read on a cadence, never faster than the interval
      const uint32_t interval = static_cast<uint32_t>(cfg_.poll_interval_ms) * 1000U;
      if ((now_us_ - last_poll_us_) >= interval) {
        last_poll_us_ = now_us_;
        active = true;
      }
    }
  }
  return active;
}

template <HubPort Port, LimitsLike Limits>
uint32_t Bno08x<Port, Limits>::takeIntTime() noexcept {
  uint32_t t = now_us_;
  if constexpr (detail::HasTakeIntTimestamp<Port>) {
    uint32_t captured = 0;
    if (port_.TakeIntTimestamp(captured)) {
      t = captured;
    }
  }
  return t;
}

template <HubPort Port, LimitsLike Limits>
std::size_t Bno08x<Port, Limits>::maxRead() const noexcept {
  std::size_t v = adv_.max_transfer_read;
  if (v == 0U) {
    v = adv_.max_cargo_read;
  }
  if ((v < reg::kShtpHeaderLen) || (v > kReadBuf)) {
    v = kReadBuf;
  }
  return v;
}

template <HubPort Port, LimitsLike Limits>
std::size_t Bno08x<Port, Limits>::maxWrite() const noexcept {
  std::size_t v = Limits::kTxCargo;
  if ((adv_.max_cargo_write >= reg::kShtpHeaderLen) && (adv_.max_cargo_write < v)) {
    v = adv_.max_cargo_write;
  }
  if ((adv_.max_transfer_write >= reg::kShtpHeaderLen) && (adv_.max_transfer_write < v)) {
    v = adv_.max_transfer_write;
  }
  return v;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::pumpRxBus() noexcept {
  if constexpr (kTransport == Transport::Spi) {
    if (tx_pending_) {
      return; // the duplex transfer of the pending write also reads what the hub has (SHTP S3.3)
    }
  }
  for (std::size_t n = 0; n < kMaxReadsPerPoll; ++n) {
    if (!intActive() || !readTransfer()) {
      break;
    }
  }
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::readTransfer() noexcept {
  if (!rx_.Open()) {
    t_int_us_ = takeIntTime();
  }
  const std::size_t n = rx_.NextReadLen(maxRead());
  bool ok = false;
  if constexpr (kTransport == Transport::I2c) {
    ok = port_.I2cRead(cfg_.i2c_address, rd_buf_.data(), n);
  } else if constexpr (kTransport == Transport::Spi) {
    ok = port_.SpiTransfer(zeros_.data(), rd_buf_.data(), n);
  } else {
    ok = false; // not reachable: only the I2C and SPI pumps call this
  }
  if (!ok) {
    ++stats_.bus_errors;
    return false;
  }
  onTransfer(std::span<const uint8_t>{rd_buf_}.first(n));
  return true;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onTransfer(std::span<const uint8_t> transfer) noexcept {
  trace_total_ = transfer.size();
  trace_len_ = std::min(transfer.size(), kTraceBytes);
  std::copy_n(transfer.begin(), trace_len_, trace_.begin());
  if (state_ == State::Faulted) {
    return; // no session: only stop requests are written, nothing read is used
  }
  const detail::RxResult r = rx_.Accept(transfer);
  switch (r) {
  case detail::RxResult::CargoComplete:
    dispatch(rx_.Channel(), rx_.Cargo());
    break;
  case detail::RxResult::Violation:
    noteViolation();
    break;
  case detail::RxResult::Partial:
  case detail::RxResult::Null:
  case detail::RxResult::Dropped:
    break;
  default:
    break;
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::pumpRxUart() noexcept {
  for (std::size_t round = 0; round < kMaxReadsPerPoll; ++round) {
    std::size_t got = 0;
    if constexpr ((kTransport == Transport::UartShtp) || (kTransport == Transport::UartRvc)) {
      got = port_.UartRead(uart_rx_chunk_.data(), uart_rx_chunk_.size());
    }
    got = std::min(got, uart_rx_chunk_.size());
    for (std::size_t i = 0; i < got; ++i) {
      if (uart_.Feed(uart_rx_chunk_[i]) == detail::FrameEvent::Frame) {
        t_int_us_ = takeIntTime();
        onUartFrame(uart_.Frame());
      }
    }
    if (got < uart_rx_chunk_.size()) {
      break;
    }
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onUartFrame(std::span<const uint8_t> frame) noexcept {
  if (frame[0] == detail::kUartProtoControl) {
    if (frame.size() == 3U) { // Buffer Status Notification (SHTP S4.3)
      token_bytes_ = detail::Le16(frame, 1U);
      token_us_ = now_us_;
      token_valid_ = true;
      bsq_out_ = false;
    }
    return;
  }
  onTransfer(frame.subspan(1U));
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::pumpRxRvc() noexcept {
  for (std::size_t round = 0; round < kMaxReadsPerPoll; ++round) {
    std::size_t got = 0;
    if constexpr (kTransport == Transport::UartRvc) {
      got = port_.UartRead(uart_rx_chunk_.data(), kRvcChunk);
    }
    got = std::min(got, kRvcChunk);
    for (std::size_t i = 0; i < got; ++i) {
      if (rvc_.Feed(uart_rx_chunk_[i])) {
        const RvcFrame& f = rvc_.Frame();
        Event ev{};
        ev.kind = EventKind::Rvc;
        ev.t_us = now_us_;
        ev.v = {f.index, f.yaw, f.pitch, f.roll, f.ax,
                f.ay,    f.az,  f.mi,    f.mr,   f.mi_mr_valid ? 1 : 0};
        pushEvent(ev);
      }
    }
    if (got < kRvcChunk) {
      break;
    }
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::dispatch(uint8_t channel, std::span<const uint8_t> cargo) noexcept {
  std::size_t role = reg::kChannelCount;
  for (std::size_t r = 0; r < reg::kChannelCount; ++r) {
    if (adv_.channel[r] == channel) {
      role = r;
      break;
    }
  }
  const bool is_advert = (role == reg::kChanCommand) && (cargo[0] == reg::kShtpAdvertise);
  if ((state_ == State::WaitAdvert) && !is_advert && !(role == reg::kChanCommand)) {
    ++stats_.early_cargo; // V16: nothing but the advertisement may come first
    return;
  }
  switch (role) {
  case reg::kChanCommand:
    onCommandChannel(cargo);
    break;
  case reg::kChanExecutable:
    onExecutable(cargo);
    break;
  case reg::kChanControl:
    onControl(cargo);
    break;
  case reg::kChanInputNormal:
    onInput(cargo, false);
    break;
  case reg::kChanInputWake:
    onInput(cargo, true);
    break;
  case reg::kChanGyroRv:
    onGyroRv(cargo);
    break;
  default:
    ++stats_.bad_channel;
    break;
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onCommandChannel(std::span<const uint8_t> cargo) noexcept {
  if (cargo[0] == reg::kShtpErrorList) {
    ++stats_.hub_errors;
    Event ev{};
    ev.kind = EventKind::HubError;
    ev.t_us = t_int_us_;
    const std::size_t n = std::min<std::size_t>(cargo.size() - 1U, kEventValues);
    for (std::size_t i = 0; i < n; ++i) {
      ev.v[i] = cargo[i + 1U];
    }
    ev.detail = (cargo.size() > 1U) ? cargo[1] : 0U;
    pushEvent(ev);
    return;
  }
  if (cargo[0] != reg::kShtpAdvertise) {
    return; // responses 2 to 255 are reserved
  }
  bool bad = false;
  adv_ = detail::DefaultAdvert();
  detail::ParseAdvertisement(cargo.subspan(1U), adv_, bad);
  if (bad) {
    ++stats_.bad_advert;
  }
  // our request on the wire was answered by a running hub; any other advertisement is a boot's,
  // and its reset-complete is still coming (a request that failed while the hub booted included)
  const bool answered = headWaits(detail::OpKind::Advertise);
  if (answered) {
    completeHead(Error::Ok, 0U);
  }
  if (state_ == State::Ready) {
    hubRestarted(0U);
  } else {
    state_ = State::WaitReady;
    since_us_ = now_us_;
    advert_seen_ = true;
    reset_seen_ = answered; // we asked a hub that was already running: nothing more is coming
    init_seen_ = answered;
    maybeReady();
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onExecutable(std::span<const uint8_t> cargo) noexcept {
  if (cargo[0] != reg::kExecResetComplete) {
    return;
  }
  if ((state_ == State::Ready) && !reset_heard_ && inStartupWindow()) {
    reset_heard_ = true; // the session's first: the boot's, after an answered advertisement
  } else if (state_ == State::Ready) {
    hubRestarted(1U); // a restart whose advertisement was not seen
  } else if (state_ == State::WaitReady) {
    reset_heard_ = true;
    reset_seen_ = true;
    maybeReady();
  } else {
    // before the advertisement: rejected by dispatch
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onControl(std::span<const uint8_t> cargo) noexcept {
  std::size_t pos = 0;
  while (pos < cargo.size()) {
    const std::size_t len = detail::ControlLen(cargo[pos]);
    if (len == 0U) {
      ++stats_.unknown_report;
      return;
    }
    if ((pos + len) > cargo.size()) {
      ++stats_.short_report;
      return;
    }
    onControlReport(cargo.subspan(pos, len));
    pos += len;
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onControlReport(std::span<const uint8_t> rep) noexcept {
  switch (rep[0]) {
  case reg::kRepGetFeatureRsp:
    onFeatureResponse(rep);
    break;
  case reg::kRepProductIdRsp:
    onProductId(rep);
    break;
  case reg::kRepFrsWriteRsp:
    onFrsWriteResponse(rep);
    break;
  case reg::kRepFrsReadRsp:
    onFrsReadResponse(rep);
    break;
  case reg::kRepCommandRsp:
    onCommandResponse(rep);
    break;
  case reg::kRepFlushDone:
    onFlushDone(rep);
    break;
  default:
    break; // timestamps on the control channel carry nothing for a control report
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onInput(std::span<const uint8_t> cargo, bool wake) noexcept {
  std::size_t pos = 0;
  int64_t ref_ticks = 0;
  while (pos < cargo.size()) {
    const uint8_t id = cargo[pos];
    std::size_t len = 0;
    if ((id == reg::kRepBaseTimestamp) || (id == reg::kRepTimestampRebase)) {
      len = reg::kLenTimestamp;
    } else if (id == reg::kRepFlushDone) {
      len = 2U;
    } else if (id < kSensorSlots) {
      len = adv_.report_len[id];
    } else {
      // not a sensor ID
    }
    if (len == 0U) {
      ++stats_.unknown_report; // V13: the next report's position is unknown
      return;
    }
    if ((pos + len) > cargo.size()) {
      ++stats_.short_report; // V14
      return;
    }
    const std::span<const uint8_t> rep = cargo.subspan(pos, len);
    pos += len;
    if (id == reg::kRepFlushDone) {
      onFlushDone(rep);
    } else if (id == reg::kRepBaseTimestamp) {
      const int32_t delta = detail::S32(detail::Le32(rep, 1U));
      if (delta == 0x7FFF'FFFF) {
        ++stats_.base_overflow; // RM S7.2.1: overflow; no base for this cargo
        ref_ticks = 0;
      } else {
        ref_ticks = -static_cast<int64_t>(delta);
      }
    } else if (id == reg::kRepTimestampRebase) {
      ref_ticks += static_cast<int64_t>(detail::S32(detail::Le32(rep, 1U)));
    } else {
      emitSensor(rep, wake, ref_ticks);
    }
  }
}

// every sensor slot has a layout entry, so an advertised length never indexes past the table
static_assert(detail::kSpecs.size() == kSensorSlots, "bno08x: one layout per sensor slot");

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::emitSensor(std::span<const uint8_t> rep, bool wake,
                                      int64_t ref_ticks) noexcept {
  SensorId sid{};
  const uint8_t id = rep[0];
  if (!ToSensorId(id, sid) || (detail::kSpecs[id].count == 0U)) {
    ++stats_.unknown_report;
    return;
  }
  if (rep.size() < reg::kLenSensorHeader) {
    ++stats_.short_report; // no room for the sequence, status and delay bytes
    return;
  }
  Event ev{};
  ev.kind = EventKind::Sensor;
  ev.sensor = sid;
  ev.wake = wake;
  ev.accuracy = static_cast<Accuracy>(rep[2] & 0x03U);
  ev.report_seq = rep[1];
  const int64_t delay = static_cast<int64_t>((static_cast<uint32_t>(rep[2] >> 2U) << 8U) | rep[3]);
  const int64_t micros = (ref_ticks + delay) * static_cast<int64_t>(reg::kDelayTickUs);
  ev.t_us = t_int_us_ + static_cast<uint32_t>(static_cast<uint64_t>(micros));
  if (!detail::DecodeFields(detail::kSpecs[id], rep, 0U, ev)) {
    ++stats_.short_report; // the advertised length is shorter than the layout
    return;
  }
  if (detail::kSpecs[id].lsb_only && ((ev.v[0] & 0xFF00) != 0)) {
    ++stats_.payload_upper_nonzero;
  }
  if (have_latest_[id] &&
      (rep[1] !=
       static_cast<uint8_t>((static_cast<uint32_t>(last_report_seq_[id]) + 1U) & 0xFFU))) {
    ++stats_.report_seq_gaps;
  }
  last_report_seq_[id] = rep[1];
  latest_[id] = ev;
  have_latest_[id] = true;
  pushEvent(ev);
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onGyroRv(std::span<const uint8_t> cargo) noexcept {
  if (cargo.size() != reg::kLenGyroRv) {
    ++stats_.bad_gyrorv; // V15
    return;
  }
  Event ev{};
  ev.kind = EventKind::Sensor;
  ev.sensor = SensorId::GyroIntegratedRotationVector;
  ev.wake = false;
  ev.t_us = t_int_us_;
  ev.accuracy = Accuracy::Unreliable;
  detail::SensorSpec spec = detail::Run(detail::FieldKind::S16, 4U, 2U, 7U);
  if (!detail::DecodeFields(spec, cargo, 4U, ev)) {
    ++stats_.short_report;
    return;
  }
  const std::size_t slot = static_cast<std::size_t>(detail::ToUnderlying(ev.sensor));
  latest_[slot] = ev;
  have_latest_[slot] = true;
  pushEvent(ev);
}

// --------------------------------------------------------------------------------------------
// Events
// --------------------------------------------------------------------------------------------

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::pushEvent(const Event& ev) noexcept {
  const std::size_t free_slots = Limits::kEventQueue - ev_count_;
  const std::size_t need = (pending_dropped_ > 0U) ? 2U : 1U;
  if (free_slots < need) {
    ++stats_.events_dropped;
    ++pending_dropped_;
    return;
  }
  if (pending_dropped_ > 0U) {
    Event marker{};
    marker.kind = EventKind::Dropped;
    marker.detail = pending_dropped_;
    marker.t_us = now_us_;
    events_[(ev_head_ + ev_count_) % Limits::kEventQueue] = marker;
    ++ev_count_;
    pending_dropped_ = 0;
  }
  events_[(ev_head_ + ev_count_) % Limits::kEventQueue] = ev;
  ++ev_count_;
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::PopEvent(Event& out) noexcept {
  if (ev_count_ == 0U) {
    return false;
  }
  out = events_[ev_head_];
  ev_head_ = (ev_head_ + 1U) % Limits::kEventQueue;
  --ev_count_;
  return true;
}

template <HubPort Port, LimitsLike Limits>
Result<Event> Bno08x<Port, Limits>::Latest(SensorId id) const noexcept {
  if (!detail::KnownSensor(id)) {
    return Result<Event>::Failure(Error::InvalidArgument);
  }
  const auto slot = static_cast<std::size_t>(detail::ToUnderlying(id));
  if (!have_latest_[slot]) {
    return Result<Event>::Failure(Error::NotAdvertised);
  }
  return Result<Event>::Success(latest_[slot]);
}

template <HubPort Port, LimitsLike Limits>
Result<FeatureConfig> Bno08x<Port, Limits>::Feature(SensorId id) const noexcept {
  if (!detail::KnownSensor(id)) {
    return Result<FeatureConfig>::Failure(Error::InvalidArgument);
  }
  return Result<FeatureConfig>::Success(
      features_[static_cast<std::size_t>(detail::ToUnderlying(id))]);
}

template <HubPort Port, LimitsLike Limits>
const Metadata* Bno08x<Port, Limits>::GetMetadata(SensorId id) const noexcept {
  if (!detail::KnownSensor(id)) {
    return nullptr;
  }
  const Metadata& m = metadata_[static_cast<std::size_t>(detail::ToUnderlying(id))];
  return m.valid ? &m : nullptr;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::emitOpDone(const detail::Op& op, Error err, uint32_t detail_word,
                                      const std::array<int32_t, kEventValues>* vals) noexcept {
  if (op.silent) {
    return;
  }
  Event ev{};
  ev.kind = EventKind::OpDone;
  ev.op = op.id;
  ev.error = err;
  ev.detail = detail_word;
  ev.t_us = now_us_;
  if (vals != nullptr) {
    ev.v = *vals;
  }
  pushEvent(ev);
}

// --------------------------------------------------------------------------------------------
// Operations
// --------------------------------------------------------------------------------------------

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::enqueue(const detail::Op& op) noexcept {
  if (op.stop) {
    const OpId same = queuedStop(op);
    if (same != 0U) {
      return Result<OpId>::Success(same); // the same stop waits already: it answers both calls
    }
  }
  // the last slot is kept for a stop request; a full queue gives way to one (RD-05)
  // the driver's own advertisement request may use it too: without it no stop can go out
  const bool reserved = op.stop || (op.kind == detail::OpKind::Advertise);
  const std::size_t room = reserved ? Limits::kOpQueue : (Limits::kOpQueue - 1U);
  bool has_room = op_count_ < room;
  if (!has_room && op.stop) {
    has_room = evictForStop();
  }
  if (!has_room) {
    return Result<OpId>::Failure(Error::Busy);
  }
  detail::Op& slot = ops_[(op_head_ + op_count_) % Limits::kOpQueue];
  slot = op;
  slot.id = next_op_;
  ++next_op_;
  if (next_op_ == 0U) {
    next_op_ = 1U;
  }
  ++op_count_;
  return Result<OpId>::Success(slot.id);
}

template <HubPort Port, LimitsLike Limits>
OpId Bno08x<Port, Limits>::queuedStop(const detail::Op& op) const noexcept {
  // only the newest request for the same target can answer again: an enable queued after a stop
  // must not have the last word
  OpId same{0};
  bool found = false;
  for (std::size_t k = op_count_; (k > 0U) && !found; --k) {
    const detail::Op& q = ops_[(op_head_ + k - 1U) % Limits::kOpQueue];
    if (detail::SameTarget(q, op)) {
      found = true;
      if (q.stop && !q.sent && (q.kind == op.kind) && (q.req_len == op.req_len) &&
          (q.req == op.req)) {
        same = q.id;
      }
    }
  }
  return same;
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::evictForStop() noexcept {
  bool done = false;
  for (std::size_t k = op_count_; (k > 0U) && !done; --k) {
    const detail::Op& q = ops_[(op_head_ + k - 1U) % Limits::kOpQueue];
    const bool internal = q.silent && (q.kind != detail::OpKind::SetFeature);
    if (!q.stop && !q.started && !q.sent && !internal) {
      removeAt(k - 1U, Error::Busy);
      done = true;
    }
  }
  return done;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::removeAt(std::size_t index, Error err) noexcept {
  const detail::Op victim = ops_[(op_head_ + index) % Limits::kOpQueue];
  for (std::size_t j = index; (j + 1U) < op_count_; ++j) {
    ops_[(op_head_ + j) % Limits::kOpQueue] = ops_[(op_head_ + j + 1U) % Limits::kOpQueue];
  }
  --op_count_;
  if ((victim.kind == detail::OpKind::FrsRead) || (victim.kind == detail::OpKind::FrsWrite)) {
    frs_dst_ = nullptr;
  }
  if (victim.silent && (victim.kind == detail::OpKind::SetFeature)) {
    restore_next_ = std::min(restore_next_, static_cast<std::size_t>(victim.sensor));
  }
  emitOpDone(victim, err, 0U, nullptr);
}

template <HubPort Port, LimitsLike Limits>
Result<void> Bno08x<Port, Limits>::checkStop() const noexcept {
  if (kTransport == Transport::UartRvc) {
    return Result<void>::Failure(Error::Unsupported);
  }
  if (state_ == State::Idle) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  return Result<void>::Success();
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::enqueueStopExec(uint8_t code) noexcept {
  const Result<void> allowed = checkStop();
  if (!allowed.ok()) {
    return Result<OpId>::Failure(allowed.error);
  }
  detail::Op op{};
  op.kind = detail::OpKind::Executable;
  op.channel = reg::kChanExecutable;
  op.no_response = true;
  op.stop = true;
  op.req[0] = code;
  op.req_len = 1U;
  return enqueue(op);
}

template <HubPort Port, LimitsLike Limits>
Result<void> Bno08x<Port, Limits>::checkReady(bool needs_shtp) const noexcept {
  if (needs_shtp && (kTransport == Transport::UartRvc)) {
    return Result<void>::Failure(Error::Unsupported);
  }
  if (state_ != State::Ready) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  return Result<void>::Success();
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::headWaits(detail::OpKind kind) const noexcept {
  return (op_count_ > 0U) && ops_[op_head_].sent && (ops_[op_head_].kind == kind);
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::frsBusy() const noexcept {
  for (std::size_t i = 0; i < op_count_; ++i) {
    const detail::OpKind k = ops_[(op_head_ + i) % Limits::kOpQueue].kind;
    if ((k == detail::OpKind::FrsRead) || (k == detail::OpKind::FrsWrite)) {
      return true;
    }
  }
  return false;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::startHead() noexcept {
  if ((op_count_ == 0U) || tx_pending_) {
    return;
  }
  detail::Op& op = ops_[op_head_];
  const bool may_send = (state_ == State::Ready) || (op.kind == detail::OpKind::Advertise) ||
                        ((state_ == State::Faulted) && op.stop);
  if (op.sent || !may_send) {
    return;
  }
  if (!op.started) { // the limit also covers a write the hub does not let through
    op.started = true;
    op.start_us = now_us_;
    op.limit_us = static_cast<uint32_t>(cfg_.op_timeout_ms) * 1000U;
    if (op.kind == detail::OpKind::Command) {
      op_v_.fill(0); // the results of this command, from its first response on
    }
  }
  const Result<void> r = queueTx(op.channel, std::span<const uint8_t>{op.req}.first(op.req_len));
  if (!r.ok()) {
    completeHead(r.error, 0U);
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::afterSent() noexcept {
  detail::Op& op = ops_[op_head_];
  op.sent = true;
  const uint32_t ms =
      (op.kind == detail::OpKind::FrsWrite) ? cfg_.frs_write_timeout_ms : cfg_.op_timeout_ms;
  op.start_us = now_us_;
  op.limit_us = static_cast<uint32_t>(ms) * 1000U;
  if (op.kind == detail::OpKind::SetFeature) {
    features_[op.sensor] = detail::DecodeFeature(op.req);
  }
  if ((op.kind == detail::OpKind::Executable) && (op.req[0] == reg::kExecReset)) {
    tx_seq_.fill(0U); // the hub restarts its sequence numbers (SHTP S2.2.1), also in Faulted
  }
  if (op.no_response) {
    completeHead(Error::Ok, 0U);
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::completeHead(Error err, uint32_t detail_word) noexcept {
  completeHeadWith(err, detail_word, nullptr);
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::completeHeadWith(
    Error err, uint32_t detail_word, const std::array<int32_t, kEventValues>* vals) noexcept {
  if (op_count_ == 0U) {
    return;
  }
  const detail::Op op = ops_[op_head_];
  op_head_ = (op_head_ + 1U) % Limits::kOpQueue;
  --op_count_;
  afterComplete(op, err);
  if ((op.kind == detail::OpKind::ProductId) && restart_event_pending_) {
    restart_event_pending_ = false;
    Event ev{};
    ev.kind = EventKind::Reset;
    ev.t_us = now_us_;
    ev.detail = (err == Error::Ok) ? product_.reset_cause : kHubUnknownCause;
    pushEvent(ev);
  }
  emitOpDone(op, err, detail_word, vals);
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::afterComplete(const detail::Op& op, Error err) noexcept {
  const bool feature = op.kind == detail::OpKind::SetFeature;
  // what auto-restore re-applies (KB-CPP-120); a newer request for the sensor has the last word
  if (feature && !op.silent && (err == Error::Ok) && !userFeatureQueued(op.sensor)) {
    desired_[op.sensor] = detail::DecodeFeature(op.req);
  }
  const bool session_ended =
      (err == Error::HubReset) || (err == Error::NotInitialized) || (err == Error::Framing);
  if (feature && op.silent && (err != Error::Ok) && !session_ended) {
    restoreFailed(op.sensor); // a restore write that failed is tried again; a restart restores all
  }
  if ((op.kind == detail::OpKind::FrsRead) || (op.kind == detail::OpKind::FrsWrite)) {
    frs_dst_ = nullptr; // the caller's buffer is not touched after the operation ended
  }
  if ((op.kind == detail::OpKind::Advertise) && (err != Error::Ok)) {
    advert_asked_ = false; // asked again advert_retry_ms after the last request
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::failAll(Error err, bool keep_stops) noexcept {
  tx_pending_ = false;
  const std::size_t n = op_count_;
  for (std::size_t i = 0; i < n; ++i) {
    if (keep_stops && ops_[op_head_].stop && !ops_[op_head_].sent) {
      detail::Op op = ops_[op_head_]; // moved to the tail, in order
      op.started = false;
      op.no_response = true; // nothing is read in Faulted: a stop completes when written
      op_head_ = (op_head_ + 1U) % Limits::kOpQueue;
      ops_[(op_head_ + op_count_ - 1U) % Limits::kOpQueue] = op;
    } else {
      completeHead(err, 0U);
    }
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::checkOpTimeout() noexcept {
  if ((op_count_ == 0U) || !(ops_[op_head_].sent || ops_[op_head_].started)) {
    return;
  }
  // elapsed time, not a signed difference: a Poll() gap of 2^31 us still ends the operation
  if ((now_us_ - ops_[op_head_].start_us) >= ops_[op_head_].limit_us) {
    if (!ops_[op_head_].sent) { // it never went out: forget the write that was waiting for its turn
      tx_pending_ = false;
      releaseWake();
      abortUartTx();
    }
    completeHead(Error::Timeout, 0U);
  }
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::enqueueExec(uint8_t code, bool silent) noexcept {
  detail::Op op{};
  op.kind = detail::OpKind::Executable;
  op.silent = silent;
  op.channel = reg::kChanExecutable;
  op.no_response = true;
  op.req[0] = code;
  op.req_len = 1U;
  return enqueue(op);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::enqueueCommand(uint8_t cmd, const std::array<uint8_t, 9>& p,
                                                  bool no_response) noexcept {
  const Result<void> ready = checkReady(true);
  if (!ready.ok()) {
    return Result<OpId>::Failure(ready.error);
  }
  detail::Op op{};
  op.kind = detail::OpKind::Command;
  op.no_response = no_response;
  op.cmd = cmd;
  ++cmd_seq_;
  op.cmd_seq = cmd_seq_;
  op.req[0] = reg::kRepCommandReq;
  op.req[1] = op.cmd_seq;
  op.req[2] = cmd;
  for (std::size_t i = 0; i < p.size(); ++i) {
    op.req[3U + i] = p[i];
  }
  op.req_len = static_cast<uint8_t>(reg::kLenCommand);
  return enqueue(op);
}

// --------------------------------------------------------------------------------------------
// Transmit
// --------------------------------------------------------------------------------------------

template <HubPort Port, LimitsLike Limits>
Result<void> Bno08x<Port, Limits>::queueTx(uint8_t channel,
                                           std::span<const uint8_t> payload) noexcept {
  const std::size_t total = reg::kShtpHeaderLen + payload.size();
  if (total > maxWrite()) {
    return Result<void>::Failure(
        Error::Oversize); // the hub does not take fragmented writes (DS p22)
  }
  if (tx_pending_) {
    return Result<void>::Failure(Error::Busy);
  }
  const uint8_t wire_channel = adv_.channel[channel];
  detail::EncodeHeader(tx_buf_, static_cast<uint32_t>(total), wire_channel, tx_seq_[channel]);
  tx_seq_[channel] = static_cast<uint8_t>((static_cast<uint32_t>(tx_seq_[channel]) + 1U) & 0xFFU);
  std::copy(payload.begin(), payload.end(),
            tx_buf_.begin() + static_cast<std::ptrdiff_t>(reg::kShtpHeaderLen));
  tx_len_ = total;
  tx_pending_ = true;
  return Result<void>::Success();
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::pumpTx() noexcept {
  startHead();
  if (!tx_pending_) {
    if constexpr (kTransport == Transport::UartShtp) {
      uartPump();
    }
    return;
  }
  Error e = Error::Unsupported;
  if constexpr (kTransport == Transport::I2c) {
    e = txBus();
  } else if constexpr (kTransport == Transport::Spi) {
    e = txSpi();
  } else if constexpr (kTransport == Transport::UartShtp) {
    e = txUart();
  } else {
    // UART-RVC never transmits SHTP
  }
  if (e == Error::Busy) {
    return; // not finished yet; try again next Poll
  }
  tx_pending_ = false;
  if (e == Error::Ok) {
    afterSent();
  } else {
    txFailed(e);
  }
  if constexpr (kTransport == Transport::Spi) {
    // after afterSent(): a cargo that restarts the session finds the queue as it is, the write
    // already belonging to its operation
    if (duplex_len_ > 0U) {
      const std::size_t n = duplex_len_;
      duplex_len_ = 0U;
      onTransfer(std::span<const uint8_t>{rd_buf_}.first(n));
    }
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::txFailed(Error err) noexcept {
  if (op_count_ > 0U) {
    completeHead(err, 0U);
  }
}

template <HubPort Port, LimitsLike Limits>
Error Bno08x<Port, Limits>::txBus() noexcept {
  if constexpr (kTransport == Transport::I2c) {
    return port_.I2cWrite(cfg_.i2c_address, tx_buf_.data(), tx_len_) ? Error::Ok : Error::Bus;
  } else {
    return Error::Unsupported;
  }
}

template <HubPort Port, LimitsLike Limits>
Error Bno08x<Port, Limits>::txSpi() noexcept {
  if constexpr (kTransport == Transport::Spi) {
    const bool had_int = port_.IntAsserted();
    if (!had_int) { // SHTP S3.1: WAKE low, then the hub asserts H_INTN when it can do a duplex
                    // transfer
      if (!wake_active_) {
        if (!port_.SetWake(false)) {
          return Error::Bus;
        }
        wake_active_ = true;
        wake_start_us_ = now_us_;
        return Error::Busy;
      }
      if (!port_.IntAsserted()) {
        if ((now_us_ - wake_start_us_) >= cfg_.wake_timeout_us) {
          wake_active_ = false;
          static_cast<void>(port_.SetWake(true));
          return Error::Timeout;
        }
        return Error::Busy;
      }
    }
    if (!rx_.Open()) {
      t_int_us_ = takeIntTime(); // the first H_INTN of a cargo times it (SHTP S2.6)
    } else {
      static_cast<void>(takeIntTime()); // a continuation's edge is consumed, not used
    }
    const bool ok = port_.SpiTransfer(tx_buf_.data(), rd_buf_.data(), tx_len_);
    if (wake_active_) {
      wake_active_ = false;
      static_cast<void>(port_.SetWake(true)); // WAKE is released after the transfer
    }
    if (!ok) {
      return Error::Bus;
    }
    duplex_len_ = tx_len_; // what the hub sent meanwhile: used once the write is accounted for
    return Error::Ok;
  } else {
    return Error::Unsupported;
  }
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::uartTokenOk(std::size_t total_len) noexcept {
  if (!token_valid_) {
    return false;
  }
  const uint32_t timeout_ms =
      (adv_.uart_timeout_ms != 0U) ? adv_.uart_timeout_ms : cfg_.uart_token_timeout_ms;
  const uint64_t limit_us = static_cast<uint64_t>(timeout_ms) * 1000U; // no wrap for any advert
  if (static_cast<uint64_t>(now_us_ - token_us_) > limit_us) {
    token_valid_ = false; // a BSN is valid until the timeout (SHTP S4.5)
    return false;
  }
  return token_bytes_ >= total_len;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::uartSendBsq() noexcept {
  if (uart_tx_pos_ < uart_tx_len_) {
    return;
  }
  if (bsq_out_) {
    if ((now_us_ - bsq_us_) < (static_cast<uint32_t>(cfg_.uart_bsq_retry_ms) * 1000U)) {
      return;
    }
    ++stats_.bsq_retries; // the query or its notification was lost: ask again (SHTP S4.3)
  }
  uart_tx_len_ =
      detail::EncodeUartFrame(uart_tx_, detail::kUartProtoControl, std::span<const uint8_t>{});
  uart_tx_pos_ = 0;
  uart_tx_cargo_ = false;
  bsq_out_ = true;
  bsq_us_ = now_us_;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::uartPump() noexcept {
  if constexpr (kTransport == Transport::UartShtp) {
    if (uart_tx_pos_ >= uart_tx_len_) {
      return;
    }
    if constexpr (detail::HasUartWrite<Port> &&
                  (detail::PacesUartTx<Port> || !detail::HasUartWriteByte<Port>)) {
      if (port_.UartWrite(uart_tx_.data() + uart_tx_pos_, uart_tx_len_ - uart_tx_pos_)) {
        uart_tx_pos_ = uart_tx_len_;
      }
    } else if constexpr (detail::HasUartWriteByte<Port>) {
      while (uart_tx_pos_ < uart_tx_len_) {
        const uint32_t now = port_.NowUs();
        if ((uart_last_byte_us_ != 0U) && ((now - uart_last_byte_us_) < reg::kUartHostByteGapUs)) {
          break; // at least 100 us between bytes from the host (SHTP S4.5)
        }
        if (!port_.UartWriteByte(uart_tx_[uart_tx_pos_])) {
          break;
        }
        uart_last_byte_us_ = (now != 0U) ? now : 1U;
        ++uart_tx_pos_;
      }
    } else {
      static_assert(detail::kAlwaysFalse<Port>, "bno08x: a UART-SHTP port needs a transmit hook");
    }
    if (uart_tx_pos_ >= uart_tx_len_) {
      uart_frame_open_ = false; // its last byte is a flag: the hub's frame is closed
    } else if (uart_tx_pos_ > 0U) {
      uart_frame_open_ = true;
    } else {
      // nothing went out: a pending closing flag keeps the frame open
    }
  }
}

template <HubPort Port, LimitsLike Limits>
Error Bno08x<Port, Limits>::txUart() noexcept {
  if constexpr (kTransport == Transport::UartShtp) {
    if (!uart_tx_cargo_) {
      if (uart_tx_pos_ < uart_tx_len_) { // a BSQ is still going out
        uartPump();
        return Error::Busy;
      }
      if (!uartTokenOk(tx_len_)) {
        uartSendBsq();
        uartPump();
        return Error::Busy;
      }
      uart_tx_len_ = detail::EncodeUartFrame(uart_tx_, detail::kUartProtoShtp,
                                             std::span<const uint8_t>{tx_buf_}.first(tx_len_));
      uart_tx_pos_ = 0;
      if (uart_tx_len_ == 0U) {
        return Error::Oversize;
      }
      uart_tx_cargo_ = true;
      token_valid_ = false; // the token ends with the next host write
    }
    uartPump();
    if (uart_tx_pos_ >= uart_tx_len_) {
      uart_tx_cargo_ = false;
      return Error::Ok;
    }
    return Error::Busy;
  } else {
    return Error::Unsupported;
  }
}

template <HubPort Port, LimitsLike Limits>
Result<void> Bno08x<Port, Limits>::RvcSend(std::span<const uint8_t> text) noexcept {
  if constexpr (kTransport == Transport::UartRvc) {
    if (!cfg_.rvc_inputs_experimental) {
      return Result<void>::Failure(Error::Unsupported); // A8: the $MIB inputs are not verified
    }
    if constexpr (detail::HasUartWriteByte<Port>) {
      for (const uint8_t b : text) {
        if (!port_.UartWriteByte(b)) {
          return Result<void>::Failure(Error::Bus);
        }
      }
      return Result<void>::Success();
    } else {
      return Result<void>::Failure(Error::Unsupported);
    }
  } else {
    static_cast<void>(text);
    return Result<void>::Failure(Error::Unsupported);
  }
}

// --------------------------------------------------------------------------------------------
// Responses
// --------------------------------------------------------------------------------------------

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onFeatureResponse(std::span<const uint8_t> rep) noexcept {
  SensorId sid{};
  if (!ToSensorId(rep[1], sid)) {
    return;
  }
  const FeatureConfig fc = detail::DecodeFeature(rep);
  features_[rep[1]] = fc;
  const bool mine =
      (op_count_ > 0U) && ops_[op_head_].sent && (ops_[op_head_].sensor == rep[1]) &&
      ((ops_[op_head_].kind == detail::OpKind::GetFeature) ||
       (cfg_.set_feature_confirm && (ops_[op_head_].kind == detail::OpKind::SetFeature)));
  std::array<int32_t, kEventValues> v{};
  v[0] = static_cast<int32_t>(rep[2]);
  v[1] = fc.change_sensitivity;
  v[2] = static_cast<int32_t>(fc.interval_us);
  v[3] = static_cast<int32_t>(fc.batch_us);
  v[4] = static_cast<int32_t>(fc.sensor_config);
  if (mine) {
    completeHeadWith(Error::Ok, 0U, &v);
    return;
  }
  Event ev{};
  ev.kind = EventKind::FeatureChanged; // unsolicited: a rate changed (RM S6.5.5)
  ev.sensor = sid;
  ev.t_us = t_int_us_;
  ev.v = v;
  pushEvent(ev);
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onProductId(std::span<const uint8_t> rep) noexcept {
  if (!headWaits(detail::OpKind::ProductId)) {
    ++stats_.product_id_extra; // a later entry of an answered request, or unsolicited
    return;
  }
  ProductId p{};
  p.reset_cause = rep[1];
  p.version_major = rep[2];
  p.version_minor = rep[3];
  p.part_number = detail::Le32(rep, 4U);
  p.build = detail::Le32(rep, 8U);
  p.version_patch = detail::Le16(rep, 12U);
  p.valid = true;
  detail::Op& op = ops_[op_head_];
  const std::size_t entry = op.responses;
  if (entry == 0U) {
    product_ = p; // `Product()` is entry 0 of the request
  }
  if (entry < product_entries_.size()) {
    product_entries_[entry] = p;
    product_count_ = entry + 1U;
  }
  ++op.responses;
  if (op.responses >= cfg_.product_id_entries) {
    completeHead(Error::Ok, 0U);
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onFlushDone(std::span<const uint8_t> rep) noexcept {
  SensorId sid{};
  if (!ToSensorId(rep[1], sid)) {
    return;
  }
  const bool mine = headWaits(detail::OpKind::Flush) && (ops_[op_head_].sensor == rep[1]);
  Event ev{};
  ev.kind = EventKind::FlushDone;
  ev.sensor = sid;
  ev.t_us = t_int_us_;
  pushEvent(ev);
  if (mine) {
    completeHead(Error::Ok, 0U);
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onSh2Initialize(std::span<const uint8_t> rep) noexcept {
  static_cast<void>(rep);
  if ((state_ == State::Ready) && !init_heard_ && inStartupWindow()) {
    // the session's first one, after `Ready` without it (require_init_response false, or an
    // answered advertisement): the boot's
    init_heard_ = true;
  } else if (state_ == State::Ready) {
    hubRestarted(2U); // a restart whose advertisement and reset-complete were not seen
  } else if (state_ == State::WaitReady) {
    init_heard_ = true;
    init_seen_ = true;
    maybeReady();
  } else {
    // before the advertisement: rejected by dispatch
  }
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::commandIsFinal(const detail::Op& op,
                                          std::span<const uint8_t> rep) noexcept {
  if (op.cmd == reg::kCmdErrors) {
    return rep[7] == 255U; // error source 255: no more errors (RM S6.4.1)
  }
  if (op.cmd == reg::kCmdCounters) {
    return rep[4] == 1U; // the second response of the pair
  }
  return true;
}

template <HubPort Port, LimitsLike Limits>
Error Bno08x<Port, Limits>::commandError(const detail::Op& op, std::span<const uint8_t> rep,
                                         uint32_t& detail_word) noexcept {
  Error e = Error::Ok;
  switch (op.cmd) {
  case reg::kCmdInitialize:
  case reg::kCmdSaveDcd:
  case reg::kCmdMeCal:
    e = (rep[5] == 0U) ? Error::Ok : Error::DeviceFault;
    break;
  case reg::kCmdSimpleCal:
    if (rep[5] == 1U) { // the finish response: R1 is the status (RM S6.4.10.2)
      detail_word = rep[6];
      e = (rep[6] == 0U) ? Error::Ok : Error::Refused;
    }
    break;
  default:
    break;
  }
  return e;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::fillCommandResult(const detail::Op& op,
                                             std::span<const uint8_t> rep) noexcept {
  const std::size_t generic =
      (op.cmd == reg::kCmdCounters) ? 2U : 11U; // the counts of a pair are kept
  for (std::size_t i = 0; i < generic; ++i) {
    op_v_[i] = static_cast<int32_t>(rep[5U + i]);
  }
  if (op.cmd == reg::kCmdCounters) {
    const auto first = static_cast<int32_t>(detail::Le32(rep, 8U));
    const auto second = static_cast<int32_t>(detail::Le32(rep, 12U));
    const std::size_t at = (rep[4] == 0U) ? 2U : 4U;
    op_v_[at] = first;
    op_v_[at + 1U] = second;
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onCommandResponse(std::span<const uint8_t> rep) noexcept {
  const bool autonomous = (rep[2] & reg::kCmdUnsolicited) != 0U;
  const uint8_t id = static_cast<uint8_t>(rep[2] & 0x7FU);
  if (autonomous) {
    if (id == reg::kCmdInitialize) {
      onSh2Initialize(rep);
    }
    return;
  }
  if ((op_count_ == 0U) || !ops_[op_head_].sent) {
    return;
  }
  const detail::Op& op = ops_[op_head_];
  if ((op.kind != detail::OpKind::Command) || (op.cmd != id) || (op.cmd_seq != rep[3])) {
    return; // a late or foreign response
  }
  fillCommandResult(op, rep);
  if (id == reg::kCmdErrors) {
    if (rep[7] != 255U) {
      Event ev{};
      ev.kind = EventKind::Sh2Error;
      ev.t_us = now_us_;
      for (std::size_t i = 0; i < 6U; ++i) {
        ev.v[i] = static_cast<int32_t>(rep[5U + i]);
      }
      pushEvent(ev);
    }
  }
  if (commandIsFinal(op, rep)) {
    uint32_t extra = 0;
    const Error e = commandError(op, rep, extra);
    completeHeadWith(e, extra, &op_v_);
  }
}

// --------------------------------------------------------------------------------------------
// FRS
// --------------------------------------------------------------------------------------------

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::frsStartVerify() noexcept {
  detail::Op& op = ops_[op_head_];
  op.phase = 3U;
  op.sent = false;
  op.no_response = false;
  scratch_.length = 0;
  frs_dst_ = &scratch_;
  op.req.fill(0U);
  op.req[0] = reg::kRepFrsReadReq;
  detail::PutLe16(op.req, 4U, op.frs_type);
  op.req_len = static_cast<uint8_t>(reg::kLenFrsReadReq);
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::frsWriteStep() noexcept {
  detail::Op& op = ops_[op_head_];
  const std::size_t n = std::min<std::size_t>(2U, frs_wr_len_ - frs_wr_pos_);
  op.req.fill(0U);
  op.req[0] = reg::kRepFrsWriteData;
  detail::PutLe16(op.req, 2U, static_cast<uint32_t>(frs_wr_pos_));
  detail::PutLe32(op.req, 4U, frs_wr_[frs_wr_pos_]);
  if (n == 2U) {
    detail::PutLe32(op.req, 8U, frs_wr_[frs_wr_pos_ + 1U]);
  }
  frs_wr_pos_ += n;
  op.req_len = static_cast<uint8_t>(reg::kLenFrsWriteData);
  op.sent = false;
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onFrsWriteResponse(std::span<const uint8_t> rep) noexcept {
  if ((op_count_ == 0U) || !ops_[op_head_].sent ||
      (ops_[op_head_].kind != detail::OpKind::FrsWrite) || (ops_[op_head_].phase == 3U)) {
    return;
  }
  const uint8_t status = rep[1];
  if (status == reg::kFrsWCompleted) { // success = 3 (ambiguity A27)
    if (cfg_.frs_verify && (frs_wr_len_ > 0U)) {
      frsStartVerify();
    } else {
      completeHead(Error::Ok, status);
    }
  } else if (status == reg::kFrsWReady) {
    if (frs_wr_len_ > 0U) {
      ops_[op_head_].phase = 1U;
      frsWriteStep();
    }
  } else if (status == reg::kFrsWWordsReceived) {
    if (frs_wr_pos_ < frs_wr_len_) {
      frsWriteStep();
    }
  } else if (status == reg::kFrsWRecordValid) {
    // intermediate: the record passed validation, completion follows
  } else {
    completeHead(Error::Refused, status); // 1, 2, 5, 6, 7, 9, 10, 11, 12 and anything above
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::frsReadFinished(Error err, uint32_t detail_word) noexcept {
  detail::Op& op = ops_[op_head_];
  if (op.kind == detail::OpKind::FrsWrite) { // the read-back of a write
    bool same = (err == Error::Ok) && (scratch_.length == frs_wr_len_);
    for (std::size_t i = 0; same && (i < frs_wr_len_); ++i) {
      same = scratch_.words[i] == frs_wr_[i];
    }
    completeHead(same ? Error::Ok : ((err == Error::Ok) ? Error::Refused : err),
                 same ? 0U : (detail_word | 0x100U));
    return;
  }
  if ((err == Error::Ok) && (op.phase == 1U)) {
    parseMetadata(op.frs_type, op.sensor);
  }
  completeHead(err, detail_word);
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::onFrsReadResponse(std::span<const uint8_t> rep) noexcept {
  const bool verifying = (op_count_ > 0U) && (ops_[op_head_].kind == detail::OpKind::FrsWrite) &&
                         (ops_[op_head_].phase == 3U);
  const bool reading = (op_count_ > 0U) && (ops_[op_head_].kind == detail::OpKind::FrsRead);
  if (!(verifying || reading) || !ops_[op_head_].sent || (frs_dst_ == nullptr)) {
    return;
  }
  if (detail::Le16(rep, 12U) != ops_[op_head_].frs_type) {
    ++stats_.frs_foreign; // a late response of another record (RM Figure 39, bytes 12 and 13)
    return;
  }
  const uint8_t status = rep[1] & 0x0FU;
  const std::size_t count = std::min<std::size_t>(rep[1] >> 4U, 2U);
  const std::size_t offset = detail::Le16(rep, 2U);
  frs_dst_->last_status = status;
  if ((status == reg::kFrsRNoError) || (status == reg::kFrsRCompleted)) {
    if ((offset + count) > kFrsMaxWords) {
      frsReadFinished(Error::Overflow, status);
      return;
    }
    for (std::size_t i = 0; i < count; ++i) {
      frs_dst_->words[offset + i] = detail::Le32(rep, 4U + (4U * i));
    }
    frs_dst_->length = std::max(frs_dst_->length, offset + count);
    if (status == reg::kFrsRCompleted) {
      frsReadFinished(Error::Ok, status);
    }
  } else if (status == reg::kFrsREmpty) {
    frs_dst_->length = 0;
    frsReadFinished(Error::Ok, status);
  } else {
    frsReadFinished(detail::FrsReadStatusError(status), status);
  }
}

template <HubPort Port, LimitsLike Limits>
void Bno08x<Port, Limits>::parseMetadata(uint16_t record, uint8_t sensor) noexcept {
  const FrsBuffer& b = scratch_;
  Metadata m{};
  if ((b.length < 9U) || (sensor >= kSensorSlots)) {
    metadata_[sensor % kSensorSlots] = m;
    return;
  }
  const auto& w = b.words;
  m.version_me = static_cast<uint8_t>(w[0] & 0xFFU);
  m.version_mh = static_cast<uint8_t>((w[0] >> 8U) & 0xFFU);
  m.version_sh = static_cast<uint8_t>((w[0] >> 16U) & 0xFFU);
  m.range = w[1];
  m.resolution = w[2];
  m.power_q10 = static_cast<uint16_t>(w[3] & 0xFFFFU);
  m.revision = static_cast<uint16_t>(w[3] >> 16U);
  m.min_period_us = w[4];
  m.fifo_max = static_cast<uint16_t>(w[5] & 0xFFFFU);
  m.fifo_reserved = static_cast<uint16_t>(w[5] >> 16U);
  m.batch_bytes = static_cast<uint16_t>(w[6] & 0xFFFFU);
  m.vendor_id_len = static_cast<uint16_t>(w[6] >> 16U);
  m.q1 = static_cast<uint16_t>(w[7] & 0xFFFFU);
  m.q2 = static_cast<uint16_t>(w[7] >> 16U);
  m.specific_len = static_cast<uint16_t>(w[8] & 0xFFFFU);
  m.q3 = static_cast<uint16_t>(w[8] >> 16U);
  std::size_t at = 9;
  if (m.revision >= 4U) {
    m.max_period_us = (b.length > 9U) ? w[9] : 0U;
    at = 10;
  }
  const std::size_t specific_words = m.specific_len / 4U;
  if ((record == (reg::kFrsMetadataFirst + 0x1BU)) && (specific_words >= 1U) &&
      ((at + 1U) <= b.length)) {
    m.activity_classes = w[at]; // 0xE31C: bit n set means activity class n is supported (RM S5.1.1)
  }
  at += specific_words;
  const std::size_t chars = std::min<std::size_t>(m.vendor_id_len, m.vendor_id.size() - 1U);
  for (std::size_t i = 0; i < chars; ++i) {
    const std::size_t word = at + (i / 4U);
    if (word >= b.length) {
      break;
    }
    m.vendor_id[i] =
        std::bit_cast<char>(static_cast<uint8_t>((w[word] >> (8U * (i % 4U))) & 0xFFU));
  }
  m.valid = true;
  metadata_[sensor] = m;
}

// --------------------------------------------------------------------------------------------
// Public operations
// --------------------------------------------------------------------------------------------

namespace detail {

/// A two-byte control request (Get Feature, Product ID, Force Flush) for `sensor`.
[[nodiscard]] inline Op MakeControl(OpKind kind, uint8_t report, uint8_t sensor) noexcept {
  Op op{};
  op.kind = kind;
  op.sensor = sensor;
  op.req[0] = report;
  op.req[1] = sensor;
  op.req_len = 2U;
  return op;
}

/// The sensor a raw sensor's samples are made from (RM S6.5.4: the raw sensors need their base
/// running).
[[nodiscard]] constexpr SensorId RawBase(SensorId id) noexcept {
  SensorId base = SensorId::Accelerometer;
  if (id == SensorId::RawGyroscope) {
    base = SensorId::GyroscopeCalibrated;
  } else if (id == SensorId::RawMagnetometer) {
    base = SensorId::MagneticFieldCalibrated;
  } else {
    // the raw accelerometer
  }
  return base;
}

} // namespace detail

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::Reset() noexcept {
  return enqueueStopExec(reg::kExecReset);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::HubOn() noexcept {
  const Result<void> ready = checkReady(true);
  if (!ready.ok()) {
    return Result<OpId>::Failure(ready.error);
  }
  return enqueueExec(reg::kExecOn);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::HubSleep() noexcept {
  return enqueueStopExec(reg::kExecSleep);
}

template <HubPort Port, LimitsLike Limits>
Error Bno08x<Port, Limits>::checkFeature(SensorId id, const FeatureConfig& fc,
                                         bool off) const noexcept {
  if (!detail::KnownSensor(id)) {
    return Error::InvalidArgument; // nothing is sent for an ID the driver does not know
  }
  if (kTransport == Transport::UartRvc) {
    return Error::Unsupported;
  }
  // Switching a sensor off is never refused for the session state or for options an off request
  // does not have: once `Begin()` ran it always acts (the restore list forgets the sensor at once);
  // in Faulted it is written without waiting for a reply.
  if (!off || (state_ == State::Idle)) {
    const Result<void> ready = checkReady(true);
    if (!ready.ok()) {
      return ready.error;
    }
  }
  return checkFeatureOptions(id, fc, off);
}

template <HubPort Port, LimitsLike Limits>
Error Bno08x<Port, Limits>::checkFeatureOptions(SensorId id, const FeatureConfig& fc,
                                                bool off) const noexcept {
  Error err = Error::Ok;
  if (id == SensorId::WheelEncoder) {
    err = Error::Unsupported; // no layout is documented (ambiguity A30)
  } else if (!off && cfg_.strict_variant && (id == SensorId::MotionRequest) &&
             (cfg_.variant != Variant::Bno086)) {
    err = Error::Variant; // the interactive calibration is a BNO086 feature (A3, A4)
  } else if (!off && detail::NoSensitivity(id) &&
             ((fc.change_sensitivity != 0) || fc.change_enable)) {
    err = Error::InvalidArgument; // the sensor has no change sensitivity
  } else if (cfg_.strict_raw && IsRawSensor(id) && !off &&
             (wantedInterval(static_cast<std::size_t>(detail::ToUnderlying(detail::RawBase(id)))) ==
              0U)) {
    err = Error::InvalidArgument; // the raw sensor needs its base sensor on
  } else {
    // every check passed
  }
  return err;
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::SetFeature(SensorId id, const FeatureConfig& fc) noexcept {
  const bool off = (fc.interval_us == 0U);
  const Error invalid = checkFeature(id, fc, off);
  if (invalid != Error::Ok) {
    return Result<OpId>::Failure(invalid);
  }
  FeatureConfig send = fc;
  if (off) {
    send = FeatureConfig{}; // an off request carries no other option
  }
  detail::Op op{};
  op.kind = detail::OpKind::SetFeature;
  op.stop = off;
  op.no_response = !cfg_.set_feature_confirm || (off && (state_ == State::Faulted));
  op.sensor = static_cast<uint8_t>(detail::ToUnderlying(id));
  detail::EncodeFeature(op.req, op.sensor, send);
  op.req_len = static_cast<uint8_t>(reg::kLenGetFeature);
  const Result<OpId> queued = enqueue(op);
  if (queued.ok() && off) {
    desired_[op.sensor] = FeatureConfig{}; // auto-restore must not re-enable it
  }
  return queued; // an enable is remembered for auto-restore only once the hub accepted it
}

template <HubPort Port, LimitsLike Limits>
bool Bno08x<Port, Limits>::userFeatureQueued(std::size_t sensor) const noexcept {
  bool queued = false;
  for (std::size_t i = 0; (i < op_count_) && !queued; ++i) {
    const detail::Op& q = ops_[(op_head_ + i) % Limits::kOpQueue];
    queued = (q.kind == detail::OpKind::SetFeature) && !q.silent && (q.sensor == sensor);
  }
  return queued;
}

template <HubPort Port, LimitsLike Limits>
uint32_t Bno08x<Port, Limits>::wantedInterval(std::size_t sensor) const noexcept {
  uint32_t interval = desired_[sensor].interval_us;
  for (std::size_t i = 0; i < op_count_; ++i) { // a queued request that is not answered yet counts
    const detail::Op& q = ops_[(op_head_ + i) % Limits::kOpQueue];
    if ((q.kind == detail::OpKind::SetFeature) && !q.silent && (q.sensor == sensor)) {
      interval = detail::DecodeFeature(q.req).interval_us;
    }
  }
  return interval;
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::GetFeature(SensorId id) noexcept {
  if (!detail::KnownSensor(id)) {
    return Result<OpId>::Failure(
        Error::InvalidArgument); // nothing is sent for an ID the driver does not know
  }
  const Result<void> ready = checkReady(true);
  if (!ready.ok()) {
    return Result<OpId>::Failure(ready.error);
  }
  return enqueue(detail::MakeControl(detail::OpKind::GetFeature, reg::kRepGetFeatureReq,
                                     static_cast<uint8_t>(detail::ToUnderlying(id))));
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::Flush(SensorId id) noexcept {
  if (!detail::KnownSensor(id)) {
    return Result<OpId>::Failure(
        Error::InvalidArgument); // nothing is sent for an ID the driver does not know
  }
  const Result<void> ready = checkReady(true);
  if (!ready.ok()) {
    return Result<OpId>::Failure(ready.error);
  }
  return enqueue(detail::MakeControl(detail::OpKind::Flush, reg::kRepForceFlush,
                                     static_cast<uint8_t>(detail::ToUnderlying(id))));
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::ReadProductId() noexcept {
  const Result<void> ready = checkReady(true);
  if (!ready.ok()) {
    return Result<OpId>::Failure(ready.error);
  }
  return enqueue(detail::MakeControl(detail::OpKind::ProductId, reg::kRepProductIdReq, 0U));
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::ReadMetadata(SensorId id) noexcept {
  if (!detail::KnownSensor(id)) {
    return Result<OpId>::Failure(
        Error::InvalidArgument); // nothing is sent for an ID the driver does not know
  }
  const Result<void> ready = checkReady(true);
  if (!ready.ok()) {
    return Result<OpId>::Failure(ready.error);
  }
  const uint16_t record = MetadataRecord(id);
  if (record == 0U) {
    return Result<OpId>::Failure(Error::Unsupported); // 0x2C and 0x2D have no metadata record
  }
  if (frsBusy()) {
    return Result<OpId>::Failure(Error::Busy);
  }
  detail::Op op{};
  op.kind = detail::OpKind::FrsRead;
  op.phase = 1U; // metadata: parsed when complete
  op.sensor = static_cast<uint8_t>(detail::ToUnderlying(id));
  op.frs_type = record;
  op.req[0] = reg::kRepFrsReadReq;
  detail::PutLe16(op.req, 4U, record);
  op.req_len = static_cast<uint8_t>(reg::kLenFrsReadReq);
  const Result<OpId> queued = enqueue(op);
  if (queued.ok()) {
    scratch_.length = 0;
    frs_dst_ = &scratch_;
  }
  return queued;
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::ReadFrs(uint16_t record, FrsBuffer& out) noexcept {
  const Result<void> ready = checkReady(true);
  if (!ready.ok()) {
    return Result<OpId>::Failure(ready.error);
  }
  if (frsBusy()) {
    return Result<OpId>::Failure(Error::Busy);
  }
  detail::Op op{};
  op.kind = detail::OpKind::FrsRead;
  op.frs_type = record;
  op.req[0] = reg::kRepFrsReadReq;
  detail::PutLe16(op.req, 4U, record);
  op.req_len = static_cast<uint8_t>(reg::kLenFrsReadReq);
  const Result<OpId> queued = enqueue(op);
  if (queued.ok()) {
    out.length = 0;
    out.last_status = 0;
    frs_dst_ = &out;
  }
  return queued;
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::WriteFrs(uint16_t record,
                                            std::span<const uint32_t> words) noexcept {
  const Result<void> ready = checkReady(true);
  if (!ready.ok()) {
    return Result<OpId>::Failure(ready.error);
  }
  if (words.size() > kFrsMaxWords) {
    return Result<OpId>::Failure(Error::InvalidArgument);
  }
  if (frsBusy()) {
    return Result<OpId>::Failure(Error::Busy);
  }
  detail::Op op{};
  op.kind = detail::OpKind::FrsWrite;
  op.frs_type = record;
  op.req[0] = reg::kRepFrsWriteReq;
  detail::PutLe16(op.req, 2U, static_cast<uint32_t>(words.size()));
  detail::PutLe16(op.req, 4U, record);
  op.req_len = static_cast<uint8_t>(reg::kLenFrsWriteReq);
  const Result<OpId> queued = enqueue(op);
  if (queued.ok()) {
    std::copy(words.begin(), words.end(), frs_wr_.begin());
    frs_wr_len_ = words.size();
    frs_wr_pos_ = 0;
  }
  return queued;
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::EraseFrs(uint16_t record) noexcept {
  return WriteFrs(record, std::span<const uint32_t>{});
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::Tare(AxisMask axes, TareBasis basis) noexcept {
  const bool allowed = (axes == AxisMask::All) || (axes == AxisMask::ZOnly) || cfg_.tare_any_mask;
  if (!allowed || (detail::ToUnderlying(axes) == 0U) || (detail::ToUnderlying(basis) > 5U)) {
    return Result<OpId>::Failure(Error::InvalidArgument);
  }
  std::array<uint8_t, 9> p{};
  p[0] = 0U;
  p[1] = detail::ToUnderlying(axes);
  p[2] = detail::ToUnderlying(basis);
  return enqueueCommand(reg::kCmdTare, p, true);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::PersistTare() noexcept {
  std::array<uint8_t, 9> p{};
  p[0] = 1U;
  return enqueueCommand(reg::kCmdTare, p, true);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::SetReorientation(const Quat14& q) noexcept {
  std::array<uint8_t, 9> p{};
  p[0] = 2U;
  const std::array<int16_t, 4> comp{q.x, q.y, q.z, q.w};
  for (std::size_t i = 0; i < comp.size(); ++i) {
    const auto u = static_cast<uint16_t>(comp[i]);
    p[1U + (2U * i)] = static_cast<uint8_t>(u & 0xFFU);
    p[2U + (2U * i)] = static_cast<uint8_t>(u >> 8U);
  }
  return enqueueCommand(reg::kCmdTare, p, true);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::ClearReorientation() noexcept {
  return SetReorientation(Quat14{});
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::ConfigureMeCal(const MeCal& cal) noexcept {
  std::array<uint8_t, 9> p{};
  p[0] = cal.accel ? 1U : 0U;
  p[1] = cal.gyro ? 1U : 0U;
  p[2] = cal.mag ? 1U : 0U;
  p[3] = 0U; // configure
  p[4] = cal.planar ? 1U : 0U;
  p[5] = cal.on_table ? 1U : 0U;
  return enqueueCommand(reg::kCmdMeCal, p, false);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::GetMeCal() noexcept {
  std::array<uint8_t, 9> p{};
  p[3] = 1U; // get
  return enqueueCommand(reg::kCmdMeCal, p, false);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::SaveDcd() noexcept {
  return enqueueCommand(reg::kCmdSaveDcd, std::array<uint8_t, 9>{}, false);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::SetPeriodicDcd(bool enable) noexcept {
  std::array<uint8_t, 9> p{};
  p[0] = enable ? 0U : 1U;
  return enqueueCommand(reg::kCmdPeriodicDcd, p, true);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::ClearDcdAndReset() noexcept {
  return enqueueCommand(reg::kCmdClearDcdReset, std::array<uint8_t, 9>{}, true);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::GetOscillator() noexcept {
  return enqueueCommand(reg::kCmdOscillator, std::array<uint8_t, 9>{}, false);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::Initialize() noexcept {
  std::array<uint8_t, 9> p{};
  p[0] = 1U; // the sensor hub
  return enqueueCommand(reg::kCmdInitialize, p, false);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::GetCounts(SensorId id) noexcept {
  if (!detail::KnownSensor(id)) {
    return Result<OpId>::Failure(
        Error::InvalidArgument); // nothing is sent for an ID the driver does not know
  }
  std::array<uint8_t, 9> p{};
  p[0] = 0U;
  p[1] = static_cast<uint8_t>(detail::ToUnderlying(id));
  return enqueueCommand(reg::kCmdCounters, p, false);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::ClearCounts(SensorId id) noexcept {
  if (!detail::KnownSensor(id)) {
    return Result<OpId>::Failure(
        Error::InvalidArgument); // nothing is sent for an ID the driver does not know
  }
  std::array<uint8_t, 9> p{};
  p[0] = 1U;
  p[1] = static_cast<uint8_t>(detail::ToUnderlying(id));
  return enqueueCommand(reg::kCmdCounters, p, true);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::ReportErrors(uint8_t severity) noexcept {
  std::array<uint8_t, 9> p{};
  p[0] = severity;
  return enqueueCommand(reg::kCmdErrors, p, false);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::StartSimpleCal(uint32_t interval_us) noexcept {
  const uint32_t interval = (interval_us == 0U) ? 10000U : interval_us; // RM S6.4.10: default 10 ms
  std::array<uint8_t, 9> p{};
  p[0] = 0U;
  for (std::size_t i = 0; i < 4U; ++i) {
    p[1U + i] = static_cast<uint8_t>((interval >> (8U * i)) & 0xFFU);
  }
  return enqueueCommand(reg::kCmdSimpleCal, p, false);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::FinishSimpleCal() noexcept {
  std::array<uint8_t, 9> p{};
  p[0] = 1U;
  return enqueueCommand(reg::kCmdSimpleCal, p, false);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::SetMotionIntent(MotionIntent intent) noexcept {
  if (cfg_.strict_variant && (cfg_.variant != Variant::Bno086)) {
    return Result<OpId>::Failure(Error::Variant); // interactive calibration is a BNO086 feature
  }
  if (detail::ToUnderlying(intent) > 4U) {
    return Result<OpId>::Failure(Error::InvalidArgument);
  }
  std::array<uint8_t, 9> p{};
  p[0] = detail::ToUnderlying(intent);
  return enqueueCommand(reg::kCmdInteractiveCal, p, true);
}

template <HubPort Port, LimitsLike Limits>
Result<OpId> Bno08x<Port, Limits>::SendWheel(const WheelSample& s) noexcept {
  if (s.wheel > 1U) {
    return Result<OpId>::Failure(Error::InvalidArgument);
  }
  std::array<uint8_t, 9> p{};
  p[0] = s.wheel;
  for (std::size_t i = 0; i < 4U; ++i) {
    p[1U + i] = static_cast<uint8_t>((s.timestamp_us >> (8U * i)) & 0xFFU);
  }
  p[5] = static_cast<uint8_t>(s.data & 0xFFU);
  p[6] = static_cast<uint8_t>(s.data >> 8U);
  p[7] = s.velocity ? 1U : 0U;
  return enqueueCommand(reg::kCmdWheel, p, true);
}

} // namespace bno08x

#endif // BNO08X_IPP
