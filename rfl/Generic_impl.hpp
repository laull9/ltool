#ifndef RFL_GENERIC_IMPL_HPP_
#define RFL_GENERIC_IMPL_HPP_

/*

MIT License

Copyright (c) 2023-2024 Code17 GmbH

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "rfl/Generic.hpp"

namespace rfl {

inline Generic::Generic() : value_(std::nullopt) {}

inline Generic::Generic(Generic&& _other) noexcept = default;

inline Generic::Generic(const Generic& _other) = default;

inline Generic::Generic(const VariantType& _value) : value_(_value) {}

inline Generic::Generic(VariantType&& _value) noexcept : value_(std::move(_value)) {}

inline Generic::Generic(const ReflectionType& _r) : value_(from_reflection_type(_r)) {}

inline Generic::~Generic() = default;
inline Generic::VariantType Generic::from_reflection_type(
    const ReflectionType& _r) noexcept {
  if (!_r) {
    return std::nullopt;
  } else {
    return std::visit([](const auto& _v) -> VariantType { return _v; }, *_r);
  }
}
inline bool Generic::is_null() const noexcept {
  return std::get_if<std::nullopt_t>(&value_) && true;
}

inline Generic& Generic::operator=(const VariantType& _value) {
  value_ = _value;
  return *this;
}

inline Generic& Generic::operator=(VariantType&& _value) noexcept {
  value_ = std::move(_value);
  return *this;
}

inline Generic& Generic::operator=(const Generic& _other) = default;

inline Generic& Generic::operator=(Generic&& _other) = default;
inline Generic::ReflectionType Generic::reflection() const noexcept {
  return std::visit([](const auto& _v) -> ReflectionType { return _v; },
                    value_);
}

}  // namespace rfl

#endif
