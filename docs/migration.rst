Migrating from socket.io-client-cpp
===================================

.. default-domain:: cpp

This guide is for applications moving from the official
`socket.io-client-cpp <https://github.com/socketio/socket.io-client-cpp>`_
library to ``sioxx``. The client/socket split and most method names are
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
       // Engine.IO and the root namespace are connected.
   });
   client.set_close_listener([](const std::string& reason) {
       std::cerr << "closed: " << reason << '\n';
   });
   client.set_fail_listener([] {
       // The configured reconnect attempts were exhausted.
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

Callbacks run on sioxx's connection background thread. Continue dispatching
to the UI or application executor before accessing thread-affine state.

Check unsupported or different features
---------------------------------------

Do not treat the migration as a namespace-only rename if the old application
uses one of these APIs:

* ``opened()``, ``get_sessionid()``, and ``sync_close()`` have no direct
  equivalents. Track application connection state from lifecycle callbacks and
  use ``close()`` for shutdown.
* Reconnecting/reconnect listeners and built-in log-level setters have no
  direct equivalents. Use the open, close, fail, and error listeners with the
  application's logging.
* Proxy configuration and an application-supplied ``asio::io_context`` are not
  currently exposed.
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
