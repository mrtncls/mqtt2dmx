#include "json_contract.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

json_t *Json_Parse(const char *data, size_t length, size_t limit)
{
    if (!data || !length || length > limit || memchr(data, 0, length))
    {
        fprintf(stderr, "Invalid JSON buffer or length\n");
        return NULL;
    }
    json_error_t error;
    json_t *result = json_loadb(data, length, JSON_REJECT_DUPLICATES, &error);
    if (!result)
        fprintf(stderr, "Invalid JSON at line %d: %s\n", error.line, error.text);
    return result;
}

bool Json_Keys(const json_t *object, const char *const *allowed)
{
    if (!json_is_object(object))
        return false;
    const char *key;
    json_t *value;
    json_object_foreach((json_t *)object, key, value)
    {
        size_t i = 0;
        while (allowed[i] && strcmp(key, allowed[i]))
            i++;
        if (!allowed[i])
            return false;
    }
    return true;
}

bool Json_Duration(const json_t *value, uint32_t *milliseconds)
{
    if (!json_is_number(value))
        return false;
    double seconds = json_number_value(value);
    if (!isfinite(seconds) || seconds < 0 || seconds > 3600)
        return false;
    *milliseconds = (uint32_t)(seconds * 1000 + 0.5);
    return true;
}

bool Json_String(const json_t *value, char *out, size_t capacity)
{
    if (!json_is_string(value))
        return false;
    size_t length = json_string_length(value);
    if (!length || length >= capacity || strlen(json_string_value(value)) != length)
        return false;
    memcpy(out, json_string_value(value), length + 1);
    return true;
}
