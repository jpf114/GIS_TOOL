#pragma once

namespace gis::core {

/** Register default Logger sinks (stderr + in-memory ring buffer). Idempotent. */
void initDefaultLogging();

} // namespace gis::core
