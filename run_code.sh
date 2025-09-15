#!/usr/bin/zsh

#[[ -f "$1" ]] && echo "$1 file exist." || echo "$1 file does not exist."
# How to get file name, file path, file extension etc:
# https://zaiste.net/posts/zsh-get-filename-extension-path/
# Check if input file exists
if [[ -d "$1" ]] 
then 
  echo "$1 directory exist."
else 
  echo "$1 directory does not exist."
  exit
fi

set -x # print each command executed with the expanded variables
program_dir="$1"
EXE_FILE_NAME=${program_dir#*/}
OUTPUT_FILE_DIR="output-file"
OUTPUT_FILE_PATH="$OUTPUT_FILE_DIR/$EXE_FILE_NAME:t:r.output"

#clang-format -i PROGRAM_FILE_PATH
# output to terminal
#./waf --run $EXE_FILE_NAME --cwd=$OUTPUT_FILE_DIR

# output to OUTPUT_FILE
./waf --run $EXE_FILE_NAME --cwd=$OUTPUT_FILE_DIR > $OUTPUT_FILE_PATH 2>&1

# using gdb to debug
#./waf run $EXE_FILE_NAME --cwd=$OUTPUT_FILE_DIR --command-template="gdb %s"

# using gdb to debug
#./waf --run="scratch/grad-pc/grad-pc" --command-template="gdb --args %s --num_nodes=49 --use_gradpc=true --send_packet_num=600" --cwd=output-file

# run simulation with input arguments
#./waf --run="scratch/grad-pc/grad-pc --show_dsr_log=true" --cwd=output-file
