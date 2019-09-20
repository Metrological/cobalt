// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_TRAITS_DETAILS_H_
#define BASE_TASK_TASK_TRAITS_DETAILS_H_

#include <initializer_list>
#include <tuple>
#include <type_traits>
#include <utility>

#include "base/cpp14oncpp11.h"

namespace base {
namespace trait_helpers {

// Checks if any of the elements in |ilist| is true.
// Similar to std::any_of for the case of constexpr initializer_list.
#if __cplusplus >= 201402L
inline constexpr bool any_of(std::initializer_list<bool> ilist) {
  for (auto c : ilist) {
    if (c)
      return true;
  }
  return false;
}

// Checks if all of the elements in |ilist| are true.
// Similar to std::any_of for the case of constexpr initializer_list.
inline constexpr bool all_of(std::initializer_list<bool> ilist) {
  for (auto c : ilist) {
    if (!c)
      return false;
  }
  return true;
}

// Counts the elements in |ilist| that are equal to |value|.
// Similar to std::count for the case of constexpr initializer_list.
template <class T>
inline constexpr size_t count(std::initializer_list<T> ilist, T value) {
  size_t c = 0;
  for (const auto& v : ilist) {
    c += (v == value);
  }
  return c;
}
#endif

// CallFirstTag is an argument tag that helps to avoid ambiguous overloaded
// functions. When the following call is made:
//    func(CallFirstTag(), arg...);
// the compiler will give precedence to an overload candidate that directly
// takes CallFirstTag. Another overload that takes CallSecondTag will be
// considered iff the preferred overload candidates were all invalids and
// therefore discarded.
struct CallSecondTag {};
struct CallFirstTag : CallSecondTag {};

// A trait filter class |TraitFilterType| implements the protocol to get a value
// of type |ArgType| from an argument list and convert it to a value of type
// |TraitType|. If the argument list contains an argument of type |ArgType|, the
// filter class will be instantiated with that argument. If the argument list
// contains no argument of type |ArgType|, the filter class will be instantiated
// using the default constructor if available; a compile error is issued
// otherwise. The filter class must have the conversion operator TraitType()
// which returns a value of type TraitType.

// |InvalidTrait| is used to return from GetTraitFromArg when the argument is
// not compatible with the desired trait.
struct InvalidTrait {};

// Returns an object of type |TraitFilterType| constructed from |arg| if
// compatible, or |InvalidTrait| otherwise.
template <class TraitFilterType,
          class ArgType,
          class CheckArgumentIsCompatible = std::enable_if_t<
              std::is_constructible<TraitFilterType, ArgType>::value>>
constexpr TraitFilterType GetTraitFromArg(CallFirstTag, ArgType arg) {
  return TraitFilterType(arg);
}

template <class TraitFilterType, class ArgType>
constexpr InvalidTrait GetTraitFromArg(CallSecondTag, ArgType /*arg*/) {
  return InvalidTrait();
}

// Returns an object of type |TraitFilterType| constructed from a compatible
// argument in |args...|, or default constructed if none of the arguments are
// compatible. This is the implementation of GetTraitFromArgList() with a
// disambiguation tag.
#if __cplusplus < 201402L
template <class TraitFilterType,
          class ArgTypes1,
          class ArgTypes2,
          class ArgTypes3,
          class ArgTypes4,
          class TestCompatibleArgument = std::enable_if_t<
              std::is_constructible<TraitFilterType, ArgTypes1>::value ||
              std::is_constructible<TraitFilterType, ArgTypes2>::value ||
              std::is_constructible<TraitFilterType, ArgTypes3>::value ||
              std::is_constructible<TraitFilterType, ArgTypes4>::value>>
TraitFilterType GetTraitFromArgListImpl(CallFirstTag,
                                        ArgTypes1 arg1,
                                        ArgTypes2 arg2,
                                        ArgTypes3 arg3,
                                        ArgTypes4 arg4) {
  return std::get<TraitFilterType>(
      std::make_tuple(GetTraitFromArg<TraitFilterType>(CallFirstTag(), arg1),
                      GetTraitFromArg<TraitFilterType>(CallFirstTag(), arg2),
                      GetTraitFromArg<TraitFilterType>(CallFirstTag(), arg3),
                      GetTraitFromArg<TraitFilterType>(CallFirstTag(), arg4)));
}

template <class TraitFilterType,
          class ArgTypes1,
          class ArgTypes2,
          class ArgTypes3,
          class TestCompatibleArgument = std::enable_if_t<
              std::is_constructible<TraitFilterType, ArgTypes1>::value ||
              std::is_constructible<TraitFilterType, ArgTypes2>::value ||
              std::is_constructible<TraitFilterType, ArgTypes3>::value>>
TraitFilterType GetTraitFromArgListImpl(CallFirstTag,
                                        ArgTypes1 arg1,
                                        ArgTypes2 arg2,
                                        ArgTypes3 arg3) {
  return std::get<TraitFilterType>(
      std::make_tuple(GetTraitFromArg<TraitFilterType>(CallFirstTag(), arg1),
                      GetTraitFromArg<TraitFilterType>(CallFirstTag(), arg2),
                      GetTraitFromArg<TraitFilterType>(CallFirstTag(), arg3)));
}

template <class TraitFilterType,
          class ArgTypes1,
          class ArgTypes2,
          class TestCompatibleArgument = std::enable_if_t<
              std::is_constructible<TraitFilterType, ArgTypes1>::value ||
              std::is_constructible<TraitFilterType, ArgTypes2>::value>>
TraitFilterType GetTraitFromArgListImpl(CallFirstTag,
                                        ArgTypes1 arg1,
                                        ArgTypes2 arg2) {
  return std::get<TraitFilterType>(
      std::make_tuple(GetTraitFromArg<TraitFilterType>(CallFirstTag(), arg1),
                      GetTraitFromArg<TraitFilterType>(CallFirstTag(), arg2)));
}

template <class TraitFilterType,
          class ArgTypes1,
          class TestCompatibleArgument = std::enable_if_t<
              std::is_constructible<TraitFilterType, ArgTypes1>::value>>
constexpr TraitFilterType GetTraitFromArgListImpl(CallFirstTag,
                                                  ArgTypes1 arg1) {
  return TraitFilterType(arg1);
}
#else
template <class TraitFilterType,
          class... ArgTypes,
          class TestCompatibleArgument = std::enable_if_t<any_of(
              {std::is_constructible<TraitFilterType, ArgTypes>::value...})>>
constexpr TraitFilterType GetTraitFromArgListImpl(CallFirstTag,
                                                  ArgTypes... args) {
  return std::get<TraitFilterType>(std::make_tuple(
      GetTraitFromArg<TraitFilterType>(CallFirstTag(), args)...));
}
#endif

template <class TraitFilterType, class... ArgTypes>
constexpr TraitFilterType GetTraitFromArgListImpl(CallSecondTag,
                                                  ArgTypes... /*args*/) {
  static_assert(std::is_constructible<TraitFilterType>::value,
                "TaskTraits contains a Trait that must be explicity "
                "initialized in its constructor.");
  return TraitFilterType();
}

// Constructs an object of type |TraitFilterType| from a compatible argument in
// |args...|, or using the default constructor, and returns its associated trait
// value using conversion to |TraitFilterType::ValueType|. If there are more
// than one compatible argument in |args|, generates a compile-time error.
template <class TraitFilterType, class... ArgTypes>
CONSTEXPR typename TraitFilterType::ValueType GetTraitFromArgList(
    ArgTypes... args) {
#if __cplusplus >= 201402L
  static_assert(
      count({std::is_constructible<TraitFilterType, ArgTypes>::value...},
            true) <= 1,
      "Multiple arguments of the same type were provided to the "
      "constructor of TaskTraits.");
#endif
  return GetTraitFromArgListImpl<TraitFilterType>(CallFirstTag(), args...);
}

// Returns true if this trait is explicitly defined in an argument list, i.e.
// there is an argument compatible with this trait in |args...|.
#if __cplusplus < 201402L
template <class TraitFilterType>
constexpr bool TraitIsDefined() {
  return false;
}

template <class TraitFilterType, class ArgType1>
constexpr bool TraitIsDefined(ArgType1 /*arg1*/) {
  return std::is_constructible<TraitFilterType, ArgType1>::value;
}

template <class TraitFilterType, class ArgType1, class ArgType2>
constexpr bool TraitIsDefined(ArgType1 /*arg1*/, ArgType2 /*arg2*/) {
  return std::is_constructible<TraitFilterType, ArgType1>::value ||
         std::is_constructible<TraitFilterType, ArgType2>::value;
}

template <class TraitFilterType, class ArgType1, class ArgType2, class ArgType3>
constexpr bool TraitIsDefined(ArgType1 /*arg1*/,
                              ArgType2 /*arg2*/,
                              ArgType3 /*arg3*/) {
  return std::is_constructible<TraitFilterType, ArgType1>::value ||
         std::is_constructible<TraitFilterType, ArgType2>::value ||
         std::is_constructible<TraitFilterType, ArgType3>::value;
}

template <class TraitFilterType,
          class ArgType1,
          class ArgType2,
          class ArgType3,
          class ArgType4>
constexpr bool TraitIsDefined(ArgType1 /*arg1*/,
                              ArgType2 /*arg2*/,
                              ArgType3 /*arg3*/,
                              ArgType4 /*arg4*/) {
  return std::is_constructible<TraitFilterType, ArgType1>::value ||
         std::is_constructible<TraitFilterType, ArgType2>::value ||
         std::is_constructible<TraitFilterType, ArgType3>::value ||
         std::is_constructible<TraitFilterType, ArgType4>::value;
}
#else
template <class TraitFilterType, class... ArgTypes>
constexpr bool TraitIsDefined(ArgTypes... args) {
  return any_of({std::is_constructible<TraitFilterType, ArgTypes>::value...});
}
#endif

// Helper class to implemnent a |TraitFilterType|.
template <typename T>
struct BasicTraitFilter {
  using ValueType = T;

