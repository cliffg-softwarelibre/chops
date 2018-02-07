/** @file
 *
 *  @ingroup test_module
 *
 *  @brief Test scenarios for @c basic_io_interface class template.
 *
 *  @author Cliff Green
 *  @date 2018
 *  @copyright Cliff Green, MIT License
 *
 */

#include "catch.hpp"

#include <experimental/internet> // endpoint declarations
#include <experimental/socket>

#include <memory> // std::shared_ptr
#include <system_error> // std::error_code
#include <string_view>
#include <string>
#include <set>
#include <cstddef> // std::size_t

#include "net_ip/queue_stats.hpp"
#include "net_ip/basic_io_interface.hpp"

#include "utility/shared_buffer.hpp"
#include "utility/make_byte_array.hpp"

using namespace std::experimental::net;

constexpr int qs_base = 42;

struct io_handler_base_mock {
  bool started = false;

  bool is_io_started() const { return started; }

  chops::net::output_queue_stats get_output_queue_stats() const { 
    return chops::net::output_queue_stats { qs_base, qs_base +1 };
  }

  template <typename MH, typename MF>
  void start_io(std::size_t, MH&&, MF&&) { started = true; }

  template <typename MH>
  void start_io(std::string_view, MH&&) { started = true; }

  template <typename MH>
  void start_io(std::size_t, MH&&) { started = true; }

  template <typename MH>
  void start_io(std::size_t, const ip::tcp::endpoint&, MH&&) { started = true; }

  template <typename MH>
  void start_io(std::size_t, const ip::udp::endpoint&, MH&&) { started = true; }

  void start_io() { started = true; }

  void start_io(const ip::tcp::endpoint&) { started = true; }

  void start_io(const ip::udp::endpoint&) { started = true; }

  void stop_io() { started = false; }



};

struct tcp_io_handler_mock : public io_handler_base_mock {

  using socket_type = ip::tcp::socket;
  using endpoint_type = ip::tcp::endpoint;

  socket_type sock;

  tcp_io_handler_mock (io_context& ioc) : io_handler_base_mock(), sock(ioc) { }

  socket_type& get_socket() { return sock; }

  void send(chops::const_shared_buffer) { }
  void send(chops::const_shared_buffer, const endpoint_type&) { }

};

struct udp_io_handler_mock : public io_handler_base_mock {

  using socket_type = ip::udp::socket;
  using endpoint_type = ip::udp::endpoint;

  socket_type sock;

  udp_io_handler_mock (io_context& ioc) : io_handler_base_mock(), sock(ioc) { }

  socket_type& get_socket() { return sock; }

  void send(chops::const_shared_buffer) { }
  void send(chops::const_shared_buffer, const endpoint_type&) { }

};

template <typename IOH>
void basic_io_interface_test_default_constructed() {

  chops::net::basic_io_interface<IOH> io_intf { };

  GIVEN ("A default constructed basic_io_interface") {
    WHEN ("is_valid is called") {
      THEN ("the return is false") {
        REQUIRE_FALSE (io_intf.is_valid());
      }
    }
    AND_WHEN ("is_io_started or get_socket or get_output_queue_stats is called on an invalid basic_io_interface") {
      THEN ("an exception is thrown") {
        REQUIRE_THROWS (io_intf.is_io_started());
        REQUIRE_THROWS (io_intf.get_socket());
        REQUIRE_THROWS (io_intf.get_output_queue_stats());
      }
    }
    AND_WHEN ("send or start_io or stop_io is called on an invalid basic_io_interface") {
      THEN ("false is returned") {

        chops::const_shared_buffer buf(nullptr, 0);
        typename IOH::endpoint_type endp;

        REQUIRE_FALSE (io_intf.send(nullptr, 0));
        REQUIRE_FALSE (io_intf.send(buf));
        REQUIRE_FALSE (io_intf.send(chops::mutable_shared_buffer()));
        REQUIRE_FALSE (io_intf.send(nullptr, 0, endp));
        REQUIRE_FALSE (io_intf.send(buf, endp));
        REQUIRE_FALSE (io_intf.send(chops::mutable_shared_buffer(), endp));

        REQUIRE_FALSE (io_intf.start_io(0, [] { }, [] { }));
        REQUIRE_FALSE (io_intf.start_io("testing, hah!", [] { }));
        REQUIRE_FALSE (io_intf.start_io(0, [] { }));
        REQUIRE_FALSE (io_intf.start_io(0, endp, [] { }));
        REQUIRE_FALSE (io_intf.start_io());
        REQUIRE_FALSE (io_intf.start_io(endp));

        REQUIRE_FALSE (io_intf.stop_io());
      }
    }
  } // end given

}

