#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "hdr_histogram.h"

int main(int argc, char **argv) {
  int64_t lowest = 1;
  int64_t highest = 1000000000;
  int sig_digits = 3;
  struct hdr_histogram *histogram;
  //struct hdr_interval_recorder;

  int rv = hdr_init(lowest, highest, sig_digits, &histogram);

  // memory footprint:
  //  when range is [1, 1,000,000]:  mem usage = 90 KB
  //  when range is [1, 1,000,000,000]: mem usage = 170 KB
  //  when range is [1, 1,000,000,000,000]: mem usage = 250 KB
  printf("memory footprint: %ld\n", hdr_get_memory_size(histogram));
  int max = 10000000;
  int min = 1;
  for (int i = max; i >= min; i--) {
    hdr_record_value(histogram, i);
  }
  printf("memory footprint afterwards: %ld\n", hdr_get_memory_size(histogram));

  int p50 = hdr_value_at_percentile(histogram, 50.);
  int p90 = hdr_value_at_percentile(histogram, 90.);
  int p95 = hdr_value_at_percentile(histogram, 95.);
  int p99 = hdr_value_at_percentile(histogram, 99.);
  printf("min value = %ld, max-value = %ld, 50%% = %d, 90%% = %d, "
         "95%% = %d, 99%% = %d\n",
         hdr_min(histogram),
         hdr_max(histogram),
         p50,
         p90,
         p95,
         p99);

  hdr_percentiles_print(histogram, stdout, 5, 1.0, CLASSIC);

  return 0;
}
