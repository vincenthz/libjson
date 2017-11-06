#include <catch.hpp>

#include "json.h"

#include <queue>
#include <map>
#include <fstream>
#include <cstring>

using namespace std;

namespace {

// Map that binds a string describing a json_type to its corresponding
// json_type enum value
const map<string, json_type> string_to_type_map() {
    map<string, json_type> map;

    map.insert(pair<string, json_type>("JSON_NONE", JSON_NONE));
    map.insert(pair<string, json_type>("JSON_ARRAY_BEGIN", JSON_ARRAY_BEGIN));
    map.insert(pair<string, json_type>("JSON_OBJECT_BEGIN", JSON_OBJECT_BEGIN));
    map.insert(pair<string, json_type>("JSON_ARRAY_END", JSON_ARRAY_END));
    map.insert(pair<string, json_type>("JSON_OBJECT_END", JSON_OBJECT_END));
    map.insert(pair<string, json_type>("JSON_INT", JSON_INT));
    map.insert(pair<string, json_type>("JSON_FLOAT", JSON_FLOAT));
    map.insert(pair<string, json_type>("JSON_STRING", JSON_STRING));
    map.insert(pair<string, json_type>("JSON_KEY", JSON_KEY));
    map.insert(pair<string, json_type>("JSON_TRUE", JSON_TRUE));
    map.insert(pair<string, json_type>("JSON_FALSE", JSON_FALSE));
    map.insert(pair<string, json_type>("JSON_NULL", JSON_NULL));
    map.insert(pair<string, json_type>("JSON_BSTRING", JSON_BSTRING));
    map.insert(pair<string, json_type>("JSON_PARTIAL_KEY", JSON_PARTIAL_KEY));
    map.insert(pair<string, json_type>("JSON_PARTIAL_VALUE", JSON_PARTIAL_VALUE));
    map.insert(pair<string, json_type>("JSON_PARTIAL_STRING", JSON_PARTIAL_STRING));

    return map;
}

// TEST RESOURCES
const string RESOURCES_PATH = "unit-tests/resources/";
const string SIMPLE_DOC = RESOURCES_PATH + "simple_doc.json";
const string SIMPLE_DOC_EVENTS = RESOURCES_PATH + "simple_doc.events";
const string COMPLETE_DOC = RESOURCES_PATH + "complete_doc.json";
const string COMPLETE_DOC_EVENTS = RESOURCES_PATH + "complete_doc.events";
const string COMPLETE_DOC_COMPRESSED = RESOURCES_PATH + "complete_doc_compressed.json";
const string COMPLETE_DOC_PARTIAL_MODE_EVENTS = RESOURCES_PATH + "complete_doc_partial_mode.events";
const string COMPLETE_DOC_INPLACE_EVENTS = RESOURCES_PATH + "complete_doc_inplace.events";
const string COMPLETE_DOC_SPLIT = RESOURCES_PATH + "complete_doc_split.json";
const string COMPLETE_DOC_SPLIT_EVENTS = RESOURCES_PATH + "complete_doc_split.events";
const string COMPLETE_DOC_SPLIT_INPLACE_EVENTS = RESOURCES_PATH + "complete_doc_split_inplace.events";
const string DATA_LIMIT_CHUNKS = RESOURCES_PATH + "data_limit_chunks.json";
const string NESTING_LIMIT_CHUNKS = RESOURCES_PATH + "nesting_limit_chunks.json";
const string CHUNKS_DOC_LAST_VALUE = RESOURCES_PATH + "chunks_last_value.json";
const string CHUNKS_DOC_LAST_VALUE_EVENTS = RESOURCES_PATH + "chunks_last_value.events";
const string DATA_LIMIT_DOC = RESOURCES_PATH + "data_limit_doc.json";
const string DATA_LIMIT_DOC_EVENTS = RESOURCES_PATH + "data_limit_doc.events";

/**
 * Wrapper of the json_parser library that handles callbacks internally and provide a
 * data structure describing the caught events
 */
class JSONParserEventsCollector {
public:
    JSONParserEventsCollector(json_config* config) {
        REQUIRE(InitializeParser(config) == 0);
    }

