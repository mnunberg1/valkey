set testmodule [file normalize tests/modules/redismodule_compat.so]

start_server {tags {"modules"}} {
    r module load $testmodule

    test {RedisModule_ config compat API checks pass} {
        assert_equal [r redismodulecompat.checks] {OK}
    }

    test {RedisModule_ compat module config is reachable via CONFIG GET/SET} {
        assert_equal [r config get redismodulecompat.compat_bool] "redismodulecompat.compat_bool yes"
        r config set redismodulecompat.compat_bool no
        assert_equal [r config get redismodulecompat.compat_bool] "redismodulecompat.compat_bool no"
    }

    test {maxmemory is left unchanged by the compat API checks} {
        assert_equal [r config get maxmemory] "maxmemory 0"
    }
}
