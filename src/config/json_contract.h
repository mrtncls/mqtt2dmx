#ifndef json_contract_h
#define json_contract_h

#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>

json_t *Json_Parse(const char *data, size_t length, size_t limit);
bool Json_Keys(const json_t *object, const char *const *allowed);
bool Json_Duration(const json_t *value, uint32_t *milliseconds);
bool Json_String(const json_t *value, char *out, size_t capacity);

#endif
