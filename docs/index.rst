sioxx
=====

.. default-domain:: cpp

A modern Socket.IO client for C++
---------------------------------

``sioxx`` is a C++ Socket.IO client built on Boost.Asio, Boost.Beast and
nlohmann/json. It speaks Engine.IO v4 and gives C++ applications the familiar
Socket.IO model: connect a client, open namespace sockets, listen for events,
emit values, and handle acknowledgements.

.. code-block:: cpp

   sioxx::client client;
   auto chat = client.socket("/chat");

   chat->on("message", [](const std::string&, sioxx::message data) {
       std::cout << data.dump() << '\n';
   });

   client.connect("wss://example.com");

Why sioxx?
----------

The established C++ Socket.IO client relies on WebSocket++, standalone Asio,
and RapidJSON. ``sioxx`` is intended for applications that already use
Boost.Asio, Boost.Beast, and nlohmann/json, avoiding additional dependencies
with overlapping functionality.

Its public API retains the compact, event-driven model expected from a
Socket.IO client, while its protocol and transport layers are built around
these libraries and modern C++20. The design choices and implementation
trade-offs are discussed in `sioxx — a modern C++ Socket.IO client
<https://dev.to/jfayot/sioxx-a-modern-c-socketio-client-nlohmannjson-boostbeast-json-or-messagepack-1hj1>`_.

Features
--------

* Engine.IO v4 and Socket.IO namespace support.
* WebSocket and secure WebSocket transports through Boost.Beast and OpenSSL.
* HTTP long-polling fallback, or a polling-only mode when required.
* JSON and MessagePack wire protocols selectable for each client.
* Custom parser strategies for other wire formats.
* Event listeners, event emission, acknowledgements, and lifecycle callbacks.
* Namespace authentication, custom Engine.IO paths, and handshake query
  parameters.
* Configurable reconnection with capped exponential back-off and jitter.
* Modern CMake targets, installation, and downstream ``find_package(sioxx)``.

Architecture
------------

The public API separates Socket.IO packet handling from the Engine.IO
connection and its concrete transports. A parser strategy translates between
Socket.IO packets and Engine.IO message frames, while namespace sockets route
events and acknowledgements back to the application.

.. graphviz:: _diagrams/architecture.dot
   :alt: Layered architecture of the sioxx library
   :align: center

Roadmap
-------

``sioxx`` currently focuses on the core Socket.IO client functionality. The
following roadmap outlines possible extensions inspired by features available
in the JavaScript implementation. It is indicative rather than a commitment to
specific releases or delivery dates.

Client protocol completeness
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* Complete support for binary attachments with the default JSON parser,
  including ``BINARY_EVENT`` and ``BINARY_ACK`` packet reconstruction.
* Upgrade an established HTTP long-polling connection to WebSocket.
* Try alternative transports when the preferred transport cannot connect.
* Investigate WebTransport support once there is sufficient demand and suitable
  support in the underlying C++ libraries.

Client reliability
~~~~~~~~~~~~~~~~~~

* Configurable acknowledgement timeouts.
* Automatic retries for events that are not acknowledged.
* Connection-state recovery, including restoration of the client session and
  reception of packets missed during a temporary disconnection.
* More control over reconnection attempts, delays, backoff and jitter.
* Offline buffering and optional volatile events that may be discarded while
  the client is disconnected.

Client API
~~~~~~~~~~

* Catch-all listeners for incoming and outgoing events.
* Additional connection options for authentication, query parameters, headers,
  proxies and transport selection.
* Improved connection and transport diagnostics.
* Coroutine-friendly asynchronous APIs alongside the existing callback-based
  interface.

Socket.IO server
~~~~~~~~~~~~~~~~

A native C++ Socket.IO server is a longer-term objective. Its development could
progress incrementally:

* Engine.IO v4 server support over WebSocket and HTTP long-polling.
* Socket.IO packet handling with JSON and MessagePack parsers.
* Events, acknowledgements and namespaces.
* Rooms and broadcasting, including exclusion and targeting operators.
* Connection and packet middleware for authentication, authorization, logging
  and rate limiting.
* Connection-state recovery and missed-packet delivery.
* A pluggable adapter interface for multi-process or distributed deployments.

The first server version would target single-process applications. Compatibility
with the JavaScript server's distributed adapters and administration tooling
would be considered separately once the core protocol and API are stable.

Where to go next
----------------

Read :doc:`examples` for complete usage patterns or go directly to the
:doc:`api` reference.

.. toctree::
   :hidden:
   :maxdepth: 2

   Home <self>
   Examples <examples>
   API <api>
