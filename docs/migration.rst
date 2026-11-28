Migration and upgrade guide
===========================

.. default-domain:: cpp

The following sections cover applications moving from the official
`socket.io-client-cpp <https://github.com/socketio/socket.io-client-cpp>`_
library to ``sioxx``. See :ref:`upgrade-to-sioxx-1-0` for upgrades from an
earlier sioxx release. The client/socket split and most method names are
deliberately familiar. The main source change is replacing the
``sio::message`` class hierarchy with ``nlohmann::json`` values.

Before changing code, confirm that the server speaks Engine.IO 4 (normally a
Socket.IO 3.x or 4.x server). ``sioxx`` does not provide the ``allowEIO3``
compatibility mode needed by older servers.

Replace the dependency
----------------------

``sioxx`` requires C++17. After installing it, replace the old include and
CMake target:

.. code-block:: cmake

   # Before
   target_link_libraries(my_app PRIVATE sioclient)

   # After
   find_package(sioxx CONFIG REQUIRED)
   target_link_libraries(my_app PRIVATE sioxx::sioxx)

Use the aggregate public header in application code:

.. code-block:: cpp

   // Before
   #include <sio_client.h>

   // After
   #include <sioxx/sioxx.hpp>

The exported target supplies the required include and link settings. For a
static sioxx installation, its package configuration also locates the OpenSSL
and Threads dependencies.

Port a small client
-------------------

The following ``socket.io-client-cpp`` client sends two arguments and reads
an acknowledgement:

.. code-block:: cpp

   sio::client client;
   client.set_reconnect_attempts(5);
   client.set_reconnect_delay(1000);

   auto socket = client.socket("/chat");
   socket->on("message", [](sio::event& event) {
       const auto& args = event.get_messages();
       std::cout << args.at(0)->get_string() << '\n';
   });

   client.set_socket_open_listener([socket](const std::string& nsp) {
       if (nsp != "/chat") return;

       sio::message::list args;
       args.push("Ada");
       args.push(sio::int_message::create(42));
       socket->emit("hello", args, [](const sio::message::list& reply) {
           std::cout << reply.at(0)->get_string() << '\n';
       });
   });

   client.connect("https://example.com");

The equivalent ``sioxx`` code is:

.. code-block:: cpp

   #include <chrono>
   #include <iostream>
   #include <memory>
   #include <sioxx/sioxx.hpp>

   using namespace std::chrono_literals;

   sioxx::client_options options;
   options.reconnect_attempts = 5;
   options.reconnect_delay = 1s;

   sioxx::client client(options);
   auto socket = client.socket("/chat");

   socket->on("message", [](const std::string&, sioxx::message args) {
       std::cout << args.at(0).get<std::string>() << '\n';
   });

   std::weak_ptr<sioxx::socket> weak_socket = socket;
   socket->on_connect([weak_socket] {
       if (auto socket = weak_socket.lock()) {
           socket->emit(
               "hello", sioxx::json::array({"Ada", 42}),
               [](sioxx::message reply) {
                   std::cout << reply.at(0).get<std::string>() << '\n';
               });
       }
   });

   client.connect("https://example.com");

Create sockets and register their listeners before ``client.connect()``. This
ensures that namespace lifecycle and early server events cannot arrive before
their handlers are installed.

Translate message values
------------------------

``sioxx::message`` and ``sioxx::json`` are both aliases for
``nlohmann::json``. Values are owned directly, so there is no ``message::ptr``
and no factory hierarchy.

.. list-table:: Common message conversions
   :header-rows: 1
   :widths: 42 58

   * - ``socket.io-client-cpp``
     - ``sioxx``
   * - ``sio::null_message::create()``
     - ``nullptr`` or ``sioxx::json()``
   * - ``sio::bool_message::create(value)``
     - ``sioxx::json(value)``
   * - ``sio::int_message::create(value)``
     - ``sioxx::json(value)``
   * - ``sio::double_message::create(value)``
     - ``sioxx::json(value)``
   * - ``sio::string_message::create(value)``
     - ``sioxx::json(value)``
   * - ``sio::array_message::create()``
     - ``sioxx::json::array()``
   * - ``sio::object_message::create()``
     - ``sioxx::json::object()``
   * - ``message->get_flag()``
     - ``value.is_string()``, ``is_number()``, ``is_array()``, and similar
   * - ``message->get_string()``
     - ``value.get<std::string>()``
   * - ``message->get_int()``
     - ``value.get<std::int64_t>()``
   * - ``message->get_vector()``
     - iterate over ``value`` after checking ``value.is_array()``
   * - ``message->get_map()``
     - use ``value.at("key")`` or iterate over ``value.items()``
   * - ``sio::message::list``
     - ``sioxx::json::array()`` or ``sioxx::make_args(...)``

