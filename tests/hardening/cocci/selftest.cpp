// Not built. Every rule in this directory must match something here;
// static-analysis.sh checks that, so a rule that stops matching (a
// Coccinelle upgrade, a typo) is noticed instead of passing quietly.
#include <glib.h>
#include <json.h>
#include <luna-service2/lunaservice.h>

int periodOf(json_object* root)
{
    json_object* period = json_object_object_get(root, "period");
    if (!period)
        return -1;
    return json_object_get_int(period);
}

bool continousOf(json_object* root)
{
    json_object* continous = NULL;
    if (!json_object_object_get_ex(root, "continous", &continous))
        return false;
    return json_object_get_boolean(continous);
}

void keepToken(GHashTable* table, LSMessage* m, int id)
{
    g_hash_table_insert(table, (gpointer) LSMessageGetUniqueToken(m), GINT_TO_POINTER(id));
}
