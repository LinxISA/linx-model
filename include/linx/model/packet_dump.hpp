#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace linx::model {

class PacketDumpWriter;

namespace detail {

template <class T> struct IsUniquePtr : std::false_type {};

template <class T, class Deleter>
struct IsUniquePtr<std::unique_ptr<T, Deleter>> : std::true_type {};

template <class T> struct IsSharedPtr : std::false_type {};

template <class T> struct IsSharedPtr<std::shared_ptr<T>> : std::true_type {};

template <class T>
inline constexpr bool kIsSmartPtr =
    IsUniquePtr<std::remove_cv_t<T>>::value || IsSharedPtr<std::remove_cv_t<T>>::value;

template <class T>
concept HasDumpFields =
    requires(const T &value, PacketDumpWriter &writer) { value.DumpFields(writer); };

template <class T>
concept HasToString = requires(const T &value) {
  { value.ToString() } -> std::convertible_to<std::string>;
};

template <class T>
concept StreamWritable = requires(std::ostream &os, const T &value) { os << value; };

} // namespace detail

template <class T> void DumpValue(std::ostream &os, const T &value);

class PacketDumpWriter {
public:
  explicit PacketDumpWriter(std::ostream &os) : os_(os) {}

  template <class T> void Field(std::string_view name, const T &value) {
    if (!first_) {
      os_ << ", ";
    }
    first_ = false;
    os_ << name << '=';
    DumpValue(os_, value);
  }

private:
  std::ostream &os_;
  bool first_ = true;
};

template <class T> void DumpValue(std::ostream &os, const T &value) {
  using ValueT = std::remove_cvref_t<T>;

  if constexpr (std::is_same_v<ValueT, bool>) {
    os << std::boolalpha << value;
  } else if constexpr (detail::kIsSmartPtr<ValueT>) {
    if (!value) {
      os << "null";
      return;
    }
    DumpValue(os, *value);
  } else if constexpr (detail::HasDumpFields<ValueT>) {
    os << '{';
    PacketDumpWriter writer(os);
    value.DumpFields(writer);
    os << '}';
  } else if constexpr (detail::HasToString<ValueT>) {
    os << value.ToString();
  } else if constexpr (detail::StreamWritable<ValueT>) {
    os << value;
  } else {
    os << "<unprintable>";
  }
}

template <class T> std::string DumpString(const T &value) {
  std::ostringstream oss;
  DumpValue(oss, value);
  return oss.str();
}

} // namespace linx::model