    JSONParserEventsCollector() {
        json_config config;
        memset(&config, 0, sizeof(json_config));
        REQUIRE(InitializeParser(&config) == 0);
    }

    ~JSONParserEventsCollector() {
        json_parser_free(&json_parser_);
    }

    int ProcessString(string s) {
        return json_parser_string(&json_parser_, s.c_str(), s.size(), &processed_);
    }

    uint32_t GetProcessedBytes() { return processed_; }

    queue<pair<int, string> > GetGeneratedEventsQueue() { return generated_events_queue_; }

    json_parser& GetParserInternalStatus() { return json_parser_; }

    bool IsFinalState() { return json_parser_is_done(&json_parser_); }

private:
    json_config json_config_;
    json_parser json_parser_;

    uint32_t processed_;
    queue<pair<int, string> > generated_events_queue_;

    int InitializeParser(json_config* config) {
        memset(&json_parser_, 0, sizeof(json_parser_));
        json_config_ = *config;

        return json_parser_init(&json_parser_,
                                &json_config_,
                                &JsonParserCallback,
                                &generated_events_queue_);
    }

    static int JsonParserCallback(void *user_data, int type, const char *data, uint32_t length) {
        queue<pair<int, string> >* generated_events_queue =
            static_cast<queue<pair<int, string> >*>(user_data);

        generated_events_queue->push(make_pair(type, string(data, length)));

        return 0;
    }

};

/**
 * Wrapper of the json_printer that handles callbacks internally and provide a
 * string containing the printed document
 */
class JSONPrinterEventsCollector {
public:
    JSONPrinterEventsCollector() {
        int error = json_print_init(&json_printer_,
                                    &JsonPrinterCallback,
                                    &printed_document_);
        REQUIRE(!error);
    }

    ~JSONPrinterEventsCollector() {
        json_print_free(&json_printer_);
    }

    int PrintJSONDocument(queue<pair<int, string> >& events) {
        pair<int, string> event;
        int ret = 0;
        while (!ret && !events.empty()) {
            event = events.front();

            ret = json_print_raw(&json_printer_,
                                 event.first,
                                 event.second.c_str(),
                                 static_cast<uint32_t>(event.second.length()));

            events.pop();
        }

        return ret;
    }

    string& GetPrintedDocument() { return printed_document_; }

private:
    json_printer json_printer_;

    string printed_document_;

