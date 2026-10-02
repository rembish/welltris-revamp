# Turn a binary file into a C array: cmake -DIN=file -DOUT=file.c -DNAME=symbol -P embed.cmake
file(READ ${IN} HEX HEX)
string(LENGTH "${HEX}" N)
math(EXPR LEN "${N} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" BYTES "${BYTES}")
file(WRITE ${OUT} "const unsigned char ${NAME}[] = {\n${BYTES}\n};\nconst int ${NAME}_len = ${LEN};\n")
