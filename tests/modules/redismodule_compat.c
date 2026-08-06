/* Exercises the Redis-module compatibility layer (redismodule.h) added for
 * the Config Get/Set/Iterator API and the RedisModule_ACLCheckKeyPrefixPermissions
 * adapter shim. Deliberately includes "redismodule.h" instead of
 * "valkeymodule.h" so this also proves the compat header itself compiles
 * cleanly with real RedisModule_* naming. */
#include "redismodule.h"
#include <string.h>
#include <strings.h>

static int compat_bool_val = 0;

static int getCompatBoolConfig(const char *name, void *privdata) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(privdata);
    return compat_bool_val;
}

static int setCompatBoolConfig(const char *name, int val, void *privdata, RedisModuleString **err) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(privdata);
    REDISMODULE_NOT_USED(err);
    compat_bool_val = val;
    return REDISMODULE_OK;
}

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            RedisModule_ReplyWithError(ctx, "FAILED: " #cond);                          \
            return REDISMODULE_OK;                                                      \
        }                                                                               \
    } while (0)

static int CompatChecksCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    /* ConfigGetType against known core configs of each type. */
    RedisModuleConfigType type;
    CHECK(RedisModule_ConfigGetType("appendonly", &type) == REDISMODULE_OK);
    CHECK(type == REDISMODULE_CONFIG_TYPE_BOOL);
    CHECK(RedisModule_ConfigGetType("maxmemory", &type) == REDISMODULE_OK);
    CHECK(type == REDISMODULE_CONFIG_TYPE_NUMERIC);
    CHECK(RedisModule_ConfigGetType("maxmemory-policy", &type) == REDISMODULE_OK);
    CHECK(type == REDISMODULE_CONFIG_TYPE_ENUM);
    CHECK(RedisModule_ConfigGetType("no-such-config-xyz", &type) == REDISMODULE_ERR);

    /* ConfigGetBool / generic ConfigGet round trip against a core bool config. */
    int bval;
    CHECK(RedisModule_ConfigGetBool(ctx, "appendonly", &bval) == REDISMODULE_OK);
    RedisModuleString *generic = NULL;
    CHECK(RedisModule_ConfigGet(ctx, "appendonly", &generic) == REDISMODULE_OK);
    size_t generic_len;
    const char *generic_str = RedisModule_StringPtrLen(generic, &generic_len);
    CHECK(!strcasecmp(generic_str, bval ? "yes" : "no"));
    RedisModule_FreeString(ctx, generic);

    /* ConfigGetNumeric against a core numeric config, then set/restore via
     * ConfigSetNumeric, then via the generic ConfigSet. */
    long long orig_maxmemory;
    CHECK(RedisModule_ConfigGetNumeric(ctx, "maxmemory", &orig_maxmemory) == REDISMODULE_OK);
    RedisModuleString *err = NULL;
    CHECK(RedisModule_ConfigSetNumeric(ctx, "maxmemory", orig_maxmemory + 1024, &err) == REDISMODULE_OK);
    long long new_maxmemory;
    CHECK(RedisModule_ConfigGetNumeric(ctx, "maxmemory", &new_maxmemory) == REDISMODULE_OK);
    CHECK(new_maxmemory == orig_maxmemory + 1024);
    RedisModuleString *orig_str = RedisModule_CreateStringPrintf(ctx, "%lld", orig_maxmemory);
    CHECK(RedisModule_ConfigSet(ctx, "maxmemory", orig_str, &err) == REDISMODULE_OK);
    RedisModule_FreeString(ctx, orig_str);
    CHECK(RedisModule_ConfigGetNumeric(ctx, "maxmemory", &new_maxmemory) == REDISMODULE_OK);
    CHECK(new_maxmemory == orig_maxmemory);

    /* ConfigGetEnum against a core enum config. */
    RedisModuleString *enum_val = NULL;
    CHECK(RedisModule_ConfigGetEnum(ctx, "maxmemory-policy", &enum_val) == REDISMODULE_OK);
    RedisModule_FreeString(ctx, enum_val);

    /* Set/Get round trip on our own module-registered bool config. */
    CHECK(RedisModule_ConfigSetBool(ctx, "redismodulecompat.compat_bool", 1, &err) == REDISMODULE_OK);
    CHECK(RedisModule_ConfigGetBool(ctx, "redismodulecompat.compat_bool", &bval) == REDISMODULE_OK);
    CHECK(bval == 1);
    CHECK(compat_bool_val == 1);

    /* LoadDefaultConfigs should fail here: OnLoad already finished and
     * already called LoadConfigs for this module. */
    CHECK(RedisModule_LoadDefaultConfigs(ctx) == REDISMODULE_ERR);

    /* Iterator should find both maxmemory* configs via a glob pattern. */
    RedisModuleConfigIterator *iter = RedisModule_ConfigIteratorCreate(ctx, "maxmemor*");
    int found_maxmemory = 0, found_policy = 0, count = 0;
    const char *name;
    while ((name = RedisModule_ConfigIteratorNext(iter)) != NULL) {
        count++;
        if (!strcmp(name, "maxmemory")) found_maxmemory = 1;
        if (!strcmp(name, "maxmemory-policy")) found_policy = 1;
    }
    RedisModule_ConfigIteratorRelease(ctx, iter);
    CHECK(found_maxmemory && found_policy && count >= 2);

    /* ACLCheckKeyPrefixPermissions adapter shim: real Redis signature is
     * (user, RedisModuleString *prefix, int flags), routed through Valkey's
     * native 4-arg (user, const char*, len, flags) implementation. */
    RedisModuleString *username = RedisModule_CreateString(ctx, "default", 7);
    RedisModuleUser *user = RedisModule_GetModuleUserFromUserName(username);
    RedisModule_FreeString(ctx, username);
    CHECK(user != NULL);
    RedisModuleString *prefix = RedisModule_CreateString(ctx, "", 0);
    int aclres = RedisModule_ACLCheckKeyPrefixPermissions(user, prefix, REDISMODULE_CMD_KEY_ACCESS);
    RedisModule_FreeString(ctx, prefix);
    RedisModule_FreeModuleUser(user);
    CHECK(aclres == REDISMODULE_OK);

    RedisModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (RedisModule_Init(ctx, "redismodulecompat", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (RedisModule_RegisterBoolConfig(ctx, "compat_bool", 0, REDISMODULE_CONFIG_DEFAULT, getCompatBoolConfig,
                                       setCompatBoolConfig, NULL, NULL) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    if (RedisModule_LoadConfigs(ctx) == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "redismodulecompat.checks", CompatChecksCommand, "readonly", 0, 0, 0) ==
        REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    return REDISMODULE_OK;
}