  constexpr operator ValueType() const { return value; }

  ValueType value = {};
};

template <typename ArgType>
struct BooleanTraitFilter : public BasicTraitFilter<bool> {
  CONSTEXPR BooleanTraitFilter() { this->value = false; }
  CONSTEXPR BooleanTraitFilter(ArgType) { this->value = true; }
};

template <typename ArgType, ArgType DefaultValue>
struct EnumTraitFilter : public BasicTraitFilter<ArgType> {
  CONSTEXPR EnumTraitFilter() { this->value = DefaultValue; }
  CONSTEXPR EnumTraitFilter(ArgType arg) { this->value = arg; }
};

// Tests whether multiple given argtument types are all valid traits according
// to the provided ValidTraits. To use, define a ValidTraits
template <typename ArgType>
struct RequiredEnumTraitFilter : public BasicTraitFilter<ArgType> {
  CONSTEXPR RequiredEnumTraitFilter(ArgType arg) { this->value = arg; }
};

#ifdef STARBOARD
// Allows instantiation of multiple types in one statement. Used to prevent
// instantiation of the constructor of TaskTraits with inappropriate argument
// types.
template <class...>
struct InitTypes {};
#endif

#if __cplusplus >= 201402L
// Tests whether a given trait type is valid or invalid by testing whether it is
// convertible to the provided ValidTraits type. To use, define a ValidTraits
// type like this:
//
// struct ValidTraits {
//   ValidTraits(MyTrait);
//   ...
// };
template <class ValidTraits, class... ArgTypes>
struct AreValidTraits
    : std::integral_constant<
          bool,
          all_of({std::is_convertible<ArgTypes, ValidTraits>::value...})> {};
#endif

}  // namespace trait_helpers
}  // namespace base

#endif  // BASE_TASK_TASK_TRAITS_DETAILS_H_
