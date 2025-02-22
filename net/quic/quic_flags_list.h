// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file intentionally does not have header guards, it's included
// inside a macro to generate values. The following line silences a
// presubmit warning that would otherwise be triggered by this:
// no-include-guard-because-multiply-included
// NOLINT(build/header_guard)

// This file contains the list of QUIC protocol flags.

// Time period for which a given connection_id should live in the time-wait
// state.
QUIC_FLAG(int64_t, FLAGS_quic_time_wait_list_seconds, 200)

// Currently, this number is quite conservative.  The max QPS limit for an
// individual server silo is currently set to 1000 qps, though the actual max
// that we see in the wild is closer to 450 qps.  Regardless, this means that
// the longest time-wait list we should see is 200 seconds * 1000 qps, 200000.
// Of course, there are usually many queries per QUIC connection, so we allow a
// factor of 3 leeway.
//
// Maximum number of connections on the time-wait list. A negative value implies
// no configured limit.
QUIC_FLAG(int64_t, FLAGS_quic_time_wait_list_max_connections, 600000)

// If true, require handshake confirmation for QUIC connections, functionally
// disabling 0-rtt handshakes.
// TODO(rtenneti): Enable this flag after CryptoServerTest's are fixed.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_require_handshake_confirmation,
          false)

// If true, disable pacing in QUIC.
QUIC_FLAG(bool, FLAGS_quic_disable_pacing_for_perf_tests, false)

// If true, enforce that QUIC CHLOs fit in one packet.
QUIC_FLAG(bool, FLAGS_quic_enforce_single_packet_chlo, true)

// If true, allows packets to be buffered in anticipation of a future CHLO, and
// allow CHLO packets to be buffered until next iteration of the event loop.
QUIC_FLAG(bool, FLAGS_quic_allow_chlo_buffering, true)

// If greater than zero, mean RTT variation is multiplied by the specified
// factor and added to the congestion window limit.
QUIC_FLAG(double, FLAGS_quic_bbr_rtt_variation_weight, 0.0f)

// Congestion window gain for QUIC BBR during PROBE_BW phase.
QUIC_FLAG(double, FLAGS_quic_bbr_cwnd_gain, 2.0f)

// If true, adjust congestion window when doing bandwidth resumption in BBR.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_fix_bbr_cwnd_in_bandwidth_resumption,
          true)

// When true, defaults to BBR congestion control instead of Cubic.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_default_to_bbr, false)

// If true, use BBRv2 as the default congestion controller.
// Takes precedence over --quic_default_to_bbr.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_default_to_bbr_v2, false)

// If buffered data in QUIC stream is less than this threshold, buffers all
// provided data or asks upper layer for more data.
QUIC_FLAG(uint32_t, FLAGS_quic_buffered_data_threshold, 8192u)

// Max size of data slice in bytes for QUIC stream send buffer.
QUIC_FLAG(uint32_t, FLAGS_quic_send_buffer_max_data_slice_size, 4096u)

// Anti-amplification factor. Before address validation, server will
// send no more than factor times bytes received.
QUIC_FLAG(int32_t, FLAGS_quic_anti_amplification_factor, 3)

// Enables 3 new connection options to make PROBE_RTT more aggressive
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_less_probe_rtt, false)

// If true, enable QUIC v99.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_99, true)

// When true, set the initial congestion control window from connection options
// in QuicSentPacketManager rather than TcpCubicSenderBytes.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_unified_iw_options, false)

// Number of packets that the pacing sender allows in bursts during pacing.
QUIC_FLAG(int32_t, FLAGS_quic_lumpy_pacing_size, 2)

// Congestion window fraction that the pacing sender allows in bursts during
// pacing.
QUIC_FLAG(double, FLAGS_quic_lumpy_pacing_cwnd_fraction, 0.25f)

// Default enables QUIC ack decimation and adds a connection option to disable
// it.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_ack_decimation, false)

// If true, QUIC offload pacing when using USPS as egress method.
QUIC_FLAG(bool, FLAGS_quic_restart_flag_quic_offload_pacing_to_usps2, false)

// Max time that QUIC can pace packets into the future in ms.
QUIC_FLAG(int32_t, FLAGS_quic_max_pace_time_into_future_ms, 10)

// Smoothed RTT fraction that a connection can pace packets into the future.
QUIC_FLAG(double, FLAGS_quic_pace_time_into_future_srtt_fraction, 0.125f)

