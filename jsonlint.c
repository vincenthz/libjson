/*
 * Copyright (C) 2009 Vincent Hanquez <vincent@snarc.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 or version 3.0 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <locale.h>
#include <getopt.h>
#include <errno.h>

#include "json.h"

char *indent_string = NULL;

char *string_of_errors[] =
{
	[JSON_ERROR_NO_MEMORY] = "out of memory",
	[JSON_ERROR_BAD_CHAR] = "bad character",
	[JSON_ERROR_POP_EMPTY] = "stack empty",
	[JSON_ERROR_POP_UNEXPECTED_MODE] = "pop unexpected mode",
	[JSON_ERROR_NESTING_LIMIT] = "nesting limit",
	[JSON_ERROR_DATA_LIMIT] = "data limit",
	[JSON_ERROR_COMMENT_NOT_ALLOWED] = "comment not allowed by config",
	[JSON_ERROR_UNEXPECTED_CHAR] = "unexpected char",
	[JSON_ERROR_UNICODE_MISSING_LOW_SURROGATE] = "missing unicode low surrogate",
	[JSON_ERROR_UNICODE_UNEXPECTED_LOW_SURROGATE] = "unexpected unicode low surrogate",
	[JSON_ERROR_COMMA_OUT_OF_STRUCTURE] = "error comma out of structure",
};

static int printchannel(void *userdata, const char *data, uint32_t length)
{
	FILE *channel = userdata;
	int ret;
	/* should check return value */
	ret = fwrite(data, length, 1, channel);
	return 0;
}

static int prettyprint(void *userdata, int type, const char *data, uint32_t length)
{
	json_printer *printer = userdata;
	
	return json_print_pretty(printer, type, data, length);
}

FILE *open_filename(const char *filename, const char *opt, int is_input)
{
	FILE *input;
	if (strcmp(filename, "-") == 0)
		input = (is_input) ? stdin : stdout;
	else {
		input = fopen(filename, opt);
		if (!input) {
			fprintf(stderr, "error: cannot open %s: %s", filename, strerror(errno));
			return NULL;
		}
	}
	return input;
}

void close_filename(const char *filename, FILE *file)
{
	if (strcmp(filename, "-") != 0)
		fclose(file);
}

int process_file(json_parser *parser, FILE *input, int *retlines, int *retcols)
{
	char buffer[4096];
	int ret = 0;
	int32_t read;
	int lines, col, i;

	lines = 1;
	col = 0;
	while (1) {
		uint32_t processed;
		read = fread(buffer, 1, 4096, input);
		if (read <= 0)
			break;
		ret = json_parser_string(parser, buffer, read, &processed);
		for (i = 0; i < processed; i++) {
			if (buffer[i] == '\n') { col = 0; lines++; } else col++;
		}
		if (ret)
			break;
	}
	if (retlines) *retlines = lines;
	if (retcols) *retcols = col;
	return ret;
}

static int do_verify(json_config *config, const char *filename)
{
	FILE *input;
	json_parser parser;
	int ret;

	input = open_filename(filename, "r", 1);
	if (!input)
		return 2;

	/* initialize the parser structure. we don't need a callback in verify */
	ret = json_parser_init(&parser, config, NULL, NULL);
	if (ret) {
		fprintf(stderr, "error: initializing parser failed (code=%d): %s\n", ret, string_of_errors[ret]);
		return ret;
	}

	ret = process_file(&parser, input, NULL, NULL);
	if (ret)
		return 1;

	ret = json_parser_is_done(&parser);
	if (!ret)
		return 1;
	
	close_filename(filename, input);
	return 0;
}

static int do_parse(json_config *config, const char *filename)
{
	FILE *input;
	json_parser parser;
	int ret;
	int col, lines;

	input = open_filename(filename, "r", 1);
	if (!input)
		return 2;

	/* initialize the parser structure. we don't need a callback in verify */
	ret = json_parser_init(&parser, config, NULL, NULL);
	if (ret) {
		fprintf(stderr, "error: initializing parser failed (code=%d): %s\n", ret, string_of_errors[ret]);
		return ret;
	}

	ret = process_file(&parser, input, &lines, &col);
	if (ret) {
		fprintf(stderr, "line %d, col %d: [code=%d] %s\n",
		        lines, col, ret, string_of_errors[ret]);
		return 1;
	}

	ret = json_parser_is_done(&parser);
	if (!ret) {
		fprintf(stderr, "syntax error\n");
		return 1;
	}
	
	close_filename(filename, input);
	return 0;
}