    static int JsonPrinterCallback(void *user_data, const char *data, uint32_t length) {
        string *printed_document = static_cast<string*>(user_data);
        printed_document->append(data, length);
        return 0;
    }
};

string ReadContentOfFile(const string file) {
    ifstream input_stream(file.c_str());
    if(input_stream.fail())
        throw "Error opening file " + file;

    string s((istreambuf_iterator<char>(input_stream)), istreambuf_iterator<char>());

    return s;
}

/**
 * Reads a file chunk by chunk based on the provided separator and parses each
 * chunk as a string into a queue
 */
queue<string> ReadFileChunkByChunk(const string file, const char separator) {
    ifstream input_stream(file.c_str());
    if(input_stream.fail())
        throw "Error opening file " + file;

    queue<string> queue;
    string line;
    while (getline(input_stream, line, separator))
    {
        queue.push(line);
    }

    return queue;
}

/**
 * Reads a file line by line and parses each line as a string into a queue
 */
queue<string> ReadFileLineByLine(const string file) {
    return ReadFileChunkByChunk(file, '\n');
}

/**
 * Reads a file containing a list of events describing the structure of a JSON
 * document.
 * Every line of the file is composed as follows:
 * JSON_TYPE:value
 */
queue<pair<int, string> > LoadEventsQueueFromFile(const string file) {
    ifstream input_stream(file.c_str());
    if(input_stream.fail())
        throw "Error opening file " + file;

    queue<pair<int, string> > queue;
    const map<string, json_type> conversion_map = string_to_type_map();

    string type, value;
    while (getline(input_stream, type, ':') && getline(input_stream, value))
    {
        queue.push(make_pair(conversion_map.at(type), value));
    }
    return queue;
}

void RequireEqualEventsQueues(queue<pair<int, string> >& events_queue,
                              queue<pair<int, string> >& expected_events_queue) {
    pair<int, string> parsed, expected;

    int line = 1;
    while (!events_queue.empty() && !expected_events_queue.empty()) {
        parsed = events_queue.front();
        expected = expected_events_queue.front();

        INFO("Check line: " << line++);
        INFO("Parsed: " << parsed.first << ":" << parsed.second);
        INFO("Expected: " << expected.first << ":" << expected.second);
        REQUIRE(parsed.first == expected.first);
        REQUIRE(parsed.second == expected.second);

        events_queue.pop();
        expected_events_queue.pop();
    }
    REQUIRE(events_queue.empty());
    REQUIRE(expected_events_queue.empty());
}

inline void ParseJSONDocument(JSONParserEventsCollector& parser,
                              const string document) {
    INFO("Parsed json document:\n" << document);
    REQUIRE(parser.ProcessString(document) == 0);
    REQUIRE(parser.GetProcessedBytes() == document.length());
}


void ParseChunckedDocument(JSONParserEventsCollector& parser,
                           queue<string> document) {
        string chunk;
        while (!document.empty()){
            chunk = document.front();
            ParseJSONDocument(parser, chunk);
            // In case partial data callbacks are enabled check that no internal buffer is kept
            if (parser.GetParserInternalStatus().config.mode == PARTIAL_DATA_CALLBACKS) {
                    REQUIRE(parser.GetParserInternalStatus().buffer_offset == 0);
            }
            // In case in place parsing is enabled check that buffer size is always zero
            if (parser.GetParserInternalStatus().config.mode == IN_PLACE) {
                    REQUIRE(parser.GetParserInternalStatus().buffer_size == 0);
            }

            document.pop();
            if (!document.empty())
                REQUIRE(!parser.IsFinalState());
        }
        REQUIRE(parser.IsFinalState());
}

void RequireEscapedCharactersAreCorrectlyTransformed(JSONParserEventsCollector& parser) {
    WHEN("UNICODE escaped sequences are parsed") {
        const string doc = "[\"\\uf944\\ufbde\\ufe3b\\u277a\\u260e\\u2108\\u0123\\u4567\\u89AB\\uCDEF\\uabcd\\uef4A\"";
        ParseJSONDocument(parser, doc);

        THEN("all UNICODE escaped sequences are transformed into UTF-8 encoded characters") {
            string parsed_string = parser.GetGeneratedEventsQueue().back().second;
            REQUIRE(parsed_string == "籠ﯞ︻❺☎℈ģ䕧覫췯ꯍ");
        }
    }

    WHEN("escaped sequences are parsed") {
        const string doc = "[\"\\b\\f\\n\\r\\t\\\"\\\\\\/\"";
        ParseJSONDocument(parser, doc);

        THEN("all escaped sequences are transformed into the corresponding representation") {
            string parsed_string = parser.GetGeneratedEventsQueue().back().second;
            REQUIRE(parsed_string == "\b\f\n\r\t\"\\/");
        }
    }
}

void RequireDataLimitsAreApplied(json_config config) {
    WHEN("max value length is set to 6") {
        config.max_data = 6;
        JSONParserEventsCollector parser(&config);

        AND_WHEN("a value longer than 6 is parsed") {
            THEN("an error is returned") {
                queue<string> document = ReadFileLineByLine(DATA_LIMIT_CHUNKS);
                string chunk;
                while (!document.empty()){
                    chunk = document.front();
                    int state = parser.ProcessString(chunk);

                    INFO("Parsed json document:\n" << chunk);
                    REQUIRE(state == JSON_ERROR_DATA_LIMIT);

                    document.pop();
                }
            }
        }

        AND_WHEN("a value 6 bytes long is parsed") {
            string doc = "{\"key001\":\"value1\",       \"key002\":123456, \"key003\":[1,2,3,4,5,6], "
                         "\"key004\":\"\\t\\n\\b\\r\\f\\\\\",\"key005\":\"\u0130\u0130\u0130\",\"key006\":\"\\u0130AAAA\"}";

            THEN("parsing is successful") {
                ParseJSONDocument(parser, doc);
                REQUIRE(parser.IsFinalState());
            }
        }
    }
}

void RequireNestingLimitsAreApplied(json_config config) {
    WHEN("max nesting depth is set to 3") {
        config.max_nesting = 3;
        JSONParserEventsCollector parser(&config);

        AND_WHEN("a document deeper than 3 is parsed") {
            THEN("an error is returned") {
                queue<string> document = ReadFileLineByLine(NESTING_LIMIT_CHUNKS);
                string chunk;
                while (!document.empty()){
                    chunk = document.front();
                    int state = parser.ProcessString(chunk);

                    INFO("Parsed json document:\n" << chunk);
                    REQUIRE(state == JSON_ERROR_NESTING_LIMIT);

                    document.pop();
                }
            }
        }

        AND_WHEN("a document 3 levels deep is parsed") {
            string doc = "{\"key\":[{\"key\":\"value\"},{\"key\":\"value\"},{\"key\":\"value\"}]}";

            THEN("parsing is successful") {
                ParseJSONDocument(parser, doc);
                REQUIRE(parser.IsFinalState());
            }
        }
    }
}

} // namespace