// Mechanism to override version label and ALPN for IETF interop.
QUIC_FLAG(int32_t, FLAGS_quic_ietf_draft_version, 0)

// If true, stop resetting ideal_next_packet_send_time_ in pacing sender.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_donot_reset_ideal_next_packet_send_time,
    false)

// If true, enable experiment for testing PCC congestion-control.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_pcc3, false)

// When true, ensure BBR allows at least one MSS to be sent in response to an
// ACK in packet conservation.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_one_mss_conservation, false)

// Enables the BBQ5 connection option, which forces saved aggregation values to
// expire when the bandwidth increases more than 25% in QUIC BBR STARTUP.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_slower_startup4, false)

// When true and the BBR9 connection option is present, BBR only considers
// bandwidth samples app-limited if they're not filling the pipe.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_flexible_app_limited, false)

// If true, calling StopReading() on a level-triggered QUIC stream sequencer
// will cause the sequencer to discard future data.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_stop_reading_when_level_triggered,
          false)

// When the STMP connection option is sent by the client, timestamps in the QUIC
// ACK frame are sent and processed.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_send_timestamps, false)

// When in STARTUP and recovery, do not add bytes_acked to QUIC BBR's CWND in
// CalculateCongestionWindow()
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_bbr_no_bytes_acked_in_startup_recovery,
    false)

// If true, use common code for checking whether a new stream ID may be
// allocated.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_use_common_stream_check, false)

// If true, QuicEpollClock::Now() will monotonically increase.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_monotonic_epoll_clock, false)

// If true, enables the BBS4 and BBS5 connection options, which reduce BBR's
// pacing rate in STARTUP as more losses occur as a fraction of CWND.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr_startup_rate_reduction,
          false)

// If true, enable QUIC version 47.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_47, false)

// If true, enable QUIC version 48.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_48_2, true)

// If true, disable QUIC version 39.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_39, true)

// If true and using Leto for QUIC shared-key calculations, GFE will react to a
// failure to contact Leto by sending a REJ containing a fallback ServerConfig,
// allowing the client to continue the handshake.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_send_quic_fallback_server_config_on_leto_error,
    false)

// If true, enable QUIC version 49.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_49, true)

// If true, GFE will not request private keys when fetching QUIC ServerConfigs
// from Leto.
QUIC_FLAG(bool,
          FLAGS_quic_restart_flag_dont_fetch_quic_private_keys_from_leto,
          false)

// In v44 and above, where STOP_WAITING is never sent, close the connection if
// it's received.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_do_not_accept_stop_waiting,
          false)

// If true, set burst token to 2 in cwnd bootstrapping experiment.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_conservative_bursts, false)

// If true, export number of packets written per write operation histogram.")
QUIC_FLAG(bool, FLAGS_quic_export_server_num_packets_per_write_histogram, false)

// If true, uses conservative cwnd gain and pacing gain.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_conservative_cwnd_and_pacing_gains,
          false)

// When true, QUIC Version Negotiation packets will randomly include fake
// versions.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_version_negotiation_grease,
          true)

// If true, use predictable version negotiation versions.
QUIC_FLAG(bool, FLAGS_quic_disable_version_negotiation_grease_randomness, false)

// If true, do not add connection ID of packets with unknown connection ID
// and no version to time wait list, instead, send appropriate responses
// depending on the packets' sizes and drop them.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_reject_unprocessable_packets_statelessly,
    false)

// If true, when RTO fires and there is no packet to be RTOed, let connection
// send.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_fix_rto_retransmission3, true)

// Maximum number of tracked packets.
QUIC_FLAG(int64_t, FLAGS_quic_max_tracked_packet_count, 10000)

// If true, HTTP request header names sent from QuicSpdyClientBase(and
// descendents) will be automatically converted to lower case.
QUIC_FLAG(bool, FLAGS_quic_client_convert_http_header_name_to_lowercase, true)

// If true, do not send STOP_WAITING if no_stop_waiting_frame_.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_simplify_stop_waiting, false)

// If true, allow client to enable BBRv2 on server via connection option 'B2ON'.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_allow_client_enabled_bbr_v2,
          false)

// If true, will negotiate the ACK delay time.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_negotiate_ack_delay_time, false)

// If true, QuicFramer::WriteClientVersionNegotiationProbePacket uses
// length-prefixed connection IDs.
QUIC_FLAG(bool, FLAGS_quic_prober_uses_length_prefixed_connection_ids, false)

