#ifndef RFL_YAML_READER_HPP_
#define RFL_YAML_READER_HPP_

#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "../Result.hpp"
#include "../always_false.hpp"
#include "../thirdparty/fkYAML/node.hpp"

namespace rfl {
namespace yaml {

struct Reader {
  using Node = ::fkyaml::node;
  using InputArrayType = Node*;
  using InputObjectType = Node*;
  using InputVarType = Node*;

  template <class T>
  static constexpr bool has_custom_constructor =
      (requires(InputVarType var) { T::from_yaml_obj(var); });

  Reader() = default;

  explicit Reader(const std::string_view& /*_yaml_str*/) noexcept {}

  rfl::Result<InputVarType> get_field_from_array(
      const size_t _idx, const InputArrayType _arr) const noexcept {
    if (!_arr || !_arr->is_sequence() || _idx >= _arr->size()) {
      return error("Index " + std::to_string(_idx) + " out of bounds.");
    }
    return &((*_arr)[_idx]);
  }

  rfl::Result<InputVarType> get_field_from_object(
      const std::string& _name, const InputObjectType& _obj) const noexcept {
    if (!_obj || !_obj->is_mapping() || !_obj->contains(_name)) {
      return error("Object contains no field named '" + _name + "'.");
    }
    return &((*_obj)[_name]);
  }

  bool is_empty(const InputVarType& _var) const noexcept {
    return !_var || _var->is_null();
  }

  template <class T>
  rfl::Result<T> to_basic_type(const InputVarType& _var) const noexcept {
    if (!_var) {
      return error("Cannot cast an empty YAML node.");
    }

    try {
      if constexpr (std::is_same<std::remove_cvref_t<T>, std::string>()) {
        if (_var->is_string()) {
          return _var->template get_value<std::string>();
        }
        return ::fkyaml::node::serialize(*_var);

      } else if constexpr (std::is_same<std::remove_cvref_t<T>, bool>()) {
        return _var->template get_value<bool>();

      } else if constexpr (std::is_floating_point<std::remove_cvref_t<T>>()) {
        if (_var->is_float_number()) {
          return static_cast<T>(_var->as_float());
        }
        if (_var->is_integer()) {
          return static_cast<T>(_var->as_int());
        }
        return _var->template get_value<T>();

      } else if constexpr (std::is_integral<std::remove_cvref_t<T>>()) {
        if (_var->is_integer()) {
          return static_cast<T>(_var->as_int());
        }
        return static_cast<T>(_var->template get_value<int64_t>());

      } else {
        static_assert(rfl::always_false_v<T>, "Unsupported type.");
      }
    } catch (std::exception& e) {
      return error(e.what());
    }
  }

  rfl::Result<InputArrayType> to_array(
      const InputVarType& _var) const noexcept {
    if (!_var || !_var->is_sequence()) {
      return error("Could not cast to sequence!");
    }
    return _var;
  }

  template <class ArrayReader>
  std::optional<Error> read_array(const ArrayReader& _array_reader,
                                  const InputArrayType& _arr) const noexcept {
    if (!_arr || !_arr->is_sequence()) {
      return Error("Could not read YAML sequence.");
    }
    for (auto& node : _arr->as_seq()) {
      const auto err = _array_reader.read(&node);
      if (err) {
        return err;
      }
    }
    return std::nullopt;
  }

  template <class ObjectReader>
  std::optional<Error> read_object(const ObjectReader& _object_reader,
                                   const InputObjectType& _obj) const noexcept {
    if (!_obj || !_obj->is_mapping()) {
      return Error("Could not read YAML map.");
    }
    for (auto& pair : _obj->as_map()) {
      try {
        const auto k = pair.first.template get_value<std::string>();
        _object_reader.read(std::string_view(k), &pair.second);
      } catch (std::exception&) {
        continue;
      }
    }
    return std::nullopt;
  }

  rfl::Result<InputObjectType> to_object(
      const InputVarType& _var) const noexcept {
    if (!_var || !_var->is_mapping()) {
      return error("Could not cast to map!");
    }
    return _var;
  }

  template <class T>
  rfl::Result<T> use_custom_constructor(
      const InputVarType _var) const noexcept {
    try {
      return T::from_yaml_obj(_var);
    } catch (std::exception& e) {
      return error(e.what());
    }
  }
};

}  // namespace yaml
}  // namespace rfl

#endif
