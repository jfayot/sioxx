#include <sioxx/sioxx.hpp>

int main()
{
  sioxx::client client;
  const auto message = sioxx::make_args("installed", 1);
  return message.size() == 2 ? 0 : 1;
}
