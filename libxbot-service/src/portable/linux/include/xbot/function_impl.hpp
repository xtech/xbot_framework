#ifndef FUNCTION_IMPL_HPP
#define FUNCTION_IMPL_HPP

#include <functional>

#define XBOT_FUNCTION_TYPEDEF std::function
#define XBOT_FUNCTION_FOR_METHOD(class, method, instance) std::bind(method, instance)

#endif  // FUNCTION_IMPL_HPP
