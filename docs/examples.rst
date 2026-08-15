Examples
========

.. default-domain:: cpp

The snippets below cover the most common client configurations. For a
complete executable, see
`examples/basic_client/main.cpp
<https://github.com/jfayot/sioxx/blob/main/examples/basic_client/main.cpp>`_ in the
project repository.

Connect and receive events
--------------------------

Create namespace sockets before connecting so their handlers are ready when
the server accepts the namespace:

.. code-block:: cpp

   #include <iostream>
   #include <sioxx/sioxx.hpp>

   sioxx::client client;
   auto chat = client.socket("/chat");

   chat->on_connect([] {
       std::cout << "chat connected\n";
   });

   chat->on("message", [](const std::string& event, sioxx::message data) {
       std::cout << event << ": " << data.dump() << '\n';
   });

   client.connect("wss://example.com");

Callbacks run on the connection's background thread. Dispatch back to your
application thread before modifying thread-affine state such as a UI.

Emit an event and receive an acknowledgement
---------------------------------------------

Payloads are ``nlohmann::json`` values. Pass a callback as the third argument
when the server should acknowledge the event. Events emitted before the
namespace connects are buffered and sent in order after connection:

.. code-block:: cpp

   chat->emit("hello", sioxx::json{{"name", "Ada"}});

   chat->emit(
       "sum",
       sioxx::json::array({1, 2, 3}),
       [](sioxx::message reply) {
           std::cout << "server replied: " << reply.dump() << '\n';
       });

Reply to a server acknowledgement request
-----------------------------------------

Use the acknowledgement-aware listener overload when the server expects the
client to reply. The reply function is available only when the incoming event
contains an acknowledgement ID, and sends at most one response:

.. code-block:: cpp

   chat->on(
       "question",
       [](const std::string&, sioxx::message data,
          sioxx::socket::ack_callback acknowledge) {
           if (acknowledge) {
               acknowledge(sioxx::json::array({"answer", 42}));
           }
       });

Send and receive binary attachments
-----------------------------------

Create binary values with ``sioxx::binary_message()`` and place them at any
depth in an event payload. The default JSON parser automatically emits the
Socket.IO placeholder header followed by the binary attachment frames; the
application does not need to construct ``BINARY_EVENT`` packets itself:

.. code-block:: cpp

   auto image = sioxx::binary_message(
       std::vector<std::uint8_t>{0x89, 0x50, 0x4e, 0x47});

   chat->emit(
       "upload",
       sioxx::json{{"name", "avatar.png"}, {"bytes", image}},
       [](sioxx::message reply) {
           std::cout << "upload reply: " << reply.dump() << '\n';
       });

Listener payloads remain arrays of Socket.IO arguments. Check
``is_binary()`` before reading a value with ``get_binary()``:

.. code-block:: cpp

   chat->on("download", [](const std::string&, sioxx::message arguments) {
       if (!arguments.is_array() || arguments.empty()) return;

       const auto& file = arguments.at(0);
       if (!file.is_object() || !file.contains("bytes")) return;

       const auto& value = file.at("bytes");
       if (!value.is_binary()) return;

       const auto& bytes = value.get_binary();
       std::cout << "received " << bytes.size() << " bytes\n";
   });

Binary values also work in acknowledgements. For example, an
acknowledgement-aware listener can return a binary preview:

.. code-block:: cpp

   chat->on(
       "request_preview",
       [](const std::string&, sioxx::message,
          sioxx::socket::ack_callback acknowledge) {
           if (!acknowledge) return;
           acknowledge(sioxx::json::array({sioxx::binary_message(
               std::vector<std::uint8_t>{0x01, 0x02, 0x03})}));
       });

The same API works over WebSocket and HTTP long-polling, with nested or
multiple attachments, and with either built-in parser.

Select the wire parser
----------------------

The default JSON parser interoperates with a standard Socket.IO server and
supports binary attachments as shown above. The server and client parser must
always match. Select MessagePack when the server uses
``socket.io-msgpack-parser`` or when carrying binary-heavy payloads in a single
MessagePack frame:

