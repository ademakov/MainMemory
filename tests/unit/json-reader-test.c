#include <stdio.h>
#include <stdlib.h>

#include "base/json.h"

static int fail = 0;

const char *
token_string(mm_json_token_t token)
{
	switch (token) {
	case MM_JSON_INITIAL:
		return "initial";
	case MM_JSON_PARTIAL:
		return "partial";
	case MM_JSON_INVALID:
		return "invalid";
	case MM_JSON_START_DOCUMENT:
		return "start-document";
	case MM_JSON_END_DOCUMENT:
		return "end-document";
	case MM_JSON_START_OBJECT:
		return "start-object";
	case MM_JSON_END_OBJECT:
		return "end-object";
	case MM_JSON_START_ARRAY:
		return "start-array";
	case MM_JSON_END_ARRAY:
		return "end-array";
	case MM_JSON_NAME:
		return "name";
	case MM_JSON_STRING:
		return "string";
	case MM_JSON_NUMBER:
		return "number";
	case MM_JSON_FALSE:
		return "false";
	case MM_JSON_TRUE:
		return "true";
	case MM_JSON_NULL:
		return "null";
	default:
		return "invalid";
	}
}

void
test_single(const char *text, mm_json_token_t *tokens)
{
	struct mm_json_reader reader;
	mm_json_reader_prepare(&reader, &mm_global_arena);

	mm_json_reader_feed(&reader, text, strlen(text));
	for (size_t i = 0; tokens[i] != (mm_json_token_t) -1; i++) {
		mm_json_token_t token = mm_json_reader_next(&reader);
		if (token != tokens[i]) {
			fprintf(stderr, "# expect: %s\n",
				token_string(tokens[i]));
			fprintf(stderr, "# really: %s\n",
				token_string(token));
			fail++;
		}
	}

	mm_json_reader_cleanup(&reader);
}

int
main()
{
	test_single("", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_PARTIAL,
		-1});

	test_single("x", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single(",", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single(":", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});

	test_single("false", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_FALSE,
		MM_JSON_END_DOCUMENT,
		MM_JSON_PARTIAL,
		-1});
	test_single("true", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_TRUE,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("null", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NULL,
		MM_JSON_END_DOCUMENT,
		-1});

	test_single("\"\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"foo\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});

	test_single("[]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("{}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_END_OBJECT,
		MM_JSON_END_DOCUMENT,
		-1});

	test_single("[false]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_FALSE,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("[false, true]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_FALSE,
		MM_JSON_TRUE,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("[false, true, null]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_FALSE,
		MM_JSON_TRUE,
		MM_JSON_NULL,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("[[[[]]]]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_START_ARRAY,
		MM_JSON_START_ARRAY,
		MM_JSON_START_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("[[],[],[]]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_START_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_START_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_START_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("[{},{},{}]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_START_OBJECT,
		MM_JSON_END_OBJECT,
		MM_JSON_START_OBJECT,
		MM_JSON_END_OBJECT,
		MM_JSON_START_OBJECT,
		MM_JSON_END_OBJECT,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		-1});

	test_single("[x]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_INVALID,
		-1});
	test_single("[,]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_INVALID,
		-1});
	test_single("[:]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_INVALID,
		-1});
	test_single("[}]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_INVALID,
		-1});
	test_single("[]]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("[false,]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_FALSE,
		MM_JSON_INVALID,
		-1});

	test_single("{\"foo\" : false}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_NAME,
		MM_JSON_FALSE,
		MM_JSON_END_OBJECT,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("{\"\" : false, \"\" : true}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_NAME,
		MM_JSON_FALSE,
		MM_JSON_NAME,
		MM_JSON_TRUE,
		MM_JSON_END_OBJECT,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("{\"\" : false, \"\" : true, \"\" : null}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_NAME,
		MM_JSON_FALSE,
		MM_JSON_NAME,
		MM_JSON_TRUE,
		MM_JSON_NAME,
		MM_JSON_NULL,
		MM_JSON_END_OBJECT,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("{\"\" : {}, \"\" : {}, \"\" : {}}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_NAME,
		MM_JSON_START_OBJECT,
		MM_JSON_END_OBJECT,
		MM_JSON_NAME,
		MM_JSON_START_OBJECT,
		MM_JSON_END_OBJECT,
		MM_JSON_NAME,
		MM_JSON_START_OBJECT,
		MM_JSON_END_OBJECT,
		MM_JSON_END_OBJECT,
		MM_JSON_END_DOCUMENT,
		-1});

	test_single("{x}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_INVALID,
		-1});
	test_single("{,}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_INVALID,
		-1});
	test_single("{:}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_INVALID,
		-1});
	test_single("{]}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_INVALID,
		-1});
	test_single("{}}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_END_OBJECT,
		MM_JSON_END_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("{\"\":}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_NAME,
		MM_JSON_INVALID,
		-1});
	test_single("{\"\":false,}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_NAME,
		MM_JSON_FALSE,
		MM_JSON_INVALID,
		-1});

	test_single("\"\\b\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\f\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\n\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\r\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\t\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\/\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\\"\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\\\\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\u0123\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\u4567\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\u89ab\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\ucdef\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\u00AB\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("\"\\uCDEF\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_STRING,
		MM_JSON_END_DOCUMENT,
		-1});

	test_single("\"\x01\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("\"\x1f\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("\"\\z\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("\"\\u \"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("\"\\u0\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("\"\\u01\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("\"\\u012\"", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});

	test_single("0", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_PARTIAL,
		-1});

	test_single("0 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("1 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("9 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("0.1 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("0e0 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("0e1 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("1e1 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("123 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("123.4 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("123.456 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("123.456e7 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("123.456e78 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("123.456e789 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("123.456E+7 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("123.456E-7 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("-0 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("-1 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("-123.456E-789 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_NUMBER,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("[0]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_NUMBER,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("[0, 1]", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_ARRAY,
		MM_JSON_NUMBER,
		MM_JSON_NUMBER,
		MM_JSON_END_ARRAY,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("{\"\" : 0}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_NAME,
		MM_JSON_NUMBER,
		MM_JSON_END_OBJECT,
		MM_JSON_END_DOCUMENT,
		-1});
	test_single("{\"\" : 0, \"\" : 1}", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_START_OBJECT,
		MM_JSON_NAME,
		MM_JSON_NUMBER,
		MM_JSON_NAME,
		MM_JSON_NUMBER,
		MM_JSON_END_OBJECT,
		MM_JSON_END_DOCUMENT,
		-1});

	test_single("- ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("01 ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("1. ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("1e ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("1e- ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});
	test_single("1e+ ", (mm_json_token_t[]) {
		MM_JSON_START_DOCUMENT,
		MM_JSON_INVALID,
		-1});

	return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
