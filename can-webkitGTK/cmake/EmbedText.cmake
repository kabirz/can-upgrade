file(READ "${INPUT}" HTML_CONTENT)
string(LENGTH "${HTML_CONTENT}" CONTENT_LEN)

string(REPLACE "\\" "\\\\" HTML_CONTENT "${HTML_CONTENT}")
string(REPLACE "\"" "\\\"" HTML_CONTENT "${HTML_CONTENT}")
string(REPLACE "\n" "\\n\"\n\"" HTML_CONTENT "${HTML_CONTENT}")

file(WRITE "${OUTPUT}" "
/* Auto-generated from ${INPUT} - do not edit */
#pragma once
static const char ${VARNAME}[] = \"${HTML_CONTENT}\";
static const unsigned int ${VARNAME}_LEN = ${CONTENT_LEN};
")
