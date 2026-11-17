#ifndef XD_SAVE_EDITION_H
#define XD_SAVE_EDITION_H

#include "xd_save.h"

namespace xd::save {

enum class Edition { Unknown, XD, XG };

Edition DetectEdition(const Save& save);
const char* EditionName(Edition e);

}

#endif