Object construction becomes much shorter:

.. code-block:: cpp

   // Before
   auto user = sio::object_message::create();
   user->get_map()["name"] = sio::string_message::create("Ada");
   user->get_map()["active"] = sio::bool_message::create(true);

   // After
   sioxx::json user = {{"name", "Ada"}, {"active", true}};

Listener payloads and acknowledgement payloads are always JSON arrays holding
all Socket.IO arguments. Preserve the array when an event has multiple
arguments:

.. code-block:: cpp

   socket->on("position", [](const std::string&, sioxx::message args) {
       if (args.size() < 2) return;
       const double x = args.at(0).get<double>();
       const double y = args.at(1).get<double>();
       // Use x and y.
   });

For emission, passing an array expands its elements into separate Socket.IO
arguments. Passing a scalar or object sends one argument. For example,
``emit("position", sioxx::json::array({10, 20}))`` sends two arguments, while
``emit("position", sioxx::json{{"x", 10}, {"y", 20}})`` sends one object.

Create binary values with ``sioxx::binary_message(...)`` and place them at any
depth in an event or acknowledgement payload. Both the default JSON parser and
the optional MessagePack parser support binary values; the selected parser
must still match the server.

Port acknowledgements
---------------------

An outgoing acknowledgement callback changes only from
``sio::message::list`` to ``sioxx::message``:

.. code-block:: cpp

   socket->emit(
       "save", sioxx::json{{"id", 42}},
       [](sioxx::message reply) {
           if (!reply.empty() && reply.at(0) == "saved") {
               // The server acknowledged the event.
           }
       });

For an event that the client must acknowledge, use the acknowledgement-aware
``on`` overload. It replaces ``event.need_ack()`` and
``event.put_ack_message(...)``:

.. code-block:: cpp

   socket->on(
       "question",
       [](const std::string&, sioxx::message args,
          sioxx::socket::ack_callback acknowledge) {
           if (acknowledge) {
               acknowledge(sioxx::json::array({"answer", 42}));
           }
       });

``acknowledge`` is empty when the server did not request a response. A valid
callback sends at most one response, even if application code invokes it more
than once.

Move connection settings into client_options
--------------------------------------------

Most settings that were passed to ``connect()`` or changed through setters are
fixed when a ``sioxx::client`` is constructed:

.. list-table:: Connection configuration
   :header-rows: 1
   :widths: 44 56

   * - ``socket.io-client-cpp``
     - ``sioxx``
   * - ``set_reconnect_attempts(n)``
     - ``options.reconnect_attempts = n``
   * - ``set_reconnect_delay(ms)``
     - ``options.reconnect_delay = std::chrono::milliseconds(ms)``
   * - ``set_reconnect_delay_max(ms)``
     - ``options.reconnect_delay_max = std::chrono::milliseconds(ms)``
   * - ``connect(uri, query)``
     - set ``options.query``, then call ``connect(uri)``
   * - ``connect(uri, query, headers)``
     - set ``options.query`` and ``options.extra_headers``
   * - root authentication passed to ``connect``
     - ``client.socket("/", auth)`` before ``connect``
   * - namespace authentication passed when obtaining a socket
     - ``client.socket("/private", auth)``
   * - ``set_proxy_basic_auth(uri, username, password)``
     - set ``options.proxy`` to a ``sioxx::proxy_options`` value

For example:

.. code-block:: cpp

   sioxx::client_options options;
   options.query = {{"device", "desktop"}};
   options.extra_headers = {{"Authorization", "Bearer example-token"}};
   options.engineio_path = "/realtime/";

   sioxx::client client(options);
   auto private_socket = client.socket(
       "/private", sioxx::json{{"token", "example-token"}});
   client.connect("wss://example.com");

