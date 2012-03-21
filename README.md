libjson - simple and efficient json parser and printer in C
===========================================================

Introduction
------------

libjson is a simple library without any dependancies to parse and pretty print
the json format (RFC4627). The JSON format is a concise and structured data
format.

Features
--------

* interruptible parser: append data to the state how you want it.
* No object model integrated
* Small codebase: handcoded parser and efficient factorisation make the code smalls and perfect for embedding.
* Fast: use efficient code and small parsing tables for maximum efficiency.
* Full JSON support.
* UTF8 validation of the input.
* No number conversion: user convert data the way they want.
* Secure: optional limits on nesting level, and on data size.
* Optional comments: in YAML/python style and C style.
* Optional user defined allocation functions.

libjson parser is an interruptible handcoded state parse. the parser takes
character or string as input. Since it's interruptible, it's up to the
user to feed the stream to the parser, which permits complete flexibility
as to whether the data is coming from a pipe, a network socket, a file on disk,
a serial line, or crafted by the user.

The parser doesn't create an object tree for you, but each time it comes up
with an element in this data, it just callback to the user with the type found and
for some type, the data associated with it. It can be compared to the SAX way of XML,
hence it's called SAJ (Simple API for JSon).

The parser doesn't convert number to any native C format, but instead callback
with a string that is a valid JSon number. JSon number can be of any size,
so that's up to user to decide whetever or not, the number can map to native C type
int32\_t, int64\_t, or a complex integer type. As well the user has a choice to
refuse the integer at the callback stage if the length is not appropriate.

The parser optionally allows YAML and/or C comments to be ignored if the config
structure is set accordingly, otherwise a JSON\_ERROR\_COMMENT\_NOT\_ALLOWED is returned.

Embedding & Build system
------------------------

The primary use case of this pieces of code is providing JSON capability to
your program or library, without adding an extra build dependency.  You can add
it to your project directly, and integrate it without any fuss.

The \"build system\" available with the library is just a way to test that
everything in the library conforms to specifications and features. It's not
necessarily intended as a way to build portable dynamic library (.so or .dll).
It should works in simple case of building on Linux and BSDs though.

For others use (eg. windows, OS X, obscure unixes), it is much simpler to
integrate the library in your program or library directly.

Simple build fixes to build on more platforms will be accepted though.

Contributing
------------

Open a pull request with your new feature, simple code fix, or documentation
fixes. Please conform to coding style, and to the spirit of the library:
policy is not imposed by the library.

The Parser API
--------------

The parser API is really simple, totaling only 5 API calls:

 * json\_parser\_init
 * json\_parser\_char
 * json\_parser\_string
 * json\_parser\_is\_done
 * json\_parser\_free

json\_parser\_init initializes a new parser context from a parser config and
takes a callback + userdata. This callback function is used everything the
parser need to communicate a type and value to the client side of the library.

json\_parser\_char take one character and inject it in the parser. on parsing
success it will return a 0 value, but on parsing error it returns a parsing
error that represents the type of the error encounters. see JSON\_ERROR\_\*
for the full set of return values.

json\_parser\_string is similar to json\_parser\_char except that it takes a string
and a length.  it also returns the number of character processed, which is
useful when an parser error happened in the stream to pinpoint where.

json\_parser\_is\_done permits to test whetever or not the parser is in a
terminated state. it involves not beeing into any structure.

json\_parser\_free is the opposite of init, it just free the allocated structure.

The Printer API
---------------

the printer API is simple too:

 * json\_printer\_init
 * json\_printer\_free
 * json\_printer\_pretty
 * json\_printer\_raw

json\_printer\_init initialise a printing context and takes a callback + userdata
that will be called for every character that the printer wants to output. the
caller can have the printer callback redirect to anything it wants.

json\_printer\_free is the opposite of init

json\_printer\_raw takes a json type and an optional data and length value
depending on the type. it's up to the caller to verify that the order of type
are JSON-compliant, otherwise the generated document won't be able to be parsed
again.

json\_printer\_pretty works like json\_printer\_raw but is targetted for human
reading by appending newlines and spaces

Jsonlint utility program
------------------------

jsonlint utility provided with the library to verify, or reformat json stream.
also useful as example on how to use the library.
