import sys, re
while 1:
	line = sys.stdin.readline()
	if not line:
		break
	line = line.replace("pt_node_t", "JsonNode").replace("pt_boolean_get", "json_get_bool").replace("pt_array_new", "json_mkarray").replace("pt_double_get", "json_get_number").replace("pt_double_new", "json_mknumber").replace("pt_integer_get", "(int)json_get_number").replace("pt_string_get", "json_get_string").replace("pt_string_new", "json_mkstring").replace("pt_map_set", "json_append_member").replace("pt_map_get", "json_find_member").replace("pt_integer_new(", "json_mknumber((double)").replace("pt_array_get", "json_find_element").replace("pt_map_new", "json_mkobject").replace("pt_array_push_back", "json_append_element").replace("pt_array_len", "json_get_num_mems").replace("pt_init()", "").replace("\"pillowtalk.h\"", "\"pouch.h\"\n#include \"json.h\"")
	sys.stdout.write(line)
