#include <sioxx/sioxx.hpp>

int main()
{
  sioxx::client client;
  const auto socket = client.socket("/installed");
  const auto message = sioxx::make_args("installed", 1);
  return socket->nsp() == "/installed" && message.size() == 2 ? 0 : 1;
}