Do not embed an application path or query in the URI passed to ``connect()``.
``client_options::engineio_path`` and ``client_options::query`` replace them;
the path defaults to ``/socket.io/``. The reserved ``EIO``, ``transport``, and
``sid`` query keys are managed by the library.

Update lifecycle handling
-------------------------

Client-level listener names are similar, but close and error details differ:

.. code-block:: cpp

   client.set_open_listener([] {
       // Engine.IO is connected. Use socket->on_connect() for a namespace.
   });
   client.set_close_listener([](const std::string& reason) {
       std::cerr << "closed: " << reason << '\n';
   });
   client.set_fail_listener([] {
       // The configured reconnect attempts were exhausted.
   });
   client.set_reconnect_listener([](unsigned attempt, unsigned delay_ms) {
       // A retry was scheduled. attempt is zero-based.
   });
   client.set_reconnecting_listener([] {
       // The back-off elapsed and the retry is starting.
   });
   client.set_error_listener([](const std::string& error) {
       std::cerr << error << '\n';
   });

Use per-socket callbacks instead of the old client-wide socket open/close
listeners:

.. code-block:: cpp

   socket->on_connect([] { /* /chat connected */ });
   socket->on_disconnect([](const std::string& reason) {
       // /chat disconnected.
   });

The remaining namespace method translations are direct:

.. list-table:: Namespace operations
   :header-rows: 1
   :widths: 44 56

   * - ``socket.io-client-cpp``
     - ``sioxx``
   * - ``client.socket("")``
     - ``client.socket("/")`` (an empty string is also normalized to ``/``)
   * - ``socket->get_namespace()``
     - ``socket->nsp()``
   * - ``socket->close()``
     - ``socket->disconnect()``
   * - reconnect a namespace
     - ``socket->connect()``
   * - ``socket->off(name)`` / ``off_all()``
     - unchanged
   * - ``socket->on_any(listener)``
     - unchanged
   * - ``client.close()``
     - unchanged
   * - ``client.sync_close()``
     - unchanged

``client.close()`` requests shutdown without waiting. ``client.sync_close()``
also waits for library-owned network workers and active timed acknowledgement
operations. Use ``client.close()`` from library callbacks. Transport callbacks
can run on a sioxx worker or an application-supplied I/O context. Continue
dispatching to the UI or application executor before accessing thread-affine
state.

Namespace disconnect listeners and reconnect listeners may call
``client.close()`` to cancel further reconnect attempts. From another thread,
``client.sync_close()`` wakes a reconnect worker waiting on its back-off delay
and waits for the worker to finish, including any callback registered with
``set_reconnecting_listener()``.

Client shutdown now discards all buffered events and pending acknowledgement
callbacks, including emissions without a timeout. Previously only timed
emissions were cancelled. Every ``emit()`` overload now throws
``std::logic_error`` after shutdown until ``client.connect()`` is called again.
Move emissions for the next session after that call. Namespace disconnects,
transport failures, and reconnect exhaustion preserve unsent events; timed
deadlines continue to run. Events already handed to the transport are not
automatically replayed unless the new per-emission retry overload is used.
Pass ``emit(event, data, timeout, retries, callback)`` to allow that many
additional attempts. The timeout applies to each attempt, including offline
time. Existing overloads retain their timeout and delivery behavior.

Applications that passed an ``asio::io_context`` to socket.io-client-cpp can
retain the same ownership model:

.. code-block:: cpp

   boost::asio::io_context io_context;
   sioxx::client client(io_context, options);

The supplied context must outlive the client. The application runs and stops
it. ``client.sync_close()`` requests WebSocket or HTTP polling shutdown on
that context and waits for sioxx-owned workers and active timed
acknowledgement callbacks. Pending I/O and ordinary transport callbacks can
outlive the call. Keep callback state alive until the application has drained
the client's work on the context. If the context has stopped, restart it and
resume dispatch to complete transport shutdown.

The connection-state and session accessors translate directly:

.. code-block:: cpp

   if (client.opened()) {
       std::cout << "Engine.IO session: " << client.get_sessionid() << '\n';
   }

``get_sessionid()`` returns a value rather than a reference so callers can
safely retain the result while a connection transition replaces the session.