template <typename IOH>
void basic_io_interface_test_two() {

  chops::net::basic_io_interface<IOH> io_intf { };

  io_context ioc;
  auto ioh = std::make_shared<IOH>(ioc);
  io_intf = chops::net::basic_io_interface<IOH>(ioh);

  GIVEN ("A default constructed basic_io_interface and an io handler") {
    WHEN ("an basic_io_interface with a weak ptr to the io handler is assigned to it") {
      THEN ("the return is true") {
        REQUIRE (io_intf.is_valid());
      }
    }
    AND_WHEN ("is_io_started or get_output_queue_stats is called") {
      THEN ("values are returned") {
        REQUIRE_FALSE (io_intf.is_io_started());
        chops::net::output_queue_stats s = io_intf.get_output_queue_stats();
        REQUIRE (s.output_queue_size == qs_base);
        REQUIRE (s.bytes_in_output_queue == (qs_base + 1));
      }
    }
    AND_WHEN ("send or start_io or stop_io is called") {
      THEN ("true is returned") {

        chops::const_shared_buffer buf(nullptr, 0);
        typename IOH::endpoint_type endp;

        REQUIRE (io_intf.send(nullptr, 0));
        REQUIRE (io_intf.send(buf));
        REQUIRE (io_intf.send(chops::mutable_shared_buffer()));
        REQUIRE (io_intf.send(nullptr, 0, endp));
        REQUIRE (io_intf.send(buf, endp));
        REQUIRE (io_intf.send(chops::mutable_shared_buffer(), endp));

        REQUIRE (io_intf.start_io(0, [] { }, [] { }));
        REQUIRE (io_intf.start_io("testing, hah!", [] { }));
        REQUIRE (io_intf.start_io(0, [] { }));
        REQUIRE (io_intf.start_io(0, endp, [] { }));
        REQUIRE (io_intf.start_io(endp));
        REQUIRE (io_intf.start_io());

        REQUIRE (io_intf.is_io_started());

        REQUIRE (io_intf.stop_io());
        REQUIRE_FALSE (io_intf.is_io_started());
      }
    }
  } // end given

}

template <typename IOH>
void basic_io_interface_test_compare() {

  chops::net::basic_io_interface<IOH> io_intf1 { };

  io_context ioc;
  auto ioh1 = std::make_shared<IOH>(ioc);
  chops::net::basic_io_interface<IOH> io_intf2(ioh1);

  chops::net::basic_io_interface<IOH> io_intf3 { };

  auto ioh2 = std::make_shared<IOH>(ioc);
  chops::net::basic_io_interface<IOH> io_intf4(ioh2);

  chops::net::basic_io_interface<IOH> io_intf5 { };

  GIVEN ("Three default constructed basic_io_interfaces and two with io handlers") {
    WHEN ("all three are inserted in a multiset") {
      std::multiset<chops::net::basic_io_interface<IOH> > a_set { io_intf1, io_intf2, io_intf3, io_intf4, io_intf5 };
      THEN ("the invalid basic_io_interfaces are first in the set") {
        REQUIRE (a_set.size() == 5);
        auto i = a_set.cbegin();
        REQUIRE_FALSE (i->is_valid());
        ++i;
        REQUIRE_FALSE (i->is_valid());
        ++i;
        REQUIRE_FALSE (i->is_valid());
        ++i;
        REQUIRE (i->is_valid());
        ++i;
        REQUIRE (i->is_valid());
      }
    }
    AND_WHEN ("two invalid basic_io_interfaces are compared for equality") {
      THEN ("they compare equal") {
        REQUIRE (io_intf1 == io_intf3);
        REQUIRE (io_intf3 == io_intf5);
      }
    }
    AND_WHEN ("two valid basic_io_interfaces are compared for equality") {
      THEN ("they compare equal if both point to the same io handler") {
        REQUIRE_FALSE (io_intf2 == io_intf4);
        io_intf2 = io_intf4;
        REQUIRE (io_intf2 == io_intf4);
      }
    }
    AND_WHEN ("an invalid basic_io_interface is order compared with a valid basic_io_interface") {
      THEN ("the invalid compares less than the valid") {
        REQUIRE (io_intf1 < io_intf2);
      }
    }
  } // end given

}

SCENARIO ( "Basic io interface test, udp",
           "[basic_io_interface] [udp]" ) {
  basic_io_interface_test_default_constructed<udp_io_handler_mock>();
  basic_io_interface_test_two<udp_io_handler_mock>();
  basic_io_interface_test_compare<udp_io_handler_mock>();
}

SCENARIO ( "Basic io interface test, tcp",
           "[basic_io_interface] [tcp]" ) {
  basic_io_interface_test_default_constructed<tcp_io_handler_mock>();
  basic_io_interface_test_two<tcp_io_handler_mock>();
  basic_io_interface_test_compare<tcp_io_handler_mock>();
}
