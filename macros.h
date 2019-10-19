#pragma once

#define ATTR_NONNULL_ALL __attribute__((nonnull))
#define ATTR_NONNULL(...) __attribute__((nonnull( __VA_ARGS__ )))
