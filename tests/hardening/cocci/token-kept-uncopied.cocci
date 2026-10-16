// The unique token belongs to its message, and luna-service2 frees the
// message when the call returns, or when its subscription ends. A token put
// in a container as is - to be looked up later from the cancel callback - is
// read after it is freed. Store a copy (g_strdup(), std::string).
@@
expression T, M, V;
type C;
@@
(
* g_hash_table_insert(T, \(LSMessageGetUniqueToken(M)\|(C) LSMessageGetUniqueToken(M)\), V)
|
* g_hash_table_replace(T, \(LSMessageGetUniqueToken(M)\|(C) LSMessageGetUniqueToken(M)\), V)
|
* g_hash_table_add(T, \(LSMessageGetUniqueToken(M)\|(C) LSMessageGetUniqueToken(M)\))
|
* g_ptr_array_add(T, \(LSMessageGetUniqueToken(M)\|(C) LSMessageGetUniqueToken(M)\))
|
* g_slist_append(T, \(LSMessageGetUniqueToken(M)\|(C) LSMessageGetUniqueToken(M)\))
|
* g_slist_prepend(T, \(LSMessageGetUniqueToken(M)\|(C) LSMessageGetUniqueToken(M)\))
|
* g_list_append(T, \(LSMessageGetUniqueToken(M)\|(C) LSMessageGetUniqueToken(M)\))
|
* g_list_prepend(T, \(LSMessageGetUniqueToken(M)\|(C) LSMessageGetUniqueToken(M)\))
)
