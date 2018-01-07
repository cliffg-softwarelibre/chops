/** @file
 *
 *  @ingroup test_module
 *
 *  @brief Utility code shared between @c net_ip tests.
 *
 *  The body of a msg is constructed of a preamble followed by a repeated 
 *  char. There are three forms of messages:
 *  1. Variable len: header is 16 bit big endian integer containing length of body
 *  2. Text, CR LF: body is followed by Ascii CR and LF chars
 *  3. Text, LF: body is followed by Ascii LF char
 *
 *  @author Cliff Green
 *  @date 2017, 2018
 *  @copyright Cliff Green, MIT License
 *
 */

#include "catch.hpp"

#include <string_view>
#include <string>
#include <cstddef> // std::size_t, std::byte
#include <cstdint> // std::uint16_t
#include <vector>
#include <algorithm>
#include <memory> // std::shared_ptr, std::make_shared

#include <boost/endian/conversion.hpp>

#include <experimental/buffer>
#include <experimental/internet>

#include "utility/make_byte_array.hpp"
#include "utility/shared_buffer.hpp"
#include "net_ip/io_interface.hpp"

#include "../test/net_ip/detail/shared_utility_test.hpp"

namespace chops {
namespace test {

chops::mutable_shared_buffer make_body_buf(std::string_view pre, char body_char, std::size_t num_body_chars) {
  chops::mutable_shared_buffer buf(pre.data(), pre.size());
  std::string body(num_body_chars, body_char);
  return buf.append(body.data(), body.size());
}

chops::mutable_shared_buffer make_variable_len_msg(const chops::mutable_shared_buffer& body) {
  std::uint16_t hdr = boost::endian::native_to_big(static_cast<std::uint16_t>(body.size()));
  chops::mutable_shared_buffer msg(static_cast<const void*>(&hdr), 2);
  return msg.append(body.data(), body.size());
}

chops::mutable_shared_buffer make_cr_lf_text_msg(const chops::mutable_shared_buffer& body) {
  chops::mutable_shared_buffer msg(body.data(), body.size());
  auto ba = chops::make_byte_array(0x0D, 0x0A); // CR, LF
  return msg.append(ba.data(), ba.size());
}

chops::mutable_shared_buffer make_lf_text_msg(const chops::mutable_shared_buffer& body) {
  chops::mutable_shared_buffer msg(body.data(), body.size());
  auto ba = chops::make_byte_array(0x0A); // LF
  return msg.append(ba.data(), ba.size());
}


std::size_t variable_len_msg_frame(std::experimental::net::mutable_buffer buf) {
  // assert buf.size() == 2
  std::uint16_t hdr;
  std::byte* hdr_ptr = static_cast<std::byte*>(static_cast<void*>(&hdr));
  const std::byte* buf_ptr = static_cast<const std::byte*>(buf.data());
  *(hdr_ptr+0) = *(buf_ptr+0);
  *(hdr_ptr+1) = *(buf_ptr+1);
  return boost::endian::big_to_native(hdr);
}

} // end namespace test
} // end namespace chops

void make_msg_test() {
  using namespace chops::test;

  GIVEN ("A body consisting of a preamble and a char to repeat") {

    auto body = make_body_buf("HappyNewYear!", 'Q', 10);
    REQUIRE (body.size() == 23);

    WHEN ("make_variable_len_msg is called") {
      auto msg = make_variable_len_msg(body);
      THEN ("the correct header is prepended") {
        REQUIRE (msg.size() == 25); // full size of msg
        REQUIRE (*(msg.data()+0) == std::byte(0x00));
        REQUIRE (*(msg.data()+1) == std::byte(0x17)); // header is 16 bits, value 23 in big endian
        REQUIRE (*(msg.data()+2) == std::byte(0x48)); // 'H' in ascii
        REQUIRE (*(msg.data()+3) == std::byte(0x61)); // 'a' in ascii
        REQUIRE (*(msg.data()+15) == std::byte(0x51)); // 'Q' in ascii
        REQUIRE (*(msg.data()+16) == std::byte(0x51)); // 'Q' in ascii
      }
    }

    AND_WHEN ("make_cr_lf_text_msg is called") {
      auto msg = make_cr_lf_text_msg(body);
      THEN ("CR and LF are appended") {
        REQUIRE (msg.size() == 25); // full size of msg
        REQUIRE (*(msg.data()+0) == std::byte(0x48)); // 'H' in ascii
        REQUIRE (*(msg.data()+1) == std::byte(0x61)); // 'a' in ascii
        REQUIRE (*(msg.data()+13) == std::byte(0x51)); // 'Q' in ascii
        REQUIRE (*(msg.data()+14) == std::byte(0x51)); // 'Q' in ascii
        REQUIRE (*(msg.data()+23) == std::byte(0x0D)); // CR in ascii
        REQUIRE (*(msg.data()+24) == std::byte(0x0A)); // LF in ascii
      }
    }

    AND_WHEN ("make_lf_text_msg is called") {
      auto msg = make_lf_text_msg(body);
      THEN ("LF is appended") {
        REQUIRE (msg.size() == 24); // full size of msg
        REQUIRE (*(msg.data()+0) == std::byte(0x48)); // 'H' in ascii
        REQUIRE (*(msg.data()+1) == std::byte(0x61)); // 'a' in ascii
        REQUIRE (*(msg.data()+13) == std::byte(0x51)); // 'Q' in ascii
        REQUIRE (*(msg.data()+14) == std::byte(0x51)); // 'Q' in ascii
        REQUIRE (*(msg.data()+23) == std::byte(0x0A)); // LF in ascii
      }
    }

    AND_WHEN ("a larger buffer is passed to make_variable_len_msg") {
      auto body = make_body_buf("HappyNewYear!", 'Q', 500);
      REQUIRE (body.size() == 513);

      auto msg = make_variable_len_msg(body);

      THEN ("the correct header is prepended") {
        REQUIRE (msg.size() == 515); // full size of msg
        REQUIRE (*(msg.data()+0) == std::byte(0x02));
        REQUIRE (*(msg.data()+1) == std::byte(0x01)); // header is 16 bits, value 513 in big endian
      }
    }

  } // end given
}

