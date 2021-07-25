#!/bin/bash

CONV_NAME() {
	sed -E 's/[^a-z0-9_]/_/g'
}

CONV_WHAT() {
	sed ':a;N;$!ba;s/"/\\"/g;s/\n/\\n"\n"/g;s/\t/\\t/g'
}

let num=0

for i in $* ; do
	let num=$num+1
done

echo "// LOL WHO FILES"
echo "#define NUM_WEBPAGE_FILES $num"
for i in $* ; do
	echo "char *webpage_file_$(echo "$i" | CONV_NAME) ="
	echo "\"$(cat "$i" | CONV_WHAT)\";"
done
echo "char *webpage_filenames[$num] = {"
for i in $* ; do
	echo "	\"/$(echo "$i" | CONV_WHAT)\","
done
echo "};"
echo "char **webpage_files[$num] = {"
for i in $* ; do
	echo "	&webpage_file_$(echo "$i" | CONV_NAME),"
done
echo "};"
