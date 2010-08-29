#ifndef CCACHE_LANGUAGE_H
#define CCACHE_LANGUAGE_H

const char *language_for_file(const char *fname);
const char *p_language_for_language(const char *language);
const char *extension_for_language(const char *language);
int language_is_supported(const char *language);
int language_is_preprocessed(const char *language);

#endif /* CCACHE_LANGUAGE_H */
