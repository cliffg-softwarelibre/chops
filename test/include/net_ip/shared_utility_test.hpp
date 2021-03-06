/** @file
 *
 *  @ingroup test_module
 *
 *  @brief Declarations and implementations for utility code shared between 
 *  @c net_ip tests.
 *
 *  The general Chops Net IP test strategy is to have message senders and message 
 *  receivers, with a flag specifying whether the receiver is to loop back the
 *  messages. For TCP it is independent of whether the sender or receiver is an 
 *  acceptor or connector, although most tests have the connector being a sender. In 
 *  the test routines, coordination is typically needed to know when a connection has 
 *  been made or sender / receiver is ready so that message flow can start. At the 
 *  higher layers, the Chops Net IP library facilities provide connection state
 *  change function object callbacks.
 *
 *  When the message flow is finished, an empty body message is sent to the receiver
 *  (and looped back if the reply flag is set), which signals an "end of message 
 *  flow" condition. The looped back empty message may not arrive back to the 
 *  sender since connections or handlers are in the process of being taken down.
 *
 *  @author Cliff Green
 *
 *  Copyright (c) 2017-2018 by Cliff Green
 *
 *  Distributed under the Boost Software License, Version 1.0. 
 *  (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 *
 */

#ifndef SHARED_UTILITY_TEST_HPP_INCLUDED
#define SHARED_UTILITY_TEST_HPP_INCLUDED

#include <string_view>
#include <cstddef> // std::size_t, std::byte
#include <cstdint> // std::uint16_t
#include <vector>
#include <utility> // std::forward, std::move
#include <atomic>
#include <memory> // std::shared_ptr
#include <thread>
#include <system_error>

#include <cassert>
#include <limits>

#include <experimental/buffer>
#include <experimental/internet> // ip::udp::endpoint

#include <boost/endian/conversion.hpp>

#include "utility/shared_buffer.hpp"
#include "utility/repeat.hpp"
#include "utility/make_byte_array.hpp"

#include "net_ip/io_interface.hpp"

#include "net_ip/component/simple_variable_len_msg_frame.hpp"

namespace chops {
namespace test {

inline chops::mutable_shared_buffer make_body_buf(std::string_view pre, 
                                                  char body_char, 
                                                  std::size_t num_body_chars) {
  chops::mutable_shared_buffer buf(pre.data(), pre.size());
  std::string body(num_body_chars, body_char);
  return buf.append(body.data(), body.size());
}

inline chops::const_shared_buffer make_variable_len_msg(const chops::mutable_shared_buffer& body) {
  assert(body.size() < std::numeric_limits<std::uint16_t>::max());
  std::uint16_t hdr = boost::endian::native_to_big(static_cast<std::uint16_t>(body.size()));
  chops::mutable_shared_buffer msg(static_cast<const void*>(&hdr), 2);
  return chops::const_shared_buffer(std::move(msg.append(body.data(), body.size())));
}

inline chops::const_shared_buffer make_cr_lf_text_msg(const chops::mutable_shared_buffer& body) {
  chops::mutable_shared_buffer msg(body.data(), body.size());
  auto ba = chops::make_byte_array(0x0D, 0x0A); // CR, LF
  return chops::const_shared_buffer(std::move(msg.append(ba.data(), ba.size())));
}

inline chops::const_shared_buffer make_lf_text_msg(const chops::mutable_shared_buffer& body) {
  chops::mutable_shared_buffer msg(body.data(), body.size());
  auto ba = chops::make_byte_array(0x0A); // LF
  return chops::const_shared_buffer(std::move(msg.append(ba.data(), ba.size())));
}

inline std::size_t decode_variable_len_msg_hdr(const std::byte* buf_ptr, std::size_t sz) {
  assert (sz == 2);
  std::uint16_t hdr;
  std::byte* hdr_ptr = static_cast<std::byte*>(static_cast<void*>(&hdr));
  *(hdr_ptr+0) = *(buf_ptr+0);
  *(hdr_ptr+1) = *(buf_ptr+1);
  return boost::endian::big_to_native(hdr);
}

template <typename F>
chops::const_shared_buffer make_empty_body_msg(F&& func) {
  return func( chops::mutable_shared_buffer{ } );
}

inline auto make_empty_variable_len_msg() { return make_empty_body_msg(make_variable_len_msg); }
inline auto make_empty_cr_lf_text_msg() { return make_empty_body_msg(make_cr_lf_text_msg); }
inline auto make_empty_lf_text_msg() { return make_empty_body_msg(make_lf_text_msg); }

using vec_buf = std::vector<chops::const_shared_buffer>;

template <typename F>
vec_buf make_msg_vec(F&& func, std::string_view pre, char body_char, int num_msgs) {
  vec_buf vec;
  chops::repeat(num_msgs, [&vec, f = std::forward<F>(func), pre, body_char] (int i) {
      vec.push_back (f(make_body_buf(pre, body_char, i+1)));
    }
  );
  return vec;
}

using test_counter = std::atomic_size_t;

template <typename IOT>
struct msg_hdlr {
  using endp_type = typename IOT::endpoint_type;
  using const_buf = std::experimental::net::const_buffer;

  bool               reply;
  test_counter&      cnt;

  msg_hdlr(bool rep, test_counter& c) : reply(rep), cnt(c) { }

