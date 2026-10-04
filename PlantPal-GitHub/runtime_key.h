#pragma once
#include <cstddef>
#include <cstring>

namespace plantpal {
// Runtime-only credential input. No echo, file, EEPROM, or NVS writes.
// The browser/serial input is still sensitive and is NOT a password field.
class RuntimeKey {
 public:
  enum class Result { NONE, ACCEPTED, REJECTED };
  bool ready() const { return key_[0] != '\0'; }
  const char* value() const { return key_; }

  Result feed(char c) {
    if (ready()) return Result::NONE; // One key per boot; restart to replace it.
    if (c != '\r' && c != '\n') {
      if (length_ < sizeof(line_) - 1) line_[length_++] = c;
      else overflow_ = true;
      return Result::NONE;
    }
    if (length_ == 0 && !overflow_) return Result::NONE;
    bool valid = !overflow_ && length_ > 4 &&
        std::memcmp(line_, "KEY ", 4) == 0;
    for (std::size_t i = 4; valid && i < length_; ++i) {
      const char ch = line_[i];
      valid = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z');
    }
    if (valid) {
      std::memcpy(key_, line_ + 4, length_ - 4);
      key_[length_ - 4] = '\0';
    }
    // Clear the temporary line, including rejected input.
    volatile char* buffer = line_;
    for (std::size_t i = 0; i < sizeof(line_); ++i) buffer[i] = 0;
    length_ = 0;
    overflow_ = false;
    return valid ? Result::ACCEPTED : Result::REJECTED;
  }

 private:
  char key_[65] = {};
  char line_[69] = {}; // "KEY " + up to 64 alphanumeric characters + NUL
  std::size_t length_ = 0;
  bool overflow_ = false;
};
} // namespace plantpal