// When true, QuicFramer allows parsing failures of source connection ID for
// the PROX version.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_parse_prox_source_connection_id,
          true)

// If true and H2PR connection option is received, write_blocked_streams_ uses
// HTTP2 (tree-style) priority write scheduler.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_use_http2_priority_write_scheduler,
          true)

// If true, close connection if there are too many (> 1000) buffered control
// frames.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_add_upper_limit_of_buffered_control_frames3,
    true)

// If true, static streams should never be closed before QuicSession
// destruction.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_active_streams_never_negative,
          true)

// If true and FIFO connection option is received, write_blocked_streams uses
// FIFO(stream with smallest ID has highest priority) write scheduler.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_enable_fifo_write_scheduler,
          true)

// If true and LIFO connection option is received, write_blocked_streams uses
// LIFO(stream with largest ID has highest priority) write scheduler.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_enable_lifo_write_scheduler,
          true)

// When true, remove obsolete functionality intended to test IETF QUIC recovery.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_sent_packet_manager_cleanup,
          true)

// If true, QuicSession::ShouldKeepConnectionAlive() will not consider locally
// closed streams whose highest byte offset is not received yet.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_aggressive_connection_aliveness,
          true)

// If true, QuicStreamSequencer will not take in new data if the stream is
// reset.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_no_stream_data_after_reset,
          false)

// If true, enable IETF style probe timeout.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_pto, true)

// When true, QuicFramer will use QueueUndecryptablePacket on all QUIC versions.
QUIC_FLAG(bool,
          FLAGS_quic_restart_flag_quic_framer_uses_undecryptable_upcall,
          true)

// When true, QuicUtils::GenerateStatelessResetToken will hash connection IDs
// instead of XORing the bytes
QUIC_FLAG(bool,
          FLAGS_quic_restart_flag_quic_use_hashed_stateless_reset_tokens,
          true)

// This flag enables a temporary workaround which makes us reply to a specific
// invalid packet that is sent by an Android UDP network conformance test.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_reply_to_old_android_conformance_test,
          true)

// If true, no SPDY SETTINGS will be sent after handshake is confirmed.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_do_not_send_settings, true)

// The maximum amount of CRYPTO frame data that can be buffered.
QUIC_FLAG(int32_t, FLAGS_quic_max_buffered_crypto_bytes, 16 * 1024)

// If true, use the saved time of the last sent inflight packet rather than
// traversing the deque.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_simple_inflight_time, true)

// If true, QUIC supports both QUIC Crypto and TLS 1.3 for the handshake
// protocol.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_supports_tls_handshake, true)

// If true, deprecate SpuriousRetransmitDetected and call SpuriousLossDetected
// instead.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_detect_spurious_loss, true)

// If true, a stream will reset itself if it receives a stream frame that
// includes a data beyond the close offset.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_rst_if_stream_frame_beyond_close_offset,
    true)

// If true, enable IETF loss detection as described in
// https://tools.ietf.org/html/draft-ietf-quic-recovery-22#section-6.1.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_enable_ietf_loss_detection,
          true)

// If true, skip packet number before sending the last PTO retransmission.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_skip_packet_number_for_pto,
          true)
// If true, enable HTTP/2 default scheduling(round robin).
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_rr_write_scheduler, true)

// If true, when timer fires in RTO or PTO mode, make sure there is enough
// credits to retransmit one packet.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_grant_enough_credits, false)

// If true, combine QuicPacketGenerator and QuicPacketCreator.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_combine_generator_and_creator,
          true)

// If true, QuicFramer does not create an encrypter/decrypter for the
// ENCRYPTION_INITIAL level.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_framer_doesnt_create_initial_encrypter,
    false)

// If true, server drops client initial packets in datagrams < 1200 bytes.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_donot_process_small_initial_packets,
          true)

// If true, treat queued QUIC packets as sent.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_treat_queued_packets_as_sent,
          false)

// Call NeuterHandshakePackets() at most once per connection.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_neuter_handshake_packets_once,
          false)

// If true, support HTTP/3 priority in v99.
QUIC_FLAG(bool, FLAGS_quic_allow_http3_priority, false)

// If true, enable QUIC version 50.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_50, false)

// If true, enable QUIC MTU discovery version 2.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_mtu_discovery_v2, false)
