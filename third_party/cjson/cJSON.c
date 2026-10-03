/*
 * Minimal cJSON-compatible JSON parser used by the TwitchBot recovery build.
 *
 * Supported JSON values:
 *   object, array, string, number, true, false, null.
 *
 * Supported cJSON API subset:
 *   cJSON_Parse
 *   cJSON_Delete
 *   cJSON_GetObjectItemCaseSensitive
 *   cJSON_GetArraySize
 *   cJSON_GetArrayItem
 *   cJSON_IsString
 *   cJSON_IsNumber
 *   cJSON_IsArray
 *   cJSON_IsObject
 *
 * This is deliberately small and is not a full replacement for upstream
 * cJSON outside this project.
 */

#include "cJSON.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *cursor;

} Parser;

static cJSON *parse_value(
    Parser *parser
);

static void skip_whitespace(
    Parser *parser
)
{
    while (parser->cursor != NULL &&
           *parser->cursor != '\0' &&
           isspace((unsigned char)*parser->cursor))
    {
        ++parser->cursor;
    }
}

static cJSON *new_item(
    int type
)
{
    cJSON *item;

    item =
        (cJSON *)calloc(
            1,
            sizeof(cJSON)
        );

    if (item != NULL)
    {
        item->type = type;
    }

    return item;
}

static void append_child(
    cJSON *parent,
    cJSON *child
)
{
    cJSON *last;

    if (parent == NULL ||
        child == NULL)
    {
        return;
    }

    if (parent->child == NULL)
    {
        parent->child =
            child;

        return;
    }

    last =
        parent->child;

    while (last->next != NULL)
    {
        last =
            last->next;
    }

    last->next =
        child;

    child->prev =
        last;
}

static int hex_value(
    char c
)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f')
    {
        return 10 + c - 'a';
    }

    if (c >= 'A' && c <= 'F')
    {
        return 10 + c - 'A';
    }

    return -1;
}

static int read_hex4(
    const char *text,
    unsigned int *value
)
{
    int i;
    int digit;

    unsigned int result = 0;

    if (text == NULL ||
        value == NULL)
    {
        return 0;
    }

    for (i = 0; i < 4; ++i)
    {
        digit =
            hex_value(
                text[i]
            );

        if (digit < 0)
        {
            return 0;
        }

        result =
            (result << 4) |
            (unsigned int)digit;
    }

    *value =
        result;

    return 1;
}

static int append_utf8(
    char *output,
    size_t capacity,
    size_t *length,
    unsigned int codepoint
)
{
    size_t position;

    if (output == NULL ||
        length == NULL)
    {
        return 0;
    }

    position =
        *length;

    if (codepoint <= 0x7F)
    {
        if (position + 1 >=
            capacity)
        {
            return 0;
        }

        output[position++] =
            (char)codepoint;
    }
    else if (codepoint <= 0x7FF)
    {
        if (position + 2 >=
            capacity)
        {
            return 0;
        }

        output[position++] =
            (char)(0xC0 |
                   ((codepoint >> 6) &
                    0x1F));

        output[position++] =
            (char)(0x80 |
                   (codepoint &
                    0x3F));
    }
    else if (codepoint <= 0xFFFF)
    {
        if (position + 3 >=
            capacity)
        {
            return 0;
        }

        output[position++] =
            (char)(0xE0 |
                   ((codepoint >> 12) &
                    0x0F));

        output[position++] =
            (char)(0x80 |
                   ((codepoint >> 6) &
                    0x3F));

        output[position++] =
            (char)(0x80 |
                   (codepoint &
                    0x3F));
    }
    else if (codepoint <= 0x10FFFF)
    {
        if (position + 4 >=
            capacity)
        {
            return 0;
        }

        output[position++] =
            (char)(0xF0 |
                   ((codepoint >> 18) &
                    0x07));

        output[position++] =
            (char)(0x80 |
                   ((codepoint >> 12) &
                    0x3F));

        output[position++] =
            (char)(0x80 |
                   ((codepoint >> 6) &
                    0x3F));

        output[position++] =
            (char)(0x80 |
                   (codepoint &
                    0x3F));
    }
    else
    {
        return 0;
    }

    *length =
        position;

    return 1;
}

