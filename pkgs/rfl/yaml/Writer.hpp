#ifndef RFL_YAML_WRITER_HPP_
#define RFL_YAML_WRITER_HPP_

#include <string>
#include <string_view>
#include <type_traits>

#include "../Ref.hpp"
#include "../always_false.hpp"
#include "../common.hpp"
#include "../thirdparty/fkYAML/node.hpp"

namespace rfl::yaml {

class RFL_API Writer {
 public:
  enum Flags {
    no_flags = 0,
    string_multiline_literal = 1,
    string_all_literal = 2
  };

  struct YAMLArray {
    ::fkyaml::node* val_;
  };

  struct YAMLObject {
    ::fkyaml::node* val_;
  };

  struct YAMLVar {};

  using OutputArrayType = YAMLArray;
  using OutputObjectType = YAMLObject;
  using OutputVarType = YAMLVar;

  explicit Writer(::fkyaml::node* _root, Flags _flags = no_flags)
      : root_(_root), flags_(_flags) {}

  ~Writer() = default;

  OutputArrayType array_as_root(const size_t /*_size*/) const {
    *root_ = ::fkyaml::node::sequence();
    return OutputArrayType{root_};
  }

  OutputObjectType object_as_root(const size_t /*_size*/) const {
    *root_ = ::fkyaml::node::mapping();
    return OutputObjectType{root_};
  }

  OutputVarType null_as_root() const {
    *root_ = nullptr;
    return OutputVarType{};
  }

  template <class T>
  OutputVarType value_as_root(const T& _var) const {
    *root_ = to_node(_var);
    return OutputVarType{};
  }

  OutputArrayType add_array_to_array(const size_t /*_size*/,
                                     OutputArrayType* _parent) const {
    auto& seq = _parent->val_->as_seq();
    seq.emplace_back(::fkyaml::node::sequence());
    return OutputArrayType{&seq.back()};
  }

  OutputArrayType add_array_to_object(const std::string_view& _name,
                                      const size_t /*_size*/,
                                      OutputObjectType* _parent) const {
    auto& child = (*_parent->val_)[std::string(_name)];
    child = ::fkyaml::node::sequence();
    return OutputArrayType{&child};
  }

  void add_comment_to_array(const std::string_view& /*_comment*/,
                            OutputArrayType* /*_parent*/) const {}

  void add_comment_to_object(const std::string_view& /*_comment*/,
                             OutputObjectType* /*_parent*/) const {}

  OutputObjectType add_object_to_array(const size_t /*_size*/,
                                       OutputArrayType* _parent) const {
    auto& seq = _parent->val_->as_seq();
    seq.emplace_back(::fkyaml::node::mapping());
    return OutputObjectType{&seq.back()};
  }

  OutputObjectType add_object_to_object(const std::string_view& _name,
                                        const size_t /*_size*/,
                                        OutputObjectType* _parent) const {
    auto& child = (*_parent->val_)[std::string(_name)];
    child = ::fkyaml::node::mapping();
    return OutputObjectType{&child};
  }

  template <class T>
  OutputVarType add_value_to_array(const T& _var,
                                   OutputArrayType* _parent) const {
    _parent->val_->as_seq().emplace_back(to_node(_var));
    return OutputVarType{};
  }

  template <class T>
  OutputVarType add_value_to_object(const std::string_view& _name,
                                    const T& _var,
                                    OutputObjectType* _parent) const {
    (*_parent->val_)[std::string(_name)] = to_node(_var);
    return OutputVarType{};
  }

  OutputVarType add_null_to_array(OutputArrayType* _parent) const {
    _parent->val_->as_seq().emplace_back(nullptr);
    return OutputVarType{};
  }

  OutputVarType add_null_to_object(const std::string_view& _name,
                                   OutputObjectType* _parent) const {
    (*_parent->val_)[std::string(_name)] = nullptr;
    return OutputVarType{};
  }

  void end_array(OutputArrayType* /*_arr*/) const {}

  void end_object(OutputObjectType* /*_obj*/) const {}

 private:
  template <class T>
  ::fkyaml::node to_node(const T& _var) const {
    if constexpr (std::is_same<std::remove_cvref_t<T>, std::string>()) {
      return ::fkyaml::node(_var);
    } else if constexpr (std::is_same<std::remove_cvref_t<T>, bool>()) {
      return ::fkyaml::node(_var);
    } else if constexpr (std::is_floating_point<std::remove_cvref_t<T>>()) {
      return ::fkyaml::node(static_cast<double>(_var));
    } else if constexpr (std::is_unsigned<std::remove_cvref_t<T>>()) {
      return ::fkyaml::node(static_cast<int64_t>(_var));
    } else if constexpr (std::is_integral<std::remove_cvref_t<T>>()) {
      return ::fkyaml::node(static_cast<int64_t>(_var));
    } else {
      static_assert(rfl::always_false_v<T>, "Unsupported type.");
    }
  }

 private:
  ::fkyaml::node* root_;
  Flags flags_;
};

}  // namespace rfl::yaml

#endif
