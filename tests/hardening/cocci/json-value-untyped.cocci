// A member read with json_object_get_int/_boolean/_string/_double whose
// type nobody checked. json-c converts any type on request: "abc" reads as
// period 0, the string "false" as continous true, {} as the name "{ }", and
// NaN is cast to int with undefined behaviour. Check json_object_is_type()
// on the value first.
@@
expression R, K;
identifier v;
type T;
@@
(
v = json_object_object_get(R, K);
|
T v = json_object_object_get(R, K);
|
json_object_object_get_ex(R, K, &v)
)
... when != json_object_is_type(v, ...)
(
* json_object_get_int(v)
|
* json_object_get_boolean(v)
|
* json_object_get_string(v)
|
* json_object_get_double(v)
)