static char *parse_string_raw(
    Parser *parser
)
{
    const char *start;
    const char *scan;

    char *result;

    size_t capacity;
    size_t length = 0;

    if (parser == NULL ||
        parser->cursor == NULL ||
        *parser->cursor != '"')
    {
        return NULL;
    }

    start =
        parser->cursor + 1;

    scan =
        start;

    while (*scan != '\0')
    {
        if (*scan == '"')
        {
            break;
        }

        if (*scan == '\\')
        {
            ++scan;

            if (*scan == '\0')
            {
                return NULL;
            }
        }

        ++scan;
    }

    if (*scan != '"')
    {
        return NULL;
    }

    /*
     * Decoded text can never be longer than the encoded substring plus
     * a terminator.
     */
    capacity =
        (size_t)(scan - start) +
        1;

    result =
        (char *)malloc(
            capacity
        );

    if (result == NULL)
    {
        return NULL;
    }

    scan =
        start;

    while (*scan != '\0' &&
           *scan != '"')
    {
        unsigned char c =
            (unsigned char)*scan++;

        if (c != '\\')
        {
            if (length + 1 >=
                capacity)
            {
                free(result);
                return NULL;
            }

            result[length++] =
                (char)c;

            continue;
        }

        c =
            (unsigned char)*scan++;

        switch (c)
        {
            case '"':
                result[length++] = '"';
                break;

            case '\\':
                result[length++] = '\\';
                break;

            case '/':
                result[length++] = '/';
                break;

            case 'b':
                result[length++] = '\b';
                break;

            case 'f':
                result[length++] = '\f';
                break;

            case 'n':
                result[length++] = '\n';
                break;

            case 'r':
                result[length++] = '\r';
                break;

            case 't':
                result[length++] = '\t';
                break;

            case 'u':
            {
                unsigned int codepoint;

                if (!read_hex4(
                        scan,
                        &codepoint))
                {
                    free(result);
                    return NULL;
                }

                scan += 4;

                /*
                 * Handle a UTF-16 surrogate pair.
                 */
                if (codepoint >= 0xD800 &&
                    codepoint <= 0xDBFF)
                {
                    unsigned int low;

                    if (scan[0] != '\\' ||
                        scan[1] != 'u' ||
                        !read_hex4(
                            scan + 2,
                            &low) ||
                        low < 0xDC00 ||
                        low > 0xDFFF)
                    {
                        free(result);
                        return NULL;
                    }

                    scan += 6;

                    codepoint =
                        0x10000 +
                        ((codepoint - 0xD800)
                         << 10) +
                        (low - 0xDC00);
                }
                else if (codepoint >= 0xDC00 &&
                         codepoint <= 0xDFFF)
                {
                    free(result);
                    return NULL;
                }

                if (!append_utf8(
                        result,
                        capacity,
                        &length,
                        codepoint))
                {
                    free(result);
                    return NULL;
                }

                break;
            }

            default:
                free(result);
                return NULL;
        }
    }

    if (*scan != '"')
    {
        free(result);
        return NULL;
    }

    result[length] =
        '\0';

    parser->cursor =
        scan + 1;

    return result;
}

static cJSON *parse_string(
    Parser *parser
)
{
    cJSON *item;

    char *value;

    value =
        parse_string_raw(
            parser
        );

    if (value == NULL)
    {
        return NULL;
    }

    item =
        new_item(
            cJSON_String
        );

    if (item == NULL)
    {
        free(value);
        return NULL;
    }

    item->valuestring =
        value;

    return item;
}

static cJSON *parse_number(
    Parser *parser
)
{
    char *end;

    double value;

    cJSON *item;

    if (parser == NULL ||
        parser->cursor == NULL)
    {
        return NULL;
    }

    errno = 0;

    value =
        strtod(
            parser->cursor,
            &end
        );

    if (end ==
        parser->cursor)
    {
        return NULL;
    }

    if (errno == ERANGE)
    {
        return NULL;
    }

    item =
        new_item(
            cJSON_Number
        );

    if (item == NULL)
    {
        return NULL;
    }

    item->valuedouble =
        value;

    if (value >= (double)INT_MAX)
    {
        item->valueint =
            INT_MAX;
    }
    else if (value <= (double)INT_MIN)
    {
        item->valueint =
            INT_MIN;
    }
    else
    {
        item->valueint =
            (int)value;
    }

    parser->cursor =
        end;

    return item;
}

static int consume_literal(
    Parser *parser,
    const char *literal
)
{
    size_t length;

    if (parser == NULL ||
        parser->cursor == NULL ||
        literal == NULL)
    {
        return 0;
    }

    length =
        strlen(literal);

    if (strncmp(
            parser->cursor,
            literal,
            length) != 0)
    {
        return 0;
    }

    parser->cursor +=
        length;

    return 1;
}

static cJSON *parse_array(
    Parser *parser
)
{
    cJSON *array;

    if (parser == NULL ||
        parser->cursor == NULL ||
        *parser->cursor != '[')
    {
        return NULL;
    }

    array =
        new_item(
            cJSON_Array
        );

    if (array == NULL)
    {
        return NULL;
    }

    ++parser->cursor;

    skip_whitespace(
        parser
    );

    if (*parser->cursor == ']')
    {
        ++parser->cursor;
        return array;
    }

    while (1)
    {
        cJSON *child;

        skip_whitespace(
            parser
        );

        child =
            parse_value(
                parser
            );

        if (child == NULL)
        {
            cJSON_Delete(array);
            return NULL;
        }

        append_child(
            array,
            child
        );

        skip_whitespace(
            parser
        );

        if (*parser->cursor == ',')
        {
            ++parser->cursor;
            continue;
        }

        if (*parser->cursor == ']')
        {
            ++parser->cursor;
            return array;
        }

        cJSON_Delete(array);
        return NULL;
    }
}