  bool operator()(const_buf buf, chops::net::basic_io_interface<IOT> io_intf, endp_type endp) {
    chops::const_shared_buffer sh_buf(buf.data(), buf.size());
    if (sh_buf.size() > 2) { // not a shutdown message
      ++cnt;
      if (reply) {
        io_intf.send(sh_buf, endp);
      }
      return true;
    }
    if (reply) {
      // may not make it back to sender, depending on TCP connection or UDP reliability
      io_intf.send(sh_buf, endp);
    }
    return false;
  }

};

using tcp_msg_hdlr = msg_hdlr<chops::net::tcp_io>;
using udp_msg_hdlr = msg_hdlr<chops::net::udp_io>;

inline bool tcp_start_io (chops::net::tcp_io_interface io, bool reply, 
                   std::string_view delim, test_counter& cnt) {
  if (delim.empty()) {
    return io.start_io(2, tcp_msg_hdlr(reply, cnt), 
                       chops::net::make_simple_variable_len_msg_frame(decode_variable_len_msg_hdr));
  }
  return io.start_io(delim, tcp_msg_hdlr(reply, cnt));
}

constexpr int udp_max_buf_size = 65507;

inline bool udp_start_io (chops::net::udp_io_interface io, bool reply, test_counter& cnt) {
  return io.start_io(udp_max_buf_size, udp_msg_hdlr(reply, cnt));
}

inline bool udp_start_io (chops::net::udp_io_interface io, bool receiving, test_counter& cnt,
                          const std::experimental::net::ip::udp::endpoint& remote_endp) {
  if (receiving) {
    return io.start_io(remote_endp, udp_max_buf_size, udp_msg_hdlr(false, cnt));
  }
  return io.start_io(remote_endp);
}

struct io_handler_mock {
  using socket_type = int;
  using endpoint_type = std::experimental::net::ip::udp::endpoint;

  socket_type sock = 3;
  bool started = false;
  constexpr static std::size_t qs_base = 42;

  bool is_io_started() const { return started; }

  socket_type& get_socket() { return sock; }

  chops::net::output_queue_stats get_output_queue_stats() const { 
    return chops::net::output_queue_stats { qs_base, qs_base +1 };
  }

  bool send_called = false;

  void send(chops::const_shared_buffer) { send_called = true; }
  void send(chops::const_shared_buffer, const endpoint_type&) { send_called = true; }

  bool mf_sio_called = false;
  bool delim_sio_called = false;
  bool rd_sio_called = false;
  bool rd_endp_sio_called = false;
  bool send_sio_called = false;
  bool send_endp_sio_called = false;

  template <typename MH, typename MF>
  bool start_io(std::size_t, MH&&, MF&&) {
    return started ? false : started = true, mf_sio_called = true, true;
  }

  template <typename MH>
  bool start_io(std::string_view, MH&&) {
    return started ? false : started = true, delim_sio_called = true, true;
  }

  template <typename MH>
  bool start_io(std::size_t, MH&&) {
    return started ? false : started = true, rd_sio_called = true, true;
  }

  template <typename MH>
  bool start_io(const endpoint_type&, std::size_t, MH&&) {
    return started ? false : started = true, rd_endp_sio_called = true, true;
  }

  bool start_io() {
    return started ? false : started = true, send_sio_called = true, true;
  }

  bool start_io(const endpoint_type&) {
    return started ? false : started = true, send_endp_sio_called = true, true;
  }

  bool stop_io() {
    return started ? started = false, true : false;
  }

};

using io_handler_mock_ptr = std::shared_ptr<io_handler_mock>;
using io_interface_mock = chops::net::basic_io_interface<io_handler_mock>;

struct net_entity_mock {

  io_handler_mock_ptr  iop;
  std::thread          thr;
  
  net_entity_mock() : iop(std::make_shared<io_handler_mock>()) { }

  using socket_type = double;
  using endpoint_type = int;

  constexpr static double special_val = 42.0;
  double dummy = special_val;

  bool started = false;

  bool is_started() const { return started; }

  double& get_socket() { return dummy; }

  template <typename F1, typename F2>
  bool start(F1&& io_state_chg_func, F2&& err_func ) {
    if (started) {
      return false;
    }
    started = true;
    thr = std::thread([ this, ios = std::move(io_state_chg_func), ef = std::move(err_func)] () mutable {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ios(io_interface_mock(iop), 1, true);
        ef(io_interface_mock(iop), 
                 std::make_error_code(chops::net::net_ip_errc::message_handler_terminated));
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ios(io_interface_mock(iop), 0, false);
      }
    );
    return true;
  }

  bool stop() {
    if (!started) {
      return false;
    }
    started = false;
    join_thr();
    return true;
  }

  void join_thr() { thr.join(); }

};

inline void io_state_chg_mock(io_interface_mock, std::size_t, bool) { }
inline void err_func_mock(io_interface_mock, std::error_code) { }

std::experimental::net::ip::udp::endpoint make_udp_endpoint(const char* addr, int port_num) {
  return std::experimental::net::ip::udp::endpoint(std::experimental::net::ip::make_address(addr),
                           static_cast<unsigned short>(port_num));
}


} // end namespace test
} // end namespace chops

#endif

