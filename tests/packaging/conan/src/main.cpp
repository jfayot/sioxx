#include <sioxx/sioxx.hpp>

int main()
{
  sioxx::client client;
  const auto message = sioxx::make_args("conan", 2);
  return message.size() == 2 ? 0 : 1;
}