.. code-block:: cpp

   sioxx::client_options options;
   options.parser = sioxx::parser_kind::msgpack;

   sioxx::client client(options);
   client.connect("wss://example.com");

Use HTTP long-polling
---------------------

WebSocket is preferred and sioxx falls back to polling when the initial
WebSocket connection fails. Polling can also be forced for restrictive
networks or transport-specific testing:

.. code-block:: cpp

   sioxx::client_options options;
   options.force_http_polling = true;

   sioxx::client client(options);
   client.connect("https://example.com");

Configure TLS and request headers
---------------------------------

Add authentication or application headers to the handshake with
``extra_headers``. Keep certificate verification enabled in production:

.. code-block:: cpp

   sioxx::client_options options;
   options.extra_headers = {
       {"Authorization", "Bearer example-token"},
       {"X-Client-Version", "1.0"},
   };

   // Development with a self-signed certificate only:
   // options.verify_tls = false;

Authenticate a namespace and customize the endpoint
---------------------------------------------------

Pass authentication data when creating a namespace socket. The payload is
sent with each namespace ``CONNECT``, including reconnects. Update it before
calling ``connect()`` again when a token is refreshed:

.. code-block:: cpp

   sioxx::client_options options;
   options.engineio_path = "/realtime/";
   options.query = {
       {"client", "desktop"},
       {"version", "2"},
   };

   sioxx::client client(options);
   auto private_socket = client.socket(
       "/private", sioxx::json{{"token", "example-token"}});
   client.connect("wss://example.com");

   // Later, after authentication expires:
   private_socket->set_auth(
       sioxx::json{{"token", "refreshed-token"}});
   private_socket->disconnect();
   private_socket->connect();

``engineio_path`` defaults to ``/socket.io/``. Query keys and values are
percent-encoded; the reserved Engine.IO keys ``EIO``, ``transport``, and
``sid`` cannot be supplied by the application.

Configure reconnection
----------------------

Reconnects use capped exponential back-off with jitter:

.. code-block:: cpp

   using namespace std::chrono_literals;

   sioxx::client_options options;
   options.reconnect_attempts = 5;
   options.reconnect_delay = 1s;
   options.reconnect_delay_max = 30s;
   options.reconnect_randomization_factor = 0.5;

Run the bundled client and server
---------------------------------

The repository contains a C++ client and a matching Node.js Socket.IO server.
Build the client from the repository root:

.. code-block:: console

   $ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   $ cmake --build build --parallel

Start the server in another terminal:

.. code-block:: console

   $ cd examples/basic_client/server
   $ pnpm install
   $ pnpm start

Then run the C++ client:

.. code-block:: console

   $ ./build/sioxx_basic_client ws://localhost:3000

The example also supports ``msgpack``, ``cbor``, and ``polling`` modes. See
the `test-server guide
<https://github.com/jfayot/sioxx/tree/main/examples/basic_client/server>`_ for all
matching commands.

Build the Qt chat example
-------------------------

An optional Qt Widgets chat client demonstrates a typical GUI integration,
including namespace authentication, reconnects, event acknowledgements,
catch-all listeners, transport and parser selection, and binary attachments
with either JSON or MessagePack. Most importantly, it queues sioxx's
background-thread callbacks onto Qt's GUI thread before touching widgets.

With Qt 6 or Qt 5 installed, enable the examples and build the target. CMake
skips this target without error when Qt Widgets is unavailable:

.. code-block:: console

   $ cmake -S . -B build-qt -DSIOXX_BUILD_EXAMPLES=ON \
       -DSIOXX_BUILD_TESTS=OFF
   $ cmake --build build-qt --target sioxx_qt_chat --parallel
   $ pnpm --dir examples/qt_chat install --frozen-lockfile
   $ pnpm --dir examples/qt_chat start
   $ ./build-qt/sioxx_qt_chat

See the `Qt chat README
<https://github.com/jfayot/sioxx/tree/main/examples/qt_chat>`_ for parser and
binary-sharing instructions.
