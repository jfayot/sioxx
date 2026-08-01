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