template <typename F>
void make_msg_set_test(F&& f) {
  using namespace chops::test;

  GIVEN ("A preamble and a char to repeat") {
    WHEN ("make_msg_set is called") {
      auto vb = make_msg_set(f, "Good tea!", 'Z', 20);
      THEN ("a vector of buffers is returned") {
        REQUIRE (vb.size() == 21); // account for empty body message at end
        int delta = vb[20].size();
        REQUIRE (delta <= 2);
        chops::repeat(20, [&vb, delta] (const int& i) { REQUIRE (vb[i].size() == (i+10+delta)); } );
      }
    }
  } // end given
}

SCENARIO ( "Shared Net IP test utility, make msg", "[shared_test_utility_make_msg]" ) {

  make_msg_test();

}

SCENARIO ( "Shared Net IP test utility, make msg set", "[shared_test_utility_make_msg_set]" ) {
  using namespace chops::test;

  make_msg_set_test(make_variable_len_msg);
  make_msg_set_test(make_cr_lf_text_msg);
  make_msg_set_test(make_lf_text_msg);

}

SCENARIO ( "Shared Net IP test utility, variable len msg frame", "[shared_test_utility_msg_frame]" ) {
  using namespace chops::test;
  using namespace std::experimental::net;

  GIVEN ("A two byte buffer that is a variable len msg header") {

    auto ba = chops::make_byte_array(0x02, 0x01); // 513 in big endian

    WHEN ("the variable len msg frame function is called") {
      THEN ("the correct length is returned") {
        REQUIRE(variable_len_msg_frame(mutable_buffer(ba.data(), ba.size())) == 513);
      }
    }
  } // end given
}

SCENARIO ( "Shared Net IP test utility, msg hdlr", "[shared_test_utility_msg_hdlr]" ) {
  using namespace chops::test;
  using namespace std::experimental::net;

  struct ioh_mock {
    using endpoint_type = std::experimental::net::ip::tcp::endpoint;
    using socket_type = std::experimental::net::ip::tcp::socket;

    bool send_called = false;

    void send(chops::const_shared_buffer) { send_called = true; }
  };

  auto iohp = std::make_shared<ioh_mock>();
  REQUIRE(!iohp->send_called);
  ip::tcp::endpoint endp { };

  auto msg = make_variable_len_msg(make_body_buf("Bah, humbug!", 'T', 4));
  auto empty = make_variable_len_msg(chops::mutable_shared_buffer());

  GIVEN ("A mock io handler, a msg with a body, and an empty body msg") {

    WHEN ("a msg hdlr is created with reply true and the messages passed in") {
      msg_hdlr<ioh_mock> mh(true);
      THEN ("true is always returned and send has been called") {
        REQUIRE(mh(const_buffer(msg.data(), msg.size()), chops::net::io_interface<ioh_mock>(iohp), endp));
        REQUIRE(iohp->send_called);
        REQUIRE(mh(const_buffer(empty.data(), empty.size()), chops::net::io_interface<ioh_mock>(iohp), endp));
      }
    }
    AND_WHEN ("a msg hdlr is created with reply false and the messages passed in") {
      msg_hdlr<ioh_mock> mh(false);
      THEN ("true is returned, then false is returned") {
        REQUIRE(mh(const_buffer(msg.data(), msg.size()), chops::net::io_interface<ioh_mock>(iohp), endp));
        REQUIRE(!mh(const_buffer(empty.data(), empty.size()), chops::net::io_interface<ioh_mock>(iohp), endp));
      }
    }
  } // end given
}