Obtain namespace sockets from the client
----------------------------------------

Replace direct construction of ``sioxx::socket`` with
``client.socket("/chat")``. The constructor and the packet-delivery hooks
``dispatch_event()``, ``dispatch_ack()``, and ``mark_connected()`` are now
private. Register listeners and acknowledgement callbacks to receive server
events and observe connection state.

Check unsupported or different features
---------------------------------------

Do not treat the migration as a namespace-only rename if the old application
uses one of these APIs:

* Built-in log-level setters have no direct equivalents. Use the lifecycle and
  error listeners with the application's logging.
* ``sioxx`` can use HTTP long-polling and automatically falls back to it when
  the initial WebSocket connection fails. Set
  ``options.force_http_polling = true`` when polling must be used from the
  start.

Migration checklist
-------------------

#. Change the project to C++17 and link ``sioxx::sioxx``.
#. Replace ``sio::`` types and message factories with ``sioxx::json`` values.
#. Keep every event's arguments in a JSON array when receiving or sending
   multiple arguments.
#. Move query parameters, headers, authentication, reconnection, and path
   settings into ``client_options`` or the relevant namespace socket.
#. Replace incoming acknowledgement mutation with the acknowledgement-aware
   listener overload.
#. Audit the unsupported APIs above.
#. Run the application against the same Socket.IO server and verify connect,
   namespace authentication, representative events, acknowledgements,
   reconnection, and clean shutdown.

See :doc:`examples` for more sioxx patterns and :doc:`api` for the complete
public API.

.. _upgrade-to-sioxx-1-0:

Upgrade from sioxx 0.2.0 to 1.0
-------------------------------

Apply the changes below in order. If you already use 0.3.0 or 0.4.0, start
with the changes introduced after that version.

Changes in 0.3.0
~~~~~~~~~~~~~~~~

* Keep one owner for each ``client``. Clients are non-copyable. Share the
  namespace handles returned by ``client.socket()``. Construct a default
  client with ``sioxx::client client;`` or pass a ``client_options`` object
  to the constructor. Set options by name if you previously used positional
  aggregate initialization, since ``client_options`` gained a proxy field.
* Use ``set_open_listener()`` for the Engine.IO handshake and
  ``socket::on_connect()`` when work depends on the server accepting a
  namespace. The open listener now fires even when the client never requests
  the root namespace.
* Use ``close()`` to request shutdown and ``sync_close()`` to wait for the
  library's workers. Call ``sync_close()`` and destroy the client outside
  library callbacks. With a caller-owned Asio context, also drain pending
  transport work on that context before releasing callback state.
* Update scripts that use ``examples/test_server`` or the example sources at
  the root of ``examples``. The basic client, CBOR parser, and matching server
  now live under ``examples/basic_client``.

Changes in 0.4.0
~~~~~~~~~~~~~~~~

* Replace direct socket construction with ``client.socket()``. Replace calls
  to ``dispatch_event()``, ``dispatch_ack()``, or ``mark_connected()`` with
  the public listener, acknowledgement, and namespace control APIs.
* Move emissions for the next session after the next explicit
  ``client.connect()``. Explicit shutdown drops buffered events and pending
  acknowledgements. All ``emit()`` overloads throw ``std::logic_error`` until
  ``client.connect()`` is called again. Applications that need queued work to
  survive shutdown must store it separately.
* Use a server URL whose DNS name or IP address matches the TLS certificate.
  For a private CA, include it in the CA bundle selected by ``SSL_CERT_FILE``.
  Server identity is checked even through an HTTP proxy.

Changes in 1.0
~~~~~~~~~~~~~~

* Keep incoming payloads within the :doc:`resource_limits` for the selected
  parser and transport. Binary JSON packets and nested JSON and MessagePack
  values that exceed their limits are discarded. An invalid Engine.IO
  handshake reports an error instead of opening a session.
* Update installed-package consumers to
  ``find_package(sioxx 1.0 CONFIG REQUIRED)``. From 1.0, compatible CMake
  packages match within the same major version. Rebuild and relink consumers
  with matching JSON versions and build settings when upgrading the shared
  library. Where supported, its versioned file name uses the full release
  version from 1.0 onward.
