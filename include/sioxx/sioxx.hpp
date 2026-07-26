/**
 * @file sioxx.hpp
 * @brief Master header that pulls in the complete public API of the sioxx
 * library.
 *
 * Including this file is sufficient for most users – it brings in the client,
 * client options, message type, packet definition, parser interface and socket
 * namespace class.
 *
 * @note The header uses IWYU‑style `#include` directives with `export` pragma
 *       so that downstream translation units see the full definitions.
 */
#pragma once

#include "sioxx/client.hpp"          // IWYU pragma: export
#include "sioxx/client_options.hpp"  // IWYU pragma: export
#include "sioxx/message.hpp"         // IWYU pragma: export
#include "sioxx/packet.hpp"          // IWYU pragma: export
#include "sioxx/parser.hpp"          // IWYU pragma: export
#include "sioxx/socket.hpp"          // IWYU pragma: export
