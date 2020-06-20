#include <stdbool.h>
#include <string.h>
#include "debug.h"

bool is_str_lit(char const* str) {
    if(
        (str[0] == '\'' && str[strlen(str)-1] == '\'') ||
        (str[0] == '"' && str[strlen(str)-1] == '"')
    )
        return true;
    return false;
}

int contains_quotes(char const* str) {
    char* single_quote = strchr(str, '\'');
    char* double_quote = strchr(str, '"');
    if (single_quote != NULL && double_quote != NULL) {
        printf_debug("DEBUG: Cannot mix single and double quotes\n");
        return -1;
    } else if (single_quote == NULL && double_quote == NULL) {
        return 0; // does not contain quotes
    }
    return single_quote == NULL ? '"' : '\'';
}

int contains_valid_quotes(char const* str) {
    char* single_quote = strchr(str, '\'');
    char* double_quote = strchr(str, '"');
    char quote; 
    char* pos;
    if (single_quote == NULL) {
        quote = '"';
        pos = double_quote;
    } else {
        quote = '\'';
        pos = single_quote;
    }
    int quote_count = 0;
    while (pos != NULL) {
        pos = strchr(++pos, quote);
        ++quote_count; 
    }
    if (quote_count % 2 != 0) {
        printf_debug("DEBUG: Invalid (odd) number quotation marks\n");
        return -1;
    }
    return quote;
}

char* strip_quotes(char* str) {
    // this function assumes that if 'str' starts with a quote it ends with a quote
    char* begin = str;
    char* end = str + strlen(str) - 1;
    if (*begin != '\'' && *begin != '"')
        return str; // no quotes to strip, do nothing

    *begin = '\0';
    *end = '\0';
    return begin+1;
}

static int get_quote(char const* str, size_t len) {
    int index = 0;
    while (index < len) {
        if (str[index] == '\'')
            return '\'';
        else if (str[index] == '"')
            return '"';
        ++index;
    }
    return 0;
}

static void replace_within_quotes(char* str, char const* to_replace, size_t len) {
    int const quote = get_quote(str, len);
    if(quote == 0) // string doesn't contain quotes so do nothing
        return;
    
    bool inside_quote = false;
    char* pos = str;
    int index = 0;
    while (index < len) {
        if(*pos == quote) {
            inside_quote = !inside_quote;
        } else if (inside_quote) {
            for (int i = 0; i < strlen(to_replace); i++) {
                if (*pos == to_replace[i]){
                    *pos = -to_replace[i];
                }
            }
        }
        ++pos;
        ++index;
    }
}

static void restore_within_quotes(char* str, size_t len) {
    int const quote = get_quote(str, len);
    if(quote == 0) // string doesn't contain quotes so do nothing
        return;

    bool inside_quote = false;
    char* pos = str;
    int index = 0;
    while (index < len) {
        if(*pos == quote) {
            inside_quote = !inside_quote;
        } else if (inside_quote) {
            if (*pos < 0)
                *pos = -(*pos);
        }
        ++pos;
        ++index;
    }
}

/*
    This alternative strchr implementation functions the same as the stdlib version with
    added support for quoted strings.
    
    strchr2 returns the location of char c in string s unless c is inside quotation marks.
*/
char* strchr2(char const* s, int c) {
    // this function assumes 's' contains valid quotes!
    int const quote = get_quote(s, strlen(s));
    if (quote == 0) // doesn't contain quotes so call regular strchr
        return strchr(s, c);

    bool inside_quote = false;
    char* pos = (char*)s;
    while (*pos != '\0') {
        if(*pos == quote) {
            inside_quote = !inside_quote;
        } else if (!inside_quote) {
            if (*pos == c)
                return pos;
        }
        ++pos;
    }
    return NULL;
}

/*
    This alternative strtok implementation functions the same as the stdlib version with
    added support for quoted strings.

    See strtok documentation for return values and how to use.
*/
char* strtok2(char* str, char const* delim) {
    static char* current_str;
    static size_t current_len;
    if (str != NULL) {
        current_str = str;
        current_len = strlen(current_str);
    }
    replace_within_quotes(current_str, delim, current_len);
    char* to_return = strtok(str, delim);
    restore_within_quotes(current_str, current_len);
    return to_return;
}