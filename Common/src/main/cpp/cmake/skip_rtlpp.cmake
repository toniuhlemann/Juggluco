# Windows-Host-Ersatz fuer das rtlpp-Werkzeug (braucht dort Ninja+Compiler+ICU, siehe
# CMakeLists.txt). rtlpp expandiert die RTL()/RTLFMT()-Marker zu bidi-umgeformten
# String-Literalen; hier werden die Marker stattdessen zu Identitaets-Makros erklaert.
# Einzige Folge: arabische UI-Labels rendern in Lese- statt Darstellungsreihenfolge.
file(READ "${RTLPP_IN}" _content)
# u8"..." ist seit C++20 char8_t und passt nicht mehr auf std::string_view; rtlpps echte
# Ausgabe erzeugt plain-Literale. Der Prefix faellt weg, die UTF-8-Bytes bleiben identisch.
string(REPLACE "u8\"" "\"" _content "${_content}")
string(REPLACE "u8R\"" "R\"" _content "${_content}")
file(WRITE "${RTLPP_OUT}" "#define RTL(...) __VA_ARGS__\n#define RTLFMT(...) __VA_ARGS__\n${_content}")
