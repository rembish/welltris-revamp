/* stb_image implementation (PNG and JPEG only), kept in its own translation unit (third-party, not linted). */
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
