#include "../runtime_key.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using plantpal::RuntimeKey;
RuntimeKey::Result send(RuntimeKey& parser, const std::string& text) {
  auto result = RuntimeKey::Result::NONE;
  for (char c : text) {
    auto next = parser.feed(c);
    if (next != RuntimeKey::Result::NONE) result = next;
  }
  return result;
}
int main() {
  RuntimeKey parser;
  assert(!parser.ready());
  assert(send(parser, "\r\n") == RuntimeKey::Result::NONE);
  for (const auto& bad : {"KEY\n", "KEY \n", "fake123\n", "key fake123\n",
                          "KEY fake 123\n", "KEY fake!123\n"}) {
    assert(send(parser, bad) == RuntimeKey::Result::REJECTED);
    assert(!parser.ready());
  }
  assert(send(parser, "KEY " + std::string(65, 'x') + "\n") == RuntimeKey::Result::REJECTED);
  assert(send(parser, "KEY DEMO") == RuntimeKey::Result::NONE);
  assert(!parser.ready());
  assert(send(parser, "1234\r\n") == RuntimeKey::Result::ACCEPTED);
  assert(std::strcmp(parser.value(), "DEMO1234") == 0);
  send(parser, "KEY REPLACEMENT\n");
  assert(std::strcmp(parser.value(), "DEMO1234") == 0);
  RuntimeKey freshBoot;
  assert(!freshBoot.ready());
  assert(send(freshBoot, "KEY " + std::string(64, 'x') + "\n") == RuntimeKey::Result::ACCEPTED);
  assert(std::strlen(freshBoot.value()) == 64);
  RuntimeKey controlChar;
  assert(send(controlChar, std::string("KEY A\0B\n", 8)) == RuntimeKey::Result::REJECTED);
  std::cout << "PASS: runtime key input, boundaries, rejection, CRLF, one-key-per-boot\n";
}
