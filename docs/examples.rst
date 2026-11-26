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

The client creates and manages the namespace socket returned by
``client.socket("/chat")``. Callbacks run on the connection's background thread.
Dispatch back to your application thread before modifying thread-affine state
such as a UI.

Use an existing Asio event loop
-------------------------------

Pass a caller-owned ``boost::asio::io_context`` to make both WebSocket and
HTTP polling networking run on an application's existing event loop:

.. code-block:: cpp

   #include <boost/asio/executor_work_guard.hpp>
   #include <boost/asio/io_context.hpp>
   #include <sioxx/sioxx.hpp>
   #include <thread>

   boost::asio::io_context io_context;
   auto work = boost::asio::make_work_guard(io_context);
   sioxx::client_options options;
   sioxx::client client(io_context, options);

   client.connect("wss://example.com");
   std::thread io_thread([&] { io_context.run(); });

   // From the application thread when ready to shut down.
   client.sync_close();
   work.reset();
   io_thread.join();

The context must outlive the client. The application runs and stops it.
Transport callbacks are serialized on an internal strand. ``sync_close()``
waits for library-owned workers and active timed acknowledgement callbacks.
Transport shutdown completes on the supplied context, where I/O and ordinary
transport callbacks may still be pending or active when ``sync_close()``
returns. Keep callback state alive while the application drains the client's
work. In this example the context is dedicated to the client, so releasing the
work guard and joining the I/O thread lets pending work finish.

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

Event buffer lifecycle
----------------------

Each namespace has an in-memory FIFO buffer shared by all ``emit()`` overloads.
The server's namespace CONNECT acknowledgement triggers its flush. Namespace
disconnects (including ``socket->disconnect()``), transport failures, and
automatic reconnects preserve unsent events and pending ACK callbacks.
Exhausting reconnect attempts also preserves them: a later ``client.connect()``
can deliver buffered work. Timed emissions keep their original deadlines
through all these states; expired entries are removed.

Explicit ``client.close()``, ``client.sync_close()``, and client destruction
discard every buffered event and pending ACK callback without notification.
Socket destruction discards its own buffer and pending ACK callbacks.
Already running callbacks may finish. After client shutdown, every ``emit()``
overload throws ``std::logic_error`` until ``client.connect()`` is called again,
including on retained sockets or sockets obtained after shutdown. Listeners and
namespace authentication remain installed; the next connection starts with an
empty buffer.

.. code-block:: cpp

   client.close();
   client.connect("ws://localhost:3000");
   chat->emit("hello", sioxx::json::array({"new session"}));

Events already handed to a transport, including an emission in progress during
shutdown, cannot be recalled. They are only retried when the retry overload
described below is used. Transport
handoff does not guarantee server delivery. Pending ACK callbacks without a
deadline can remain allocated until an ACK or shutdown. The buffer has no size
limit; use timed emissions to expire stale work. FIFO ordering is per namespace,
with concurrent calls ordered by acquisition of the socket mutex. There is no
ordering guarantee between namespaces.

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

Bound acknowledgement waiting
-----------------------------

Pass a positive duration and an outcome callback to ``emit``:

.. code-block:: cpp

   chat->emit("save", sioxx::json::array({"draft"}), std::chrono::seconds(5),
       [](sioxx::socket::ack_status status, sioxx::message reply) {
           if (status == sioxx::socket::ack_status::timeout) {
               std::cerr << "Acknowledgement deadline elapsed\n";
               return;
           }
           std::cout << reply.dump() << '\n';
       });

The callback must be nonempty. The deadline begins at ``emit``, including time
buffered before connection. Events that expire during packet preparation,
waiting for the socket lock, or in the socket buffer are discarded; already-sent
events cannot be recalled. Late acknowledgements are ignored and events are not
retried. Timeout reply data is null. Existing acknowledgement callbacks without
a duration keep their original behavior.

