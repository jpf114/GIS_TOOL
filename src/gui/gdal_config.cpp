#include "gdal_config.h"

#include <gdal_priv.h>

namespace gis::gui {

void configureGdalRuntime() {
    CPLSetConfigOption("GDAL_NUM_THREADS", "ALL_CPUS");
    CPLSetConfigOption("GDAL_CACHEMAX", "512");
    CPLSetConfigOption("CPL_DEBUG", "OFF");
    CPLSetConfigOption("CPL_LOG_ERRORS", "ON");
}

}
