/** @file 
 *
 *  @ingroup net_ip_component_module
 *
 *  @brief A class template that manages a collection of 
 *  @c basic_io_interface objects and provides "send to all" functionality.
 *
 *  @author Cliff Green
 *  @date 2018
 *  @copyright Cliff Green, MIT License
 *
 */

#ifndef SEND_TO_ALL_HPP_INCLUDED
#define SEND_TO_ALL_HPP_INCLUDED

#include <cstddef> // std::size_t
#include <utility> // std::move

#include <mutex>
#include <vector>

#include "net_ip/basic_io_interface.hpp"
#include "net_ip/queue_stats.hpp"

#include "utility/erase_where.hpp"
#include "utility/shared_buffer.hpp"

namespace chops {
namespace net {

/**
 *  @brief Manage a collection of @c basic_io_interface objects and provide a way
 *  to send data to all.
 *
 */
template <typename IOH>
class send_to_all {
private:
  using lock_guard = std::lock_guard<std::mutex>;
  using io_intfs   = std::vector<basic_io_interface<IOH> >;

private:
  mutable std::mutex    m_mutex;
  io_intfs              m_io_intfs;

public:
  void add_io_interface(basic_io_interface<IOH> io) {
    lock_guard gd { m_mutex };
    m_io_intfs.push_back(io);
  }

  void remove_io_interface(basic_io_interface<IOH> io) {
    lock_guard gd { m_mutex };
    chops::erase_where(m_io_intfs, io);
  }

  void send(chops::const_shared_buffer buf) const {
    lock_guard gd { m_mutex };
    for (auto io : m_io_intfs) {
      io.send(buf);
    }
  }
  void send(const void* buf, std::size_t sz) const {
    send(chops::const_shared_buffer(buf, sz));
  }
  void send(chops::mutable_shared_buffer&& buf) const { 
    send(chops::const_shared_buffer(std::move(buf)));
  }

  std::size_t size() const noexcept {
    lock_guard gd { m_mutex };
    return m_io_intfs.size();
  }

  auto get_total_output_queue_stats() const noexcept {
    chops::net::output_queue_stats tot { };
    lock_guard gd { m_mutex };
    for (auto io : m_io_intfs) {
      auto qs = io.get_output_queue_stats();
      tot.output_queue_size += qs.output_queue_size;
      tot.bytes_in_output_queue += qs.bytes_in_output_queue;
    }
    return tot;
  }
};

} // end net namespace
} // end chops namespace

#endif

