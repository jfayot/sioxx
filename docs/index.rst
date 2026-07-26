sioxx API Reference
===================

.. This page is populated from Doxygen XML through Breathe.
   The Breathe project name used below must match breathe_projects in conf.py.

.. default-domain:: cpp

``sioxx`` is a C++ Socket.IO client built on Boost.Asio, Boost.Beast and
nlohmann/json. It implements Engine.IO v4 and supports JSON, MessagePack,
WebSocket, TLS, HTTP long-polling, namespaces, acknowledgements and automatic
reconnection.

.. contents:: API contents
   :local:
   :depth: 2

Client
------

The :cpp:class:`sioxx::client` class is the main entry point. Configure it
with :cpp:struct:`sioxx::client_options`, obtain namespace sockets with
:cpp:func:`sioxx::client::socket`, and then connect to the server.

.. doxygenenum:: sioxx::parser_kind
   :project: sioxx

.. doxygenstruct:: sioxx::client_options
   :project: sioxx
   :members:

.. doxygenclass:: sioxx::client
   :project: sioxx
   :members:
   :protected-members:

Socket
------

A :cpp:class:`sioxx::socket` represents a single Socket.IO namespace.
It provides event listeners, event emission, acknowledgements and namespace
connection lifecycle callbacks.

.. doxygenclass:: sioxx::socket
   :project: sioxx
   :members:
   :protected-members:

Messages
--------

Socket.IO values are represented directly with ``nlohmann::json``.
``sioxx::json`` and ``sioxx::message`` are aliases for
``nlohmann::json``, while ``sioxx::message_list`` represents an array of
event arguments.

.. doxygenfile:: message.hpp
   :project: sioxx

Parser extension API
--------------------

Applications can provide a custom wire-format parser by deriving from
:cpp:class:`sioxx::parser_base` and assigning a factory to
:cpp:member:`sioxx::client_options::parser_factory`.

.. doxygenenum:: sioxx::packet_type
   :project: sioxx

.. doxygenstruct:: sioxx::packet
   :project: sioxx
   :members:

.. doxygentypedef:: sioxx::frame_writer
   :project: sioxx

.. doxygenclass:: sioxx::parser_base
   :project: sioxx
   :members:
   :protected-members:

Related links
-------------

* `Source repository <https://github.com/jfayot/sioxx>`_
* `Issue tracker <https://github.com/jfayot/sioxx/issues>`_
