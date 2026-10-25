/* Minimaler Ersatz fuer miniz.h: nur der Entpacker (tinfl).
   Die vollstaendige Bibliothek: https://github.com/richgel999/miniz (MIT, siehe LICENSE). */
#pragma once
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS
#include "miniz_common.h"
#include "miniz_tinfl.h"
