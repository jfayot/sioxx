/**
 * @file packet.hpp
 * @brief Low‑level representation of an Engine.IO / Socket.IO packet.
 *
 * Packets travel across the transport layer (WebSocket or HTTP polling) and
 * are encoded/decoded by a `parser_base` implementation.
 */

#ifndef SIOXX_PACKET_HPP
#define SIOXX_PACKET_HPP

#include <string>

#include "message.hpp"

namespace sioxx
{

/**
 * @enum packet_type
 * @brief Enumerates the different Socket.IO packet kinds.
 *
 * Values match the official Socket.IO protocol specification.
 */
enum class packet_type : int
{
  connect = 0,        ///< Namespace connection request
  disconnect = 1,     ///< Namespace disconnection
  event = 2,          ///< Regular event with optional data
  ack = 3,            ///< Acknowledgement for a previous event
  connect_error = 4,  ///< Connection error payload
  binary_event = 5,   ///< Event that carries binary attachments
  binary_ack = 6      ///< Ack that carries binary attachments
};

/**
 * @struct packet
 * @brief Complete packet structure as used by parsers.
 *
 * - `type`      – one of `packet_type`.
 * - `nsp`       – namespace (default "/").
 * - `id`        – ACK identifier (‑1 if not used).
 * - `data`      – JSON payload (`message`).
 * - `attachments` – number of binary attachments (relevant for binary packets).
 */
struct packet
{
  packet_type type{packet_type::event};  ///< Packet type
  std::string nsp{"/"};                  ///< Namespace
  int id{-1};                            ///< ACK identifier
  json data;                             ///< Payload (JSON value)
  int attachments{0};                    ///< Binary attachment count
};

}  // namespace sioxx

#endif  // SIOXX_PACKET_HPP
