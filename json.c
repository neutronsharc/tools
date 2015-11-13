#include <stdio.h>
//#include <jansson.h>

#include "parson.h"



int main(int argc, char** argv) {

  if (argc < 2) {
    printf("Usage: %s <json file>\n", argv[0]);
    return -1;
  }

  JSON_Value *root_value;
  char* json_file= argv[1];


  root_value = json_parse_file_with_comments(json_file);
  printf("parse cfg file %s ret %p\n", json_file, root_value);
  if (!root_value) {
    printf("parse cfg %s failed\n", json_file);
    return -1;
  }

  int t = json_value_get_type(root_value);
  printf("top json value type: %d\n", t);

  JSON_Object* root_obj = json_value_get_object(root_value);
  JSON_Array* pools = json_object_dotget_array(root_obj, "pools");

  int num_pools = json_array_get_count(pools);
  printf("pools array %p, %d elems\n", pools, num_pools);

  for (int i = 0; i < num_pools; i++) {

    JSON_Object* pool = json_array_get_object(pools, i);

    printf("[pool %d]\n", i + 1);
    printf("name = %s\n", json_object_get_string(pool, "name"));

    JSON_Array * shards = json_object_get_array(pool, "shards");

    int num_shards = json_array_get_count(shards);
    printf("have %d shards\n", num_shards);

    for (int s = 0; s < num_shards; s++) {

      JSON_Object* shard = json_array_get_object(shards, s);

      JSON_Array * slaves = json_object_get_array(shard, "slave");
      const char* master = json_object_get_string(shard, "master");

      JSON_Value* vstart = json_object_get_value(shard, "range_start");
      JSON_Value* vend = json_object_get_value(shard, "range_end");

      int start = -1, end = -1;
      if (vstart) {
        start = json_value_get_number(vstart);
      }
      if (vend) {
        end = json_value_get_number(vend);
      }
      printf("shard %d: start %d, end %d, master %s, %d slaves\n",
              s, start, end, master,
              json_array_get_count(slaves));

    }
  }


  char* ps = json_serialize_to_string_pretty(root_value);
  //printf("json content:\n%s\n", ps);

  json_value_free(root_value);

  return 0;
}


