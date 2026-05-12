#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "json.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FILE *input = fmemopen((void *)data, size, "r");
    if (!input) {
        return 0;
    }

    json_parser parser;
    json_parser_init(&parser, NULL, NULL, NULL);

    int ret = 0;
    int lines, col;
    ret = process_file(&parser, input, &lines, &col);

    json_parser_free(&parser);
    fclose(input);

    return 0;
}
  