SCENARIO("an entirely buffered JSON document needs to be parsed") {

    GIVEN("a parser with a default configuration") {
        JSONParserEventsCollector parser;

        WHEN("a simple JSON document is parsed by the library") {
            const string document = ReadContentOfFile(SIMPLE_DOC);
            ParseJSONDocument(parser, document);
            REQUIRE(parser.IsFinalState());

            THEN("all the data types are correctly identified") {
                queue<pair<int, string> > events_queue =
                    parser.GetGeneratedEventsQueue();
                queue<pair<int, string> > expected_events_queue =
                    LoadEventsQueueFromFile(SIMPLE_DOC_EVENTS);

                RequireEqualEventsQueues(events_queue, expected_events_queue);
            }
        }

        WHEN("a complete JSON document is parsed by the library") {
            const string document = ReadContentOfFile(COMPLETE_DOC);
            ParseJSONDocument(parser, document);
            REQUIRE(parser.IsFinalState());

            THEN("all the data types are correctly identified") {
                queue<pair<int, string> > events_queue =
                    parser.GetGeneratedEventsQueue();
                queue<pair<int, string> > expected_events_queue =
                    LoadEventsQueueFromFile(COMPLETE_DOC_EVENTS);

                RequireEqualEventsQueues(events_queue, expected_events_queue);
            }
        }
    }


    GIVEN("a parser with partial data callbacks mode enabled") {
        json_config config;
        memset(&config, 0, sizeof(json_config));
        config.mode = PARTIAL_DATA_CALLBACKS;
        JSONParserEventsCollector parser(&config);

        WHEN("a complete JSON document is parsed by the library") {
            const string document = ReadContentOfFile(COMPLETE_DOC);
            ParseJSONDocument(parser, document);
            REQUIRE(parser.IsFinalState());

            THEN("all the data types are correctly identified") {
                queue<pair<int, string> > events_queue =
                    parser.GetGeneratedEventsQueue();
                queue<pair<int, string> > expected_events_queue =
                    LoadEventsQueueFromFile(COMPLETE_DOC_PARTIAL_MODE_EVENTS);

                RequireEqualEventsQueues(events_queue, expected_events_queue);
            }
        }
    }


    GIVEN("a parser with in-place parsing mode enabled") {
        json_config config;
        memset(&config, 0, sizeof(json_config));
        config.mode = IN_PLACE;
        JSONParserEventsCollector parser(&config);

        WHEN("a complete JSON document is parsed by the library") {
            const string document = ReadContentOfFile(COMPLETE_DOC);
            ParseJSONDocument(parser, document);
            REQUIRE(parser.IsFinalState());

            THEN("all the data types are correctly identified") {
                queue<pair<int, string> > events_queue =
                    parser.GetGeneratedEventsQueue();
                queue<pair<int, string> > expected_events_queue =
                    LoadEventsQueueFromFile(COMPLETE_DOC_INPLACE_EVENTS);

                RequireEqualEventsQueues(events_queue, expected_events_queue);
            }
        }
    }
}

