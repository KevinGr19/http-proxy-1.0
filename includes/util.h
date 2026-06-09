#pragma once

#define __EINTR_LOOP(return_type, error_value, func, ...)\
    return_type r;\
    while( ((r = (func)(__VA_ARGS__)) == (error_value)) && (errno == EINTR) ){\
        errno = 0;\
    }

#define EINTR_LOOP(return_type, error_value, func, ...){\
    __EINTR_LOOP(return_type, error_value, func, ##__VA_ARGS__)\
    return r;\
}
