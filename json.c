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


  root_value = json_parse_file(json_file);
  printf("parse cfg file %s ret %p\n", json_file, root_value);

  if (root_value) {
    char* ps = json_serialize_to_string_pretty(root_value);
    printf("json content:\n%s\n", ps);

    json_value_free(root_value);
  }

  return 0;
}