SCENARIO("a partially buffered JSON document needs to be parsed") {

    GIVEN("a parser with a default configuration") {
        WHEN("a chunked JSON document is parsed by the library") {
            JSONParserEventsCollector parser;

            queue<string> document = ReadFileLineByLine(COMPLETE_DOC_SPLIT);
            ParseChunckedDocument(parser, document);

            THEN("all the data types are correctly identified") {
                queue<pair<int, string> > events_queue =
                    parser.GetGeneratedEventsQueue();
                queue<pair<int, string> > expected_events_queue =
                    LoadEventsQueueFromFile(COMPLETE_DOC_EVENTS);

                RequireEqualEventsQueues(events_queue, expected_events_queue);
            }
        }

        WHEN("the parser is fed with a chunk ending with a primitive value") {
            queue<string> chunks = ReadFileChunkByChunk(CHUNKS_DOC_LAST_VALUE, '#');
            queue<pair<int, string> > expected_last_values_events_queue =
                LoadEventsQueueFromFile(CHUNKS_DOC_LAST_VALUE_EVENTS);

            THEN("the primitive value is correctly parsed") {
                while (!chunks.empty()) {
                    JSONParserEventsCollector parser;
                    ParseJSONDocument(parser, chunks.front());
                    INFO("Parsed chunks: " + chunks.front());
                    REQUIRE(parser.GetGeneratedEventsQueue().back().first ==
                            expected_last_values_events_queue.front().first);
                    REQUIRE(parser.GetGeneratedEventsQueue().back().second ==
                            expected_last_values_events_queue.front().second);

                    chunks.pop();
                    expected_last_values_events_queue.pop();
                }
            }
        }
    }


    GIVEN("a parser with partial data callbacks mode enabled") {
        json_config config;
        memset(&config, 0, sizeof(json_config));
        config.mode = PARTIAL_DATA_CALLBACKS;
        JSONParserEventsCollector parser(&config);

        WHEN("a chunked JSON document is parsed by the library") {
            queue<string> document = ReadFileLineByLine(COMPLETE_DOC_SPLIT);

            THEN("the library doesn't keep any buffered data after parsing a chunk "
                 "and all the data types are correctly identified") {
                ParseChunckedDocument(parser, document);

                queue<pair<int, string> > events_queue =
                    parser.GetGeneratedEventsQueue();
                queue<pair<int, string> > expected_events_queue =
                    LoadEventsQueueFromFile(COMPLETE_DOC_SPLIT_EVENTS);

                RequireEqualEventsQueues(events_queue, expected_events_queue);
            }
        }
    }


    GIVEN("a parser with in-place parsing enabled") {
        json_config config;
        memset(&config, 0, sizeof(json_config));
        config.mode = IN_PLACE;
        JSONParserEventsCollector parser(&config);

        WHEN("a chunked JSON document is parsed by the library") {
            queue<string> document = ReadFileLineByLine(COMPLETE_DOC_SPLIT);

            THEN("the library doesn't keep any buffered data after parsing a chunk "
                 "and all the data types are correctly identified") {
                ParseChunckedDocument(parser, document);

                queue<pair<int, string> > events_queue =
                    parser.GetGeneratedEventsQueue();
                queue<pair<int, string> > expected_events_queue =
                    LoadEventsQueueFromFile(COMPLETE_DOC_SPLIT_INPLACE_EVENTS);

                RequireEqualEventsQueues(events_queue, expected_events_queue);
            }
        }
    }
}

