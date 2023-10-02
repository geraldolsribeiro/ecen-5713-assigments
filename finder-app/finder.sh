#!/bin/bash -e

if [ $# != 2 ]; then exit 1; fi

write_dir=$1
shift
text=$1
shift

pushd "$write_dir" || exit 1
num_files=$( find . -type f | wc -l )
num_matches=$( grep "$text" * | wc -l )

echo "The number of files are ${num_files} and the number of matching lines are ${num_matches}"
