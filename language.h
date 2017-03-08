#ifndef CCACHE_LANGUAGE_H
#define CCACHE_LANGUAGE_H

#ifndef WIN32
#   include <stdbool.h>
#endif

const char *language_for_file(const char *fname);
const char *p_language_for_language(const char *language);
const char *extension_for_language(const char *language);
bool language_is_supported(const char *language);
bool language_is_preprocessed(const char *language);

#endif // CCACHE_LANGUAGE_H