SCENARIO("a JSON document containing escaped characters need to be parsed") {

    GIVEN("a parser with a default configuration") {
        JSONParserEventsCollector parser;

        RequireEscapedCharactersAreCorrectlyTransformed(parser);
    }


    GIVEN("a parser with partial data callbacks mode enabled") {
        json_config config;
        memset(&config, 0, sizeof(json_config));
        config.mode = PARTIAL_DATA_CALLBACKS;
        JSONParserEventsCollector parser(&config);

        RequireEscapedCharactersAreCorrectlyTransformed(parser);
    }


    GIVEN("a parser with inplace mode enabled") {
        json_config config;
        memset(&config, 0, sizeof(json_config));
        config.mode = IN_PLACE;
        JSONParserEventsCollector parser(&config);

        WHEN("escaped sequences are parsed") {
            const string doc = "[\"\\uf944\\ufbde\\ufe3b\\uD800\\uDC00\\b\\f\\n\\r\\t\\\"\\\\\\/\"";
            ParseJSONDocument(parser, doc);

            THEN("all escaped sequences are not transformed into the corresponding representation") {
                string parsed_string = parser.GetGeneratedEventsQueue().back().second;
                REQUIRE(parsed_string == "\\uf944\\ufbde\\ufe3b\\uD800\\uDC00\\b\\f\\n\\r\\t\\\"\\\\\\/");
            }
        }
    }
}

SCENARIO("a JSON document needs to be printed based on the events that describe its structure") {

    GIVEN("the events describing a JSON document") {
        JSONPrinterEventsCollector printer;
        queue<pair<int, string> > events_queue =
            LoadEventsQueueFromFile(COMPLETE_DOC_EVENTS);

        WHEN("the events are provided to the printer") {
            REQUIRE(printer.PrintJSONDocument(events_queue) == 0);

            THEN("a valid JSON document is produced") {
                REQUIRE(printer.GetPrintedDocument() ==
                        ReadContentOfFile(COMPLETE_DOC_COMPRESSED));
            }
        }
    }
}

SCENARIO("parsing limits need to be applied when parsing a JSON document") {
    json_config config;
    memset(&config, 0, sizeof(json_config));

    GIVEN("a parser with a default parsing mode") {
        RequireDataLimitsAreApplied(config);

        RequireNestingLimitsAreApplied(config);
    }


    GIVEN("a parser with partial data callbacks mode enabled") {
        config.mode = PARTIAL_DATA_CALLBACKS;

        WHEN("max value length is set to 4") {
            config.max_data = 4;
            JSONParserEventsCollector parser(&config);

            AND_WHEN("a JSON document containing values longer than 4 is parsed by the library") {
                const string document = ReadContentOfFile(DATA_LIMIT_DOC);
                ParseJSONDocument(parser, document);
                REQUIRE(parser.IsFinalState());

                THEN("PARTIAL_DATA events are generated to split values bigger than 4") {
                    queue<pair<int, string> > events_queue =
                        parser.GetGeneratedEventsQueue();
                    queue<pair<int, string> > expected_events_queue =
                        LoadEventsQueueFromFile(DATA_LIMIT_DOC_EVENTS);

                    RequireEqualEventsQueues(events_queue, expected_events_queue);
                }
            }
        }
    }


    GIVEN("a parser with inplace mode enabled") {
        config.mode = IN_PLACE;

        RequireNestingLimitsAreApplied(config);
    }
}
