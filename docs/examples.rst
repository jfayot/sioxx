Examples
========

.. default-domain:: cpp

The snippets below cover the most common client configurations. For a
complete executable, see
`examples/basic_client.cpp
<https://github.com/jfayot/sioxx/blob/main/examples/basic_client.cpp>`_ in the
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
when the server should acknowledge the event:

.. code-block:: cpp

   chat->emit("hello", sioxx::json{{"name", "Ada"}});

   chat->emit(
       "sum",
       sioxx::json::array({1, 2, 3}),
       [](sioxx::message reply) {
           std::cout << "server replied: " << reply.dump() << '\n';
       });

Choose MessagePack
------------------

The server and client parser must match. Use MessagePack for binary-heavy
payloads or when the server uses ``socket.io-msgpack-parser``:

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

   $ cd examples/test_server
   $ pnpm install
   $ pnpm start

Then run the C++ client:

.. code-block:: console

   $ ./build/sioxx_basic_client ws://localhost:3000

The example also supports ``msgpack``, ``cbor``, and ``polling`` modes. See
the `test-server guide
<https://github.com/jfayot/sioxx/tree/main/examples/test_server>`_ for all
matching commands.
