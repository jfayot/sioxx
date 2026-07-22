#pragma once

#include <string>

#include "message.hpp"

namespace sioxx
{

enum class packet_type : int
{
  connect = 0,
  disconnect = 1,
  event = 2,
  ack = 3,
  connect_error = 4,
  binary_event = 5,
  binary_ack = 6
};

struct packet
{
  packet_type type{packet_type::event};
  std::string nsp{"/"};
  int id{-1};
  json data;
  int attachments{0};
};

}  // namespace sioxx