static int do_format(json_config *config, const char *filename, const char *outputfile)
{
	FILE *input, *output;
	json_parser parser;
	json_printer printer;
	int ret;
	int col, lines;

	input = open_filename(filename, "r", 1);
	if (!input)
		return 2;

	output = open_filename(outputfile, "a+", 0);
	if (!output)
		return 2;

	/* initialize printer and parser structures */
	ret = json_print_init(&printer, printchannel, stdout);
	if (ret) {
		fprintf(stderr, "error: initializing printer failed: [code=%d] %s\n", ret, string_of_errors[ret]);
		return ret;
	}
	if (indent_string)
		printer.indentstr = indent_string;

	ret = json_parser_init(&parser, config, &prettyprint, &printer);
	if (ret) {
		fprintf(stderr, "error: initializing parser failed: [code=%d] %s\n", ret, string_of_errors[ret]);
		return ret;
	}

	ret = process_file(&parser, input, &lines, &col);
	if (ret) {
		fprintf(stderr, "line %d, col %d: [code=%d] %s\n",
		        lines, col, ret, string_of_errors[ret]);
		return 1;
	}

	ret = json_parser_is_done(&parser);
	if (!ret) {
		fprintf(stderr, "syntax error\n");
		return 1;
	}

	/* cleanup */
	json_parser_free(&parser);
	json_print_free(&printer);
	fwrite("\n", 1, 1, stdout);
	close_filename(filename, input);
	return 0;
}

int usage(const char *argv0)
{
	printf("usage: %s [options] JSON-FILE(s)...\n", argv0);
	printf("\t--no-comments : disallow C and YAML comments in json file (default to both on)\n");
	printf("\t--no-yaml-comments : disallow YAML comment (default to on)\n");
	printf("\t--no-c-comments : disallow C comment (default to on)\n");
	printf("\t--format : pretty print the json file to stdout (unless -o specified)\n");
	printf("\t--verify : quietly verified if the json file is valid. exit 0 if valid, 1 if not\n");
	printf("\t--max-nesting : limit the number of nesting in structure (default to no limit)\n");
	printf("\t--max-data : limit the number of characters of data (string/int/float) (default to no limit)\n");
	printf("\t--indent-string : set the string to use for indenting one level (default to 1 tab)\n");
	printf("\t-o : output to a specific file instead of stdout\n");
	exit(0);
}

int main(int argc, char **argv)
{
	int format = 0, verify = 0;
	int ret = 0, i;
	json_config config;
	char *output = "-";

	memset(&config, 0, sizeof(json_config));
	config.max_nesting = 0;
	config.max_data = 0;
	config.allow_c_comments = 1;
	config.allow_yaml_comments = 1;

	while (1) {
		int option_index;
		struct option long_options[] = {
			{ "no-comments", 0, 0, 0 },
			{ "no-yaml-comments", 0, 0, 0 },
			{ "no-c-comments", 0, 0, 0 },
			{ "format", 0, 0, 0 },
			{ "verify", 0, 0, 0 },
			{ "help", 0, 0, 0 },
			{ "max-nesting", 1, 0, 0 },
			{ "max-data", 1, 0, 0 },
			{ "indent-string", 1, 0, 0 },
		};
		int c = getopt_long(argc, argv, "o:", long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
		case 0: {
			const char *name = long_options[option_index].name;
			if (strcmp(name, "help") == 0)
				usage(argv[0]);
			else if (strcmp(name, "no-c-comments") == 0)
				config.allow_c_comments = 0;
			else if (strcmp(name, "no-yaml-comments") == 0)
				config.allow_yaml_comments = 0;
			else if (strcmp(name, "no-comments") == 0)
				config.allow_c_comments = config.allow_yaml_comments = 0;
			else if (strcmp(name, "format") == 0)
				format = 1;
			else if (strcmp(name, "verify") == 0)
				verify = 1;
			else if (strcmp(name, "max-nesting") == 0)
				config.max_nesting = atoi(optarg);
			else if (strcmp(name, "max-data") == 0)
				config.max_data = atoi(optarg);
			else if (strcmp(name, "indent-string") == 0)
				indent_string = strdup(optarg);
			break;
			}
		case 'o':
			output = strdup(optarg);
			break;
		default:
			break;
		}
	}
	if (config.max_nesting < 0)
		config.max_nesting = 0;
	if (!output)
		output = "-";
	if (optind >= argc)
		usage(argv[0]);

	for (i = optind; i < argc; i++) {
		if (format)
			ret = do_format(&config, argv[i], output);
		else if (verify)
			ret = do_verify(&config, argv[i]);
		else
			ret = do_parse(&config, argv[i]);
		if (ret)
			exit(ret);
	}
	return ret;
}
