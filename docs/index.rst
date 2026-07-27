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

The established C++ Socket.IO client carries its own JSON and WebSocket
stacks and predates modern target-based CMake. ``sioxx`` was created for
applications that already use Boost and nlohmann/json and should not need a
second set of overlapping dependencies.

Its public API keeps the small, event-driven shape expected from a Socket.IO
client while rebuilding the protocol and transport layers around current C++
libraries. The design and implementation trade-offs are described in
`sioxx — a modern C++ socket.io client
<https://dev.to/jfayot/sioxx-a-modern-c-socketio-client-nlohmannjson-boostbeast-json-or-messagepack-1hj1>`_.

Features
--------

* Engine.IO v4 and Socket.IO namespace support.
* WebSocket and secure WebSocket transports through Boost.Beast and OpenSSL.
* HTTP long-polling fallback, or a polling-only mode when required.
* JSON and MessagePack wire protocols selectable for each client.
* Custom parser strategies for other wire formats.
* Event listeners, event emission, acknowledgements, and lifecycle callbacks.
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
