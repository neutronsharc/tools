#!/bin/bash
if [ $# -lt 2 ]; then
  echo "usage: $0 [thread id] [chart name]"
  exit -1
fi

bin_dir=/home/shawn/code/FlameGraph

thread_id=$1
chart_name=$2

time_sec=10  # collect 10 seconds worth of trace
echo "will profile thread $1 for ${time_sec} seconds, then save to chart \"${chart_name}.svg\""
echo bin dir=${bin_dir}

# 1. perf record
echo "will collect data for ${time_sec} seconds"
sudo perf record -g -t ${thread_id} --call-graph dwarf -o ${chart_name}.data -- sleep ${time_sec}

# 2. parse the data, feed to processing
echo "will process the data"
sudo perf script -i ${chart_name}.data | ${bin_dir}/stackcollapse-perf.pl  > ${chart_name}.folded

# 3. convert processed data to svg graph
echo "will generate svg chart"
${bin_dir}/flamegraph.pl ${chart_name}.folded > ${chart_name}.svg

