#pragma once

bool is_str_lit(char const* str);

int contains_quotes(char const* str);
int contains_valid_quotes(char const* str);

char* strip_quotes(char* str);

char* strchr2(char const* s, int c);
char* strtok2(char* str, char const* delim);