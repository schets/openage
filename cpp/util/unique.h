// Copyright 2014-2014 the openage authors. See copying.md for legal info.

#ifndef OPENAGE_UTIL_UNIQUE_H_
#define OPENAGE_UTIL_UNIQUE_H_

#include <memory>
#include <utility>

namespace openage{
namespace util{

template<class T, class... Args>
std::unique_ptr<T> make_unique(Args&&... args){
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

} //namespace util
} //namespace openage
#endif

