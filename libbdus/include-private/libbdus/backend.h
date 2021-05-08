/* SPDX-License-Identifier: MIT */

#ifndef LIBBDUS_HEADER_BACKEND_H_
#define LIBBDUS_HEADER_BACKEND_H_

/* -------------------------------------------------------------------------- */

#include <bdus.h>

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */

bool bdus_backend_run_(
    int control_fd, struct bdus_ctx *ctx, uint32_t max_outstanding_reqs);

/* -------------------------------------------------------------------------- */

#endif /* LIBBDUS_HEADER_BACKEND_H_ */