Namespace/transport disconnects and automatic reconnection preserve deadlines.
Explicit client ``close()`` cancels all pending acknowledgements and buffered
events without notification. ``sync_close()`` also waits for timed callbacks
already running; call it outside library callbacks. New emissions throw
``std::logic_error`` after shutdown until ``client.connect()`` is called again.
Destroying the socket also cancels outstanding deadlines without notification.
Idle deadline workers may remain allocated until their sockets are destroyed.
Each socket lazily starts one deadline worker, even with a caller-owned
``io_context``. The callback runs at most once, outside socket locks, on that
worker or the ACK delivery thread. Callbacks must not throw; dispatch UI work
to the application's UI thread.

Retry an unacknowledged event
-----------------------------

Pass a retry count after the timeout to opt in for one emission. Other
``emit()`` overloads keep their existing behavior:

.. code-block:: cpp

   chat->emit(
       "save",
       sioxx::json::array({{{"operation_id", "save-42"}, {"text", "draft"}}}),
       std::chrono::seconds(5),
       2,
       [](sioxx::socket::ack_status status, sioxx::message reply) {
           // Called once on success, or after the third attempt times out.
       });

The retry count is the maximum number of additional attempts. Each attempt has
its own timeout, including preparation, buffering, and disconnected time. A
timeout consumes an attempt even if nothing was transmitted. Zero retries is
equivalent to the timeout-only overload. There is no extra back-off delay.
Ordinary disconnects and exhausted transport reconnection attempts do not stop
these deadlines. Each retry joins the end of the namespace buffer; events
emitted later can pass an operation waiting for its ACK.

Connected retries use a separate send worker. Encoding errors from buffered
sends and background retries leave the attempt to expire normally. If
encoding fails synchronously before the pending acknowledgement expires,
``emit()`` throws and cancels that acknowledgement.

Attempts reuse the same payload and receive fresh ACK IDs. ACKs for previous
attempts are ignored, so the application receives one callback: acknowledged
with reply data, or timeout with null after the last attempt. Callbacks run on
the ACK delivery thread or the existing deadline worker, outside socket locks,
also with a caller-owned ``io_context``. They must not throw. Explicit shutdown
and socket destruction silently cancel remaining attempts. ``sync_close()``
drains running retry operations and timed callbacks; a later connection cannot
revive cancelled retries.

An earlier attempt may have reached the server even when its ACK was lost.
Use a stable application operation ID and server-side deduplication if effects
must only happen once. Different retry operations are not serialized behind
each other's ACKs. Transport reconnection is configured independently.

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

Secure WebSocket and HTTPS polling connections verify both the certificate
chain and the DNS name or IP address in the connection URL by default. For a
private CA, include it in a trusted CA bundle and point ``SSL_CERT_FILE`` at
the bundle. Setting ``verify_tls`` to ``false`` disables both checks and is
intended for controlled development only.

Use ``extra_headers`` to add authentication or application headers to the
handshake.

.. code-block:: cpp

   sioxx::client_options options;
   options.extra_headers = {
       {"Authorization", "Bearer example-token"},
       {"X-Client-Version", "1.0"},
   };

Connect through an HTTP proxy
-----------------------------

Set an HTTP proxy for WebSocket and polling connections. Optional Basic
credentials authenticate with the proxy and are not forwarded to the
Socket.IO server:

.. code-block:: cpp

   sioxx::client_options options;
   options.proxy = sioxx::proxy_options{
       "http://proxy.example.com:8080", "user", "password"};

   sioxx::client client(options);
   client.connect("wss://example.com");

Only HTTP proxy endpoints are supported. Secure origin connections are
tunnelled through the proxy before TLS is established with the origin.
Certificate verification uses the host in the Socket.IO server URL.

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

   client.set_reconnect_listener([](unsigned attempt, unsigned delay_ms) {
       // A retry was scheduled. attempt is zero-based.
   });
   client.set_reconnecting_listener([] {
       // The back-off elapsed and the retry is starting.
   });

Use ``client.opened()`` to query whether the Engine.IO handshake is currently
open. ``client.get_sessionid()`` returns the handshake session ID, or an empty
string before a session exists.

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