static cJSON *parse_object(
    Parser *parser
)
{
    cJSON *object;

    if (parser == NULL ||
        parser->cursor == NULL ||
        *parser->cursor != '{')
    {
        return NULL;
    }

    object =
        new_item(
            cJSON_Object
        );

    if (object == NULL)
    {
        return NULL;
    }

    ++parser->cursor;

    skip_whitespace(
        parser
    );

    if (*parser->cursor == '}')
    {
        ++parser->cursor;
        return object;
    }

    while (1)
    {
        char *name;

        cJSON *value;

        skip_whitespace(
            parser
        );

        if (*parser->cursor != '"')
        {
            cJSON_Delete(object);
            return NULL;
        }

        name =
            parse_string_raw(
                parser
            );

        if (name == NULL)
        {
            cJSON_Delete(object);
            return NULL;
        }

        skip_whitespace(
            parser
        );

        if (*parser->cursor != ':')
        {
            free(name);
            cJSON_Delete(object);
            return NULL;
        }

        ++parser->cursor;

        skip_whitespace(
            parser
        );

        value =
            parse_value(
                parser
            );

        if (value == NULL)
        {
            free(name);
            cJSON_Delete(object);
            return NULL;
        }

        value->string =
            name;

        append_child(
            object,
            value
        );

        skip_whitespace(
            parser
        );

        if (*parser->cursor == ',')
        {
            ++parser->cursor;
            continue;
        }

        if (*parser->cursor == '}')
        {
            ++parser->cursor;
            return object;
        }

        cJSON_Delete(object);
        return NULL;
    }
}

static cJSON *parse_value(
    Parser *parser
)
{
    if (parser == NULL ||
        parser->cursor == NULL)
    {
        return NULL;
    }

    skip_whitespace(
        parser
    );

    switch (*parser->cursor)
    {
        case '{':
            return parse_object(
                parser
            );

        case '[':
            return parse_array(
                parser
            );

        case '"':
            return parse_string(
                parser
            );

        case 't':

            if (consume_literal(
                    parser,
                    "true"))
            {
                cJSON *item =
                    new_item(
                        cJSON_True
                    );

                if (item != NULL)
                {
                    item->valueint = 1;
                    item->valuedouble = 1.0;
                }

                return item;
            }

            return NULL;

        case 'f':

            if (consume_literal(
                    parser,
                    "false"))
            {
                return new_item(
                    cJSON_False
                );
            }

            return NULL;

        case 'n':

            if (consume_literal(
                    parser,
                    "null"))
            {
                return new_item(
                    cJSON_NULL
                );
            }

            return NULL;

        case '\0':
            return NULL;

        default:

            if (*parser->cursor == '-' ||
                (*parser->cursor >= '0' &&
                 *parser->cursor <= '9'))
            {
                return parse_number(
                    parser
                );
            }

            return NULL;
    }
}

cJSON *cJSON_Parse(
    const char *value
)
{
    Parser parser;

    cJSON *root;

    if (value == NULL)
    {
        return NULL;
    }

    parser.cursor =
        value;

    skip_whitespace(
        &parser
    );

    root =
        parse_value(
            &parser
        );

    if (root == NULL)
    {
        return NULL;
    }

    skip_whitespace(
        &parser
    );

    if (*parser.cursor != '\0')
    {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

void cJSON_Delete(
    cJSON *item
)
{
    while (item != NULL)
    {
        cJSON *next =
            item->next;

        if (item->child != NULL)
        {
            cJSON_Delete(
                item->child
            );
        }

        free(
            item->valuestring
        );

        free(
            item->string
        );

        free(
            item
        );

        item =
            next;
    }
}

cJSON *cJSON_GetObjectItemCaseSensitive(
    const cJSON *object,
    const char *string
)
{
    cJSON *child;

    if (object == NULL ||
        string == NULL ||
        !cJSON_IsObject(object))
    {
        return NULL;
    }

    child =
        object->child;

    while (child != NULL)
    {
        if (child->string != NULL &&
            strcmp(
                child->string,
                string) == 0)
        {
            return child;
        }

        child =
            child->next;
    }

    return NULL;
}

int cJSON_GetArraySize(
    const cJSON *array
)
{
    int count = 0;

    const cJSON *child;

    if (array == NULL ||
        !cJSON_IsArray(array))
    {
        return 0;
    }

    child =
        array->child;

    while (child != NULL)
    {
        ++count;
        child =
            child->next;
    }

    return count;
}

cJSON *cJSON_GetArrayItem(
    const cJSON *array,
    int index
)
{
    cJSON *child;

    if (array == NULL ||
        !cJSON_IsArray(array) ||
        index < 0)
    {
        return NULL;
    }

    child =
        array->child;

    while (child != NULL &&
           index > 0)
    {
        child =
            child->next;

        --index;
    }

    return child;
}

int cJSON_IsString(
    const cJSON *item
)
{
    return
        item != NULL &&
        item->type == cJSON_String;
}

int cJSON_IsNumber(
    const cJSON *item
)
{
    return
        item != NULL &&
        item->type == cJSON_Number;
}

int cJSON_IsArray(
    const cJSON *item
)
{
    return
        item != NULL &&
        item->type == cJSON_Array;
}

int cJSON_IsObject(
    const cJSON *item
)
{
    return
        item != NULL &&
        item->type == cJSON_Object;
}
