#pragma once
#include <stdint.h>
typedef struct { const char *name; uint32_t va; uint32_t size; const unsigned char *data; } ImgSection;
typedef struct { uint32_t iat; const char *dll; const char *name; } ImgImport;
extern const ImgSection img_sections[];
extern const unsigned img_nsections;
extern const ImgImport img_imports[];
extern const unsigned img_nimports;
extern const uint32_t img_base, img_size, img_entry;
extern const uint32_t img_tls[5];  // StartAddressOfRawData, End, AddressOfIndex, AddressOfCallBacks, SizeOfZeroFill
